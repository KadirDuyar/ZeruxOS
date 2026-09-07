/* =============================================================================
 * ZeruX OS — Virtual Memory Manager (VMM / 32-bit Paging) Header
 * File: kernel/include/vmm.h
 * =============================================================================
 *
 * x86 32-bit Two-Tier Paging (İki Seviyeli Sayfalama Mimari) Sürücüsüdür.
 * Sanal adresleri (Virtual Address) fiziksel adreslere (Physical Address)
 * haritalar ve donanımsal bellek korumasını (CR0.PG = 1, CR3) aktif eder.
 * =============================================================================
 */

#ifndef VMM_H
#define VMM_H

#include <stdint.h>
#include "boot_info.h"

/* x86 Paging Bayrakları (Page Directory Entry & Page Table Entry Bits) */
#define VMM_FLAG_PRESENT        (1 << 0)  /* 1 = Sayfa fiziki bellekte mevcut */
#define VMM_FLAG_WRITABLE       (1 << 1)  /* 1 = Okunabilir & Yazılabilir, 0 = Read Only */
#define VMM_FLAG_USER           (1 << 2)  /* 1 = User Mode (Ring 3), 0 = Kernel (Ring 0) */
#define VMM_FLAG_WRITETHROUGH   (1 << 3)  /* Write-Through Caching */
#define VMM_FLAG_CACHE_DISABLE  (1 << 4)  /* Cache Disable */
#define VMM_FLAG_ACCESSED       (1 << 5)  /* CPU Okuma/Yazmada 1 yapar */
#define VMM_FLAG_DIRTY          (1 << 6)  /* CPU Yazma işleminde 1 yapar */
#define VMM_PAGE_SIZE           4096      /* 4 KB */

/* Page Directory & Page Table Yapıları */
typedef uint32_t pd_entry_t;
typedef uint32_t pt_entry_t;

typedef struct {
    pt_entry_t entries[1024];
} __attribute__((aligned(4096))) page_table_t;

typedef struct {
    pd_entry_t entries[1024];
} __attribute__((aligned(4096))) page_directory_t;

/* Public API Bildirimleri */
void             vmm_init(const boot_info_t *boot_info);
page_directory_t* vmm_get_kernel_page_directory(void);
page_directory_t* vmm_create_user_dir(void);
void             vmm_free_dir(page_directory_t *pdir);
void             vmm_switch_page_directory(page_directory_t *pdir);
void             vmm_enable_paging(void);

int              vmm_map_page(page_directory_t *pdir, uint32_t virt_addr, uint32_t phys_addr, uint32_t flags);
int              vmm_unmap_page(page_directory_t *pdir, uint32_t virt_addr);
uint32_t         vmm_get_physical_address(page_directory_t *pdir, uint32_t virt_addr);

static inline void flush_tlb_single(uint32_t addr) {
    asm volatile("invlpg (%0)" ::"r" (addr) : "memory");
}

/* TLB Invalidate Assembly Primitive */
static inline void vmm_flush_tlb_entry(uint32_t virt_addr) {
    __asm__ __volatile__("invlpg (%0)" : : "r"(virt_addr) : "memory");
}

/* Adım 2.2 Doğrulama Testi */
void vmm_run_test_suite(void);

#endif /* VMM_H */
