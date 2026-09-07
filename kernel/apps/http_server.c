/* =============================================================================
 * ZeruX OS - HTTP Server App
 * File: kernel/apps/http_server.c
 * =============================================================================
 */

#include "socket.h"
#include "http_parser.h"
#include "libc.h"
#include "serial.h"
#include "task.h"
#include "vfs.h"
#include "shell.h"
#include "net.h"
#include "kheap.h"
#include "websocket.h"
#include "keyboard.h"

int g_http_server_port = 80;

/* --- WebSocket Terminal Global State --- */
volatile int g_ws_active_sock = -1;
static char ws_out_buf[128];
static int ws_out_idx = 0;

void ws_serial_capture(char c) {
    if (g_ws_active_sock >= 0) {
        ws_out_buf[ws_out_idx++] = c;
        if (c == '\n' || ws_out_idx >= 125) {
            uint8_t frame[130];
            frame[0] = 0x81; /* FIN | Text */
            frame[1] = ws_out_idx;
            for(int i = 0; i < ws_out_idx; i++) frame[2+i] = ws_out_buf[i];
            send(g_ws_active_sock, frame, 2 + ws_out_idx, 0);
            ws_out_idx = 0;
        }
    }
}

void ws_terminal_task(void) {
    serial_set_capture_fn(ws_serial_capture);
    while (1) {
        if (g_ws_active_sock < 0) {
            task_sleep_ms(100);
            continue;
        }
        
        uint8_t header[2];
        int r = recv(g_ws_active_sock, header, 2, 0);
        if (r <= 0) {
            close(g_ws_active_sock);
            g_ws_active_sock = -1;
            continue;
        }
        
        int opcode = header[0] & 0x0F;
        if (opcode == 8) { /* Close */
            close(g_ws_active_sock);
            g_ws_active_sock = -1;
            continue;
        }
        
        int payload_len = header[1] & 0x7F;
        int has_mask = (header[1] & 0x80);
        if (payload_len == 126) {
            uint8_t ext[2];
            recv(g_ws_active_sock, ext, 2, 0);
            payload_len = (ext[0] << 8) | ext[1];
        } else if (payload_len == 127) {
            /* Unsupported 64-bit length */
            close(g_ws_active_sock);
            g_ws_active_sock = -1;
            continue;
        }
        
        uint8_t mask[4] = {0};
        if (has_mask) recv(g_ws_active_sock, mask, 4, 0);
        
        if (payload_len > 0 && payload_len < 256) {
            char buf[256];
            recv(g_ws_active_sock, buf, payload_len, 0);
            for (int i = 0; i < payload_len; i++) {
                if (has_mask) buf[i] ^= mask[i % 4];
                keyboard_inject_char(buf[i]);
            }
        }
    }
}

static const char* get_content_type(const char *path) {
    size_t len = kstrlen(path);
    if (len > 5 && (kstrcmp(path + len - 5, ".html") == 0 || kstrcmp(path + len - 5, ".HTML") == 0)) return "text/html";
    if (len > 4 && (kstrcmp(path + len - 4, ".htm") == 0 || kstrcmp(path + len - 4, ".HTM") == 0)) return "text/html";
    if (len > 4 && (kstrcmp(path + len - 4, ".css") == 0 || kstrcmp(path + len - 4, ".CSS") == 0)) return "text/css";
    if (len > 3 && (kstrcmp(path + len - 3, ".js") == 0 || kstrcmp(path + len - 3, ".JS") == 0)) return "application/javascript";
    if (len > 4 && (kstrcmp(path + len - 4, ".png") == 0 || kstrcmp(path + len - 4, ".PNG") == 0)) return "image/png";
    if (len > 4 && (kstrcmp(path + len - 4, ".jpg") == 0 || kstrcmp(path + len - 4, ".JPG") == 0)) return "image/jpeg";
    if (len > 4 && (kstrcmp(path + len - 4, ".txt") == 0 || kstrcmp(path + len - 4, ".TXT") == 0)) return "text/plain";
    return "application/octet-stream";
}
static uint8_t g_http_file_buf[8192];

