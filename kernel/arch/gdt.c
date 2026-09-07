/* =============================================================================
 * ZeruX OS — Dynamic GDT (Global Descriptor Table) Implementasyonu
 * File: kernel/arch/gdt.c
 * =============================================================================
 *
 * 6 Kapılı Dinamik GDT:
 *   - Index 0 (0x00): Null Descriptor
 *   - Index 1 (0x08): Kernel Code Segment (Ring 0)
 *   - Index 2 (0x10): Kernel Data Segment (Ring 0)
 *   - Index 3 (0x18): User Code Segment   (Ring 3, Selector 0x1B)
 *   - Index 4 (0x20): User Data Segment   (Ring 3, Selector 0x23)
 *   - Index 5 (0x28): Task State Segment  (TSS Descriptor)
 * =============================================================================
 */

#include "gdt.h"
#include "tss.h"
#include "serial.h"
#include "cpu.h"

#define GDT_NUM_ENTRIES 6

static gdt_entry_t gdt_entries[GDT_NUM_ENTRIES];
static gdt_ptr_t   gdt_ptr;

/* Low-level assembly fırlatma fonksiyonları (gdt_asm.asm'de tanımlı) */
extern void gdt_flush(gdt_ptr_t *ptr);

/* =============================================================================
 * gdt_set_gate() — GDT Kapısı Oluşturur
 * =============================================================================
 */
void gdt_set_gate(int num, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    gdt_entries[num].base_low    = (base & 0xFFFF);
    gdt_entries[num].base_middle = (base >> 16) & 0xFF;
    gdt_entries[num].base_high   = (base >> 24) & 0xFF;

    gdt_entries[num].limit_low   = (limit & 0xFFFF);
    gdt_entries[num].granularity = (limit >> 16) & 0x0F;

    gdt_entries[num].granularity |= gran & 0xF0;
    gdt_entries[num].access      = access;
}

/* =============================================================================
 * dump_gdt() — GDT Tablosunu Seri Port'a Detaylı Raporlar
 * =============================================================================
 */
void dump_gdt(void) {
    uint32_t tss_base = (uint32_t)gdt_entries[5].base_low |
                        ((uint32_t)gdt_entries[5].base_middle << 16) |
                        ((uint32_t)gdt_entries[5].base_high << 24);

    serial_printf("-------------------------------------------\n");
    serial_printf("[GDT DUMP] Base Address: %p  Limit: %u bytes\n", (void*)gdt_ptr.base, gdt_ptr.limit + 1);
    serial_printf("[GDT] 0x00 (Null)        : All Zeros\n");
    serial_printf("[GDT] 0x08 (Kernel Code) : Base=0x00000000 Limit=4GB Access=0x9A DPL=0\n");
    serial_printf("[GDT] 0x10 (Kernel Data) : Base=0x00000000 Limit=4GB Access=0x92 DPL=0\n");
    serial_printf("[GDT] 0x18 (User Code)   : Base=0x00000000 Limit=4GB Access=0xFA DPL=3 (Sel: 0x1B)\n");
    serial_printf("[GDT] 0x20 (User Data)   : Base=0x00000000 Limit=4GB Access=0xF2 DPL=3 (Sel: 0x23)\n");
    serial_printf("[GDT] 0x28 (TSS)         : Base=%p Limit=%u bytes Access=0x89\n",
                  (void*)tss_base, gdt_entries[5].limit_low);
    serial_printf("-------------------------------------------\n");
}

/* =============================================================================
 * gdt_init() — Dinamik GDT ve TSS Kurulumunu Gerçekleştirir
 * =============================================================================
 */
void gdt_init(void) {
    serial_printf("===========================================\n");
    serial_printf(" ZeruX OS — Dynamic GDT & TSS Initialization\n");
    serial_printf("===========================================\n");

    gdt_ptr.limit = (sizeof(gdt_entry_t) * GDT_NUM_ENTRIES) - 1;
    gdt_ptr.base  = (uint32_t)&gdt_entries;

    /* 0x00: Null Descriptor */
    gdt_set_gate(0, 0, 0, 0, 0);

    /* 0x08: Kernel Code Segment (Ring 0, Base=0, Limit=4GB, Access=0x9A, Gran=0xCF) */
    gdt_set_gate(1, 0, 0xFFFFF, 0x9A, 0xCF);

    /* 0x10: Kernel Data Segment (Ring 0, Base=0, Limit=4GB, Access=0x92, Gran=0xCF) */
    gdt_set_gate(2, 0, 0xFFFFF, 0x92, 0xCF);

    /* 0x18: User Code Segment (Ring 3, Base=0, Limit=4GB, Access=0xFA, Gran=0xCF) -> RPL=3 -> 0x1B */
    gdt_set_gate(3, 0, 0xFFFFF, 0xFA, 0xCF);

    /* 0x20: User Data Segment (Ring 3, Base=0, Limit=4GB, Access=0xF2, Gran=0xCF) -> RPL=3 -> 0x23 */
    gdt_set_gate(4, 0, 0xFFFFF, 0xF2, 0xCF);

    /* Assembly `lgdt` ile GDT'yi yükle ve CS/DS segmentlerini güncelle */
    gdt_flush(&gdt_ptr);
    serial_printf("[GDT] 6 Descriptor Gates installed successfully.\n");

    /* TSS Yapısını Kur ve GDT Index 5 (0x28)'e bağla */
    uint32_t current_esp = cpu_read_esp();
    tss_init(GDT_KERNEL_DATA_SEL, current_esp);

    dump_gdt();
    dump_tss();

    /* 3.0.7 Testi: `str ax` ile Task Register Selector'ın 0x28 olduğunu doğrula */
    uint16_t tr_selector = tss_get_tr();
    if (tr_selector == GDT_TSS_SEL) {
        serial_printf("\n=======================================================\n");
        serial_printf(" [GDT/TSS TEST PASSED] Task Register Loaded Successfully!\n");
        serial_printf("   - Hardware TR Selector : 0x%x (Matches GDT 0x28)\n", tr_selector);
        serial_printf("   - Kernel Stack (ESP0)  : %p\n", (void*)current_esp);
        serial_printf("   - Kernel Segment (SS0) : 0x10\n");
        serial_printf("=======================================================\n\n");
    } else {
        serial_printf("[GDT/TSS TEST FAILED] Expected TR 0x28, got 0x%x\n", tr_selector);
    }
}
