/* =============================================================================
 * ZeruX OS — Kernel Log Ring Buffer (dmesg) Header
 * File: kernel/include/klog.h
 * =============================================================================
 */

#ifndef KLOG_H
#define KLOG_H

#include <stdint.h>
#include <stdbool.h>

#define KLOG_BUF_SIZE 16384 /* 16 KB Ring Buffer for Kernel Logs */

void klog_init(void);
void klog_putchar(char c);
void klog_set_suppress(bool suppress);

/* Logs the ring buffer to `out`. Returns the number of bytes written. */
uint32_t klog_dump(char *out, uint32_t max_len);

/* Flushes all logs to /disk/fat0/var/log/boot.log */
void klog_flush_to_disk(void);

#endif /* KLOG_H */
