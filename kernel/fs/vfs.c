/* =============================================================================
 * ZeruX OS — Virtual File System (VFS) Core Abstraction
 * File: kernel/fs/vfs.c
 * =============================================================================
 *
 * Sanal Dosya Sistemi (VFS):
 *   - Tüm dosya sistemi ve cihaz sürücülerini (/dev, /proc, FAT32)
 *     ortak bir yol (path) ağacında birleştirir.
 *   - File Descriptor (FD) yönetimini ve yol çözümlemesini (Path Lookup) üstlenir.
 * =============================================================================
 */

#include "vfs.h"
#include "kheap.h"
#include "serial.h"
#include "poll.h"
#include "task.h"
#include "timer.h"

#define MAX_MOUNTS 8

typedef struct {
    char        path[VFS_MAX_PATH_LEN];
    vfs_node_t *root_node;
} vfs_mount_t;

static vfs_mount_t g_mounts[MAX_MOUNTS];
static uint32_t    g_mount_count = 0;

/* Global Çekirdek File Descriptor Tablosu (0..15) */
typedef struct {
    vfs_node_t *node;
    uint32_t    offset;
    uint32_t    flags;
    int         used;
} fd_entry_t;

static fd_entry_t g_fd_table[VFS_MAX_FDS];

/* Freestanding kstrlen / kstrcmp / kstrcpy helpers */
static size_t kstrlen(const char *str) {
    size_t len = 0;
    while (str && str[len]) len++;
    return len;
}

