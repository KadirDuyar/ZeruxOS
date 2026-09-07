/* =============================================================================
 * ZeruX OS — Aurora Browser: Minimal HTTP/1.1 Client
 * File: apps/aurora/http_client.c
 * =============================================================================
 * Akış: url.c ile parse -> DNS (gerekirse) -> tcp_connect -> GET gönder ->
 *       tcp_recv döngüsüyle cevabı topla -> header/body ayır.
 *
 * Bu dosyadaki DNS/TCP kullanım deseni kernel/drivers/shell.c'deki mevcut
 * "http <host>" komutuyla BİREBİR aynıdır (zaten test edilmiş, çalışan bir
 * akış) — tek fark, shell doğrudan serial'a basarken burada cevap kmalloc'lu
 * büyüyen bir arabelleğe toplanıyor.
 * =============================================================================
 */
#include "http_client.h"
#include "url.h"
#include "net.h"     /* net_parse_ip, kmemcpy, kmemset */
#include "dns.h"     /* dns_resolve */
#include "tcp.h"     /* tcp_socket_open/connect/send/recv/close/is_connected */
#include "kheap.h"   /* kmalloc/kfree */
#include "task.h"    /* task_sleep_ms */
#include "serial.h"  /* serial_printf (debug log, VBE ekranına gitmez) */

/* ---- Local string helpers (bu dosyanın dışına sızmayan static fonksiyonlar) */

/* "1.2.3.4" -> "1.2.3.4" (host değişmeden), IP değilse ve DNS başarısızsa false */
static bool resolve_host(const char *host, uint8_t ip_out[4]) {
    if (net_parse_ip(host, ip_out)) return true;
    serial_printf("[Aurora] '%s' bir IP degil, DNS ile cozuluyor...\n", host);
    if (dns_resolve(host, ip_out)) {
        serial_printf("[Aurora] DNS: %s -> %d.%d.%d.%d\n", host,
                       ip_out[0], ip_out[1], ip_out[2], ip_out[3]);
        return true;
    }
    serial_printf("[Aurora] DNS cozumlemesi basarisiz: %s\n", host);
    return false;
}

/* buf'ı en az `needed` kapasiteye büyütür (poor-man's realloc: yeni alan +
 * kopyala + eskiyi serbest bırak). Başarısız olursa false döner, buf/cap
 * değişmeden kalır (çağıran eski buf'ı hâlâ kfree edebilir). */
static bool grow_buffer(char **buf, uint32_t *cap, uint32_t needed) {
    if (needed <= *cap) return true;

    uint32_t new_cap = *cap ? *cap * 2 : 4096;
    while (new_cap < needed) new_cap *= 2;
    if (new_cap > AURORA_HTTP_MAX_RESPONSE) new_cap = AURORA_HTTP_MAX_RESPONSE;
    if (new_cap < needed) return false; /* tavana takıldık */

    char *new_buf = (char *)kmalloc(new_cap);
    if (!new_buf) return false;

    if (*buf) {
        kmemcpy(new_buf, *buf, *cap);
        kfree(*buf);
    }
    *buf = new_buf;
    *cap = new_cap;
    return true;
}

/* "HTTP/1.1 200 OK\r\n..." ilk satırından durum kodunu çeker. */
static int parse_status_code(const char *raw) {
    const char *p = raw;
    /* "HTTP/x.x " kısmını atla */
    while (*p && *p != ' ') p++;
    if (!*p) return -1;
    p++; /* boşluğu geç */

    int code = 0;
    int digits = 0;
    while (*p >= '0' && *p <= '9' && digits < 3) {
        code = code * 10 + (*p - '0');
        p++; digits++;
    }
    return digits == 3 ? code : -1;
}

/* raw içinde "\r\n\r\n" arar, bulursa body'nin başladığı offset'i döner,
 * bulamazsa -1 döner. */
static int find_header_end(const char *raw, uint32_t len) {
    for (uint32_t i = 0; i + 3 < len; i++) {
        if (raw[i] == '\r' && raw[i + 1] == '\n' &&
            raw[i + 2] == '\r' && raw[i + 3] == '\n') {
            return (int)(i + 4);
        }
    }
    return -1;
}

