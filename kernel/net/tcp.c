/* =============================================================================
 * ZeruX OS - TCP (Transmission Control Protocol) Implementation
 * File: kernel/net/tcp.c
 * =============================================================================
 */

#include <stdbool.h>
#include "net.h"
#include "wait.h"
#include "fcntl.h"
#include "timer.h"
#include "kheap.h"
#include "task.h"
#include "tcp.h"
#include "serial.h"
#include "poll.h"

#define MAX_TCP_SOCKETS 64
tcp_socket_t g_tcp_sockets[MAX_TCP_SOCKETS];

/* Dynamic port allocation starts here */
static uint16_t g_next_local_port = 49152; 

static uint16_t tcp_get_ephemeral_port(void) {
    uint16_t start_port = g_next_local_port;
    while (1) {
        uint16_t port = g_next_local_port++;
        if (g_next_local_port > 65000) g_next_local_port = 49152;
        
        bool in_use = false;
        for (int i = 0; i < MAX_TCP_SOCKETS; i++) {
            if (g_tcp_sockets[i].active && g_tcp_sockets[i].local_port == port) {
                in_use = true;
                break;
            }
        }
        if (!in_use) return port;
        
        if (g_next_local_port == start_port) return 0; /* All ephemeral ports in use! */
    }
}

void tcp_init(void) {
    for (int i = 0; i < MAX_TCP_SOCKETS; i++) {
        g_tcp_sockets[i].active = false;
        g_tcp_sockets[i].state  = TCP_STATE_CLOSED;
    }
    serial_printf("[TCP] Transmission Control Protocol Initialized.\n");
}

/* Helper to free transmission queue and deactivate socket */
void tcp_destroy_socket(tcp_socket_t *sock) {
    sock->state = TCP_STATE_CLOSED;
    sock->active = false;
    
    tcp_segment_t *seg = sock->tx_queue_head;
    while (seg) {
        tcp_segment_t *next = seg->next;
        if (seg->payload) kfree(seg->payload);
        kfree(seg);
        seg = next;
    }
    sock->tx_queue_head = NULL;
    sock->tx_queue_tail = NULL;
    sock->tx_inflight_bytes = 0;
    
    /* Free Out-Of-Order RX Queue */
    seg = sock->rx_ooo_queue_head;
    while (seg) {
        tcp_segment_t *next = seg->next;
        if (seg->payload) kfree(seg->payload);
        kfree(seg);
        seg = next;
    }
    sock->rx_ooo_queue_head = NULL;
    
    wake_up_all(&sock->tx_wait_queue);
    wake_up_all(&sock->rx_wait_queue);
    wake_up_all(&sock->accept_wait_queue);
}

/* Internal helper: calculates checksum over Pseudo Header + TCP Header + Data */
static uint16_t tcp_checksum(uint32_t src_ip, uint32_t dest_ip, const uint8_t *tcp_payload, uint16_t tcp_len) {
    uint32_t sum = 0;
    
    /* 1. Sum pseudo header manually to avoid strict aliasing bugs (-O2) */
    uint32_t src = htonl(src_ip);
    uint32_t dst = htonl(dest_ip);
    
    sum += (src & 0xFFFF);
    sum += (src >> 16);
    sum += (dst & 0xFFFF);
    sum += (dst >> 16);
    sum += htons(6); /* Protocol 6 (TCP) */
    sum += htons(tcp_len);
    
    /* 2. Sum TCP payload (Header + Data) */
    const uint16_t *ptr = (const uint16_t*)tcp_payload;
    for (uint32_t i = 0; i < tcp_len / 2; i++) {
        sum += ptr[i];
    }
    
    /* 3. Handle odd length */
    if (tcp_len % 2) {
        sum += ((uint8_t*)tcp_payload)[tcp_len - 1]; /* Little Endian pad: byte is at even offset, goes to lower 8 bits */
    }
    
    /* 4. Fold 32-bit sum into 16 bits */
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    
    return (uint16_t)(~sum);
}

