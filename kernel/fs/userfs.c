/* =============================================================================
 * ZeruX OS — RAM (Volatile) User Filesystem for /user
 * File: kernel/fs/userfs.c
 * =============================================================================
 *
 * Simple writable in-memory filesystem mounted at /user.
 * Supports: mkdir (sub-directories), create file, write, read, rm (delete),
 * readdir, finddir.
 * Data is lost on reboot — purely for shell commands cd/mkdir/rm/edit.
 * =============================================================================
 */

#include "userfs.h"
#include "vfs.h"
#include "kheap.h"
#include "serial.h"
#include <stdint.h>
#include <stddef.h>

#define USERFS_MAX_NODES    64
#define USERFS_MAX_DATA     4096
#define USERFS_MAX_NAME     64

typedef struct userfs_node {
    char            name[USERFS_MAX_NAME];
    uint8_t         is_dir;
    uint8_t         deleted;
    uint32_t        parent_inode;
    uint8_t        *data;
    uint32_t        data_len;
    uint32_t        inode;
} userfs_node_t;

static userfs_node_t  g_nodes[USERFS_MAX_NODES];
static uint32_t       g_node_count = 0;

static int ufs_strcmp(const char *a, const char *b) {
    while (*a && (*a == *b)) { a++; b++; }
    return *(const unsigned char*)a - *(const unsigned char*)b;
}

