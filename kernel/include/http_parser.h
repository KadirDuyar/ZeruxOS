/* =============================================================================
 * ZeruX OS - HTTP Parser Library
 * File: kernel/include/http_parser.h
 * =============================================================================
 */

#ifndef HTTP_PARSER_H
#define HTTP_PARSER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define HTTP_MAX_HEADERS 16

typedef struct {
    char name[32];
    char value[128];
} http_header_t;

typedef struct {
    int  status_code;
    char method[16];
    char path[128];
    char version[16];
    
    http_header_t headers[HTTP_MAX_HEADERS];
    int header_count;
    
    int content_length;
    int header_length; /* Byte offset where the body starts */
    
    bool keep_alive;
} http_message_t;

/* Parse an HTTP request or response. Returns 0 on success, -1 on incomplete/error. */
int http_parse_message(const char *buffer, size_t len, http_message_t *msg);

#endif /* HTTP_PARSER_H */