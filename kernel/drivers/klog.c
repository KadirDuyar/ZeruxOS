/* =============================================================================
 * ZeruX OS — Kernel Log Ring Buffer (dmesg) Implementation
 * File: kernel/drivers/klog.c
 * =============================================================================
 */

#include "klog.h"

static char     g_klog_buf[KLOG_BUF_SIZE];
static uint32_t g_klog_head = 0;      /* bir sonraki yazımın gideceği konum */
static uint32_t g_klog_total = 0;     /* şimdiye kadar yazılan toplam bayt (taşma tespiti için) */
static bool     g_klog_suppress = false;

void klog_init(void) {
    g_klog_head = 0;
    g_klog_total = 0;
    g_klog_suppress = false;
}

void klog_putchar(char c) {
    if (g_klog_suppress) return;

    g_klog_buf[g_klog_head] = c;
    g_klog_head = (g_klog_head + 1) % KLOG_BUF_SIZE;
    g_klog_total++;
}

void klog_set_suppress(bool suppress) {
    g_klog_suppress = suppress;
}

uint32_t klog_dump(char *out, uint32_t max_len) {
    if (!out || max_len == 0) return 0;

    uint32_t available = (g_klog_total < KLOG_BUF_SIZE) ? g_klog_total : KLOG_BUF_SIZE;
    /* Halka henüz sarmadıysa en eski bayt index 0'dadır; sardıysa en eski bayt
     * tam olarak g_klog_head konumundadır (üzerine en son yazılacak yer). */
    uint32_t oldest = (g_klog_total < KLOG_BUF_SIZE) ? 0 : g_klog_head;

    uint32_t n = available;
    if (n > max_len) {
        /* Sadece en son max_len baytı ver, daha eski kısmı atla */
        oldest = (oldest + (n - max_len)) % KLOG_BUF_SIZE;
        n = max_len;
    }

    for (uint32_t i = 0; i < n; i++) {
        out[i] = g_klog_buf[(oldest + i) % KLOG_BUF_SIZE];
    }
    return n;
}

#include "fat32.h"
#include "vfs.h"

void klog_flush_to_disk(void) {
    uint32_t root = fat32_get_root_cluster();
    if (root == 0) return;

    /* Ensure VAR directory exists */
    uint32_t var_cluster = fat32_find_entry(root, "VAR");
    if (!var_cluster) var_cluster = fat32_find_entry(root, "var");
    if (!var_cluster) var_cluster = fat32_mkdir(root, "VAR");
    if (!var_cluster) return;

    /* Ensure LOG directory exists under VAR */
    uint32_t log_cluster = fat32_find_entry(var_cluster, "LOG");
    if (!log_cluster) log_cluster = fat32_find_entry(var_cluster, "log");
    if (!log_cluster) log_cluster = fat32_mkdir(var_cluster, "LOG");
    if (!log_cluster) return;

    /* Ensure BOOT.LOG exists under LOG */
    uint32_t file_cluster = fat32_find_entry(log_cluster, "BOOT.LOG");
    if (!file_cluster) file_cluster = fat32_find_entry(log_cluster, "boot.log");
    if (!file_cluster) file_cluster = fat32_create(log_cluster, "BOOT.LOG");
    if (!file_cluster) return;

    /* Temporarily suppress klog while writing log file so we don't recurse */
    klog_set_suppress(true);

    int fd = vfs_open("/disk/fat0/VAR/LOG/BOOT.LOG", 0);
    if (fd < 0) {
        fd = vfs_open("/disk/fat0/var/log/boot.log", 0);
    }

    if (fd >= 0) {
        static char dump_tmp[4096];
        uint32_t dump_len = klog_dump(dump_tmp, sizeof(dump_tmp) - 1);
        if (dump_len > 0) {
            vfs_write(fd, dump_tmp, dump_len);
        }
        vfs_close(fd);
    }

    klog_set_suppress(false);
}
