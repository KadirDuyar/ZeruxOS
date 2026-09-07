/* =============================================================================
 * ZeruX OS — Aurora Browser: Minimal HTTP/1.1 Client
 * File: apps/aurora/http_client.h
 * =============================================================================
 * DNS + TCP katmanlarının üzerine kurulu, tek-seferlik ("Connection: close")
 * bir GET istemcisi. ZeruX'in mevcut ağ yığınını (dns_resolve/tcp_*) kullanır,
 * hiçbir yeni socket/driver kodu eklemez.
 *
 * ÖNEMLİ SINIRLAMALAR (v0.1):
 *  - Sadece HTTP GET (POST/HEAD/vs. yok)
 *  - HTTPS/TLS desteklenmiyor (aurora_url_t.is_https == true ise reddedilir)
 *  - "Transfer-Encoding: chunked" ÇÖZÜLMÜYOR — sunucu chunked cevap dönerse
 *    body içinde ham hex chunk-size satırları görünebilir. Çoğu basit statik
 *    HTML sunucusu (nginx/apache default dosya sunumu, example.com, ZeruX'in
 *    kendi ileride yazılacak basit HTTP sunucusu) "Connection: close" ile
 *    chunked kullanmaz, bu yüzden v0.1 için yeterli.
 *  - Cevap AURORA_HTTP_MAX_RESPONSE baytı aşarsa kesilir (kheap şu an sabit
 *    128KB olduğu için büyük sayfalar için cömert bir tampon ayıramıyoruz).
 * =============================================================================
 */
#ifndef AURORA_HTTP_CLIENT_H
#define AURORA_HTTP_CLIENT_H

#include <stdint.h>
#include <stdbool.h>

/* Toplam kabul edilecek maksimum HTTP cevabı (header + body). */
#define AURORA_HTTP_MAX_RESPONSE (24 * 1024)

typedef struct {
    char    *raw;        /* kmalloc ile ayrılmış tam ham cevap (header+body), NUL-terminated */
    char    *body;        /* raw içinde body'nin başladığı nokta (header'dan sonrası) */
    uint32_t body_len;    /* body'nin byte uzunluğu */
    int      status_code; /* HTTP durum kodu (200, 404, ...); ayrıştırılamazsa -1 */
    bool     truncated;   /* AURORA_HTTP_MAX_RESPONSE sınırına takıldıysa true */
} aurora_http_response_t;

/* Senkron/blocking bir HTTP GET yapar. Çağıran task bu sırada
 * task_sleep_ms() ile uyur; scheduler diğer task'ları (GUI, shell, vs.)
 * bu sırada normal şekilde çalıştırmaya devam eder — sistem donmaz.
 *
 * Başarılı olursa true döner ve *resp doldurulur (aurora_http_free() ile
 * serbest bırakılmalı). Başarısız olursa false döner, *resp dokunulmaz. */
bool aurora_http_get(const char *url, aurora_http_response_t *resp);

/* aurora_http_get() ile alınan cevabın belleğini serbest bırakır. */
void aurora_http_free(aurora_http_response_t *resp);

#endif /* AURORA_HTTP_CLIENT_H */
