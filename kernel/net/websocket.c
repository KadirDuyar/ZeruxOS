#include "websocket.h"
#include "shell.h" // For kstrcmp, kstrlen, etc. if needed
#include "kheap.h"

static const char b64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

int base64_encode(const uint8_t *src, uint32_t src_len, char *dst, uint32_t dst_max_len) {
    uint32_t i = 0, j = 0;
    uint32_t enc_len = 4 * ((src_len + 2) / 3);
    if (dst_max_len < enc_len + 1) return -1;
    
    while (i < src_len) {
        uint32_t octet_a = i < src_len ? src[i++] : 0;
        uint32_t octet_b = i < src_len ? src[i++] : 0;
        uint32_t octet_c = i < src_len ? src[i++] : 0;
        uint32_t triple = (octet_a << 0x10) + (octet_b << 0x08) + octet_c;
        
        dst[j++] = b64_table[(triple >> 3 * 6) & 0x3F];
        dst[j++] = b64_table[(triple >> 2 * 6) & 0x3F];
        dst[j++] = (i > src_len + 1) ? '=' : b64_table[(triple >> 1 * 6) & 0x3F];
        dst[j++] = (i > src_len)     ? '=' : b64_table[(triple >> 0 * 6) & 0x3F];
    }
    dst[j] = '\0';
    return j;
}

#define SHA1_ROTL(bits, word) (((word) << (bits)) | ((word) >> (32-(bits))))
static void sha1_transform(uint32_t state[5], const uint8_t buffer[64]) {
    uint32_t a, b, c, d, e, t, w[80];
    int i;
    for (i = 0; i < 16; i++) {
        w[i] = (buffer[i*4] << 24) | (buffer[i*4+1] << 16) | (buffer[i*4+2] << 8) | buffer[i*4+3];
    }
    for (i = 16; i < 80; i++) {
        w[i] = SHA1_ROTL(1, w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16]);
    }
    a = state[0]; b = state[1]; c = state[2]; d = state[3]; e = state[4];
    for (i = 0; i < 80; i++) {
        if (i < 20)      t = (b & c) | (~b & d);
        else if (i < 40) t = b ^ c ^ d;
        else if (i < 60) t = (b & c) | (b & d) | (c & d);
        else             t = b ^ c ^ d;
        
        uint32_t k;
        if (i < 20) k = 0x5A827999;
        else if (i < 40) k = 0x6ED9EBA1;
        else if (i < 60) k = 0x8F1BBCDC;
        else k = 0xCA62C1D6;
        
        uint32_t temp = SHA1_ROTL(5, a) + t + e + k + w[i];
        e = d; d = c; c = SHA1_ROTL(30, b); b = a; a = temp;
    }
    state[0] += a; state[1] += b; state[2] += c; state[3] += d; state[4] += e;
}

void sha1_hash(const uint8_t *data, uint32_t length, uint8_t hash[20]) {
    uint32_t state[5] = { 0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0 };
    uint8_t buffer[64];
    uint32_t i, j;
    uint32_t bitlen[2];
    bitlen[0] = (length << 3);
    bitlen[1] = (length >> 29);
    
    for (i = 0; i < length; i++) {
        buffer[i % 64] = data[i];
        if ((i % 64) == 63) sha1_transform(state, buffer);
    }
    buffer[i % 64] = 0x80; i++;
    if ((i % 64) > 56) {
        while ((i % 64) != 0) { buffer[i % 64] = 0; i++; }
        sha1_transform(state, buffer);
    }
    while ((i % 64) != 56) { buffer[i % 64] = 0; i++; }
    for (j = 0; j < 8; j++) {
        buffer[56 + j] = (j < 4) ? (bitlen[1] >> (24 - 8*j)) & 0xFF : (bitlen[0] >> (24 - 8*(j-4))) & 0xFF;
    }
    sha1_transform(state, buffer);
    for (i = 0; i < 20; i++) {
        hash[i] = (state[i>>2] >> (24 - 8*(i & 3))) & 0xFF;
    }
}

static int local_kstrlen(const char *s) {
    int len = 0;
    while (s[len]) len++;
    return len;
}

static char* local_kstrcpy(char *dest, const char *src) {
    char *d = dest;
    while ((*d++ = *src++));
    return dest;
}

int websocket_generate_accept(const char *client_key, char *out_accept, uint32_t out_max_len) {
    const char *magic = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    int k_len = local_kstrlen(client_key);
    int m_len = local_kstrlen(magic);
    
    if (k_len + m_len > 128) return -1;
    
    char combined[128];
    local_kstrcpy(combined, client_key);
    local_kstrcpy(combined + k_len, magic);
    
    uint8_t hash[20];
    sha1_hash((const uint8_t*)combined, k_len + m_len, hash);
    
    return base64_encode(hash, 20, out_accept, out_max_len);
}
