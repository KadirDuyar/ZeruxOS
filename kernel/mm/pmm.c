/* =============================================================================
 * ZeruX OS — Physical Memory Manager (PMM / Bitmap Allocator) Implementasyonu
 * File: kernel/mm/pmm.c
 * =============================================================================
 *
 * Bu dosya E820 bellek haritasına dayanarak fiziksel RAM'i 4 KB'lık bloklar
 * halinde yöneten Bitmap tabanlı PMM sürücüsüdür.
 * =============================================================================
 */

#include "pmm.h"
#include "serial.h"
#include <stddef.h>

/* Linker script sembolü — Kernel kod ve veri segmentlerinin bittiği adres */
extern uint8_t _kernel_end[];

static uint32_t *g_pmm_bitmap        = NULL;
static uint32_t  g_total_blocks      = 0;
static uint32_t  g_used_blocks       = 0;
static uint32_t  g_bitmap_size_words = 0;

/* =============================================================================
 * BITMAP DİZİSİ YARDIMCI MAKROLARI
 * =============================================================================
 */
static inline void pmm_bit_set(uint32_t bit) {
    g_pmm_bitmap[bit / 32] |= (1U << (bit % 32));
}

static inline void pmm_bit_unset(uint32_t bit) {
    g_pmm_bitmap[bit / 32] &= ~(1U << (bit % 32));
}

static inline int pmm_bit_test(uint32_t bit) {
    return (g_pmm_bitmap[bit / 32] & (1U << (bit % 32))) != 0;
}

/* Bir fiziksel bellek aralığını bitmap'te SERBEST (Free = 0) olarak işaretle */
static void pmm_mark_range_free(uint64_t base_addr, uint64_t length) {
    uint32_t start_block = (uint32_t)(base_addr / PMM_PAGE_SIZE);
    uint32_t block_count = (uint32_t)(length / PMM_PAGE_SIZE);

    for (uint32_t i = 0; i < block_count; i++) {
        uint32_t block = start_block + i;
        if (block < g_total_blocks) {
            if (pmm_bit_test(block)) {
                pmm_bit_unset(block);
                if (g_used_blocks > 0) g_used_blocks--;
            }
        }
    }
}

/* Bir fiziksel bellek aralığını bitmap'te DOLU (Used = 1) olarak korumaya al */
static void pmm_mark_range_used(uint64_t base_addr, uint64_t length) {
    uint32_t start_block = (uint32_t)(base_addr / PMM_PAGE_SIZE);
    uint32_t block_count = (uint32_t)((length + PMM_PAGE_SIZE - 1) / PMM_PAGE_SIZE);

    for (uint32_t i = 0; i < block_count; i++) {
        uint32_t block = start_block + i;
        if (block < g_total_blocks) {
            if (!pmm_bit_test(block)) {
                pmm_bit_set(block);
                g_used_blocks++;
            }
        }
    }
}

/* =============================================================================
 * pmm_init() — PMM Sürücüsünü Başlat
 * =============================================================================
 */
