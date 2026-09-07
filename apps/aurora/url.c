/* =============================================================================
 * ZeruX OS — Aurora Browser: URL Parser
 * File: apps/aurora/url.c
 * =============================================================================
 */
#include "url.h"
#include <libc.h>

/* ---- Freestanding küçük string yardımcıları (kernel/drivers/shell.c'deki
 *      aynı desen: her dosya kendi local static helper'larını taşır,
 *      ortak bir string.h yok) --------------------------------------------- */
static uint32_t kstrlen_local(const char *s) {
    uint32_t n = 0;
    while (s[n]) n++;
    return n;
}

static bool starts_with_ci(const char *s, const char *prefix) {
    while (*prefix) {
        char a = *s, b = *prefix;
        if (a >= 'A' && a <= 'Z') a += 32; /* lower */
        if (b >= 'A' && b <= 'Z') b += 32;
        if (a != b) return false;
        s++; prefix++;
    }
    return true;
}

bool aurora_url_parse(const char *raw, aurora_url_t *out) {
    if (!raw || !out) return false;
    if (kstrlen_local(raw) == 0) return false;

    out->host[0] = '\0';
    out->path[0] = '/';
    out->path[1] = '\0';
    out->port = 80;
    out->is_https = false;

    const char *p = raw;

    /* 1. Şema (scheme) ayrıştırma */
    if (starts_with_ci(p, "http://")) {
        p += 7;
    } else if (starts_with_ci(p, "https://")) {
        p += 8;
        out->is_https = true;
    }
    /* şema yoksa çıplak "host/path" kabul et */

    /* 2. Host ayrıştırma: ':' veya '/' veya string sonuna kadar */
    uint32_t i = 0;
    while (*p && *p != ':' && *p != '/' && i < AURORA_URL_MAX_HOST - 1) {
        out->host[i++] = *p++;
    }
    out->host[i] = '\0';

    if (i == 0) return false; /* host boşsa geçersiz URL */

    /* Localhost Mapping -> Dynamic g_local_ip */
    if (starts_with_ci(out->host, "localhost")) {
        extern uint32_t g_local_ip;
        ksprintf(out->host, "%d.%d.%d.%d",
                 (g_local_ip >> 24) & 0xFF, (g_local_ip >> 16) & 0xFF,
                 (g_local_ip >> 8) & 0xFF, g_local_ip & 0xFF);
    }

    /* 3. Opsiyonel port */
    if (*p == ':') {
        p++;
        uint16_t port = 0;
        bool got_digit = false;
        while (*p >= '0' && *p <= '9') {
            port = (uint16_t)(port * 10 + (*p - '0'));
            p++;
            got_digit = true;
        }
        if (got_digit) out->port = port;
    }

    /* 4. Path (varsa) */
    if (*p == '/') {
        uint32_t j = 0;
        while (*p && j < AURORA_URL_MAX_PATH - 1) {
            out->path[j++] = *p++;
        }
        out->path[j] = '\0';
    }
    /* *p == '\0' ise path zaten yukarıda "/" olarak ilklendi */

    return true;
}
