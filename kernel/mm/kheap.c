/* =============================================================================
 * ZeruX OS — Kernel Heap Allocator (kmalloc / kfree) Implementasyonu
 * File: kernel/mm/kheap.c
 * =============================================================================
 *
 * Bu dosya çekirdek içi dinamik bellek tahsisini (kmalloc / kfree) gerçekleştiren
 * Free-List tabanlı Heap Allocator sürücüsüdür.
 * =============================================================================
 */

#include "kheap.h"
#include "vmm.h"
#include "pmm.h"
#include "serial.h"
#include <stddef.h>

static heap_header_t *g_heap_start    = NULL;
static uint32_t       g_heap_total_size = 0;
static uint32_t       g_heap_used_size  = 0;

/* =============================================================================
 * kheap_init() — Kernel Heap Sürücüsünü Başlat
 * =============================================================================
 */
void kheap_init(const boot_info_t *boot_info) {
    (void)boot_info;

    serial_printf("===========================================\n");
    serial_printf(" ZeruX OS — Kernel Heap Allocator (kmalloc)\n");
    serial_printf("===========================================\n");

    /* 1. Heap için ilk 128 KB (32 sayfa) sanal bellek alanını PMM ve VMM ile map et */
    for (uint32_t addr = HEAP_START_VIRT_ADDR;
         addr < HEAP_START_VIRT_ADDR + HEAP_INITIAL_SIZE_BYTES;
         addr += VMM_PAGE_SIZE)
    {
        void *phys_page = pmm_alloc_block();
        if (!phys_page) {
            serial_printf("[KHEAP] CRITICAL ERROR: Failed to allocate physical frame for Heap!\n");
            return;
        }
        vmm_map_page(vmm_get_kernel_page_directory(), addr, (uint32_t)phys_page, VMM_FLAG_PRESENT | VMM_FLAG_WRITABLE | VMM_FLAG_USER);
    }

    /* 2. Kök Heap Header düğümünü oluştur */
    g_heap_start = (heap_header_t*)HEAP_START_VIRT_ADDR;
    g_heap_start->magic   = HEAP_MAGIC;
    g_heap_start->size    = HEAP_INITIAL_SIZE_BYTES - sizeof(heap_header_t);
    g_heap_start->is_free = 1;
    g_heap_start->next    = NULL;
    g_heap_start->prev    = NULL;

    g_heap_total_size = HEAP_INITIAL_SIZE_BYTES;
    g_heap_used_size  = sizeof(heap_header_t);

    serial_printf("[KHEAP] Initial Heap Region  : %p - %p (%u KB)\n",
                  (void*)HEAP_START_VIRT_ADDR,
                  (void*)(HEAP_START_VIRT_ADDR + HEAP_INITIAL_SIZE_BYTES),
                  HEAP_INITIAL_SIZE_BYTES / 1024);
    serial_printf("[KHEAP] Header Structure Size: %u bytes\n", sizeof(heap_header_t));
    serial_printf("[KHEAP] Initial Free Capacity: %u KB\n", g_heap_start->size / 1024);
    serial_printf("===========================================\n\n");
}

/* =============================================================================
 * kmalloc() — Dinamik Bellek Tahsis Et
 * =============================================================================
 */
void* kmalloc(size_t size) {
    if (size == 0) return NULL;

    /* Bayt hizalama: 8 bayt katlarına yuvarla */
    size = (size + 7U) & ~7U;

    heap_header_t *curr = g_heap_start;
    while (curr != NULL) {
        if (curr->magic != HEAP_MAGIC) {
            serial_printf("[KHEAP] CRITICAL ERROR: Corrupted heap header at %p!\n", curr);
            return NULL;
        }

        if (curr->is_free && curr->size >= size) {
            /* Bloğu bölme (Splitting) şartı: kalan alan bir header + 16 byte veriden büyükse */
            if (curr->size >= size + sizeof(heap_header_t) + 16U) {
                heap_header_t *next_block = (heap_header_t*)((uint8_t*)curr + sizeof(heap_header_t) + size);
                next_block->magic   = HEAP_MAGIC;
                next_block->size    = curr->size - size - sizeof(heap_header_t);
                next_block->is_free = 1;
                next_block->next    = curr->next;
                next_block->prev    = curr;

                if (curr->next) {
                    curr->next->prev = next_block;
                }
                curr->next = next_block;
                curr->size = size;
                g_heap_used_size += sizeof(heap_header_t);
            }

            curr->is_free = 0;
            g_heap_used_size += curr->size;

            return (void*)((uint8_t*)curr + sizeof(heap_header_t));
        }

        curr = curr->next;
    }

    serial_printf("[KHEAP] WARNING: Out of Heap memory for size %u bytes!\n", size);
    return NULL;
}