/* Internal helper: build and transmit a TCP segment */
void tcp_send_segment(tcp_socket_t *sock, uint32_t seq, uint8_t flags, const uint8_t *data, uint16_t data_len) {
    uint32_t tcp_total_len = sizeof(tcp_hdr_t) + data_len;
    uint8_t *tcp_packet = kmalloc(tcp_total_len);
    if (!tcp_packet) return;
    
    tcp_hdr_t *hdr = (tcp_hdr_t*)tcp_packet;
    hdr->src_port = htons(sock->local_port);
    hdr->dest_port = htons(sock->remote_port);
    hdr->seq_num = htonl(seq);
    
    if (flags & TCP_ACK) {
        hdr->ack_num = htonl(sock->rcv_nxt);
    } else {
        hdr->ack_num = 0;
    }
    
    /* 5 words (20 bytes) data offset */
    hdr->data_offset_flags = htons((5 << 12) | flags);
    hdr->window_size = htons(sock->window);
    hdr->urgent_ptr = 0;
    hdr->checksum = 0;
    
    /* Copy payload if any */
    if (data_len > 0 && data) {
        kmemcpy(tcp_packet + sizeof(tcp_hdr_t), data, data_len);
    }
    
    /* Compute checksum */
    hdr->checksum = tcp_checksum(g_local_ip, sock->remote_ip, tcp_packet, tcp_total_len);
    
    /* Send via IPv4 */
    uint8_t target_ip[4];
    target_ip[0] = (sock->remote_ip >> 24) & 0xFF;
    target_ip[1] = (sock->remote_ip >> 16) & 0xFF;
    target_ip[2] = (sock->remote_ip >> 8) & 0xFF;
    target_ip[3] = sock->remote_ip & 0xFF;
    
    net_ipv4_send(target_ip, 6, tcp_packet, tcp_total_len);
    kfree(tcp_packet);
}

/* Posix-like Socket API */
int tcp_socket_open(void) {
    for (int i = 0; i < MAX_TCP_SOCKETS; i++) {
        if (!g_tcp_sockets[i].active) {
            tcp_socket_t *sock = &g_tcp_sockets[i];
            kmemset(sock, 0, sizeof(tcp_socket_t));
            sock->active = true;
            sock->state = TCP_STATE_CLOSED;
            sock->local_port = tcp_get_ephemeral_port();
            sock->window = TCP_RX_BUF_SIZE;
            sock->snd_wnd = TCP_TX_BUF_SIZE;
            
            /* Congestion Control Init */
            sock->cwnd = 1024;        /* 1 MSS */
            sock->ssthresh = 65535;   /* Max threshold */
            sock->dup_acks = 0;
            
            /* TCP Options Init */
            sock->mss = 1024;
            sock->rcv_wscale = 0;     /* Default no scaling */
            sock->snd_wscale = 0;
            
            sock->rto = 1000; /* Default 1 second RTO */
            sock->smoothed_rtt = 0;
            sock->parent_socket = -1;
            sock->backlog = 0;
            
            init_waitqueue_head(&sock->tx_wait_queue);
            init_waitqueue_head(&sock->rx_wait_queue);
            init_waitqueue_head(&sock->accept_wait_queue);
            
            return i;
        }
    }
    return -1;
}

int tcp_bind(int socket_id, uint16_t local_port) {
    if (socket_id < 0 || socket_id >= MAX_TCP_SOCKETS) return -1;
    tcp_socket_t *sock = &g_tcp_sockets[socket_id];
    
    if (!sock->active) return -1;
    
    /* Close existing socket on this port if it's TIME_WAIT or TIME_WAIT zombie */
    for (int i = 0; i < MAX_TCP_SOCKETS; i++) {
        if (i != socket_id && g_tcp_sockets[i].active && g_tcp_sockets[i].local_port == local_port) {
            if (g_tcp_sockets[i].state == TCP_STATE_TIME_WAIT || g_tcp_sockets[i].state == TCP_STATE_CLOSED || g_tcp_sockets[i].state == TCP_STATE_CLOSE_WAIT) {
                g_tcp_sockets[i].active = false; /* SO_REUSEADDR implicitly */
            } else {
                return -1; /* Port in use */
            }
        }
    }
    
    sock->local_port = local_port;
    sock->local_ip = g_local_ip;
    return 0;
}

