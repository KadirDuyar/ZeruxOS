/* =============================================================================
 * ZeruX OS — Kernel Heap Allocator (kmalloc / kfree) Header
 * File: kernel/include/kheap.h
 * =============================================================================
 *
 * Kernel içindeki dinamik bellek tahsisi (`kmalloc`) ve serbest bırakma (`kfree`)
 * hizmetini sunan Free-List tabanlı Heap Allocator sürücüsüdür.
 * =============================================================================
 */

#ifndef KHEAP_H
#define KHEAP_H

#include <stdint.h>
#include <stddef.h>
#include "boot_info.h"

#define HEAP_MAGIC              0x48454150  /* "HEAP" ASCII magic */
#define HEAP_START_VIRT_ADDR    0x40000000  /* 1 GB Sanal Adres Başlangıcı */
#define HEAP_INITIAL_SIZE_BYTES (32 * 1024 * 1024) /* Initial 32 MB Heap */

/* Heap Blok Başlık Yapısı (Header - 24 Bytes / 8-Byte Aligned) */
typedef struct heap_header {
    uint32_t            magic;     /* HEAP_MAGIC doğrulaması */
    uint32_t            size;      /* Kullanılabilir veri alanı boyutu (bayt) */
    uint8_t             is_free;   /* 1 = Serbest, 0 = Tahsis Edilmiş (Dolu) */
    uint8_t             padding[7];/* 8-byte hizalama için dolgu */
    struct heap_header *next;      /* Sonraki blok */
    struct heap_header *prev;      /* Önceki blok */
} heap_header_t;

/* Public API Bildirimleri */
void  kheap_init(const boot_info_t *boot_info);
void* kmalloc(size_t size);
void* kzalloc(size_t size);
void  kfree(void *ptr);

uint32_t kheap_get_total_size(void);
uint32_t kheap_get_used_size(void);
uint32_t kheap_get_free_size(void);

/* Adım 2.3 Doğrulama Testi */
void kheap_run_test_suite(void);

#endif /* KHEAP_H */