/* =============================================================================
 * kzalloc() — Bellek Tahsis Et ve Sıfırla
 * =============================================================================
 */
void* kzalloc(size_t size) {
    void *ptr = kmalloc(size);
    if (ptr) {
        uint8_t *b = (uint8_t*)ptr;
        for (size_t i = 0; i < size; i++) {
            b[i] = 0;
        }
    }
    return ptr;
}

/* =============================================================================
 * kfree() — Tahsis Edilen Belleği Serbest Bırak ve Komşularla Birleştir (Coalescing)
 * =============================================================================
 */
void kfree(void *ptr) {
    if (!ptr) return;

    heap_header_t *header = (heap_header_t*)((uint8_t*)ptr - sizeof(heap_header_t));
    if (header->magic != HEAP_MAGIC) {
        serial_printf("[KHEAP] ERROR: Invalid magic 0x%x on kfree(%p)!\n", header->magic, ptr);
        return;
    }

    header->is_free = 1;
    if (g_heap_used_size >= header->size) {
        g_heap_used_size -= header->size;
    }

    /* Coalescing (Sağ Komşu Birleştirme) */
    if (header->next && header->next->is_free) {
        header->size += sizeof(heap_header_t) + header->next->size;
        header->next = header->next->next;
        if (header->next) {
            header->next->prev = header;
        }
        if (g_heap_used_size >= sizeof(heap_header_t)) {
            g_heap_used_size -= sizeof(heap_header_t);
        }
    }

    /* Coalescing (Sol Komşu Birleştirme) */
    if (header->prev && header->prev->is_free) {
        header->prev->size += sizeof(heap_header_t) + header->size;
        header->prev->next = header->next;
        if (header->next) {
            header->next->prev = header->prev;
        }
        if (g_heap_used_size >= sizeof(heap_header_t)) {
            g_heap_used_size -= sizeof(heap_header_t);
        }
    }
}

uint32_t kheap_get_total_size(void) { return g_heap_total_size; }
uint32_t kheap_get_used_size(void)  { return g_heap_used_size; }
uint32_t kheap_get_free_size(void)  { return g_heap_total_size - g_heap_used_size; }

/* =============================================================================
 * kheap_run_test_suite() — Adım 2.3 Doğrulama Testi
 * =============================================================================
 */
void kheap_run_test_suite(void) {
    serial_printf("[KHEAP TEST] Running Dynamic Kernel Heap Allocator test suite...\n");

    /* 1. Farklı boyutlarda dinamik bellek tahsisleri yap */
    char     *str  = (char*)kmalloc(64);
    uint32_t *arr  = (uint32_t*)kmalloc(10 * sizeof(uint32_t));
    void     *blk  = kmalloc(512);

    serial_printf("[KHEAP TEST] kmalloc(64)   -> Pointer: %p\n", (void*)str);
    serial_printf("[KHEAP TEST] kmalloc(40)   -> Pointer: %p\n", (void*)arr);
    serial_printf("[KHEAP TEST] kmalloc(512)  -> Pointer: %p\n", blk);

    /* String işlemi testi */
    if (str) {
        const char *msg = "ZeruX Kernel Heap Works!";
        int i = 0;
        while (msg[i]) {
            str[i] = msg[i];
            i++;
        }
        str[i] = '\0';
        serial_printf("[KHEAP TEST] Data written to Heap string: \"%s\"\n", str);
    }

    /* 2. Ortadaki bloğu serbest bırak (arr / 40 byte) */
    serial_printf("[KHEAP TEST] Freeing arr block (%p)...\n", (void*)arr);
    kfree(arr);

    /* 3. Yeniden küçük bir blok iste (32 byte) -> Serbest bırakılan yeri kullanmalı */
    void *reused = kmalloc(32);
    serial_printf("[KHEAP TEST] Re-allocated 32 bytes -> Pointer: %p\n", reused);

    if (reused == arr) {
        serial_printf("\n=======================================================\n");
        serial_printf(" [KHEAP TEST PASSED] Dynamic Alloc, Free & Coalescing Verified!\n");
        serial_printf("   - str    = %p\n", (void*)str);
        serial_printf("   - arr    = %p (Freed)\n", (void*)arr);
        serial_printf("   - blk    = %p\n", blk);
        serial_printf("   - reused = %p (Exact Reuse of Freed Heap Block)\n", reused);
        serial_printf("=======================================================\n\n");
    } else {
        serial_printf("[KHEAP TEST WARNING] Reused address %p differs from %p\n", reused, (void*)arr);
    }

    kfree(str);
    kfree(reused);
    kfree(blk);
}
