/* =============================================================================
 * ZeruX OS — Virtual Process File System (procfs) Header
 * File: kernel/include/procfs.h
 * =============================================================================
 *
 * Canlı Sistem İnceleme ("Açık Kalp Ameliyatı" - /proc):
 *   - /proc/tasks   -> O an çalışan tüm PCB görevlerinin anlık dökümü
 *   - /proc/meminfo -> Fiziksel & Sanal bellek ve Heap kullanım istatistikleri
 *   - /proc/gdt     -> Donanımsal GDT ve TSS descriptor dökümü
 *   - /proc/version -> İşletim sistemi sürüm ve mimari bilgisi
 * =============================================================================
 */

#ifndef PROCFS_H
#define PROCFS_H

#include "vfs.h"

void procfs_init(void);
void procfs_run_test_suite(void);

#endif /* PROCFS_H */
