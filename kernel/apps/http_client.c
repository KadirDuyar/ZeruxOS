/* =============================================================================
 * ZeruX OS - HTTP Client App (wget clone)
 * File: kernel/apps/http_client.c
 * =============================================================================
 */

#include "socket.h"
#include "http_parser.h"
#include "libc.h"
#include "serial.h"
#include "dns.h"
#include "vfs.h"
#include "userfs.h"
#include "fat32.h"
#include "net.h"
#include "shell.h"

void http_client_run(int argc, char **argv) {
    if (argc < 3) {
        serial_printf("Usage: wget <host> <path> [output_file]\n");
        return;
    }
    
    const char *host = argv[1];
    const char *path = argv[2];
    const char *out_file = (argc >= 4) ? argv[3] : "index.html";
    
    /* 1. Resolve Host */
    uint32_t target_ip = 0;
    uint8_t ip_bytes[4];
    if (net_parse_ip(host, ip_bytes)) {
        target_ip = (ip_bytes[0] << 24) | (ip_bytes[1] << 16) | (ip_bytes[2] << 8) | ip_bytes[3];
    } else {
        serial_printf("[wget] Resolving '%s' via DNS...\n", host);
        if (!dns_resolve(host, ip_bytes)) {
            serial_printf("[wget] DNS resolution failed.\n");
            return;
        }
        target_ip = (ip_bytes[0] << 24) | (ip_bytes[1] << 16) | (ip_bytes[2] << 8) | ip_bytes[3];
    }
    
    /* 2. Connect */
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        serial_printf("[wget] Failed to open socket.\n");
        return;
    }
    
    struct sockaddr_in sin;
    sin.sin_family = AF_INET;
    sin.sin_addr = htonl(target_ip);
    sin.sin_port = htons(80);
    
    serial_printf("[wget] Connecting to %d.%d.%d.%d:80...\n", 
                  (target_ip >> 24) & 0xFF, (target_ip >> 16) & 0xFF, (target_ip >> 8) & 0xFF, target_ip & 0xFF);
                  
    if (connect(sock, (struct sockaddr*)&sin, sizeof(sin)) != 0) {
        serial_printf("[wget] Connection failed.\n");
        close(sock);
        return;
    }
    serial_printf("[wget] Connected! Sending GET request...\n");
    
    /* 3. Send Request */
    char req[256];
    int req_len = ksprintf(req, "GET %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n", path, host);
    send(sock, req, req_len, 0);
    
    /* 4. Receive and Parse Headers */
    char buf[4096];
    int total_read = 0;
    http_message_t msg;
    bool headers_parsed = false;
    
    int fd = -1; /* output file */
    
    while (1) {
        int r = recv(sock, buf + total_read, sizeof(buf) - total_read - 1, 0);
        if (r > 0) {
            total_read += r;
            buf[total_read] = '\0';
            
            if (!headers_parsed) {
                if (http_parse_message(buf, total_read, &msg) == 0) {
                    headers_parsed = true;
                    serial_printf("[wget] Response: HTTP/1.1 %d\n", msg.status_code);
                    
                    if (msg.status_code == 301 || msg.status_code == 302) {
                        serial_printf("[wget] Redirected! Please follow Location manually for now.\n");
                        /* Print location header */
                        for (int i = 0; i < msg.header_count; i++) {
                            if (kstrcmp(msg.headers[i].name, "Location") == 0) {
                                serial_printf("       Location: %s\n", msg.headers[i].value);
                            }
                        }
                        close(sock);
                        return;
                    }
                    
                    /* Open file using UserFS directly for now since it's RAM disk */
                    userfs_create(userfs_root_inode(), out_file);
                    
                    /* Construct VFS path */
                    char vfs_path[256];
                    ksprintf(vfs_path, "/user/%s", out_file);
                    
                    fd = vfs_open(vfs_path, 1); /* 1 for write */
                    if (fd >= 0) {
                        serial_printf("[wget] Downloading to '%s' (VFS fd %d)...\n", vfs_path, fd);
                    } else {
                        serial_printf("[wget] Failed to open '%s' for writing.\n", vfs_path);
                    }
                    
                    /* Write the remainder of the buffer (the body part) */
                    int body_len = total_read - msg.header_length;
                    if (body_len > 0 && fd >= 0) {
                        vfs_write(fd, buf + msg.header_length, body_len);
                    } else if (body_len > 0) {
                        serial_printf("%s", buf + msg.header_length);
                    }
                    total_read = 0; /* Reset buffer for stream */
                }
            } else {
                /* Headers already parsed, we are streaming the body */
                if (fd >= 0) {
                    vfs_write(fd, buf, r);
                } else {
                    serial_printf("%s", buf);
                }
                total_read = 0;
            }
        } else if (r == 0) {
            /* Connection closed */
            break;
        } else {
            /* Error */
            break;
        }
    }
    
    serial_printf("[wget] Done.\n");
    if (fd >= 0) vfs_close(fd);
    close(sock);
}