bool aurora_http_get(const char *url, aurora_http_response_t *resp) {
    if (!url || !resp) return false;

    aurora_url_t u;
    if (!aurora_url_parse(url, &u)) {
        serial_printf("[Aurora] Gecersiz URL: %s\n", url);
        return false;
    }
    if (u.is_https) {
        serial_printf("[Aurora] HTTPS henuz desteklenmiyor: %s\n", url);
        return false;
    }

    uint8_t ip[4];
    if (!resolve_host(u.host, ip)) return false;

    uint32_t target = ((uint32_t)ip[0] << 24) | ((uint32_t)ip[1] << 16) |
                       ((uint32_t)ip[2] << 8) | (uint32_t)ip[3];

    int sock = tcp_socket_open();
    if (sock < 0) {
        serial_printf("[Aurora] Bos TCP soketi yok.\n");
        return false;
    }

    serial_printf("[Aurora] %d.%d.%d.%d:%u adresine baglaniliyor...\n",
                  ip[0], ip[1], ip[2], ip[3], u.port);

    if (tcp_connect(sock, target, u.port) < 0) {
        serial_printf("[Aurora] tcp_connect basarisiz.\n");
        tcp_close(sock);
        return false;
    }

    /* Baglanti kurulana kadar bekle (max ~3 saniye) */
    int timeout = 300;
    while (!tcp_is_connected(sock) && timeout-- > 0) {
        task_sleep_ms(10);
    }
    if (!tcp_is_connected(sock)) {
        serial_printf("[Aurora] Baglanti zaman asimina ugradi.\n");
        tcp_close(sock);
        return false;
    }

    /* ---- GET isteğini oluştur ------------------------------------------ */
    char req[512];
    uint32_t n = 0;

    #define APPEND(str) do { \
        const char *_s = (str); \
        while (*_s && n < sizeof(req) - 1) req[n++] = *_s++; \
    } while (0)

    APPEND("GET ");
    APPEND(u.path);
    APPEND(" HTTP/1.1\r\nHost: ");
    APPEND(u.host);
    if (u.port != 80) {
        req[n++] = ':';
        /* basit port -> string */
        char portbuf[8];
        int pi = 0;
        uint16_t pv = u.port;
        if (pv == 0) { portbuf[pi++] = '0'; }
        char tmp[8]; int ti = 0;
        while (pv > 0) { tmp[ti++] = '0' + (pv % 10); pv /= 10; }
        while (ti > 0) portbuf[pi++] = tmp[--ti];
        portbuf[pi] = '\0';
        APPEND(portbuf);
    }
    APPEND("\r\nUser-Agent: Aurora/0.1 (ZeruX)\r\nConnection: close\r\n\r\n");
    req[n] = '\0';
    #undef APPEND

    serial_printf("[Aurora] Istek gonderiliyor: GET %s (Host: %s)\n", u.path, u.host);
    tcp_send(sock, (const uint8_t *)req, n, 0);

    /* ---- Cevabı topla ---------------------------------------------------- */
    char *buf = NULL;
    uint32_t cap = 0, total = 0;
    bool truncated = false;
    uint8_t chunk[512];

    for (;;) {
        if (total >= AURORA_HTTP_MAX_RESPONSE) { truncated = true; break; }

        int r = tcp_recv(sock, chunk, sizeof(chunk), 0);
        if (r <= 0) break; /* 0 = EOF/kapandi, negatif = hata */

        if (!grow_buffer(&buf, &cap, total + (uint32_t)r + 1)) {
            truncated = true;
            break;
        }
        kmemcpy(buf + total, chunk, (uint32_t)r);
        total += (uint32_t)r;
    }

    tcp_close(sock);

    if (total == 0 || !buf) {
        serial_printf("[Aurora] Sunucudan hic veri gelmedi.\n");
        if (buf) kfree(buf);
        return false;
    }

    buf[total] = '\0';

    int header_end = find_header_end(buf, total);
    resp->raw = buf;
    resp->status_code = parse_status_code(buf);
    resp->truncated = truncated;

    if (header_end >= 0) {
        resp->body = buf + header_end;
        resp->body_len = total - (uint32_t)header_end;
    } else {
        /* Header sonu bulunamadi (kesik cevap) — elimizdeki her seyi body say */
        resp->body = buf;
        resp->body_len = total;
    }

    serial_printf("[Aurora] Cevap alindi: %u bayt (durum: %d)%s\n",
                  total, resp->status_code, truncated ? " [KESILDI]" : "");

    return true;
}

void aurora_http_free(aurora_http_response_t *resp) {
    if (!resp) return;
    if (resp->raw) kfree(resp->raw);
    resp->raw = NULL;
    resp->body = NULL;
    resp->body_len = 0;
}
