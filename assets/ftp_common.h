/* =============================================================================
 * ZeruX OS - Minimal FTP Client/Server Ortak Yardimcilari
 * File: user_apps/include/ftp_common.h
 * =============================================================================
 * ftp.c (istemci) ve ftpd.c (sunucu) tarafindan ortak kullanilan:
 *   - Network byte-order donusumleri (htons/htonl - kernel tarafinda var,
 *     userland'de yok, o yuzden burada tekrar tanimlandi)
 *   - "a.b.c.d" IP string parse
 *   - Tek satirlik (CRLF sonlu) socket okuma/yazma yardimcilari
 *   - Ortak dosya yolu / port sabitleri
 *
 * PROTOKOL NOTU:
 * Bu, RFC 959'un birebir ayni degil, ondan ilham alan basitlestirilmis bir
 * FTP'dir:
 *   - Kontrol kanali : sabit port FTP_CTRL_PORT (bind edilir, dinlenir)
 *   - Veri kanali    : sabit port FTP_DATA_PORT (sunucu bunu da onceden
 *                       bind edip dinler; istemci LIST/RETR/STOR gonderdikten
 *                       hemen sonra bu porta baglanir)
 * Gercek FTP'deki PASV komutu, sunucunun kendi IP adresini "227 (h1,h2,h3,h4,
 * p1,p2)" seklinde bildirmesini gerektirir. ZeruX userland'inde su an yerel
 * IP'yi okuyacak bir syscall olmadigi icin (kernel'de g_local_ip var ama
 * disari acilmamis) bu bilgiyi PASV ile pazarlik etmek yerine iki tarafi da
 * biz yazdigimizdan sabit bir veri portu kullaniyoruz - islevsel olarak
 * passive mode ile ayni sonucu verir, sadece 227 mesaji yerine "sabit port"
 * anlasmasi var. Istersen ileride kernel'e SYS_GETIFADDR gibi bir syscall
 * eklenip gercek PASV'a gecilebilir.
 * =============================================================================
 */
#ifndef _FTP_COMMON_H
#define _FTP_COMMON_H

#include <stdint.h>
#include "syscall.h"
#include "string.h"

/* ---- Protokol sabitleri (istemci ve sunucu ayni degerleri kullanmali) ---- */
#define FTP_CTRL_PORT   2121
#define FTP_DATA_PORT   2122
#define FTP_USER        "zerux"
#define FTP_PASS        "zerux"

/* ---- Byte-order donusumleri (kernel/net/net.c ile birebir ayni mantik) ---- */
static inline uint16_t ftp_htons(uint16_t hostshort) {
    return (uint16_t)((hostshort >> 8) | (hostshort << 8));
}

static inline uint32_t ftp_htonl(uint32_t hostlong) {
    return ((hostlong & 0xFF) << 24) |
           ((hostlong & 0xFF00) << 8) |
           ((hostlong & 0xFF0000) >> 8) |
           ((hostlong >> 24) & 0xFF);
}

/* "192.168.1.5" -> host-order uint32 (a<<24|b<<16|c<<8|d). 1=basarili, 0=hata */
static inline int ftp_parse_ip(const char *s, uint32_t *out) {
    uint32_t val = 0;
    int part = 0, num = 0, digits = 0;
    while (1) {
        char c = *s;
        if (c >= '0' && c <= '9') {
            num = num * 10 + (c - '0');
            digits++;
            if (num > 255 || digits > 3) return 0;
        } else if (c == '.' || c == '\0') {
            if (digits == 0) return 0;
            val = (val << 8) | (uint32_t)num;
            part++;
            num = 0;
            digits = 0;
            if (c == '\0') break;
        } else {
            return 0;
        }
        s++;
    }
    if (part != 4) return 0;
    *out = val;
    return 1;
}

/* Basit satir sonu temizleyici: stdin'den gelen '\n'/'\r' karakterlerini siler */
static inline void ftp_strip_newline(char *s) {
    int n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r')) {
        s[--n] = '\0';
    }
}

/* Bir soketten CRLF (veya sadece LF) ile biten tek bir satir okur.
 * Donus: satir uzunlugu (>=0) basarili, -1 = baglanti kapandi/hata. */
static inline int ftp_recv_line(int fd, char *buf, int maxlen) {
    int n = 0;
    while (n < maxlen - 1) {
        char c;
        int r = read(fd, &c, 1);
        if (r <= 0) {
            if (n == 0) return -1; /* hic veri gelmeden baglanti kapandi */
            break;                  /* satir ortasinda kapandi, elimizdekini don */
        }
        if (c == '\n') break;
        if (c == '\r') continue;
        buf[n++] = c;
    }
    buf[n] = '\0';
    return n;
}

/* Bir soketten CRLF ekleyerek tek satir gonderir */
static inline void ftp_send_line(int fd, const char *s) {
    write(fd, s, strlen(s));
    write(fd, "\r\n", 2);
}

/* "dosya.txt" -> "/disk/fat0/dosya.txt" (flat dizin, alt klasor yok) */
static inline void ftp_build_path(char *out, int out_size, const char *fname) {
    const char *prefix = "/disk/fat0/";
    int i = 0;
    while (prefix[i] && i < out_size - 1) { out[i] = prefix[i]; i++; }
    int j = 0;
    while (fname[j] && i < out_size - 1) { out[i++] = fname[j++]; }
    out[i] = '\0';
}

#endif /* _FTP_COMMON_H */
