/* =============================================================================
 * ZeruX OS - VFS Poll & Select Definitions
 * File: kernel/include/poll.h
 * =============================================================================
 */

#ifndef POLL_H
#define POLL_H

#include <stdint.h>

/* Poll events */
#define POLLIN      0x001   /* There is data to read */
#define POLLPRI     0x002   /* There is urgent data to read */
#define POLLOUT     0x004   /* Writing now will not block */
#define POLLERR     0x008   /* Error condition */
#define POLLHUP     0x010   /* Hung up */
#define POLLNVAL    0x020   /* Invalid polling request */

/* pollfd struct for sys_poll */
struct pollfd {
    int fd;         /* file descriptor */
    short events;   /* requested events */
    short revents;  /* returned events */
};

/* File Descriptor Set for select */
#define FD_SETSIZE 256
typedef struct {
    uint32_t fds_bits[FD_SETSIZE / 32];
} fd_set;

#define FD_SET(fd, set)   ((set)->fds_bits[(fd) / 32] |= (1 << ((fd) % 32)))
#define FD_CLR(fd, set)   ((set)->fds_bits[(fd) / 32] &= ~(1 << ((fd) % 32)))
#define FD_ISSET(fd, set) (((set)->fds_bits[(fd) / 32] & (1 << ((fd) % 32))) != 0)
#define FD_ZERO(set)      do { for (int i = 0; i < FD_SETSIZE / 32; i++) (set)->fds_bits[i] = 0; } while (0)

struct timeval {
    uint32_t tv_sec;
    uint32_t tv_usec;
};

/* VFS Poll Callback Signature */
struct vfs_node;
typedef int (*vfs_poll_cb_t)(struct vfs_node *node, short events);

#endif /* POLL_H */