int tcp_listen(int socket_id, int backlog) {
    if (socket_id < 0 || socket_id >= MAX_TCP_SOCKETS) return -1;
    tcp_socket_t *sock = &g_tcp_sockets[socket_id];
    
    if (!sock->active || sock->state != TCP_STATE_CLOSED) return -1;
    
    sock->state = TCP_STATE_LISTEN;
    sock->backlog = backlog;
    serial_printf("[TCP] Socket %d LISTENING on port %d (backlog: %d)\n", socket_id, sock->local_port, backlog);
    return 0;
}

int tcp_accept(int socket_id) {
    if (socket_id < 0 || socket_id >= MAX_TCP_SOCKETS) return -1;
    tcp_socket_t *sock = &g_tcp_sockets[socket_id];
    
    if (!sock->active || sock->state != TCP_STATE_LISTEN) return -1;
    
    while (sock->accept_head == sock->accept_tail) {
        /* No pending established connections, wait */
        wait_event_interruptible(&sock->accept_wait_queue);
    }
    
    int child_id = sock->accept_queue[sock->accept_head];
    sock->accept_head = (sock->accept_head + 1) % 16;
    
    /* Detach from parent */
    g_tcp_sockets[child_id].parent_socket = -1;
    
    serial_printf("[TCP] Socket %d ACCEPTED child socket %d\n", socket_id, child_id);
    return child_id;
}

int tcp_connect(int socket_id, uint32_t remote_ip, uint16_t remote_port) {
    if (socket_id < 0 || socket_id >= MAX_TCP_SOCKETS) return -1;
    tcp_socket_t *sock = &g_tcp_sockets[socket_id];
    
    if (!sock->active) return -1;
    if (sock->state != TCP_STATE_CLOSED) return -1;
    
    sock->remote_ip = remote_ip;
    sock->remote_port = remote_port;
    sock->local_ip = g_local_ip;
    
    /* ISN (Initial Sequence Number) - In a real OS this should be random */
    sock->snd_nxt = timer_get_ticks();
    sock->snd_una = sock->snd_nxt;
    
    sock->state = TCP_STATE_SYN_SENT;
    
    /* Send SYN */
    tcp_send_segment(sock, sock->snd_nxt, TCP_SYN, NULL, 0);
    sock->snd_nxt++; /* SYN consumes a sequence number */
    
    return 0;
}

int tcp_send(int socket_id, const uint8_t *data, uint32_t len, uint32_t flags) {
    if (socket_id < 0 || socket_id >= MAX_TCP_SOCKETS) return -1;
    tcp_socket_t *sock = &g_tcp_sockets[socket_id];
    
    uint32_t offset = 0;
    while (offset < len) {
        uint32_t inflight = sock->snd_nxt - sock->snd_una;
        uint32_t eff_wnd = (sock->snd_wnd < sock->cwnd) ? sock->snd_wnd : sock->cwnd;
        
        /* Wait until there is space in our transmission buffer and Congestion window is open */
        while (inflight >= eff_wnd || sock->tx_inflight_bytes >= TCP_TX_BUF_SIZE) {
            if (!sock->active || sock->state != TCP_STATE_ESTABLISHED) return -1;
            
            if (flags & O_NONBLOCK) return offset > 0 ? (int)offset : -1; /* EAGAIN conceptually */
            
            wait_event_interruptible(&sock->tx_wait_queue);
        
        inflight = sock->snd_nxt - sock->snd_una;
        eff_wnd = (sock->snd_wnd < sock->cwnd) ? sock->snd_wnd : sock->cwnd;
    }
    
    uint32_t copy_len = len - offset;
    uint32_t usable_window = eff_wnd - inflight;
    
    if (copy_len > usable_window) copy_len = usable_window;
    if (copy_len > TCP_TX_BUF_SIZE - sock->tx_inflight_bytes) {
        copy_len = TCP_TX_BUF_SIZE - sock->tx_inflight_bytes;
    }
    
    /* Use dynamically calculated RTO if available, else default */
    if (sock->smoothed_rtt > 0) sock->rto = sock->smoothed_rtt * 2;
    else sock->rto = 1000;
    
    sock->last_activity_tick = timer_get_uptime_ms();
    
    /* Send in MSS chunks to avoid exceeding Ethernet MTU */
    uint32_t chunk_offset = 0;
    while (chunk_offset < copy_len) {
        uint32_t chunk = copy_len - chunk_offset;
        if (chunk > 1024) chunk = 1024; /* Conservative MSS */
        
        uint8_t segment_flags = TCP_ACK;
        if (offset + chunk_offset + chunk == len) segment_flags |= TCP_PSH; /* Push on last chunk */
        
        /* Allocate Segment */
        tcp_segment_t *seg = kzalloc(sizeof(tcp_segment_t));
        seg->seq = sock->snd_nxt;
        seg->len = chunk;
        seg->flags = segment_flags;
        seg->payload = kmalloc(chunk);
        kmemcpy(seg->payload, data + offset + chunk_offset, chunk);
        seg->send_time = timer_get_uptime_ms();
        seg->retry_count = 0;
        seg->acked = false;
        seg->next = NULL;
        
        /* Append to tx_queue */
        if (!sock->tx_queue_head) {
            sock->tx_queue_head = seg;
        } else {
            sock->tx_queue_tail->next = seg;
        }
        sock->tx_queue_tail = seg;
        
        sock->tx_inflight_bytes += chunk;
        
        /* Transmit immediately since we know it's within snd_wnd */
        tcp_send_segment(sock, seg->seq, seg->flags, seg->payload, seg->len);
        
        sock->snd_nxt += chunk;
        chunk_offset += chunk;
    }
    offset += copy_len;
    }
    
    return offset;
}