void pmm_init(const boot_info_t *boot_info) {
    serial_printf("===========================================\n");
    serial_printf(" ZeruX OS — Physical Memory Manager (PMM)\n");
    serial_printf("===========================================\n");

    if (!boot_info || boot_info->magic != BOOT_INFO_MAGIC) {
        serial_printf("[PMM] ERROR: Invalid boot_info! PMM initialization aborted.\n");
        return;
    }

    const e820_entry_t *map = (const e820_entry_t*)boot_info->e820_addr;
    uint64_t max_mem_addr = 0;

    /* 1. En yüksek SADECE KULLANILABİLİR (Usable RAM / ACPI) fiziksel bellek adresini bul */
    for (uint32_t i = 0; i < boot_info->e820_count; i++) {
        if (map[i].type == E820_TYPE_USABLE || map[i].type == E820_TYPE_ACPI_RECLAIM) {
            uint64_t entry_end = map[i].base_addr + map[i].length;
            if (entry_end > max_mem_addr) {
                max_mem_addr = entry_end;
            }
        }
    }

    /* 4GB ust siniri ekle: 32-bit Paging su an PAE desteklemedigi icin
     * fiziksel adreslerin 4GB (0xFFFFFFFF) ustune cikmasini engelliyoruz. */
    if (max_mem_addr > 0xFFFFFFFFULL) {
        max_mem_addr = 0xFFFFFFFFULL;
    }

    /* 2. Toplam 4 KB'lık sayfa bloğu ve bitmap boyutunu hesapla */
    g_total_blocks = (uint32_t)(max_mem_addr / PMM_PAGE_SIZE);
    g_bitmap_size_words = (g_total_blocks + 31) / 32;

    /* 3. Bitmap dizisini Kernel sonrasına (_kernel_end) yerleştir */
    g_pmm_bitmap = (uint32_t*)_kernel_end;
    uint32_t bitmap_bytes = g_bitmap_size_words * 4;
    uint32_t bitmap_end_addr = (uint32_t)_kernel_end + bitmap_bytes;

    /* 4. Başlangıçta TÜM belleği DOLU (1) olarak işaretle */
    for (uint32_t i = 0; i < g_bitmap_size_words; i++) {
        g_pmm_bitmap[i] = 0xFFFFFFFF;
    }
    g_used_blocks = g_total_blocks;

    /* 5. E820 haritasında "Usable RAM" (Type 1) olan alanları SERBEST (0) yap */
    for (uint32_t i = 0; i < boot_info->e820_count; i++) {
        if (map[i].type == E820_TYPE_USABLE) {
            pmm_mark_range_free(map[i].base_addr, map[i].length);
        }
    }

    /* 6. KRİTİK KORUMA: Düşük bellek (0x0 - 0x100000) alanını DOLU olarak kitle */
    pmm_mark_range_used(0x0, 0x100000);

    /* 7. KRİTİK KORUMA: Kernel kod/veri alanını ve Bitmap'in kendisini DOLU olarak kitle */
    pmm_mark_range_used(0x100000, bitmap_end_addr - 0x100000);

    uint32_t total_mb = (uint32_t)(max_mem_addr / (1024 * 1024));
    uint32_t free_blocks = pmm_get_free_blocks();
    uint32_t free_mb = (free_blocks * 4) / 1024;

    if (max_mem_addr >= 0x100000000ULL) {
        serial_printf("[PMM] Max Physical Address : 0xFFFFFFFF (%u MB / 4 GB Cap)\n", total_mb);
    } else {
        serial_printf("[PMM] Max Physical Address : %p (%u MB)\n", (void*)(uint32_t)max_mem_addr, total_mb);
    }
    serial_printf("[PMM] Total 4KB Page Frames: %u\n", g_total_blocks);
    serial_printf("[PMM] Bitmap Location      : %p - %p (%u KB)\n",
                  (void*)g_pmm_bitmap, (void*)bitmap_end_addr, (bitmap_bytes + 1023) / 1024);
    serial_printf("[PMM] Initial Free Memory   : %u MB (%u Free Pages, %u Reserved Pages)\n",
                  free_mb, free_blocks, g_used_blocks);
    serial_printf("===========================================\n\n");
}

/* =============================================================================
 * pmm_alloc_block() — 1 Adet 4 KB Fiziksel Sayfa Tahsis Et
 * =============================================================================
 */
void* pmm_alloc_block(void) {
    if (pmm_get_free_blocks() == 0) return NULL;

    for (uint32_t w = 0; w < g_bitmap_size_words; w++) {
        if (g_pmm_bitmap[w] != 0xFFFFFFFF) {
            /* Bu 32-bit kelimede en az 1 tane boş (0) bit var */
            for (int b = 0; b < 32; b++) {
                uint32_t bit_idx = w * 32 + b;
                if (bit_idx >= g_total_blocks) return NULL;

                if (!pmm_bit_test(bit_idx)) {
                    pmm_bit_set(bit_idx);
                    g_used_blocks++;
                    return (void*)(bit_idx * PMM_PAGE_SIZE);
                }
            }
        }
    }

    return NULL;
}

