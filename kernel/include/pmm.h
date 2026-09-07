/* =============================================================================
 * ZeruX OS — Physical Memory Manager (PMM / Bitmap Allocator) Header
 * File: kernel/include/pmm.h
 * =============================================================================
 *
 * PMM, sistemdeki fiziksel RAM'i 4 KB'lık sayfalara (Page Frames) böler.
 * Bitmapsel veri yapısı (Bitmap) ile hangi fiziksel sayfanın boş (0) veya
 * dolu (1) olduğunu takip eder ve sayfa düzeyinde tahsis (`pmm_alloc_block`)
 * ile serbest bırakma (`pmm_free_block`) hizmeti sunar.
 * =============================================================================
 */

#ifndef PMM_H
#define PMM_H

#include <stdint.h>
#include "boot_info.h"

/* x86 32-bit standart sayfa boyutu: 4096 byte (4 KB) */
#define PMM_PAGE_SIZE      4096
#define PMM_BLOCKS_PER_BYTE 8

/* Public API Bildirimleri */
void     pmm_init(const boot_info_t *boot_info);
void*    pmm_alloc_block(void);
void     pmm_free_block(void *phys_addr);
void*    pmm_alloc_blocks(uint32_t count);
void     pmm_free_blocks(void *phys_addr, uint32_t count);

uint32_t pmm_get_total_blocks(void);
uint32_t pmm_get_free_blocks(void);
uint32_t pmm_get_used_blocks(void);

/* PMM Doğrulama & Sınama Testi (Adım 2.1) */
void pmm_run_test_suite(void);

#endif /* PMM_H */
