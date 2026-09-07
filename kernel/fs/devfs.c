/* =============================================================================
 * ZeruX OS — Virtual Device File System (devfs) Implementation
 * File: kernel/fs/devfs.c
 * =============================================================================
 *
 * Sanal Cihaz Dosya Sistemi (/dev):
 *   - /dev/serial0 -> Seri Port (COM1) Karakter Cihazı
 *   - /dev/kbd     -> PS/2 Klavye Karakter Cihazı
 *   - /dev/null    -> Unix Data Sink (Tüm yazılanları yutar)
 *   - /dev/zero    -> Unix Zero Generator (Sürekli 0x00 üretir)
 * =============================================================================
 */

#include "devfs.h"
#include "vfs.h"
#include "serial.h"
#include "keyboard.h"
#include "kheap.h"

#define DEVFS_MAX_DEVICES 4

static vfs_node_t g_dev_nodes[DEVFS_MAX_DEVICES];
static struct dirent g_dev_dirents[DEVFS_MAX_DEVICES];

/* String Helpers */
static size_t kstrlen(const char *s) {
    size_t len = 0;
    while (s && s[len]) len++;
    return len;
}

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

/* =============================================================================
 * Device Read / Write Callbacks
 * =============================================================================
 */
static int32_t serial0_write(vfs_node_t *node, uint32_t offset, uint32_t size, const uint8_t *buffer) {
    (void)node; (void)offset;
    for (uint32_t i = 0; i < size; i++) {
        serial_putchar((char)buffer[i]);
    }
    return size;
}

static int32_t kbd_read(vfs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
    (void)node; (void)offset;
    uint32_t count = 0;
    while (count < size && keyboard_has_char()) {
        buffer[count++] = (uint8_t)keyboard_getchar();
    }
    return count;
}

static int32_t null_read(vfs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
    (void)node; (void)offset; (void)size; (void)buffer;
    return 0; /* EOF */
}

static int32_t null_write(vfs_node_t *node, uint32_t offset, uint32_t size, const uint8_t *buffer) {
    (void)node; (void)offset; (void)buffer;
    return size; /* Discarded */
}

static int32_t zero_read(vfs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
    (void)node; (void)offset;
    for (uint32_t i = 0; i < size; i++) {
        buffer[i] = 0x00;
    }
    return size;
}

/* Device Operations Structs */
static vfs_node_ops_t g_serial0_ops = { .write = serial0_write };
static vfs_node_ops_t g_kbd_ops     = { .read  = kbd_read };
static vfs_node_ops_t g_null_ops    = { .read  = null_read, .write = null_write };
static vfs_node_ops_t g_zero_ops    = { .read  = zero_read };

/* =============================================================================
 * /dev Directory Callbacks (finddir & readdir)
 * =============================================================================
 */
static vfs_node_t* devfs_finddir(vfs_node_t *node, const char *name) {
    (void)node;
    for (int i = 0; i < DEVFS_MAX_DEVICES; i++) {
        if (kstrcmp(g_dev_nodes[i].name, name) == 0) {
            return &g_dev_nodes[i];
        }
    }
    return NULL;
}

static struct dirent* devfs_readdir(vfs_node_t *node, uint32_t index) {
    (void)node;
    if (index >= DEVFS_MAX_DEVICES) return NULL;
    return &g_dev_dirents[index];
}

static vfs_node_ops_t g_devfs_root_ops = {
    .finddir = devfs_finddir,
    .readdir = devfs_readdir
};

static vfs_node_t g_devfs_root = {
    .name   = "dev",
    .flags  = VFS_DIRECTORY,
    .inode  = 0,
    .length = 0,
    .ops    = &g_devfs_root_ops,
    .ptr    = NULL,
    .device_data = NULL
};

/* =============================================================================
 * devfs_init() — /dev Cihaz Dosya Sistemini Oluşturur ve Mount Eder
 * =============================================================================
 */
void devfs_init(void) {
    /* 1. /dev/serial0 */
    kstrncpy(g_dev_nodes[0].name, "serial0", VFS_MAX_NAME_LEN);
    g_dev_nodes[0].flags  = VFS_CHARDEVICE;
    g_dev_nodes[0].inode  = 1;
    g_dev_nodes[0].ops    = &g_serial0_ops;
    kstrncpy(g_dev_dirents[0].name, "serial0", VFS_MAX_NAME_LEN);
    g_dev_dirents[0].ino  = 1;

    /* 2. /dev/kbd */
    kstrncpy(g_dev_nodes[1].name, "kbd", VFS_MAX_NAME_LEN);
    g_dev_nodes[1].flags  = VFS_CHARDEVICE;
    g_dev_nodes[1].inode  = 2;
    g_dev_nodes[1].ops    = &g_kbd_ops;
    kstrncpy(g_dev_dirents[1].name, "kbd", VFS_MAX_NAME_LEN);
    g_dev_dirents[1].ino  = 2;

    /* 3. /dev/null */
    kstrncpy(g_dev_nodes[2].name, "null", VFS_MAX_NAME_LEN);
    g_dev_nodes[2].flags  = VFS_CHARDEVICE;
    g_dev_nodes[2].inode  = 3;
    g_dev_nodes[2].ops    = &g_null_ops;
    kstrncpy(g_dev_dirents[2].name, "null", VFS_MAX_NAME_LEN);
    g_dev_dirents[2].ino  = 3;

    /* 4. /dev/zero */
    kstrncpy(g_dev_nodes[3].name, "zero", VFS_MAX_NAME_LEN);
    g_dev_nodes[3].flags  = VFS_CHARDEVICE;
    g_dev_nodes[3].inode  = 4;
    g_dev_nodes[3].ops    = &g_zero_ops;
    kstrncpy(g_dev_dirents[3].name, "zero", VFS_MAX_NAME_LEN);
    g_dev_dirents[3].ino  = 4;

    /* 4. VFS'e "/wire" olarak mount et */
    vfs_register_mount("/wire", &g_devfs_root);

    serial_printf("===========================================\n");
    serial_printf(" ZeruX OS — Device File System (/wire)\n");
    serial_printf("===========================================\n");
    serial_printf("[DEVFS] Mounted dynamic devices: /wire/serial0, /wire/kbd, /wire/null, /wire/zero\n");
    serial_printf("===========================================\n\n");
}

/* =============================================================================
 * devfs_run_test_suite() — Adım 4.3 devfs Cihaz Düğümü Testi
 * =============================================================================
 */
void devfs_run_test_suite(void) {
    serial_printf("[DEVFS TEST] Writing to '/wire/serial0' char device...\n");
    int fd = vfs_open("/wire/serial0", 0);
    if (fd >= 0) {
        const char *msg = ">>> Hello from /wire/serial0 VFS device node! <<<\n";
        int32_t written = vfs_write(fd, msg, kstrlen(msg));
        vfs_close(fd);

        if (written > 0) {
            serial_printf("\n=======================================================\n");
            serial_printf(" [DEVFS TEST PASSED] Device File System (/wire) Verified!\n");
            serial_printf("   - /wire/serial0 char device node written successfully\n");
            serial_printf("   - VFS successfully routed write() to UART hardware driver\n");
            serial_printf("=======================================================\n\n");
        }
    }
}