/* Receive Data from Socket (Blocking or Non-Blocking) */
int tcp_recv(int socket_id, uint8_t *buffer, uint32_t max_len, uint32_t flags) {
    if (socket_id < 0 || socket_id >= MAX_TCP_SOCKETS) return -1;
    tcp_socket_t *sock = &g_tcp_sockets[socket_id];
    
    if (!sock->active) return -1;
    
    /* Blocking read */
    while (sock->rx_len == 0) {
        if (sock->state == TCP_STATE_CLOSED || sock->state == TCP_STATE_CLOSE_WAIT || sock->closed_by_peer) {
            break;
        }
        
        if (flags & O_NONBLOCK) return -1; /* EAGAIN conceptually */
        
        wait_event_interruptible(&sock->rx_wait_queue);
    }
    
    if (sock->rx_len == 0) return 0; /* EOF or Closed */
    
    uint32_t read_len = sock->rx_len;
    if (read_len > max_len) read_len = max_len;
    
    kmemcpy(buffer, sock->rx_buffer, read_len);
    
    /* Shift remaining data in rx_buffer */
    if (sock->rx_len > read_len) {
        for (uint32_t i = 0; i < sock->rx_len - read_len; i++) {
            sock->rx_buffer[i] = sock->rx_buffer[read_len + i];
        }
    }
    sock->rx_len -= read_len;
    sock->window = TCP_RX_BUF_SIZE - sock->rx_len; /* Update window size dynamically */
    
    return read_len;
}

void tcp_close(int socket_id) {
    if (socket_id < 0 || socket_id >= MAX_TCP_SOCKETS) return;
    tcp_socket_t *sock = &g_tcp_sockets[socket_id];
    
    if (!sock->active) return;
    
    if (sock->state == TCP_STATE_ESTABLISHED || sock->state == TCP_STATE_SYN_SENT) {
        tcp_send_segment(sock, sock->snd_nxt, TCP_FIN | TCP_ACK, NULL, 0);
        sock->snd_nxt++; /* FIN consumes a sequence number */
        sock->state = TCP_STATE_FIN_WAIT_1;
    } else if (sock->state == TCP_STATE_CLOSE_WAIT) {
        tcp_send_segment(sock, sock->snd_nxt, TCP_FIN | TCP_ACK, NULL, 0);
        sock->snd_nxt++;
        sock->state = TCP_STATE_LAST_ACK;
    } else {
        tcp_destroy_socket(sock);
    }
}

bool tcp_is_connected(int socket_id) {
    if (socket_id < 0 || socket_id >= MAX_TCP_SOCKETS) return false;
    tcp_socket_t *sock = &g_tcp_sockets[socket_id];
    return sock->active && (sock->state == TCP_STATE_ESTABLISHED);
}

