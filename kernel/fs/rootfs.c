/* =============================================================================
 * ZeruX OS — Root File System (/) Placeholder Directories
 * File: kernel/fs/rootfs.c
 * =============================================================================
 */

#include "rootfs.h"
#include "vfs.h"
#include "serial.h"

#define ROOTFS_MAX_ENTRIES 6

static vfs_node_t g_root_nodes[ROOTFS_MAX_ENTRIES];
static struct dirent g_root_dirents[ROOTFS_MAX_ENTRIES];
static struct dirent g_dynamic_dirent; /* Geçici dirent dinamik mount'lar için */

static int kstrcmp(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++; s2++;
    }
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

static vfs_node_t* rootfs_finddir(vfs_node_t *node, const char *name) {
    (void)node;
    for (int i = 0; i < ROOTFS_MAX_ENTRIES; i++) {
        if (kstrcmp(g_root_nodes[i].name, name) == 0) {
            return &g_root_nodes[i];
        }
    }
    return NULL; /* vfs_lookup, eger NULL donerse VFS'in kendi mount tablosuna bakmaya devam eder */
}

static struct dirent* rootfs_readdir(vfs_node_t *node, uint32_t index) {
    (void)node;
    /* 1. Statik Dizinler (0..5) */
    if (index < ROOTFS_MAX_ENTRIES) {
        return &g_root_dirents[index];
    }

    /* 2. Dinamik VFS Mount'lari (index >= ROOTFS_MAX_ENTRIES) */
    uint32_t current_dynamic_idx = ROOTFS_MAX_ENTRIES;
    uint32_t vfs_idx = 0;
    const char *mpath;
    vfs_node_t *mnode;

    while ((mnode = vfs_get_mount_info(vfs_idx++, &mpath)) != NULL) {
        /* Sadece dogrudan / altindaki mount'lari goster (ornek: /live, /wire)
         * - "/" kendisini gosterme (uzunluk > 1)
         * - Ikinci bir '/' icermemeli (ornek: /disk/fat0 gosterilmez, statik /disk zaten gosterilir) */
        if (mpath[0] == '/' && mpath[1] != '\0') {
            int has_slash = 0;
            for (int i = 1; mpath[i] != '\0'; i++) {
                if (mpath[i] == '/') {
                    has_slash = 1;
                    break;
                }
            }
            if (!has_slash) {
                if (current_dynamic_idx == index) {
                    kstrncpy(g_dynamic_dirent.name, mpath + 1, VFS_MAX_NAME_LEN);
                    g_dynamic_dirent.ino = 1000 + vfs_idx; /* Sahte inode */
                    return &g_dynamic_dirent;
                }
                current_dynamic_idx++;
            }
        }
    }
    
    return NULL;
}

static struct dirent* static_dir_readdir(vfs_node_t *node, uint32_t index) {
    uint32_t current_idx = 0;
    uint32_t vfs_idx = 0;
    const char *mpath;
    vfs_node_t *mnode;

    char static_path[VFS_MAX_NAME_LEN + 2];
    static_path[0] = '/';
    kstrncpy(static_path + 1, node->name, VFS_MAX_NAME_LEN);
    size_t static_len = kstrlen(static_path);

    while ((mnode = vfs_get_mount_info(vfs_idx++, &mpath)) != NULL) {
        if (kstrncmp(mpath, static_path, static_len) == 0 && mpath[static_len] == '/') {
            const char *child_name = mpath + static_len + 1;
            int has_slash = 0;
            for (int i = 0; child_name[i]; i++) {
                if (child_name[i] == '/') { has_slash = 1; break; }
            }
            if (!has_slash && child_name[0] != '\0') {
                if (current_idx == index) {
                    kstrncpy(g_dynamic_dirent.name, child_name, VFS_MAX_NAME_LEN);
                    g_dynamic_dirent.ino = 2000 + vfs_idx;
                    return &g_dynamic_dirent;
                }
                current_idx++;
            }
        }
    }
    return NULL;
}

static vfs_node_ops_t g_static_dir_ops = {
    .readdir = static_dir_readdir,
    .finddir = NULL
};

static vfs_node_ops_t g_rootfs_ops = {
    .finddir = rootfs_finddir,
    .readdir = rootfs_readdir
};

static vfs_node_t g_rootfs_root = {
    .name   = "root",
    .flags  = VFS_DIRECTORY,
    .inode  = 0,
    .length = 0,
    .ops    = &g_rootfs_ops,
    .ptr    = NULL,
    .device_data = NULL
};

void rootfs_init(void) {
    const char *dirs[ROOTFS_MAX_ENTRIES] = {"run", "cfg", "app", "user", "core", "disk"};

    for (int i = 0; i < ROOTFS_MAX_ENTRIES; i++) {
        kstrncpy(g_root_nodes[i].name, dirs[i], VFS_MAX_NAME_LEN);
        g_root_nodes[i].flags = VFS_DIRECTORY;
        g_root_nodes[i].inode = i + 1;
        g_root_nodes[i].ops   = &g_static_dir_ops;
        
        kstrncpy(g_root_dirents[i].name, dirs[i], VFS_MAX_NAME_LEN);
        g_root_dirents[i].ino = i + 1;
    }

    /* VFS'e "/" olarak mount et */
    vfs_register_mount("/", &g_rootfs_root);

    serial_printf("===========================================\n");
    serial_printf(" ZeruX OS — Root File System (/)\n");
    serial_printf("===========================================\n");
    serial_printf("[ROOTFS] Mounted static directories: /run, /cfg, /app, /user, /core, /disk\n");
    serial_printf("===========================================\n\n");
}