/* =============================================================================
 * pmm_free_block() — Fiziksel Sayfayı Serbest Bırak
 * =============================================================================
 */
void pmm_free_block(void *phys_addr) {
    uint32_t block = (uint32_t)phys_addr / PMM_PAGE_SIZE;

    if (block < g_total_blocks && pmm_bit_test(block)) {
        pmm_bit_unset(block);
        if (g_used_blocks > 0) g_used_blocks--;
    }
}

/* =============================================================================
 * pmm_alloc_blocks() — Ardışık `count` Adet 4 KB Sayfa Tahsis Et
 * =============================================================================
 */
void* pmm_alloc_blocks(uint32_t count) {
    if (count == 0) return NULL;
    if (count == 1) return pmm_alloc_block();

    uint32_t free_count = 0;
    uint32_t start_block = 0;

    for (uint32_t b = 0; b < g_total_blocks; b++) {
        if (!pmm_bit_test(b)) {
            if (free_count == 0) start_block = b;
            free_count++;

            if (free_count == count) {
                for (uint32_t i = 0; i < count; i++) {
                    pmm_bit_set(start_block + i);
                    g_used_blocks++;
                }
                return (void*)(start_block * PMM_PAGE_SIZE);
            }
        } else {
            free_count = 0;
        }
    }

    return NULL;
}

/* =============================================================================
 * pmm_free_blocks() — Ardışık `count` Sayfayı Serbest Bırak
 * =============================================================================
 */
void pmm_free_blocks(void *phys_addr, uint32_t count) {
    uint32_t start_block = (uint32_t)phys_addr / PMM_PAGE_SIZE;
    for (uint32_t i = 0; i < count; i++) {
        pmm_free_block((void*)((start_block + i) * PMM_PAGE_SIZE));
    }
}

uint32_t pmm_get_total_blocks(void) { return g_total_blocks; }
uint32_t pmm_get_used_blocks(void)  { return g_used_blocks; }
uint32_t pmm_get_free_blocks(void)  { return g_total_blocks - g_used_blocks; }

/* =============================================================================
 * pmm_run_test_suite() — Adım 2.1 Doğrulama Testi
 * =============================================================================
 */
void pmm_run_test_suite(void) {
    serial_printf("[PMM TEST] Running Physical Memory Allocator test suite...\n");

    void *ptr1 = pmm_alloc_block();
    void *ptr2 = pmm_alloc_block();
    void *ptr3 = pmm_alloc_block();

    serial_printf("[PMM TEST] Allocated Block 1: %p\n", ptr1);
    serial_printf("[PMM TEST] Allocated Block 2: %p\n", ptr2);
    serial_printf("[PMM TEST] Allocated Block 3: %p\n", ptr3);

    serial_printf("[PMM TEST] Freeing Block 2 (%p)...\n", ptr2);
    pmm_free_block(ptr2);

    void *ptr4 = pmm_alloc_block();
    serial_printf("[PMM TEST] Re-allocated Block: %p\n", ptr4);

    if (ptr4 == ptr2) {
        serial_printf("\n=======================================================\n");
        serial_printf(" [PMM TEST PASSED] Bitmap Page Frame Reuse Verified!\n");
        serial_printf("   - ptr1 = %p, ptr2 = %p, ptr3 = %p\n", ptr1, ptr2, ptr3);
        serial_printf("   - Freed ptr2 and re-allocated ptr4 = %p (Exact Match)\n", ptr4);
        serial_printf("=======================================================\n\n");
    } else {
        serial_printf("[PMM TEST FAILED] Expected %p, got %p\n", ptr2, ptr4);
    }

    pmm_free_block(ptr1);
    pmm_free_block(ptr3);
    pmm_free_block(ptr4);
}
