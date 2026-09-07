/* =============================================================================
 * ZeruX OS — Virtual Memory Manager (VMM / 32-bit Paging) Implementasyonu
 * File: kernel/mm/vmm.c
 * =============================================================================
 *
 * Bu dosya x86 32-bit Paging mimarisini kurar. Sanal adresleri fiziksel sayfalara
 * bağlar, Page Directory & Page Table yönetimi yapar ve CR3 / CR0.PG yazmaçları
 * ile donanımsal sayfalama korumasını aktif eder.
 * =============================================================================
 */

#include "vmm.h"
#include "pmm.h"
#include "serial.h"
#include "cpu.h"
#include <stddef.h>

static page_directory_t *g_kernel_pdir = NULL;

/* =============================================================================
 * vmm_init() — Sanal Bellek Yöneticisini Başlat & Identity Mapping Kur
 * =============================================================================
 */
void vmm_init(const boot_info_t *boot_info) {
    serial_printf("===========================================\n");
    serial_printf(" ZeruX OS — Virtual Memory Manager (VMM / Paging)\n");
    serial_printf("===========================================\n");

    if (!boot_info || boot_info->magic != BOOT_INFO_MAGIC) {
        serial_printf("[VMM] ERROR: Invalid boot_info! Paging initialization aborted.\n");
        return;
    }

    /* 1. Kernel Page Directory için PMM'den 4 KB fiziki sayfa ayır */
    g_kernel_pdir = (page_directory_t*)pmm_alloc_block();
    if (!g_kernel_pdir) {
        serial_printf("[VMM] CRITICAL ERROR: Could not allocate memory for Kernel Page Directory!\n");
        return;
    }

    /* Page Directory girdilerini sıfırla (Not Present) */
    for (int i = 0; i < 1024; i++) {
        g_kernel_pdir->entries[i] = 0;
    }

    /* 2. IDENTITY MAPPING: Tümüyle RAM'i birebir bağla.
     * Kernel Heap (1 GB), ELF (2 GB) gibi sanal bellek alanlarını yeterince
     * uzağa taşıdığımız için artık Identity Map'in çakışma riski yoktur.
     * Böylece PMM'den dönen herhangi bir fiziksel adrese (örneğin yeni Page Table'lara)
     * doğrudan erişebiliriz. */
    uint32_t max_ram_addr = (uint32_t)boot_info_get_total_usable_ram() + (1024 * 1024);
    if (max_ram_addr < 0x04000000) max_ram_addr = 0x04000000; /* Min 64MB */

    serial_printf("[VMM] Building Identity Map: 0x00000000 - %p (%u MB)...\n",
                  (void*)max_ram_addr, max_ram_addr / (1024 * 1024));

    for (uint32_t addr = 0; addr < max_ram_addr; addr += VMM_PAGE_SIZE) {
        vmm_map_page(g_kernel_pdir, addr, addr, VMM_FLAG_PRESENT | VMM_FLAG_WRITABLE);
    }

    /* 3. CR3 Yazmacına Page Directory Fiziki Adresini Yükle */
    vmm_switch_page_directory(g_kernel_pdir);

    /* 4. CR0.PG (Bit 31) Açarak 32-bit Donanımsal Paging'i Aktif Et */
    vmm_enable_paging();

    uint32_t cr0 = cpu_read_cr0();
    serial_printf("[VMM] 32-bit Paging enabled successfully!\n");
    serial_printf("[VMM] CR3 Page Directory : %p\n", (void*)g_kernel_pdir);
    serial_printf("[VMM] CR0 Control Register : 0x%x (PG Bit 31 = %s)\n",
                  cr0, (cr0 & 0x80000000) ? "ACTIVE" : "INACTIVE");
    serial_printf("===========================================\n\n");
}

/* =============================================================================
 * vmm_get_kernel_page_directory()
 * =============================================================================
 */
page_directory_t* vmm_get_kernel_page_directory(void) {
    return g_kernel_pdir;
}

/* =============================================================================
 * vmm_switch_page_directory() — CR3 Yazmacına Yeni Page Directory Yükler
 * =============================================================================
 */
void vmm_switch_page_directory(page_directory_t *pdir) {
    if (!pdir) return;
    __asm__ __volatile__("mov %0, %%cr3" : : "r"((uint32_t)pdir) : "memory");
}

