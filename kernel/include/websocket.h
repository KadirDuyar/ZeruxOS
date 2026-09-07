#ifndef WEBSOCKET_H
#define WEBSOCKET_H

#include <stdint.h>

/* SHA-1 & Base64 routines for WebSocket Handshake */
void sha1_hash(const uint8_t *data, uint32_t length, uint8_t hash[20]);
int base64_encode(const uint8_t *src, uint32_t src_len, char *dst, uint32_t dst_max_len);

/* Generates the Sec-WebSocket-Accept header value from the client's Sec-WebSocket-Key */
int websocket_generate_accept(const char *client_key, char *out_accept, uint32_t out_max_len);

#endif
