/* =============================================================================
 * ZeruX OS - TCP Timers Module
 * File: kernel/net/tcp_timer.c
 * =============================================================================
 */

#include "tcp.h"
#include "timer.h"

/* External state from tcp.c */
extern tcp_socket_t g_tcp_sockets[MAX_TCP_SOCKETS];

/*
 * Retransmission Timer check.
 * Handles Timeouts and Exponential Backoff.
 */
static void tcp_timer_retransmit(tcp_socket_t *sock, uint32_t current_ms) {
    if (!sock->tx_queue_head) return;
    
    tcp_segment_t *seg = sock->tx_queue_head;
    bool backed_off = false;
    
    while (seg && sock->active) {
        if (current_ms - seg->send_time >= sock->rto) {
            if (seg->retry_count >= 5) {
                /* Max retries reached, kill socket */
                tcp_send_segment(sock, sock->snd_nxt, TCP_RST, NULL, 0);
                tcp_destroy_socket(sock);
                break;
            } else {
                /* Retransmit segment */
                seg->retry_count++;
                seg->send_time = current_ms;
                
                if (!backed_off) {
                    /* Congestion Control: Timeout */
                    sock->ssthresh = sock->cwnd / 2;
                    if (sock->ssthresh < 1024) sock->ssthresh = 1024;
                    sock->cwnd = 1024; /* Slow Start Fallback */
                    sock->dup_acks = 0;
                    
                    sock->rto *= 2; /* Exponential backoff (once per cycle) */
                    if (sock->rto > 60000) sock->rto = 60000; /* Max 60s RTO */
                    backed_off = true;
                }
                
                tcp_send_segment(sock, seg->seq, seg->flags, seg->payload, seg->len);
            }
        }
        seg = seg->next;
    }
}

/*
 * Keep-alive Timer check.
 * Sends an empty ACK if the socket has been idle.
 */
static void tcp_timer_keepalive(tcp_socket_t *sock, uint32_t current_ms) {
    if (sock->state == TCP_STATE_ESTABLISHED && sock->snd_una == sock->snd_nxt) {
        if (current_ms - sock->last_activity_tick >= 10000) { /* 10 seconds idle */
            tcp_send_segment(sock, sock->snd_nxt, TCP_ACK, NULL, 0);
            sock->last_activity_tick = current_ms;
        }
    }
}

/*
 * TIME_WAIT Timer check.
 * Closes the socket fully after 2 MSL.
 */
static void tcp_timer_timewait(tcp_socket_t *sock, uint32_t current_ms) {
    if (sock->state == TCP_STATE_TIME_WAIT) {
        if (current_ms - sock->last_activity_tick >= 5000) { /* 5 seconds MSL */
            tcp_destroy_socket(sock);
        }
    }
}

/*
 * Global TCP Timer Polling.
 * Called periodically by the kernel main loop or scheduler tick.
 */
void tcp_timer_poll(void) {
    uint32_t current_ms = timer_get_uptime_ms();
    
    for (int i=0; i<MAX_TCP_SOCKETS; i++) {
        tcp_socket_t *sock = &g_tcp_sockets[i];
        if (!sock->active) continue;
        
        tcp_timer_retransmit(sock, current_ms);
        if (!sock->active) continue;
        
        tcp_timer_keepalive(sock, current_ms);
        if (!sock->active) continue;
        
        tcp_timer_timewait(sock, current_ms);
    }
}
