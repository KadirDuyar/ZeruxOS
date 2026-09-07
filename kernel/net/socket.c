/* =============================================================================
 * ZeruX OS - POSIX-like Socket API Wrapper Implementation
 * File: kernel/net/socket.c
 * =============================================================================
 */

#include "socket.h"
#include "tcp.h"
#include "net.h"
#include "vfs.h"
#include "poll.h"
#include "kheap.h"

#define MAX_BSD_SOCKETS 64
static struct socket g_sockets[MAX_BSD_SOCKETS];

/* Allocate a BSD socket structure */
static struct socket* alloc_socket(void) {
    for (int i = 0; i < MAX_BSD_SOCKETS; i++) {
        if (g_sockets[i].state == 0) {
            g_sockets[i].state = 1; /* Allocated */
            g_sockets[i].ops = NULL;
            g_sockets[i].sk = NULL;
            g_sockets[i].file = NULL;
            return &g_sockets[i];
        }
    }
    return NULL;
}

static void free_socket(struct socket *sock) {
    if (sock) {
        sock->state = 0;
        sock->sk = NULL;
        sock->ops = NULL;
        sock->file = NULL;
    }
}

/* =============================================================================
 * VFS Operations for Sockets
 * ============================================================================= */
static int32_t socket_vfs_read(struct vfs_node *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
    (void)offset;
    struct socket *sock = (struct socket*)node->device_data;
    if (!sock || !sock->ops || !sock->ops->recvmsg) return -1;
    return sock->ops->recvmsg(sock, buffer, size, node->flags);
}

static int32_t socket_vfs_write(struct vfs_node *node, uint32_t offset, uint32_t size, const uint8_t *buffer) {
    (void)offset;
    struct socket *sock = (struct socket*)node->device_data;
    if (!sock || !sock->ops || !sock->ops->sendmsg) return -1;
    return sock->ops->sendmsg(sock, buffer, size, node->flags);
}

static void socket_vfs_close(struct vfs_node *node) {
    struct socket *sock = (struct socket*)node->device_data;
    if (sock) {
        if (sock->ops && sock->ops->close) {
            sock->ops->close(sock);
        }
        free_socket(sock);
    }
    kfree(node);
}

static int socket_vfs_poll(struct vfs_node *node, short events) {
    struct socket *sock = (struct socket*)node->device_data;
    if (!sock || !sock->ops || !sock->ops->poll) return 0; // or POLLERR
    return sock->ops->poll(sock, events);
}

static vfs_node_ops_t g_socket_ops = {
    .read = socket_vfs_read,
    .write = socket_vfs_write,
    .open = NULL,
    .close = socket_vfs_close,
    .readdir = NULL,
    .finddir = NULL,
    .poll = socket_vfs_poll
};

/* =============================================================================
 * TCP Protocol Socket Ops
 * ============================================================================= */
static int tcp_sock_bind(struct socket *sock, const struct sockaddr *addr, uint32_t addrlen) {
    if (addrlen < sizeof(struct sockaddr_in)) return -1;
    const struct sockaddr_in *sin = (const struct sockaddr_in *)addr;
    int tcp_id = (int)(uint32_t)sock->sk;
    return tcp_bind(tcp_id, ntohs(sin->sin_port));
}

static int tcp_sock_listen(struct socket *sock, int backlog) {
    int tcp_id = (int)(uint32_t)sock->sk;
    return tcp_listen(tcp_id, backlog);
}

static int tcp_sock_accept(struct socket *sock, struct socket *newsock, struct sockaddr *addr, uint32_t *addrlen) {
    int tcp_id = (int)(uint32_t)sock->sk;
    int child = tcp_accept(tcp_id);
    if (child < 0) return -1;
    
    newsock->sk = (void*)(uint32_t)child;
    
    if (addr && addrlen && *addrlen >= sizeof(struct sockaddr_in)) {
        struct sockaddr_in *sin = (struct sockaddr_in *)addr;
        sin->sin_family = AF_INET;
        *addrlen = sizeof(struct sockaddr_in);
    }
    return child;
}

static int tcp_sock_connect(struct socket *sock, const struct sockaddr *addr, uint32_t addrlen) {
    if (addrlen < sizeof(struct sockaddr_in)) return -1;
    const struct sockaddr_in *sin = (const struct sockaddr_in *)addr;
    int tcp_id = (int)(uint32_t)sock->sk;
    return tcp_connect(tcp_id, ntohl(sin->sin_addr), ntohs(sin->sin_port));
}

static int tcp_sock_sendmsg(struct socket *sock, const void *buf, size_t len, int flags) {
    int tcp_id = (int)(uint32_t)sock->sk;
    return tcp_send(tcp_id, buf, len, flags);
}

static int tcp_sock_recvmsg(struct socket *sock, void *buf, size_t len, int flags) {
    int tcp_id = (int)(uint32_t)sock->sk;
    return tcp_recv(tcp_id, buf, len, flags);
}

static int tcp_sock_poll(struct socket *sock, short events) {
    int tcp_id = (int)(uint32_t)sock->sk;
    return tcp_poll(tcp_id, events);
}

static int tcp_sock_close(struct socket *sock) {
    int tcp_id = (int)(uint32_t)sock->sk;
    tcp_close(tcp_id);
    return 0;
}