/* =============================================================================
 * vmm_enable_paging() — CR0.PG (Bit 31) Set Ederek Paging'i Açar
 * =============================================================================
 */
void vmm_enable_paging(void) {
    __asm__ __volatile__(
        "mov %%cr0, %%eax\n"
        "or  $0x80000000, %%eax\n"
        "mov %%eax, %%cr0\n"
        : : : "eax", "memory"
    );
}

/* =============================================================================
 * vmm_map_page() — Sanal Adresi Fiziksel Adrese Haritalar
 * =============================================================================
 */
int vmm_map_page(page_directory_t *pdir, uint32_t virt_addr, uint32_t phys_addr, uint32_t flags) {
    if (!pdir) return -1;

    uint32_t pd_idx = virt_addr >> 22;          /* Üst 10 bit → Directory Index */
    uint32_t pt_idx = (virt_addr >> 12) & 0x3FF;/* Orta 10 bit → Table Index */

    pd_entry_t *pde = &pdir->entries[pd_idx];
    page_table_t *pt = NULL;

    /* Page Table mevcut değilse PMM'den yeni bir 4 KB sayfa al ve oluştur */
    if (!(*pde & VMM_FLAG_PRESENT)) {
        pt = (page_table_t*)pmm_alloc_block();
        if (!pt) return -1; /* Out of memory */

        for (int i = 0; i < 1024; i++) {
            pt->entries[i] = 0;
        }

        /* PDE kaydını oluştur */
        *pde = ((uint32_t)pt & 0xFFFFF000) | VMM_FLAG_PRESENT | VMM_FLAG_WRITABLE | (flags & VMM_FLAG_USER);
    } else {
        pt = (page_table_t*)(*pde & 0xFFFFF000);
        *pde |= (flags & VMM_FLAG_USER);
    }

    /* Page Table Entry kaydını yaz */
    pt->entries[pt_idx] = (phys_addr & 0xFFFFF000) | flags | VMM_FLAG_PRESENT;

    /* TLB Önbelleğini Temizle */
    vmm_flush_tlb_entry(virt_addr);
    return 0;
}

/* =============================================================================
 * vmm_unmap_page() — Sanal Adres Haritasını Kaldırır
 * =============================================================================
 */
int vmm_unmap_page(page_directory_t *pdir, uint32_t virt_addr) {
    if (!pdir) return -1;

    uint32_t pd_idx = virt_addr >> 22;
    uint32_t pt_idx = (virt_addr >> 12) & 0x3FF;

    pd_entry_t pde = pdir->entries[pd_idx];
    if (!(pde & VMM_FLAG_PRESENT)) return -1;

    page_table_t *pt = (page_table_t*)(pde & 0xFFFFF000);
    pt->entries[pt_idx] = 0;

    vmm_flush_tlb_entry(virt_addr);
    return 0;
}

/* =============================================================================
 * vmm_get_physical_address() — Sanal Adresin Karşılığı Olan Fiziki Adresi Döner
 * =============================================================================
 */
uint32_t vmm_get_physical_address(page_directory_t *pdir, uint32_t virt_addr) {
    if (!pdir) return 0;

    uint32_t pd_idx = virt_addr >> 22;
    uint32_t pt_idx = (virt_addr >> 12) & 0x3FF;
    uint32_t offset = virt_addr & 0xFFF;

    pd_entry_t pde = pdir->entries[pd_idx];
    if (!(pde & VMM_FLAG_PRESENT)) return 0;

    page_table_t *pt = (page_table_t*)(pde & 0xFFFFF000);
    pt_entry_t pte = pt->entries[pt_idx];
    if (!(pte & VMM_FLAG_PRESENT)) return 0;

    return (pte & 0xFFFFF000) | offset;
}

/* =============================================================================
 * vmm_create_user_dir() — Create a new Page Directory cloned from Kernel
 * =============================================================================
 */
page_directory_t* vmm_create_user_dir(void) {
    /* 1. Allocate a 4KB-aligned physical page for the new Page Directory */
    page_directory_t *new_dir = (page_directory_t*)pmm_alloc_block();
    if (!new_dir) {
        serial_printf("[VMM] Failed to allocate new Page Directory\n");
        return 0;
    }

    /* 2. Copy the Kernel's Page Directory Entries (Shared Kernel Space) */
    /* Note: We share the Page Tables for Kernel space. User space PDEs are 0 in kernel_dir */
    for (int i = 0; i < 1024; i++) {
        new_dir->entries[i] = g_kernel_pdir->entries[i];
    }

    return new_dir;
}