static int kstrncmp(const char *s1, const char *s2, size_t n) {
    while (n && *s1 && (*s1 == *s2)) {
        s1++; s2++; n--;
    }
    if (n == 0) return 0;
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

static char* kstrncpy(char *dest, const char *src, size_t n) {
    size_t i = 0;
    for (i = 0; i < n - 1 && src[i]; i++) {
        dest[i] = src[i];
    }
    dest[i] = '\0';
    return dest;
}

/* =============================================================================
 * vfs_init() — VFS Katmanını ve FD Tablosunu Sıfırlar
 * =============================================================================
 */
void vfs_init(void) {
    g_mount_count = 0;
    for (int i = 0; i < MAX_MOUNTS; i++) {
        g_mounts[i].path[0] = '\0';
        g_mounts[i].root_node = NULL;
    }
    for (int i = 0; i < VFS_MAX_FDS; i++) {
        g_fd_table[i].node   = NULL;
        g_fd_table[i].offset = 0;
        g_fd_table[i].flags  = 0;
        g_fd_table[i].used   = 0;
    }

    serial_printf("===========================================\n");
    serial_printf(" ZeruX OS — Virtual File System (VFS Core)\n");
    serial_printf("===========================================\n");
    serial_printf("[VFS] Initialized VFS Layer & %d Process FD Slots.\n", VFS_MAX_FDS);
    serial_printf("===========================================\n\n");
}

/* =============================================================================
 * vfs_register_mount() — Mount Noktası Kaydeder (örn. "/", "/dev", "/proc")
 * =============================================================================
 */
int vfs_register_mount(const char *path, vfs_node_t *root_node) {
    if (!path || !root_node || g_mount_count >= MAX_MOUNTS) return -1;

    kstrncpy(g_mounts[g_mount_count].path, path, VFS_MAX_PATH_LEN);
    g_mounts[g_mount_count].root_node = root_node;
    g_mount_count++;

    serial_printf("[VFS MOUNT] Registered mount point '%s' -> Root Node '%s' (%p)\n",
                  path, root_node->name, root_node);
    return 0;
}

vfs_node_t* vfs_get_mount_info(uint32_t index, const char **out_path) {
    if (index >= g_mount_count) return NULL;
    if (out_path) *out_path = g_mounts[index].path;
    return g_mounts[index].root_node;
}

/* =============================================================================
 * vfs_lookup() — Path Çözümleme (örn. "/dev/serial0" -> serial0 vnode)
 * =============================================================================
 */
vfs_node_t* vfs_lookup(const char *path) {
    if (!path || path[0] != '/') return NULL;

    /* 1. En uzun eşleşen mount noktasını bul */
    int best_match = -1;
    size_t best_len = 0;

    for (uint32_t i = 0; i < g_mount_count; i++) {
        size_t mlen = kstrlen(g_mounts[i].path);
        if (kstrncmp(path, g_mounts[i].path, mlen) == 0) {
            if (mlen > best_len) {
                best_len = mlen;
                best_match = i;
            }
        }
    }

    if (best_match == -1) return NULL;

    vfs_node_t *curr = g_mounts[best_match].root_node;
    const char *rel_path = path + best_len;
    if (*rel_path == '/') rel_path++;
    if (*rel_path == '\0') return curr;

    /* 2. Dizin yollarını tek tek finddir ile tara */
    char comp[VFS_MAX_NAME_LEN];
    while (*rel_path != '\0') {
        int i = 0;
        while (*rel_path != '\0' && *rel_path != '/' && i < VFS_MAX_NAME_LEN - 1) {
            comp[i++] = *rel_path++;
        }
        comp[i] = '\0';
        if (*rel_path == '/') rel_path++;

        if (curr->ops && curr->ops->finddir) {
            curr = curr->ops->finddir(curr, comp);
            if (!curr) return NULL;
        } else {
            return NULL;
        }
    }

    return curr;
}

int32_t sys_fcntl(int32_t fd, int cmd, int arg) {
    if (fd < 0 || fd >= VFS_MAX_FDS || !g_fd_table[fd].used) return -1;
    
    switch (cmd) {
        case 3: /* F_GETFL */
            return g_fd_table[fd].flags;
        case 4: /* F_SETFL */
            g_fd_table[fd].flags = arg;
            if (g_fd_table[fd].node) {
                g_fd_table[fd].node->flags = arg; /* Pass to node */
            }
            return 0;
        default:
            return -1;
    }
}

/* 
 * poll() implementation
 * Returns number of fds with events, or 0 on timeout.
 */
int sys_poll(struct pollfd *fds, uint32_t nfds, int timeout_ms) {
    if (!fds || nfds == 0) return 0;
    
    uint32_t start_ms = timer_get_uptime_ms();
    int count = 0;
    
    while (1) {
        count = 0;
        for (uint32_t i = 0; i < nfds; i++) {
            int fd = fds[i].fd;
            fds[i].revents = 0;
            
            if (fd < 0 || fd >= VFS_MAX_FDS || !g_fd_table[fd].used) {
                fds[i].revents = POLLNVAL;
                count++;
                continue;
            }
            
            vfs_node_t *node = g_fd_table[fd].node;
            if (node && node->ops && node->ops->poll) {
                short revents = node->ops->poll(node, fds[i].events);
                if (revents) {
                    fds[i].revents = revents;
                    count++;
                }
            }
        }
        
        if (count > 0 || timeout_ms == 0) break;
        
        if (timeout_ms > 0 && (timer_get_uptime_ms() - start_ms >= (uint32_t)timeout_ms)) {
            break; /* Timeout */
        }
        
        /* Yield / sleep slightly instead of busy waiting */
        task_sleep_ms(5); /* Yield CPU */
    }
    
    return count;
}

/* 
 * select() wrapper over poll()
 */
int sys_select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout) {
    if (nfds < 0 || nfds > VFS_MAX_FDS) return -1;
    
    struct pollfd *pfds = kzalloc(nfds * sizeof(struct pollfd));
    if (!pfds) return -1;
    
    uint32_t pfd_count = 0;
    for (int fd = 0; fd < nfds; fd++) {
        short events = 0;
        if (readfds && FD_ISSET(fd, readfds)) events |= POLLIN;
        if (writefds && FD_ISSET(fd, writefds)) events |= POLLOUT;
        if (exceptfds && FD_ISSET(fd, exceptfds)) events |= POLLERR;
        
        if (events) {
            pfds[pfd_count].fd = fd;
            pfds[pfd_count].events = events;
            pfd_count++;
        }
    }
    
    int timeout_ms = -1; /* Infinite */
    if (timeout) {
        timeout_ms = (timeout->tv_sec * 1000) + (timeout->tv_usec / 1000);
    }
    
    int ret = sys_poll(pfds, pfd_count, timeout_ms);
    
    if (readfds) FD_ZERO(readfds);
    if (writefds) FD_ZERO(writefds);
    if (exceptfds) FD_ZERO(exceptfds);
    
    if (ret > 0) {
        for (uint32_t i = 0; i < pfd_count; i++) {
            if (pfds[i].revents & (POLLIN | POLLHUP | POLLERR)) {
                if (readfds) FD_SET(pfds[i].fd, readfds);
            }
            if (pfds[i].revents & POLLOUT) {
                if (writefds) FD_SET(pfds[i].fd, writefds);
            }
            if (pfds[i].revents & (POLLERR | POLLNVAL)) {
                if (exceptfds) FD_SET(pfds[i].fd, exceptfds);
            }
        }
    }
    
    kfree(pfds);
    return ret;
}

