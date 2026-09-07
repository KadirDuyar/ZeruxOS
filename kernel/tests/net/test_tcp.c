/* =============================================================================
 * ZeruX OS - TCP Regression Tests
 * File: kernel/tests/net/test_tcp.c
 * =============================================================================
 */


#include "tcp.h"
#include "serial.h"
#include "poll.h"
#include "fcntl.h"
#include "wait.h"

extern int tcp_socket_open(void);
extern void tcp_close(int socket_id);

static void test_tcp_connect_disconnect(void) {
    serial_printf("[TEST] TCP Connect/Disconnect: ");
    
    int sock_id = tcp_socket_open();
    if (sock_id < 0) {
        serial_printf("FAIL (Could not open socket)\n");
        return;
    }
    
    tcp_close(sock_id);
    
    if (g_tcp_sockets[sock_id].active == false && g_tcp_sockets[sock_id].state == TCP_STATE_CLOSED) {
        serial_printf("PASS\n");
    } else {
        serial_printf("FAIL (State not closed properly)\n");
    }
}

static void test_tcp_wait_queue(void) {
    serial_printf("[TEST] TCP Unified Wait Queues: ");
    
    int sock_id = tcp_socket_open();
    if (sock_id < 0) {
        serial_printf("FAIL\n");
        return;
    }
    
    tcp_socket_t *sock = &g_tcp_sockets[sock_id];
    
    /* Simulate adding to wait queue manually if needed, 
       but wait_queue_head_t should be initialized safely */
    if (sock->tx_wait_queue.head == NULL && sock->rx_wait_queue.head == NULL && sock->accept_wait_queue.head == NULL) {
        serial_printf("PASS (Initialized safely)\n");
    } else {
        serial_printf("FAIL (Not initialized)\n");
    }
    
    tcp_close(sock_id);
}

static void test_tcp_nonblocking(void) {
    serial_printf("[TEST] TCP Non-Blocking Read (EAGAIN): ");
    
    int sock_id = tcp_socket_open();
    if (sock_id < 0) {
        serial_printf("FAIL\n");
        return;
    }
    
    tcp_socket_t *sock = &g_tcp_sockets[sock_id];
    
    /* Mock established state and empty rx buffer to force EAGAIN */
    sock->state = TCP_STATE_ESTABLISHED;
    sock->rx_len = 0;
    
    /* Attempt to read with O_NONBLOCK */
    uint8_t buffer[64];
    int ret = tcp_recv(sock_id, buffer, 64, O_NONBLOCK);
    
    sock->state = TCP_STATE_CLOSED;
    
    if (ret == -1) {
        serial_printf("PASS (Returned EAGAIN equivalent)\n");
    } else {
        serial_printf("FAIL (Expected -1, got %d)\n", ret);
    }
    
    tcp_close(sock_id);
}

static void test_tcp_poll_api(void) {
    serial_printf("[TEST] TCP sys_poll/tcp_poll: ");
    
    int sock_id = tcp_socket_open();
    if (sock_id < 0) {
        serial_printf("FAIL\n");
        return;
    }
    
    tcp_socket_t *sock = &g_tcp_sockets[sock_id];
    
    /* We mock established state to see if POLLOUT works */
    sock->state = TCP_STATE_ESTABLISHED;
    sock->tx_inflight_bytes = 0;
    
    int revents = tcp_poll(sock_id, POLLIN | POLLOUT);
    if ((revents & POLLOUT) && !(revents & POLLIN)) {
        serial_printf("PASS (Reported POLLOUT correctly)\n");
    } else {
        serial_printf("FAIL (revents=%x)\n", revents);
    }
    
    sock->state = TCP_STATE_CLOSED;
    tcp_close(sock_id);
}

static void test_tcp_window_scaling(void) {
    serial_printf("[TEST] TCP Window Scaling & MSS: ");
    
    int sock_id = tcp_socket_open();
    if (sock_id < 0) {
        serial_printf("FAIL\n");
        return;
    }
    
    tcp_socket_t *sock = &g_tcp_sockets[sock_id];
    
    if (sock->mss == 1024 && sock->rcv_wscale == 0 && sock->snd_wscale == 0) {
        serial_printf("PASS (Defaults applied)\n");
    } else {
        serial_printf("FAIL (MSS=%d, wscale=%d)\n", sock->mss, sock->rcv_wscale);
    }
    
    tcp_close(sock_id);
}

void test_net_all(void) {
    serial_printf("\n=== Running Network Regression Tests ===\n");
    test_tcp_connect_disconnect();
    test_tcp_wait_queue();
    test_tcp_nonblocking();
    test_tcp_poll_api();
    test_tcp_window_scaling();
    serial_printf("========================================\n\n");
}
