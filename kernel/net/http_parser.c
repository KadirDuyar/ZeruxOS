/* =============================================================================
 * ZeruX OS - HTTP Parser Library Implementation
 * File: kernel/net/http_parser.c
 * =============================================================================
 */

#include "http_parser.h"
#include "libc.h"
#include "net.h"

static void parse_header_line(const char *line, int len, http_message_t *msg) {
    if (msg->header_count >= HTTP_MAX_HEADERS) return;
    
    int colon = -1;
    for (int i = 0; i < len; i++) {
        if (line[i] == ':') {
            colon = i;
            break;
        }
    }
    
    if (colon > 0) {
        http_header_t *h = &msg->headers[msg->header_count++];
        
        int n_len = colon;
        if (n_len >= (int)sizeof(h->name)) n_len = (int)sizeof(h->name) - 1;
        kmemcpy(h->name, line, n_len);
        h->name[n_len] = '\0';
        
        int v_start = colon + 1;
        while (v_start < len && line[v_start] == ' ') v_start++;
        
        int v_len = len - v_start;
        if (v_len >= (int)sizeof(h->value)) v_len = (int)sizeof(h->value) - 1;
        kmemcpy(h->value, line + v_start, v_len);
        h->value[v_len] = '\0';
        
        /* Parse specific headers */
        if (kstrcmp(h->name, "Content-Length") == 0 || kstrcmp(h->name, "content-length") == 0) {
            msg->content_length = katoi(h->value);
        }
        if (kstrcmp(h->name, "Connection") == 0 || kstrcmp(h->name, "connection") == 0) {
            if (kstrcmp(h->value, "keep-alive") == 0) msg->keep_alive = true;
        }
    }
}

int http_parse_message(const char *buffer, size_t len, http_message_t *msg) {
    kmemset(msg, 0, sizeof(http_message_t));
    msg->content_length = -1;
    
    const char *ptr = buffer;
    /* size_t rem = len; */
    
    /* Find end of headers \r\n\r\n */
    const char *eoh = NULL;
    for (size_t i = 0; i < len - 3; i++) {
        if (ptr[i] == '\r' && ptr[i+1] == '\n' && ptr[i+2] == '\r' && ptr[i+3] == '\n') {
            eoh = ptr + i;
            break;
        }
    }
    
    if (!eoh) return -1; /* Incomplete headers */
    
    msg->header_length = (eoh - buffer) + 4;
    
    /* Parse Request/Response line */
    const char *line_end = ptr;
    while (line_end < eoh && *line_end != '\r') line_end++;
    
    if (kstrncmp(ptr, "HTTP/", 5) == 0) {
        /* Response: HTTP/1.1 200 OK */
        msg->status_code = katoi(ptr + 9);
    } else {
        /* Request: GET /path HTTP/1.1 */
        int space1 = -1, space2 = -1;
        for (int i = 0; i < (line_end - ptr); i++) {
            if (ptr[i] == ' ') {
                if (space1 == -1) space1 = i;
                else if (space2 == -1) space2 = i;
            }
        }
        if (space1 > 0) {
            int m_len = space1;
            if (m_len >= (int)sizeof(msg->method)) m_len = (int)sizeof(msg->method) - 1;
            kmemcpy(msg->method, ptr, m_len);
        }
        if (space2 > space1) {
            int p_len = space2 - space1 - 1;
            if (p_len >= (int)sizeof(msg->path)) p_len = (int)sizeof(msg->path) - 1;
            kmemcpy(msg->path, ptr + space1 + 1, p_len);
        }
    }
    
    /* Parse Headers */
    ptr = line_end + 2; /* Skip \r\n */
    while (ptr < eoh) {
        line_end = ptr;
        while (line_end < eoh && *line_end != '\r') line_end++;
        
        parse_header_line(ptr, line_end - ptr, msg);
        ptr = line_end + 2;
    }
    
    return 0;
}
