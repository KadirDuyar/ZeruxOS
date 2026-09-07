/* =============================================================================
 * ZeruX OS - POSIX-like Socket API Wrapper
 * File: kernel/include/socket.h
 * =============================================================================
 */

#ifndef SOCKET_H
#define SOCKET_H

#include <stdint.h>
#include <stddef.h>

#define AF_INET     2
#define SOCK_STREAM 1
#define SOCK_DGRAM  2

/* Generic socket address structure */
struct sockaddr {
    uint16_t sa_family;
    char     sa_data[14];
};

/* IPv4 socket address structure */
struct sockaddr_in {
    uint16_t sin_family;
    uint16_t sin_port;
    uint32_t sin_addr;
    char     sin_zero[8];
};

/* BSD Socket Abstraction */
struct socket;

struct sock_ops {
    int (*bind)(struct socket *sock, const struct sockaddr *addr, uint32_t addrlen);
    int (*listen)(struct socket *sock, int backlog);
    int (*accept)(struct socket *sock, struct socket *newsock, struct sockaddr *addr, uint32_t *addrlen);
    int (*connect)(struct socket *sock, const struct sockaddr *addr, uint32_t addrlen);
    int (*sendmsg)(struct socket *sock, const void *buf, size_t len, int flags);
    int (*recvmsg)(struct socket *sock, void *buf, size_t len, int flags);
    int (*poll)(struct socket *sock, short events);
    int (*close)(struct socket *sock);
};

struct socket {
    int state;
    short type;
    struct sock_ops *ops;
    void *sk; /* Protocol specific socket (e.g. tcp_socket_t) */
    void *file; /* pointer to vfs_node */
};

int socket(int domain, int type, int protocol);
int bind(int sockfd, const struct sockaddr *addr, uint32_t addrlen);
int listen(int sockfd, int backlog);
int accept(int sockfd, struct sockaddr *addr, uint32_t *addrlen);
int connect(int sockfd, const struct sockaddr *addr, uint32_t addrlen);
int send(int sockfd, const void *buf, size_t len, int flags);
int recv(int sockfd, void *buf, size_t len, int flags);
int close(int sockfd);

#endif /* SOCKET_H */