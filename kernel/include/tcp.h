/* =============================================================================
 * ZeruX OS - TCP (Transmission Control Protocol) Header & State Machine
 * File: kernel/include/tcp.h
 * =============================================================================
 */

#ifndef TCP_H
#define TCP_H

#include <stdint.h>
#include <stdbool.h>
#include "net.h"
#include "wait.h"

/* TCP Flags */
#define TCP_FIN  0x01
#define TCP_SYN  0x02
#define TCP_RST  0x04
#define TCP_PSH  0x08
#define TCP_ACK  0x10
#define TCP_URG  0x20

#pragma pack(push, 1)

/* TCP Header (20 bytes minimum) */
typedef struct {
    uint16_t src_port;
    uint16_t dest_port;
    uint32_t seq_num;
    uint32_t ack_num;
    uint16_t data_offset_flags; /* 4 bits data offset, 3 bits res, 9 bits flags */
    uint16_t window_size;
    uint16_t checksum;
    uint16_t urgent_ptr;
} tcp_hdr_t;

/* TCP Pseudo Header for Checksum Calculation */
typedef struct {
    uint32_t src_ip;
    uint32_t dest_ip;
    uint8_t  reserved;
    uint8_t  protocol;
    uint16_t tcp_length;
} tcp_pseudo_hdr_t;

#pragma pack(pop)

/* TCP Retransmission Queue Segment */
typedef struct tcp_segment {
    uint32_t seq;
    uint32_t len;
    uint8_t  flags;
    uint8_t  *payload;
    uint32_t send_time;   /* For Karn's Algorithm / RTT */
    uint8_t  retry_count;
    bool     acked;
    struct tcp_segment *next;
} tcp_segment_t;

/* TCP State Machine States */
typedef enum {
    TCP_STATE_CLOSED = 0,
    TCP_STATE_LISTEN,
    TCP_STATE_SYN_SENT,
    TCP_STATE_SYN_RECEIVED,
    TCP_STATE_ESTABLISHED,
    TCP_STATE_FIN_WAIT_1,
    TCP_STATE_FIN_WAIT_2,
    TCP_STATE_CLOSE_WAIT,
    TCP_STATE_CLOSING,
    TCP_STATE_LAST_ACK,
    TCP_STATE_TIME_WAIT
} tcp_state_t;

/* Transmission Control Block (TCB) / Socket */
#define TCP_RX_BUF_SIZE 8192
#define TCP_TX_BUF_SIZE 8192

typedef struct {
    bool        active;
    tcp_state_t state;
    uint32_t    local_ip;
    uint16_t    local_port;
    uint32_t    remote_ip;
    uint16_t    remote_port;
    
    /* Sequence / Ack numbers */
    uint32_t    snd_una;        /* Send unacknowledged */
    uint32_t    snd_nxt;        /* Send next */
    uint32_t    rcv_nxt;        /* Receive next */
    
    /* Flow & Congestion Control */
    uint16_t    window;         /* Our Receive Window */
    uint16_t    snd_wnd;        /* Peer's Receive Window (Send Window) */
    uint32_t    cwnd;           /* Congestion Window */
    uint32_t    ssthresh;       /* Slow Start Threshold */
    uint8_t     dup_acks;       /* Duplicate ACK count for Fast Retransmit */
    
    /* TCP Options */
    uint8_t     rcv_wscale;     /* Our Window Scaling factor (0-14) */
    uint8_t     snd_wscale;     /* Peer's Window Scaling factor (0-14) */
    uint16_t    mss;            /* Maximum Segment Size (default 1024) */
    
    /* Buffers & Queues */
    uint8_t     rx_buffer[TCP_RX_BUF_SIZE];
    size_t      rx_len;         /* Bytes currently in rx_buffer */
    
    tcp_segment_t *tx_queue_head;
    tcp_segment_t *tx_queue_tail;
    size_t      tx_inflight_bytes; /* Total unacked payload bytes */
    
    tcp_segment_t *rx_ooo_queue_head; /* Out-Of-Order RX Queue */
    
    /* Flags */
    bool        waiting_ack;
    bool        blocking;
    bool        connected;
    bool        closed_by_peer;
    bool        keep_alive;
    
    /* Timers & Retransmission Placeholder */
    uint32_t    last_activity_tick;
    uint32_t    last_send_tick;
    uint8_t     retry_count;
    uint32_t    rto;            /* Retransmission Timeout */
    uint32_t    smoothed_rtt;
    uint32_t    retransmit_deadline;
    
    /* Server / Listen fields */
    int         parent_socket;  /* -1 if not a child */
    int         backlog;        /* Maximum pending connections */
    
    int         accept_queue[16];
    int         accept_head;
    int         accept_tail;
    
    int         syn_queue[16];
    int         syn_head;
    int         syn_tail;
    
    /* Event Flags */
    bool        read_ready;
    bool        write_ready;
    
    /* Blocked Tasks (Wait Queues) */
    wait_queue_head_t rx_wait_queue;
    wait_queue_head_t tx_wait_queue;
    wait_queue_head_t accept_wait_queue;
} tcp_socket_t;

#define MAX_TCP_SOCKETS 64
extern tcp_socket_t g_tcp_sockets[MAX_TCP_SOCKETS];

/* Core TCP API */
void tcp_init(void);
int tcp_socket_open(void);
int tcp_bind(int socket_id, uint16_t local_port);
int tcp_listen(int socket_id, int backlog);
int tcp_accept(int socket_id);
int tcp_connect(int socket_id, uint32_t dest_ip, uint16_t dest_port);
int tcp_send(int socket_id, const uint8_t *data, uint32_t len, uint32_t flags);
int tcp_recv(int socket_id, uint8_t *buffer, uint32_t max_len, uint32_t flags);
int tcp_poll(int socket_id, short events);
void tcp_close(int socket_id);
bool tcp_is_connected(int socket_id);

void tcp_receive(const uint8_t *packet, uint32_t len, uint32_t src_ip, uint32_t dest_ip);
void tcp_timer_poll(void);

/* Internal API for split modules (tcp_timer.c) */
void tcp_destroy_socket(tcp_socket_t *sock);
void tcp_send_segment(tcp_socket_t *sock, uint32_t seq, uint8_t flags, const uint8_t *data, uint16_t data_len);

#endif /* TCP_H */
