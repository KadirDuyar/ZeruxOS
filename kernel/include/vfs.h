/* =============================================================================
 * ZeruX OS — Virtual File System (VFS) Core Abstraction Header
 * File: kernel/include/vfs.h
 * =============================================================================
 *
 * "Her Şey Bir Dosyadır" (Everything is a file) Unix Felsefesi:
 *   - Dosyalar, Cihazlar (/dev), Canlı Teşhisler (/proc) ve Diskler (FAT32)
 *     aynı vfs_node_t ve vnode opsiyonel fonksiyon arayüzü üzerinden okunup yazılır.
 * =============================================================================
 */

#ifndef VFS_H
#define VFS_H

#include <stdint.h>
#include <stddef.h>
#include "poll.h"

#define VFS_MAX_PATH_LEN  256
#define VFS_MAX_NAME_LEN  128
#define VFS_MAX_FDS       64

/* VFS Node Tipleri */
#define VFS_FILE        0x01
#define VFS_DIRECTORY   0x02
#define VFS_CHARDEVICE  0x03
#define VFS_BLOCKDEVICE 0x04
#define VFS_PIPE        0x05
#define VFS_MOUNTPOINT  0x08

/* Dirent (Directory Entry) Yapısı */
struct dirent {
    char     name[VFS_MAX_NAME_LEN];
    uint32_t ino;
};

struct vfs_node;

/* VFS Operasyon Tablosu (Function Pointer Class Vnode) */
typedef struct {
    int32_t  (*read)(struct vfs_node *node, uint32_t offset, uint32_t size, uint8_t *buffer);
    int32_t  (*write)(struct vfs_node *node, uint32_t offset, uint32_t size, const uint8_t *buffer);
    void     (*open)(struct vfs_node *node);
    void     (*close)(struct vfs_node *node);
    struct dirent* (*readdir)(struct vfs_node *node, uint32_t index);
    struct vfs_node* (*finddir)(struct vfs_node *node, const char *name);
    int      (*poll)(struct vfs_node *node, short events); /* Poll callback for epoll/select */
} vfs_node_ops_t;

/* VFS Node Yapısı (Linux inode Karşılığı) */
typedef struct vfs_node {
    char            name[VFS_MAX_NAME_LEN];
    uint32_t        flags;
    uint32_t        inode;
    uint32_t        length;     /* Bayt cinsinden dosya boyutu */
    vfs_node_ops_t *ops;        /* Sürücüye özgü read/write/readdir fonksiyon tablosu */
    struct vfs_node *ptr;        /* Mountpoint ise hedef kök node */
    void            *device_data;/* Sürücüye özgü özel veri pointer'ı */
} vfs_node_t;

/* Public VFS API Bildirimleri */
void        vfs_init(void);
int         vfs_register_mount(const char *path, vfs_node_t *root_node);
vfs_node_t* vfs_lookup(const char *path);
int32_t     vfs_alloc_fd(vfs_node_t *node, uint32_t flags);
vfs_node_t* vfs_get_node(int32_t fd);
int32_t     vfs_open(const char *path, uint32_t flags);
int32_t vfs_read(int32_t fd, void *buffer, uint32_t size);
int32_t vfs_write(int32_t fd, const void *buffer, uint32_t size);
int32_t vfs_close(int32_t fd);
int32_t sys_fcntl(int32_t fd, int cmd, int arg);

struct pollfd;
struct timeval;
int sys_poll(struct pollfd *fds, uint32_t nfds, int timeout_ms);
int sys_select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout);
struct dirent* vfs_readdir(int32_t fd, uint32_t index);
vfs_node_t* vfs_get_mount_info(uint32_t index, const char **out_path);

void        vfs_run_test_suite(void);

#endif /* VFS_H */