struct sock_ops g_tcp_sock_ops = {
    .bind = tcp_sock_bind,
    .listen = tcp_sock_listen,
    .accept = tcp_sock_accept,
    .connect = tcp_sock_connect,
    .sendmsg = tcp_sock_sendmsg,
    .recvmsg = tcp_sock_recvmsg,
    .poll = tcp_sock_poll,
    .close = tcp_sock_close
};

/* =============================================================================
 * BSD Socket API
 * ============================================================================= */
int socket(int domain, int type, int protocol) {
    (void)protocol;
    if (domain != AF_INET || type != SOCK_STREAM) {
        return -1; /* Only IPv4 TCP supported for now */
    }
    
    int tcp_id = tcp_socket_open();
    if (tcp_id < 0) return -1;
    
    struct socket *sock = alloc_socket();
    if (!sock) {
        tcp_close(tcp_id);
        return -1;
    }
    
    sock->type = type;
    sock->ops = &g_tcp_sock_ops;
    sock->sk = (void*)(uint32_t)tcp_id;
    
    vfs_node_t *node = (vfs_node_t *)kmalloc(sizeof(vfs_node_t));
    if (!node) {
        free_socket(sock);
        tcp_close(tcp_id);
        return -1;
    }
    
    /* Zero out node memory and set name safely */
    uint8_t *node_ptr = (uint8_t*)node;
    for(uint32_t i=0; i<sizeof(vfs_node_t); i++) node_ptr[i] = 0;
    
    const char *s_name = "socket";
    for (int i = 0; s_name[i]; i++) node->name[i] = s_name[i];
    
    node->flags = 0;
    node->device_data = sock; /* Link VFS to BSD socket */
    node->ops = &g_socket_ops;
    
    sock->file = node;
    
    int fd = vfs_alloc_fd(node, 0);
    if (fd < 0) {
        free_socket(sock);
        tcp_close(tcp_id);
        kfree(node);
        return -1;
    }
    
    return fd;
}

int bind(int sockfd, const struct sockaddr *addr, uint32_t addrlen) {
    vfs_node_t *node = vfs_get_node(sockfd);
    if (!node || node->ops != &g_socket_ops) return -1;
    struct socket *sock = (struct socket*)node->device_data;
    if (!sock || !sock->ops || !sock->ops->bind) return -1;
    return sock->ops->bind(sock, addr, addrlen);
}

int listen(int sockfd, int backlog) {
    vfs_node_t *node = vfs_get_node(sockfd);
    if (!node || node->ops != &g_socket_ops) return -1;
    struct socket *sock = (struct socket*)node->device_data;
    if (!sock || !sock->ops || !sock->ops->listen) return -1;
    return sock->ops->listen(sock, backlog);
}

int accept(int sockfd, struct sockaddr *addr, uint32_t *addrlen) {
    vfs_node_t *node = vfs_get_node(sockfd);
    if (!node || node->ops != &g_socket_ops) return -1;
    struct socket *sock = (struct socket*)node->device_data;
    if (!sock || !sock->ops || !sock->ops->accept) return -1;
    
    struct socket *newsock = alloc_socket();
    if (!newsock) return -1;
    newsock->type = sock->type;
    newsock->ops = sock->ops;
    
    int ret = sock->ops->accept(sock, newsock, addr, addrlen);
    if (ret < 0) {
        free_socket(newsock);
        return -1;
    }
    
    vfs_node_t *cnode = (vfs_node_t *)kmalloc(sizeof(vfs_node_t));
    if (!cnode) {
        if (newsock->ops->close) newsock->ops->close(newsock);
        free_socket(newsock);
        return -1;
    }
    
    uint8_t *cnode_ptr = (uint8_t*)cnode;
    for(uint32_t i=0; i<sizeof(vfs_node_t); i++) cnode_ptr[i] = 0;
    
    const char *c_name = "socket_child";
    for (int i=0; c_name[i]; i++) cnode->name[i] = c_name[i];
    
    cnode->ops = &g_socket_ops;
    cnode->device_data = newsock;
    newsock->file = cnode;
    
    int cfd = vfs_alloc_fd(cnode, 0);
    if (cfd < 0) {
        if (newsock->ops->close) newsock->ops->close(newsock);
        free_socket(newsock);
        kfree(cnode);
        return -1;
    }
    
    return cfd;
}

int connect(int sockfd, const struct sockaddr *addr, uint32_t addrlen) {
    vfs_node_t *node = vfs_get_node(sockfd);
    if (!node || node->ops != &g_socket_ops) return -1;
    struct socket *sock = (struct socket*)node->device_data;
    if (!sock || !sock->ops || !sock->ops->connect) return -1;
    return sock->ops->connect(sock, addr, addrlen);
}

int send(int sockfd, const void *buf, size_t len, int flags) {
    (void)flags; /* Handled via node->flags or sendmsg if passed */
    return vfs_write(sockfd, buf, len);
}

int recv(int sockfd, void *buf, size_t len, int flags) {
    (void)flags; /* Handled via node->flags or recvmsg if passed */
    return vfs_read(sockfd, buf, len);
}

int close(int sockfd) {
    vfs_close(sockfd);
    return 0;
}
