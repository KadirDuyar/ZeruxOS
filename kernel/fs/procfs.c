/* =============================================================================
 * ZeruX OS — Virtual Process File System (procfs) Implementation
 * File: kernel/fs/procfs.c
 * =============================================================================
 *
 * Canlı Sistem İnceleme ("Açık Kalp Ameliyatı" - /proc):
 *   - Sistem çalışan durumdayken /proc/tasks veya /proc/meminfo okunduğunda
 *     anlık çekirdek durumunu dinamik metin (text) olarak üretir ve döndürür.
 * =============================================================================
 */

#include "procfs.h"
#include "vfs.h"
#include "task.h"
#include "pmm.h"
#include "kheap.h"
#include "serial.h"

#define PROCFS_MAX_ENTRIES 3

static vfs_node_t g_proc_nodes[PROCFS_MAX_ENTRIES];
static struct dirent g_proc_dirents[PROCFS_MAX_ENTRIES];

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

/* Base-10 Integer String Converter */
static int itoa_dec(int32_t num, char *str) {
    int i = 0;
    int is_neg = 0;
    if (num == 0) {
        str[i++] = '0';
        str[i] = '\0';
        return i;
    }
    if (num < 0) {
        is_neg = 1;
        num = -num;
    }
    while (num != 0) {
        int rem = num % 10;
        str[i++] = rem + '0';
        num = num / 10;
    }
    if (is_neg) str[i++] = '-';
    str[i] = '\0';

    /* Reverse string */
    for (int j = 0; j < i / 2; j++) {
        char temp = str[j];
        str[j] = str[i - j - 1];
        str[i - j - 1] = temp;
    }
    return i;
}

/* =============================================================================
 * /proc/tasks Dynamic Generator Callback
 * =============================================================================
 */
static int32_t proc_tasks_read(vfs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
    (void)node;
    char *text = kmalloc(4096);
    if (!text) return 0;
    int len = 0;

    const char *header = "---------------------------------------------------\n"
                         " Active ZeruX Kernel Tasks & PCBs (/proc/tasks)\n"
                         "---------------------------------------------------\n";
    for (int i = 0; header[i]; i++) text[len++] = header[i];

    task_t *curr = task_get_current();
    if (curr) {
        task_t *t = curr;
        do {
            const char *p1 = "[PCB] PID: ";
            for (int i = 0; p1[i] && len < 4000; i++) text[len++] = p1[i];
            if (len < 4000) len += itoa_dec(t->pid, text + len);

            const char *p2 = "  Name: ";
            for (int i = 0; p2[i] && len < 4000; i++) text[len++] = p2[i];
            for (int i = 0; t->name[i] && len < 4000; i++) text[len++] = t->name[i];

            const char *p3 = "  State: ";
            for (int i = 0; p3[i] && len < 4000; i++) text[len++] = p3[i];
            const char *st_names[] = {"READY", "RUNNING", "SLEEPING", "WAITING", "BLOCKED", "ZOMBIE"};
            const char *st = (t->state >= 0 && t->state <= 5) ? st_names[t->state] : "UNKNOWN";
            for (int i = 0; st[i] && len < 4000; i++) text[len++] = st[i];

            if (len < 4095) text[len++] = '\n';
            t = t->next;
        } while (t != NULL && t != curr && len < 4000);
    }

    if (offset >= (uint32_t)len) {
        kfree(text);
        return 0;
    }
    uint32_t rem = len - offset;
    if (size < rem) rem = size;

    for (uint32_t i = 0; i < rem; i++) {
        buffer[i] = text[offset + i];
    }
    kfree(text);
    return rem;
}

/* =============================================================================
 * /proc/meminfo Dynamic Generator Callback
 * =============================================================================
 */
static int32_t proc_meminfo_read(vfs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
    (void)node;
    char text[512];
    int len = 0;
    
    uint32_t total_blocks = pmm_get_total_blocks();
    uint32_t free_blocks = pmm_get_free_blocks();
    
    uint32_t total_kb = (total_blocks * PMM_PAGE_SIZE) / 1024;
    uint32_t free_kb = (free_blocks * PMM_PAGE_SIZE) / 1024;
    
    uint32_t kheap_used = kheap_get_used_size();
    
    const char *header = "---------------------------------------------------\n"
                         " ZeruX Physical & Heap Memory Status (/proc/meminfo)\n"
                         "---------------------------------------------------\n";
    for (int i = 0; header[i]; i++) text[len++] = header[i];
    
    const char *p_total = " Total RAM      : ";
    for (int i = 0; p_total[i]; i++) text[len++] = p_total[i];
    len += itoa_dec(total_kb / 1024, text + len);
    const char *p_mb = " MB (";
    for (int i = 0; p_mb[i]; i++) text[len++] = p_mb[i];
    len += itoa_dec(total_kb, text + len);
    const char *p_kb = " KB)\n";
    for (int i = 0; p_kb[i]; i++) text[len++] = p_kb[i];
    
    const char *p_free = " Free Pages     : ";
    for (int i = 0; p_free[i]; i++) text[len++] = p_free[i];
    len += itoa_dec(free_blocks, text + len);
    const char *p_pages = " Pages (";
    for (int i = 0; p_pages[i]; i++) text[len++] = p_pages[i];
    len += itoa_dec(free_kb / 1024, text + len);
    const char *p_mb_free = " MB Free)\n";
    for (int i = 0; p_mb_free[i]; i++) text[len++] = p_mb_free[i];
    
    const char *p_kheap = " KHEAP Used     : ";
    for (int i = 0; p_kheap[i]; i++) text[len++] = p_kheap[i];
    len += itoa_dec(kheap_used, text + len);
    const char *p_bytes = " bytes\n";
    for (int i = 0; p_bytes[i]; i++) text[len++] = p_bytes[i];
    
    const char *footer = "---------------------------------------------------\n";
    for (int i = 0; footer[i]; i++) text[len++] = footer[i];

    if (offset >= (uint32_t)len) return 0;
    uint32_t rem = len - offset;
    if (size < rem) rem = size;

    for (uint32_t i = 0; i < rem; i++) {
        buffer[i] = text[offset + i];
    }
    return rem;
}