/* =============================================================================
 * vfs_open(), vfs_read(), vfs_write(), vfs_close() API'leri
 * =============================================================================
 */
int32_t vfs_alloc_fd(vfs_node_t *node, uint32_t flags) {
    if (!node) return -1;

    /* Boş FD Slotu bul */
    int fd = -1;
    for (int i = 0; i < VFS_MAX_FDS; i++) {
        if (!g_fd_table[i].used) {
            fd = i;
            break;
        }
    }

    if (fd == -1) return -1;

    g_fd_table[fd].node   = node;
    g_fd_table[fd].offset = 0;
    g_fd_table[fd].flags  = flags;
    g_fd_table[fd].used   = 1;

    return fd;
}

int32_t vfs_open(const char *path, uint32_t flags) {
    vfs_node_t *node = vfs_lookup(path);
    if (!node) return -1;

    int fd = vfs_alloc_fd(node, flags);
    if (fd < 0) return -1;

    if (node->ops && node->ops->open) {
        node->ops->open(node);
    }

    return fd;
}

vfs_node_t* vfs_get_node(int32_t fd) {
    if (fd < 0 || fd >= VFS_MAX_FDS || !g_fd_table[fd].used) return NULL;
    return g_fd_table[fd].node;
}

int32_t vfs_read(int32_t fd, void *buffer, uint32_t size) {
    if (fd < 0 || fd >= VFS_MAX_FDS || !g_fd_table[fd].used) return -1;

    vfs_node_t *node = g_fd_table[fd].node;
    if (!node || !node->ops || !node->ops->read) return -1;

    int32_t bytes_read = node->ops->read(node, g_fd_table[fd].offset, size, (uint8_t*)buffer);
    if (bytes_read > 0) {
        g_fd_table[fd].offset += bytes_read;
    }
    return bytes_read;
}

int32_t vfs_write(int32_t fd, const void *buffer, uint32_t size) {
    if (fd < 0 || fd >= VFS_MAX_FDS || !g_fd_table[fd].used) return -1;

    vfs_node_t *node = g_fd_table[fd].node;
    if (!node || !node->ops || !node->ops->write) return -1;

    int32_t bytes_written = node->ops->write(node, g_fd_table[fd].offset, size, (const uint8_t*)buffer);
    if (bytes_written > 0) {
        g_fd_table[fd].offset += bytes_written;
    }
    return bytes_written;
}

int32_t vfs_close(int32_t fd) {
    if (fd < 0 || fd >= VFS_MAX_FDS || !g_fd_table[fd].used) return -1;

    vfs_node_t *node = g_fd_table[fd].node;
    if (node && node->ops && node->ops->close) {
        node->ops->close(node);
    }

    g_fd_table[fd].node   = NULL;
    g_fd_table[fd].offset = 0;
    g_fd_table[fd].used   = 0;
    return 0;
}

struct dirent* vfs_readdir(int32_t fd, uint32_t index) {
    if (fd < 0 || fd >= VFS_MAX_FDS || !g_fd_table[fd].used) return NULL;

    vfs_node_t *node = g_fd_table[fd].node;
    if (!node || !node->ops || !node->ops->readdir) return NULL;

    return node->ops->readdir(node, index);
}

void vfs_run_test_suite(void) {
    /* Test suite removed: / is now handled by rootfs_init() which mounts real directories */
}