static void ufs_strcpy(char *dst, const char *src, int maxn) {
    int i = 0;
    while (i < maxn - 1 && src[i]) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

static userfs_node_t* ufs_alloc_node(void) {
    if (g_node_count >= USERFS_MAX_NODES) return NULL;
    userfs_node_t *n = &g_nodes[g_node_count];
    n->inode = g_node_count;
    g_node_count++;
    return n;
}

static int32_t ufs_read_op(struct vfs_node *node, uint32_t offset, uint32_t size, uint8_t *buffer);
static int32_t ufs_write_op(struct vfs_node *node, uint32_t offset, uint32_t size, const uint8_t *buffer);
static struct dirent* ufs_readdir_op(struct vfs_node *node, uint32_t index);
static struct vfs_node* ufs_finddir_op(struct vfs_node *node, const char *name);

static vfs_node_ops_t g_ufs_dir_ops = {
    .read    = NULL,
    .write   = NULL,
    .readdir = ufs_readdir_op,
    .finddir = ufs_finddir_op,
};

static vfs_node_ops_t g_ufs_file_ops = {
    .read    = ufs_read_op,
    .write   = ufs_write_op,
    .readdir = NULL,
    .finddir = NULL,
};

#define USERFS_MAX_VFS_NODES 64
static vfs_node_t    g_vfs_nodes[USERFS_MAX_VFS_NODES];
static struct dirent g_temp_dirent;

static vfs_node_t* ufs_get_vnode(uint32_t inode) {
    if (inode >= USERFS_MAX_VFS_NODES) return NULL;
    return &g_vfs_nodes[inode];
}

static int32_t ufs_read_op(struct vfs_node *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
    uint32_t inode = node->inode;
    if (inode >= g_node_count) return -1;
    userfs_node_t *n = &g_nodes[inode];
    if (!n->data || n->deleted) return -1;
    if (offset >= n->data_len) return 0;
    uint32_t to_read = n->data_len - offset;
    if (to_read > size) to_read = size;
    for (uint32_t i = 0; i < to_read; i++) buffer[i] = n->data[offset + i];
    return (int32_t)to_read;
}

static int32_t ufs_write_op(struct vfs_node *node, uint32_t offset, uint32_t size, const uint8_t *buffer) {
    uint32_t inode = node->inode;
    if (inode >= g_node_count) return -1;
    userfs_node_t *n = &g_nodes[inode];
    if (n->deleted) return -1;
    if (!n->data) {
        n->data = (uint8_t*)kzalloc(USERFS_MAX_DATA);
        if (!n->data) return -1;
        n->data_len = 0;
    }
    uint32_t end = offset + size;
    if (end > USERFS_MAX_DATA) end = USERFS_MAX_DATA;
    uint32_t actual = end - offset;
    for (uint32_t i = 0; i < actual; i++) n->data[offset + i] = buffer[i];
    if (end > n->data_len) n->data_len = end;
    node->length = n->data_len;
    g_vfs_nodes[inode].length = n->data_len;
    return (int32_t)actual;
}

static struct dirent* ufs_readdir_op(struct vfs_node *node, uint32_t index) {
    uint32_t parent_inode = node->inode;
    uint32_t found = 0;
    for (uint32_t i = 0; i < g_node_count; i++) {
        if (g_nodes[i].parent_inode == parent_inode && !g_nodes[i].deleted && g_nodes[i].inode != parent_inode) {
            if (found == index) {
                ufs_strcpy(g_temp_dirent.name, g_nodes[i].name, VFS_MAX_NAME_LEN);
                g_temp_dirent.ino = g_nodes[i].inode;
                return &g_temp_dirent;
            }
            found++;
        }
    }
    return NULL;
}

static struct vfs_node* ufs_finddir_op(struct vfs_node *node, const char *name) {
    uint32_t parent_inode = node->inode;
    for (uint32_t i = 0; i < g_node_count; i++) {
        if (g_nodes[i].parent_inode == parent_inode && !g_nodes[i].deleted
            && ufs_strcmp(g_nodes[i].name, name) == 0) {
            return ufs_get_vnode(i);
        }
    }
    return NULL;
}

int userfs_mkdir(uint32_t parent_inode, const char *name) {
    for (uint32_t i = 0; i < g_node_count; i++) {
        if (g_nodes[i].parent_inode == parent_inode && !g_nodes[i].deleted
            && ufs_strcmp(g_nodes[i].name, name) == 0) {
            return -1;
        }
    }
    userfs_node_t *n = ufs_alloc_node();
    if (!n) return -1;
    ufs_strcpy(n->name, name, USERFS_MAX_NAME);
    n->is_dir       = 1;
    n->deleted      = 0;
    n->parent_inode = parent_inode;
    n->data         = NULL;
    n->data_len     = 0;
    vfs_node_t *v = &g_vfs_nodes[n->inode];
    ufs_strcpy(v->name, name, VFS_MAX_NAME_LEN);
    v->flags  = VFS_DIRECTORY;
    v->inode  = n->inode;
    v->length = 0;
    v->ops    = &g_ufs_dir_ops;
    v->ptr    = NULL;
    v->device_data = NULL;
    return (int)n->inode;
}

int userfs_create(uint32_t parent_inode, const char *name) {
    for (uint32_t i = 0; i < g_node_count; i++) {
        if (g_nodes[i].parent_inode == parent_inode && !g_nodes[i].deleted
            && ufs_strcmp(g_nodes[i].name, name) == 0) {
            return (int)g_nodes[i].inode;
        }
    }
    userfs_node_t *n = ufs_alloc_node();
    if (!n) return -1;
    ufs_strcpy(n->name, name, USERFS_MAX_NAME);
    n->is_dir       = 0;
    n->deleted      = 0;
    n->parent_inode = parent_inode;
    n->data         = NULL;
    n->data_len     = 0;
    vfs_node_t *v = &g_vfs_nodes[n->inode];
    ufs_strcpy(v->name, name, VFS_MAX_NAME_LEN);
    v->flags  = VFS_FILE;
    v->inode  = n->inode;
    v->length = 0;
    v->ops    = &g_ufs_file_ops;
    v->ptr    = NULL;
    v->device_data = NULL;
    return (int)n->inode;
}

int userfs_rm(uint32_t inode) {
    if (inode >= g_node_count) return -1;
    g_nodes[inode].deleted = 1;
    return 0;
}

int userfs_find(uint32_t parent_inode, const char *name) {
    for (uint32_t i = 0; i < g_node_count; i++) {
        if (g_nodes[i].parent_inode == parent_inode && !g_nodes[i].deleted
            && ufs_strcmp(g_nodes[i].name, name) == 0) {
            return (int)g_nodes[i].inode;
        }
    }
    return -1;
}

vfs_node_t* userfs_get_vnode(uint32_t inode) {
    return ufs_get_vnode(inode);
}

uint32_t userfs_root_inode(void) {
    return 0;
}

int userfs_is_dir(uint32_t inode) {
    if (inode >= g_node_count) return 0;
    return g_nodes[inode].is_dir;
}

int userfs_write_file(uint32_t inode, const uint8_t *data, uint32_t len) {
    if (inode >= g_node_count) return -1;
    return ufs_write_op(&g_vfs_nodes[inode], 0, len, data);
}

static vfs_node_t g_userfs_root_vnode;

void userfs_init(void) {
    userfs_node_t *root = ufs_alloc_node(); /* inode = 0 */
    ufs_strcpy(root->name, "user", USERFS_MAX_NAME);
    root->is_dir       = 1;
    root->deleted      = 0;
    root->parent_inode = 0;
    root->data         = NULL;
    root->data_len     = 0;

    vfs_node_t *v = &g_vfs_nodes[0];
    ufs_strcpy(v->name, "user", VFS_MAX_NAME_LEN);
    v->flags  = VFS_DIRECTORY;
    v->inode  = 0;
    v->length = 0;
    v->ops    = &g_ufs_dir_ops;
    v->ptr    = NULL;
    v->device_data = NULL;

    g_userfs_root_vnode = *v;
    vfs_register_mount("/user", &g_userfs_root_vnode);

    serial_printf("[USERFS] RAM-based writable filesystem mounted at /user\n");


}
