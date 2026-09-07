/* =============================================================================
 * ZeruX OS — Aurora Browser: URL Parser
 * File: apps/aurora/url.h
 * =============================================================================
 * "http://host[:port][/path]" ya da çıplak "host[/path]" formatındaki bir
 * URL string'ini host/port/path parçalarına ayırır. Sadece http:// destekler
 * (https şu an desteklenmiyor — TLS/şifreleme bu OS'te henüz yok).
 * =============================================================================
 */
#ifndef AURORA_URL_H
#define AURORA_URL_H

#include <stdint.h>
#include <stdbool.h>

#define AURORA_URL_MAX_HOST 128
#define AURORA_URL_MAX_PATH 256

typedef struct {
    char     host[AURORA_URL_MAX_HOST];
    char     path[AURORA_URL_MAX_PATH];
    uint16_t port;
    bool     is_https; /* true ise parse başarılı olur ama aurora_http_get() reddeder */
} aurora_url_t;

/* raw örnekleri: "http://example.com", "example.com/index.html",
 *                "192.168.1.10:8080/", "https://example.com" (parse edilir,
 *                fakat http_client bunu desteklemediği için reddedecektir).
 * Başarısız olursa (boş string, vs.) false döner. */
bool aurora_url_parse(const char *raw, aurora_url_t *out);

#endif /* AURORA_URL_H */