static void http_server_task(void) {
    uint16_t port = g_http_server_port;
    
    int server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock < 0) {
        serial_printf("[httpserver] Failed to open socket.\n");
        task_exit(1);
    }
    
    struct sockaddr_in sin;
    sin.sin_family = AF_INET;
    sin.sin_port = htons(port);
    sin.sin_addr = 0; /* INADDR_ANY */
    
    if (bind(server_sock, (struct sockaddr*)&sin, sizeof(sin)) != 0) {
        serial_printf("[httpserver] Failed to bind to port %d.\n", port);
        close(server_sock);
        task_exit(1);
    }
    
    if (listen(server_sock, 5) != 0) {
        serial_printf("[httpserver] Failed to listen.\n");
        close(server_sock);
        task_exit(1);
    }
    
    serial_printf("[httpserver] Listening on port %d (Background)...\n", port);
    
    while (1) {
        struct sockaddr_in client_sin;
        uint32_t addrlen = sizeof(client_sin);
        
        int client_sock = accept(server_sock, (struct sockaddr*)&client_sin, &addrlen);
        if (client_sock < 0) {
            task_sleep_ms(100);
            continue;
        }
        
        serial_printf("[httpserver] Accepted connection.\n");
        
        /* Read request */
        char *buf = (char *)kmalloc(4096);
        if (!buf) { close(client_sock); continue; }

        int r = recv(client_sock, buf, 4095, 0);
        if (r > 0) {
            buf[r] = '\0';
            http_message_t *msg = (http_message_t *)kmalloc(sizeof(http_message_t));
            if (!msg) { kfree(buf); close(client_sock); continue; }

            if (http_parse_message(buf, r, msg) == 0) {
                serial_printf("[httpserver] %s %s\n", msg->method, msg->path);
                
                if (kstrcmp(msg->method, "POST") == 0) {
                    if (kstrcmp(msg->path, "/api/shell") == 0) {
                        char *body = buf + msg->header_length;
                        if (kstrncmp(body, "edit", 4) == 0 || kstrncmp(body, "httpserver", 10) == 0) {
                            const char *err = "HTTP/1.1 400 Bad Request\r\nConnection: close\r\n\r\nError: Interactive commands are not supported.";
                            send(client_sock, err, kstrlen(err), 0);
                        } else {
                            char *out = (char*)kmalloc(16384);
                            if (out) {
                                shell_execute_command_to_buffer(body, out, 16384);
                                char *header = (char*)kmalloc(256);
                                ksprintf(header, "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: %d\r\nConnection: close\r\n\r\n", kstrlen(out));
                                send(client_sock, header, kstrlen(header), 0);
                                send(client_sock, out, kstrlen(out), 0);
                                kfree(header);
                                kfree(out);
                            }
                        }
                    } else if (kstrncmp(msg->path, "/api/firewall/add", 17) == 0) {
                          char *body = buf + msg->header_length;
                          char cmd[256];
                          ksprintf(cmd, "firewall add %s", body);
                          char dummy[1024];
                          shell_execute_command_to_buffer(cmd, dummy, 1024);
                          const char *ok = "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\nOK";
                          send(client_sock, ok, kstrlen(ok), 0);
                    } else if (kstrncmp(msg->path, "/api/firewall/del", 17) == 0) {
                          char *body = buf + msg->header_length;
                          char cmd[256];
                          ksprintf(cmd, "firewall del %s", body);
                          char dummy[1024];
                          shell_execute_command_to_buffer(cmd, dummy, 1024);
                          const char *ok = "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\nOK";
                          send(client_sock, ok, kstrlen(ok), 0);
                    } else if (kstrncmp(msg->path, "/api/file", 9) == 0) {
                        /* POST /api/file/disk/fat0/foo.txt (Save file) */
                        const char *file_path = msg->path + 9;
                        char *body = buf + msg->header_length;
                        int body_len = kstrlen(body); // Simple text length
                        
                        /* Delete & Recreate using shell command trick to avoid direct fat32 linking */
                        char rm_cmd[256];
                        ksprintf(rm_cmd, "rm %s", file_path);
                        char dummy[64];
                        shell_execute_command_to_buffer(rm_cmd, dummy, 64);
                        
                        char touch_cmd[256];
                        ksprintf(touch_cmd, "touch %s", file_path);
                        shell_execute_command_to_buffer(touch_cmd, dummy, 64);
                        
                        int fd = vfs_open(file_path, 1); // 1 = write
                        if (fd >= 0) {
                            vfs_write(fd, body, body_len);
                            vfs_close(fd);
                        }
                        
                        const char *resp = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nConnection: close\r\n\r\nSaved.";
                        send(client_sock, resp, kstrlen(resp), 0);
                    }
                }
                else if (kstrcmp(msg->method, "GET") == 0 && kstrcmp(msg->path, "/ws/terminal") == 0) {
                    /* WebSocket Handshake */
                    char accept_key[64];
                    char ws_key[64];
                    
                    /* Simple header extraction hack (we should parse properly, but it's a toy OS) */
                    const char *key_hdr = "Sec-WebSocket-Key: ";
                    char *ptr = buf;
                    int found = 0;
                    while (*ptr) {
                        if (kstrncmp(ptr, key_hdr, 19) == 0) {
                            ptr += 19;
                            int k_i = 0;
                            while (*ptr && *ptr != '\r' && *ptr != '\n' && k_i < 63) {
                                ws_key[k_i++] = *ptr++;
                            }
                            ws_key[k_i] = '\0';
                            found = 1;
                            break;
                        }
                        ptr++;
                    }
                    
                    if (found) {
                        websocket_generate_accept(ws_key, accept_key, 64);
                        char resp[256];
                        ksprintf(resp, "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: %s\r\n\r\n", accept_key);
                        send(client_sock, resp, kstrlen(resp), 0);
                        
                        /* Transfer socket to ws task */
                        g_ws_active_sock = client_sock;
                        serial_printf("[httpserver] Upgraded connection to WebSocket!\n");
                        continue; /* Don't close client_sock! */
                    } else {
                        const char *err = "HTTP/1.1 400 Bad Request\r\nConnection: close\r\n\r\n";
                        send(client_sock, err, kstrlen(err), 0);
                    }
                }
                else if (kstrcmp(msg->method, "GET") == 0 && kstrncmp(msg->path, "/api/", 5) == 0) {
                    if (kstrcmp(msg->path, "/api/net/pcap") == 0) {
                        /* Dedicated binary branch for PCAP download */
                        extern uint32_t g_pcap_head, g_pcap_count;
                        
                        /* PCAP Global Header */
                        uint8_t pcap_header[24] = {
                            0xd4, 0xc3, 0xb2, 0xa1, /* Magic */
                            0x02, 0x00, 0x04, 0x00, /* Version 2.4 */
                            0x00, 0x00, 0x00, 0x00, /* Thiszone */
                            0x00, 0x00, 0x00, 0x00, /* Sigfigs */
                            0xff, 0xff, 0x00, 0x00, /* Snaplen (65535) */
                            0x01, 0x00, 0x00, 0x00  /* Network (Ethernet) */
                        };
                        
                        /* Calculate total payload size */
                        uint32_t payload_len = 24;
                        uint32_t h = g_pcap_head;
                        for (uint32_t i=0; i<g_pcap_count; i++) {
                            payload_len += 16 + g_pcap_buffer[h].len;
                            h = (h + 1) % PCAP_MAX_PACKETS;
                        }
                        
                        char header[256];
                        ksprintf(header, "HTTP/1.1 200 OK\r\nContent-Type: application/vnd.tcpdump.pcap\r\nContent-Disposition: attachment; filename=\"zerux.pcap\"\r\nContent-Length: %u\r\nConnection: close\r\n\r\n", payload_len);
                        send(client_sock, header, kstrlen(header), 0);
                        
                        /* Create a chunk buffer to avoid network drop issues (our stack has no TCP retransmission) */
                        uint8_t *chunk = kmalloc(4096);
                        if (chunk) {
                            uint32_t c_idx = 0;
                            kmemcpy(chunk + c_idx, pcap_header, 24);
                            c_idx += 24;
                            
                            h = g_pcap_head;
                            for (uint32_t i=0; i<g_pcap_count; i++) {
                                pcap_packet_t *p = &g_pcap_buffer[h];
                                uint32_t pkt_header[4];
                                pkt_header[0] = p->timestamp_sec;
                                pkt_header[1] = p->timestamp_msec * 1000; /* microseconds */
                                pkt_header[2] = p->len;
                                pkt_header[3] = p->len;
                                
                                if (c_idx + 16 + p->len > 4000) {
                                    send(client_sock, chunk, c_idx, 0);
                                    c_idx = 0;
                                    task_sleep_ms(15); /* Let RTL8139 drain */
                                }
                                kmemcpy(chunk + c_idx, pkt_header, 16);
                                c_idx += 16;
                                kmemcpy(chunk + c_idx, p->data, p->len);
                                c_idx += p->len;
                                h = (h + 1) % PCAP_MAX_PACKETS;
                            }
                            if (c_idx > 0) {
                                send(client_sock, chunk, c_idx, 0);
                            }
                            kfree(chunk);
                        }
                    } else {
                        char *out = (char*)kmalloc(8192);
                    if (out) {
                        if (kstrcmp(msg->path, "/api/mem") == 0) {
                            shell_execute_command_to_buffer("meminfo", out, 8192);
                            shell_execute_command_to_buffer("tasks", out, 8192); /* Assuming 'tasks' lists processes */
                        } else if (kstrcmp(msg->path, "/api/dmesg") == 0) {
                            shell_execute_command_to_buffer("dmesg", out, 8192);
                        } else if (kstrcmp(msg->path, "/api/firewall") == 0) {
                            shell_execute_command_to_buffer("firewall list", out, 8192);
                        } else if (kstrcmp(msg->path, "/api/net/stats") == 0) {
                            extern uint32_t g_net_rx_packets, g_net_rx_bytes, g_net_tx_packets, g_net_tx_bytes, g_net_rx_dropped;
                            ksprintf(out, "{\"rx_packets\":%u, \"rx_bytes\":%u, \"tx_packets\":%u, \"tx_bytes\":%u, \"rx_dropped\":%u}",
                                     g_net_rx_packets, g_net_rx_bytes, g_net_tx_packets, g_net_tx_bytes, g_net_rx_dropped);
                        } else if (kstrcmp(msg->path, "/api/net/connections") == 0) {
                            shell_execute_command_to_buffer("netstat", out, 8192);
                        } else if (kstrncmp(msg->path, "/api/files", 10) == 0) {
                            const char *dir_path = "/disk/fat0";
                            if (kstrlen(msg->path) > 11) dir_path = msg->path + 10;
                            
                            int fd = vfs_open(dir_path, 0);
                            if (fd >= 0) {
                                kstrcpy(out, "[");
                                int idx = 0;
                                struct dirent *d;
                                while ((d = vfs_readdir(fd, idx++)) != NULL) {
                                    if (idx > 1) kstrcpy(out + kstrlen(out), ",");
                                    char entry[256];
                                    ksprintf(entry, "{\"name\":\"%s\"}", d->name);
                                    kstrcpy(out + kstrlen(out), entry);
                                }
                                kstrcpy(out + kstrlen(out), "]");
                                vfs_close(fd);
                            } else {
                                kstrcpy(out, "[]");
                            }
                        } else if (kstrncmp(msg->path, "/api/file", 9) == 0) {
                            /* GET /api/file?path=... (handled by path mapping if frontend sends /api/file/path) */
                            const char *file_path = msg->path + 9;
                            vfs_node_t *node = vfs_lookup(file_path);
                            if (node) {
                                kfree(out);
                                out = (char*)kmalloc(node->length + 1);
                                int fd = vfs_open(file_path, 0);
                                if (fd >= 0) {
                                    int bytes = vfs_read(fd, out, node->length);
                                    if (bytes >= 0) out[bytes] = '\0';
                                    vfs_close(fd);
                                } else {
                                    out[0] = '\0';
                                }
                            } else {
                                kstrcpy(out, "File not found.");
                            }
                        } else {
                            kstrcpy(out, "{\"error\": \"Unknown API endpoint\"}");
                        }
                        
                        /* Send out buffer as text for standard APIs */
                        char *header = (char*)kmalloc(256);
                        ksprintf(header, "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: %d\r\nConnection: close\r\n\r\n", kstrlen(out));
                        send(client_sock, header, kstrlen(header), 0);
                        send(client_sock, out, kstrlen(out), 0);
                        kfree(header);
                        kfree(out);
                    }
                    }
                }
                else if (kstrcmp(msg->method, "GET") == 0) {
                    /* Strip query string from path to prevent 404s for cache-busted URLs */
                    char *q = msg->path;
                    while (*q) {
                        if (*q == '?') { *q = '\0'; break; }
                        q++;
                    }
                    
                    /* Map path to FAT32 disk */
                    char vfs_path[256];
                    if (kstrcmp(msg->path, "/") == 0 || kstrcmp(msg->path, "/index.html") == 0) {
                        kstrcpy(vfs_path, "/disk/fat0/INDEX.HTM");
                    } else if (kstrcmp(msg->path, "/style.css") == 0) {
                        kstrcpy(vfs_path, "/disk/fat0/STYLE.CSS");
                    } else if (kstrcmp(msg->path, "/desktop.js") == 0) {
                        kstrcpy(vfs_path, "/disk/fat0/DESKTOP.JS");
                    } else {
                        /* Convert simple requests to uppercase for FAT32 compatibility */
                        ksprintf(vfs_path, "/disk/fat0%s", msg->path);
                        int i = 10; /* after "/disk/fat0" */
                        while (vfs_path[i]) {
                            if (vfs_path[i] >= 'a' && vfs_path[i] <= 'z') vfs_path[i] -= 32;
                            i++;
                        }
                    }
                    
                    vfs_node_t *node = vfs_lookup(vfs_path);
                    if (!node) {
                        const char *resp = "HTTP/1.1 404 Not Found\r\nConnection: close\r\n\r\n<h1>404 Not Found</h1>";
                        send(client_sock, resp, kstrlen(resp), 0);
                        serial_printf("[httpserver] 404 Not Found: %s\n", vfs_path);
                    } else {
                        int fd = vfs_open(vfs_path, 0); /* 0 = O_RDONLY normally */
                        if (fd >= 0) {
                            char header[256];
                            int hlen = ksprintf(header, 
                                "HTTP/1.1 200 OK\r\n"
                                "Content-Type: %s\r\n"
                                "Content-Length: %d\r\n"
                                "Connection: close\r\n\r\n", 
                                get_content_type(vfs_path), node->length);
                                
                            send(client_sock, header, hlen, 0);
                            
                            /* Stream file in chunks efficiently (8KB) using static buffer */
                            while (1) {
                                int bytes_read = vfs_read(fd, g_http_file_buf, 8192);
                                if (bytes_read <= 0) break;
                                send(client_sock, g_http_file_buf, bytes_read, 0);
                            }
                            vfs_close(fd);
                            serial_printf("[httpserver] 200 OK: %s (%d bytes)\n", vfs_path, node->length);
                        } else {
                            const char *resp = "HTTP/1.1 500 Internal Error\r\nConnection: close\r\n\r\n";
                            send(client_sock, resp, kstrlen(resp), 0);
                            serial_printf("[httpserver] 500 Error opening: %s\n", vfs_path);
                        }
                    }
                } else {
                    const char *resp = "HTTP/1.1 501 Not Implemented\r\nConnection: close\r\n\r\n";
                    send(client_sock, resp, kstrlen(resp), 0);
                }
            }
            kfree(msg);
        }
        kfree(buf);
        close(client_sock);
    }
}

void http_server_run(int argc, char **argv) {
    uint16_t port = 8080;
    if (argc >= 2) {
        port = katoi(argv[1]);
    }
    
    g_http_server_port = port;
    
    /* Create a kernel thread to run the server so shell doesn't block */
    kthread_create(http_server_task, "httpd");
    kthread_create(ws_terminal_task, "ws_term");
    serial_printf("[httpserver] Started HTTP/WS server on port %d.\n", port);
}