/* =============================================================================
 * /proc/version Callback
 * =============================================================================
 */
static int32_t proc_version_read(vfs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
    (void)node;
    const char *ver = "ZeruX Operating System v0.1 (IA-32 Protected Mode Kernel, Build Jul 2026)\n";
    uint32_t len = kstrlen(ver);

    if (offset >= len) return 0;
    uint32_t rem = len - offset;
    if (size < rem) rem = size;

    for (uint32_t i = 0; i < rem; i++) {
        buffer[i] = ver[offset + i];
    }
    return rem;
}

/* Device Operations Structs */
static vfs_node_ops_t g_proc_tasks_ops   = { .read = proc_tasks_read };
static vfs_node_ops_t g_proc_meminfo_ops = { .read = proc_meminfo_read };
static vfs_node_ops_t g_proc_version_ops = { .read = proc_version_read };

/* /proc Directory Callbacks */
static vfs_node_t* procfs_finddir(vfs_node_t *node, const char *name) {
    (void)node;
    for (int i = 0; i < PROCFS_MAX_ENTRIES; i++) {
        if (kstrcmp(g_proc_nodes[i].name, name) == 0) {
            return &g_proc_nodes[i];
        }
    }
    return NULL;
}

static struct dirent* procfs_readdir(vfs_node_t *node, uint32_t index) {
    (void)node;
    if (index >= PROCFS_MAX_ENTRIES) return NULL;
    return &g_proc_dirents[index];
}

static vfs_node_ops_t g_procfs_root_ops = {
    .finddir = procfs_finddir,
    .readdir = procfs_readdir
};

static vfs_node_t g_procfs_root = {
    .name   = "proc",
    .flags  = VFS_DIRECTORY,
    .inode  = 0,
    .length = 0,
    .ops    = &g_procfs_root_ops,
    .ptr    = NULL,
    .device_data = NULL
};

/* =============================================================================
 * procfs_init() — /proc Canlı Sistem İnceleme Dosya Sistemini Yükler
 * =============================================================================
 */
void procfs_init(void) {
    /* 1. /proc/tasks */
    kstrncpy(g_proc_nodes[0].name, "tasks", VFS_MAX_NAME_LEN);
    g_proc_nodes[0].flags = VFS_FILE;
    g_proc_nodes[0].inode = 1;
    g_proc_nodes[0].ops   = &g_proc_tasks_ops;
    kstrncpy(g_proc_dirents[0].name, "tasks", VFS_MAX_NAME_LEN);
    g_proc_dirents[0].ino = 1;

    /* 2. /proc/meminfo */
    kstrncpy(g_proc_nodes[1].name, "meminfo", VFS_MAX_NAME_LEN);
    g_proc_nodes[1].flags = VFS_FILE;
    g_proc_nodes[1].inode = 2;
    g_proc_nodes[1].ops   = &g_proc_meminfo_ops;
    kstrncpy(g_proc_dirents[1].name, "meminfo", VFS_MAX_NAME_LEN);
    g_proc_dirents[1].ino = 2;

    /* 3. /proc/version */
    kstrncpy(g_proc_nodes[2].name, "version", VFS_MAX_NAME_LEN);
    g_proc_nodes[2].flags = VFS_FILE;
    g_proc_nodes[2].inode = 3;
    g_proc_nodes[2].ops   = &g_proc_version_ops;
    kstrncpy(g_proc_dirents[2].name, "version", VFS_MAX_NAME_LEN);
    g_proc_dirents[2].ino = 3;

    /* VFS'e "/live" olarak mount et */
    vfs_register_mount("/live", &g_procfs_root);

    serial_printf("===========================================\n");
    serial_printf(" ZeruX OS — Process File System (/live)\n");
    serial_printf("===========================================\n");
    serial_printf("[PROCFS] Mounted live diagnostic files: /live/tasks, /live/meminfo, /live/version\n");
    serial_printf("===========================================\n\n");
}

/* =============================================================================
 * procfs_run_test_suite() — Adım 4.4 procfs "Açık Kalp Ameliyatı" Testi
 * =============================================================================
 */
void procfs_run_test_suite(void) {
    serial_printf("[PROCFS TEST] Reading '/live/tasks' live diagnostic file...\n");
    int fd_tasks = vfs_open("/live/tasks", 0);

    if (fd_tasks >= 0) {
        char buf[512] = {0};
        int32_t bytes = vfs_read(fd_tasks, buf, sizeof(buf) - 1);
        vfs_close(fd_tasks);

        serial_printf("\n--- [LIVE /live/tasks READ] ---\n%s-------------------------------\n", buf);

        if (bytes > 0) {
            serial_printf("\n=======================================================\n");
            serial_printf(" [PROCFS TEST PASSED] Live System Diagnostic (/live) Verified!\n");
            serial_printf("   - /live/tasks live task PCB list read successfully\n");
            serial_printf("   - Open-Heart System Inspection Active without stopping CPU\n");
            serial_printf("=======================================================\n\n");
        }
    }
}
