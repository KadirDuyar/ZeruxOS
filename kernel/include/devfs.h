/* =============================================================================
 * ZeruX OS — Virtual Device File System (devfs) Header
 * File: kernel/include/devfs.h
 * =============================================================================
 *
 * Sanal Cihaz Dosya Sistemi (/dev):
 *   - /dev/serial0 -> Seri Port (COM1) Karakter Cihazı
 *   - /dev/kbd     -> PS/2 Klavye Karakter Cihazı
 *   - /dev/null    -> Unix Data Sink (Tüm yazılanları yutar)
 *   - /dev/zero    -> Unix Zero Generator (Sürekli 0x00 üretir)
 * =============================================================================
 */

#ifndef DEVFS_H
#define DEVFS_H

#include "vfs.h"

void devfs_init(void);
void devfs_run_test_suite(void);

#endif /* DEVFS_H */