/* Safe TCP Options Parser */
static void tcp_parse_options(tcp_socket_t *sock, const uint8_t *options, uint32_t len) {
    uint32_t i = 0;
    while (i < len) {
        uint8_t kind = options[i];
        
        /* Kind 0: End of Option List */
        if (kind == 0) break;
        
        /* Kind 1: No-Operation (NOP) */
        if (kind == 1) {
            i++;
            continue;
        }
        
        /* For other options, read length */
        if (i + 1 >= len) break; /* Buffer overrun protection */
        uint8_t opt_len = options[i + 1];
        
        if (opt_len < 2 || i + opt_len > len) break; /* Invalid length or overrun */
        
        /* Process specific options */
        if (kind == 2 && opt_len == 4) {
            /* Maximum Segment Size (MSS) */
            uint16_t mss = (options[i + 2] << 8) | options[i + 3];
            if (mss > 0) {
                sock->mss = mss;
            }
        } else if (kind == 3 && opt_len == 3) {
            /* Window Scale */
            uint8_t scale = options[i + 2];
            if (scale > 14) scale = 14; /* RFC 1323 limit */
            sock->snd_wscale = scale;
            sock->rcv_wscale = 0; /* Hardcode our scale to 0 for now */
        }
        
        i += opt_len;
    }
}