/* =============================================================================
 * vmm_free_dir() - Free a Page Directory and all its user-space pages
 * =============================================================================
 */
void vmm_free_dir(page_directory_t *pdir) {
    if (!pdir || pdir == g_kernel_pdir) return;

    /* Iterate over all 1024 Page Directory Entries */
    for (int i = 0; i < 1024; i++) {
        /* Skip PDEs that belong to the shared Kernel space */
        if (pdir->entries[i] == g_kernel_pdir->entries[i]) continue;
        
        if (pdir->entries[i] & VMM_FLAG_PRESENT) {
            uint32_t pt_phys = pdir->entries[i] & 0xFFFFF000;
            page_table_t *pt = (page_table_t*)pt_phys; /* Assuming Identity Mapping for PT access */
            
            /* Free all physical frames mapped by this Page Table */
            for (int j = 0; j < 1024; j++) {
                if (pt->entries[j] & VMM_FLAG_PRESENT) {
                    uint32_t frame_phys = pt->entries[j] & 0xFFFFF000;
                    pmm_free_block((void*)frame_phys);
                }
            }
            /* Free the Page Table itself */
            pmm_free_block((void*)pt_phys);
        }
    }
    
    /* Finally, free the Page Directory itself */
    pmm_free_block((void*)pdir);
}
/* =============================================================================
 * vmm_run_test_suite() — Adım 2.2 Doğrulama Testi
 * =============================================================================
 */
void vmm_run_test_suite(void) {
    serial_printf("[VMM TEST] Running Virtual Memory Paging test suite...\n");

    /* 1. PMM'den boş 1 fiziksel sayfa ayır */
    void *phys_page = pmm_alloc_block();
    uint32_t virt_test_addr = 0xA0000000; /* Test sanal adresi */

    serial_printf("[VMM TEST] Allocating Physical Page: %p\n", phys_page);
    serial_printf("[VMM TEST] Mapping Virtual Address 0x%x -> Physical %p...\n", virt_test_addr, phys_page);

    /* 2. Sanal adres 0xA0000000'ı bu fiziksel sayfaya bağla */
    if (vmm_map_page(g_kernel_pdir, virt_test_addr, (uint32_t)phys_page, VMM_FLAG_PRESENT | VMM_FLAG_WRITABLE) != 0) {
        serial_printf("[VMM TEST FAILED] Mapping failed!\n");
        return;
    }

    /* 3. Sanal adrese veri yaz ve oku */
    volatile uint32_t *ptr = (volatile uint32_t*)virt_test_addr;
    uint32_t magic_val = 0xDEADBEEF;

    serial_printf("[VMM TEST] Writing magic value 0x%x to virtual address %p...\n", magic_val, (void*)virt_test_addr);
    *ptr = magic_val;

    uint32_t read_back = *ptr;
    uint32_t translated_phys = vmm_get_physical_address(g_kernel_pdir, virt_test_addr);

    serial_printf("[VMM TEST] Read back value from virtual address: 0x%x\n", read_back);
    serial_printf("[VMM TEST] Translated Physical Address       : %p\n", (void*)translated_phys);

    if (read_back == magic_val && translated_phys == (uint32_t)phys_page) {
        serial_printf("\n=======================================================\n");
        serial_printf(" [VMM TEST PASSED] 32-bit Paging & Virtual Map Verified!\n");
        serial_printf("   - Virtual Addr : 0x%x\n", virt_test_addr);
        serial_printf("   - Physical Addr: %p (Matches Allocated Frame)\n", phys_page);
        serial_printf("   - Value Read   : 0x%x (0xDEADBEEF Correct)\n", read_back);
        serial_printf("=======================================================\n\n");
    } else {
        serial_printf("[VMM TEST FAILED] Mismatch detected!\n");
    }

    /* Temizlik: Test haritalamasını kaldır ve fiziksel sayfayı serbest bırak */
    vmm_unmap_page(g_kernel_pdir, virt_test_addr);
    pmm_free_block(phys_page);
}