/* Receive and Process incoming TCP Segments */
void tcp_receive(const uint8_t *packet, uint32_t len, uint32_t src_ip, uint32_t dest_ip) {
    (void)dest_ip;
    if (len < sizeof(tcp_hdr_t)) return;
    
    tcp_hdr_t *hdr = (tcp_hdr_t*)packet;
    uint16_t src_port = ntohs(hdr->src_port);
    uint16_t dest_port = ntohs(hdr->dest_port);
    uint32_t seq = ntohl(hdr->seq_num);
    uint32_t ack = ntohl(hdr->ack_num);
    
    uint16_t offset_words = (ntohs(hdr->data_offset_flags) >> 12) & 0x0F;
    uint16_t flags = ntohs(hdr->data_offset_flags) & 0x01FF;
    uint8_t hdr_len = offset_words * 4;
    
    if (len < hdr_len) return;
    uint32_t payload_len = len - hdr_len;
    const uint8_t *payload = packet + hdr_len;
    
    uint16_t wnd_unscaled = ntohs(hdr->window_size);
    tcp_socket_t *sock = NULL;
    for (int i = 0; i < MAX_TCP_SOCKETS; i++) {
        if (g_tcp_sockets[i].active && 
            g_tcp_sockets[i].local_port == dest_port &&
            g_tcp_sockets[i].remote_port == src_port &&
            g_tcp_sockets[i].remote_ip == src_ip) {
            sock = &g_tcp_sockets[i];
            break;
        }
    }
    
    if (!sock) {
        /* Fallback: Look for a listening socket on this port */
        for (int i = 0; i < MAX_TCP_SOCKETS; i++) {
            if (g_tcp_sockets[i].active && 
                g_tcp_sockets[i].state == TCP_STATE_LISTEN &&
                g_tcp_sockets[i].local_port == dest_port) {
                sock = &g_tcp_sockets[i];
                break;
            }
        }
    }
    
    if (!sock) return; /* Drop packet if no match */
    
    /* Process incoming ACKs and Window */
    sock->snd_wnd = ntohs(hdr->window_size);
    
    if (flags & TCP_ACK) {
        if (ack > sock->snd_una && ack <= sock->snd_nxt) {
            sock->snd_una = ack;
            sock->snd_wnd = wnd_unscaled << sock->snd_wscale; /* Apply Window Scaling */
            
            /* Remove ACKed segments from tx_queue and sample RTT (Karn's Algorithm) */
            tcp_segment_t *seg = sock->tx_queue_head;
            while (seg && (seg->seq + seg->len <= ack)) {
                /* Karn's Algorithm: Ignore retransmitted segments for RTT */
                if (seg->retry_count == 0) {
                    uint32_t rtt = timer_get_uptime_ms() - seg->send_time;
                    if (sock->smoothed_rtt == 0) {
                        sock->smoothed_rtt = rtt;
                        sock->rto = rtt * 2;
                    } else {
                        sock->smoothed_rtt = (sock->smoothed_rtt * 7 + rtt) / 8;
                        sock->rto = sock->smoothed_rtt * 2;
                    }
                    if (sock->rto < 200) sock->rto = 200; /* Min 200ms */
                    if (sock->rto > 5000) sock->rto = 5000; /* Max 5s */
                }
                
                sock->tx_inflight_bytes -= seg->len;
                sock->tx_queue_head = seg->next;
                if (seg->payload) kfree(seg->payload);
                kfree(seg);
                seg = sock->tx_queue_head;
            }
            if (!sock->tx_queue_head) {
                sock->tx_queue_tail = NULL;
                sock->tx_inflight_bytes = 0;
            }
            
            /* Congestion Control: New ACK */
            sock->dup_acks = 0;
            if (sock->cwnd < sock->ssthresh) {
                sock->cwnd += 1024; /* Slow Start */
            } else {
                sock->cwnd += (1024 * 1024) / sock->cwnd; /* Congestion Avoidance */
            }
            if (sock->cwnd > 65535) sock->cwnd = 65535;
            
            wake_up_all(&sock->tx_wait_queue);
        } else if (ack == sock->snd_una && sock->tx_queue_head && payload_len == 0) {
            /* Congestion Control: Duplicate ACK */
            sock->dup_acks++;
            if (sock->dup_acks == 3) {
                /* Fast Retransmit (Tahoe/Reno) */
                sock->ssthresh = sock->cwnd / 2;
                if (sock->ssthresh < 1024) sock->ssthresh = 1024;
                sock->cwnd = sock->ssthresh; /* Reno-ish Fast Recovery start */
                
                tcp_segment_t *seg = sock->tx_queue_head;
                tcp_send_segment(sock, seg->seq, seg->flags, seg->payload, seg->len);
                seg->retry_count++;
            }
        } else if (sock->snd_wnd > 0) {
            /* Zero window probe or window update */
            wake_up_all(&sock->tx_wait_queue);
        }
    }
    
    /* Dynamic Window Calculation */
    if (sock->snd_wnd != (wnd_unscaled << sock->snd_wscale)) {
        sock->snd_wnd = wnd_unscaled << sock->snd_wscale;
    }
    
    /* Process incoming RST */
    if (flags & TCP_RST) {
        tcp_destroy_socket(sock);
        return;
    }
    
    sock->last_activity_tick = timer_get_uptime_ms();
    
    /* State Machine */
    switch (sock->state) {
        case TCP_STATE_LISTEN:
            if (flags & TCP_SYN) {
                /* Check backlog limit */
                int pending = 0;
                for (int i = 0; i < MAX_TCP_SOCKETS; i++) {
                    if (g_tcp_sockets[i].active && g_tcp_sockets[i].parent_socket == (sock - g_tcp_sockets)) {
                        pending++;
                    }
                }
                if (sock->syn_tail - sock->syn_head >= sock->backlog && sock->backlog > 0) {
                    serial_printf("[TCP] Listen backlog full, dropping SYN on port %d\n", sock->local_port);
                    return; /* Drop SYN if backlog full */
                }
                
                /* Clone socket */
                int child_id = tcp_socket_open();
                if (child_id >= 0) {
                    serial_printf("[TCP] SYN received on Listen socket %d, created child %d in SYN_RCVD\n", sock - g_tcp_sockets, child_id);
                    tcp_socket_t *child = &g_tcp_sockets[child_id];
                    child->local_port = sock->local_port;
                    child->local_ip = sock->local_ip;
                    child->remote_port = src_port;
                    child->remote_ip = src_ip;
                    child->parent_socket = sock - g_tcp_sockets;
                    
                    child->rcv_nxt = seq + 1;
                    child->snd_nxt = timer_get_ticks();
                    child->snd_una = child->snd_nxt;
                    child->state = TCP_STATE_SYN_RECEIVED;
                    
                    /* Parse TCP Options on SYN */
                    if (hdr_len > sizeof(tcp_hdr_t)) {
                        tcp_parse_options(child, packet + sizeof(tcp_hdr_t), hdr_len - sizeof(tcp_hdr_t));
                    }
                    
                    sock->syn_queue[sock->syn_tail % 16] = child_id;
                    sock->syn_tail++;
                    
                    tcp_send_segment(child, child->snd_nxt, TCP_SYN | TCP_ACK, NULL, 0);
                    child->snd_nxt++;
                }
            }
            break;
            
        case TCP_STATE_SYN_RECEIVED:
            if (flags & TCP_ACK) {
                sock->state = TCP_STATE_ESTABLISHED;
                sock->connected = true;
                
                if (sock->parent_socket >= 0) {
                    tcp_socket_t *parent = &g_tcp_sockets[sock->parent_socket];
                    /* Remove from SYN queue (best effort) */
                    for (int i = parent->syn_head; i < parent->syn_tail; i++) {
                        if (parent->syn_queue[i % 16] == (sock - g_tcp_sockets)) {
                            /* We don't bother shifting, just mark -1 if we wanted to */
                            break;
                        }
                    }
                    /* Add to Accept Queue */
                    if (parent->accept_tail - parent->accept_head < 16) {
                        parent->accept_queue[parent->accept_tail % 16] = sock - g_tcp_sockets;
                        parent->accept_tail++;
                        serial_printf("[TCP] Child %d ESTABLISHED, pushing to accept_queue of parent %d\n", sock - g_tcp_sockets, sock->parent_socket);
                        wake_up(&parent->accept_wait_queue);
                    }
                }
            }
            break;
            
        case TCP_STATE_SYN_SENT:
            if ((flags & (TCP_SYN | TCP_ACK)) == (TCP_SYN | TCP_ACK)) {
                sock->state = TCP_STATE_ESTABLISHED;
                sock->connected = true;
                sock->rcv_nxt = seq + 1; /* SYN consumes 1 */
                
                /* Parse TCP Options on SYN-ACK */
                if (hdr_len > sizeof(tcp_hdr_t)) {
                    tcp_parse_options(sock, packet + sizeof(tcp_hdr_t), hdr_len - sizeof(tcp_hdr_t));
                }
                
                tcp_send_segment(sock, sock->snd_nxt, TCP_ACK, NULL, 0);
            } else if (flags & TCP_RST) {
                tcp_destroy_socket(sock);
            }
            break;
            
        case TCP_STATE_ESTABLISHED:
            if (payload_len > 0) {
                /* Sequence control */
                if (seq == sock->rcv_nxt) {
                    /* In-Order Segment */
                    if (sock->rx_len + payload_len <= TCP_RX_BUF_SIZE) {
                        kmemcpy(sock->rx_buffer + sock->rx_len, payload, payload_len);
                        sock->rx_len += payload_len;
                        sock->rcv_nxt += payload_len;
                        
                        /* Check Out-Of-Order Queue for contiguous segments */
                        while (sock->rx_ooo_queue_head && sock->rx_ooo_queue_head->seq <= sock->rcv_nxt) {
                            tcp_segment_t *ooo = sock->rx_ooo_queue_head;
                            sock->rx_ooo_queue_head = ooo->next;
                            
                            /* If exactly the next expected segment */
                            if (ooo->seq == sock->rcv_nxt && sock->rx_len + ooo->len <= TCP_RX_BUF_SIZE) {
                                kmemcpy(sock->rx_buffer + sock->rx_len, ooo->payload, ooo->len);
                                sock->rx_len += ooo->len;
                                sock->rcv_nxt += ooo->len;
                            }
                            
                            if (ooo->payload) kfree(ooo->payload);
                            kfree(ooo);
                        }
                        
                        if (payload_len > 0) {
                            sock->window = (TCP_RX_BUF_SIZE - sock->rx_len) >> sock->rcv_wscale; /* Dynamic Window scaled */
                            
                            wake_up_all(&sock->rx_wait_queue);
                        }
                    }
                    /* Send ACK for new rcv_nxt */
                    tcp_send_segment(sock, sock->snd_nxt, TCP_ACK, NULL, 0);
                    
                } else if (seq > sock->rcv_nxt && seq < sock->rcv_nxt + sock->window) {
                    /* Out-of-Order Segment - Queue it */
                    tcp_segment_t *seg = kzalloc(sizeof(tcp_segment_t));
                    seg->seq = seq;
                    seg->len = payload_len;
                    seg->payload = kmalloc(payload_len);
                    kmemcpy(seg->payload, payload, payload_len);
                    seg->next = NULL;
                    
                    /* Insert Sorted into rx_ooo_queue */
                    if (!sock->rx_ooo_queue_head || sock->rx_ooo_queue_head->seq > seq) {
                        seg->next = sock->rx_ooo_queue_head;
                        sock->rx_ooo_queue_head = seg;
                    } else {
                        tcp_segment_t *curr = sock->rx_ooo_queue_head;
                        while (curr->next && curr->next->seq < seq) {
                            curr = curr->next;
                        }
                        if (curr->seq != seq) { /* Avoid exact duplicates */
                            seg->next = curr->next;
                            curr->next = seg;
                        } else {
                            if (seg->payload) kfree(seg->payload);
                            kfree(seg);
                        }
                    }
                    
                    /* Send DUP ACK for our rcv_nxt to trigger Fast Retransmit */
                    tcp_send_segment(sock, sock->snd_nxt, TCP_ACK, NULL, 0);
                    
                } else {
                    /* Old Duplicate Segment or Outside Window */
                    tcp_send_segment(sock, sock->snd_nxt, TCP_ACK, NULL, 0);
                }
            }
            
            if (flags & TCP_FIN) {
                sock->closed_by_peer = true;
                sock->rcv_nxt++; /* FIN consumes 1 */
                
                wake_up_all(&sock->rx_wait_queue);
                
                tcp_send_segment(sock, sock->snd_nxt, TCP_ACK, NULL, 0);
                sock->state = TCP_STATE_CLOSE_WAIT;
            }
            break;
            
        case TCP_STATE_FIN_WAIT_1:
            if (flags & TCP_FIN) {
                sock->rcv_nxt++;
                tcp_send_segment(sock, sock->snd_nxt, TCP_ACK, NULL, 0);
                if (flags & TCP_ACK) {
                    sock->state = TCP_STATE_TIME_WAIT;
                } else {
                    sock->state = TCP_STATE_CLOSING;
                }
            } else if (flags & TCP_ACK) {
                sock->state = TCP_STATE_FIN_WAIT_2;
            }
            break;
            
        case TCP_STATE_FIN_WAIT_2:
            if (flags & TCP_FIN) {
                sock->rcv_nxt++;
                tcp_send_segment(sock, sock->snd_nxt, TCP_ACK, NULL, 0);
                sock->state = TCP_STATE_TIME_WAIT; /* In a real stack, a timer runs here */
                
                /* For simplicity, we just clean up */
                tcp_destroy_socket(sock);
            }
            break;
            
        case TCP_STATE_LAST_ACK:
            if (flags & TCP_ACK) {
                tcp_destroy_socket(sock);
            }
            break;
            
        case TCP_STATE_CLOSING:
            if (flags & TCP_ACK) {
                sock->state = TCP_STATE_TIME_WAIT;
                
                /* For simplicity, we just clean up */
                tcp_destroy_socket(sock);
            }
            break;
            
        default:
            break;
    }
}

/* Poll socket for events */
int tcp_poll(int socket_id, short events) {
    if (socket_id < 0 || socket_id >= MAX_TCP_SOCKETS) return POLLERR;
    tcp_socket_t *sock = &g_tcp_sockets[socket_id];
    
    if (!sock->active) return POLLERR;
    
    int revents = 0;
    
    if (events & POLLIN) {
        if (sock->rx_len > 0) revents |= POLLIN;
        if (sock->state == TCP_STATE_CLOSED || sock->state == TCP_STATE_CLOSE_WAIT || sock->closed_by_peer) {
            revents |= POLLHUP;
        }
    }
    
    if (events & POLLOUT) {
        uint32_t inflight = sock->snd_nxt - sock->snd_una;
        uint32_t eff_wnd = (sock->snd_wnd < sock->cwnd) ? sock->snd_wnd : sock->cwnd;
        
        if (inflight < eff_wnd && sock->tx_inflight_bytes < TCP_TX_BUF_SIZE) {
            if (sock->state == TCP_STATE_ESTABLISHED) {
                revents |= POLLOUT;
            }
        }
    }
    
    return revents;
}

