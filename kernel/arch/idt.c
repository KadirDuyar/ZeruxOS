/* =============================================================================
 * ZeruX OS — Interrupt Descriptor Table (IDT) Implementasyonu
 * File: kernel/arch/idt.c
 * =============================================================================
 *
 * Bu dosya 256 elemanlı IDT tablosunu bellekte tahsis eder, kapıları doldurur
 * ve x86 CPU'suna `lidt` instruction'ı ile tablonun adresini bildirir.
 *
 * GDT vs IDT KOD YÜKLEME:
 *   - GDT: `lgdt [gdt_ptr]` ile yüklenir
 *   - IDT: `lidt [idtr]` ile yüklenir
 *
 * Her iki komut da Ring 0 yetkisi gerektirir.
 * =============================================================================
 */

#include "idt.h"
#include "serial.h"
#include "ports.h"

/* 256 kapılık IDT dizisi ve IDTR pointer'ı (statik bellek tahsisi) */
static idt_gate_t idt[IDT_NUM_ENTRIES];
static idtr_t     idtr;

/* =============================================================================
 * idt_set_gate() — Tek bir IDT Kapısını Konfigüre Et
 * =============================================================================
 *
 *  32-bit Handler adresi (base) iki parçaya bölünür:
 *    - Düşük 16-bit (base & 0xFFFF)   → low_offset
 *    - Yüksek 16-bit (base >> 16)     → high_offset
 *
 *  Parametreler:
 *    num   : Vektör numarası (0..255)
 *    base  : Handler fonksiyonunun 32-bit bellek adresi
 *    sel   : Segment Selector (0x08 = Kernel Code Segment)
 *    flags : Gate erişim bayrakları (örn: IDT_FLAGS_KERNEL_INT)
 */
void idt_set_gate(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags) {
    idt[num].low_offset  = (uint16_t)(base & 0xFFFF);
    idt[num].selector    = sel;
    idt[num].always0     = 0;
    idt[num].flags       = flags;
    idt[num].high_offset = (uint16_t)((base >> 16) & 0xFFFF);
}

/* =============================================================================
 * idt_init() — IDT Tablosunu Sıfırla ve CPU'ya Yükle
 * =============================================================================
 */
void idt_init(void) {
    /* 1. Tüm IDT kapılarını sıfırla */
    for (int i = 0; i < IDT_NUM_ENTRIES; i++) {
        idt[i].low_offset  = 0;
        idt[i].selector    = 0;
        idt[i].always0     = 0;
        idt[i].flags       = 0;
        idt[i].high_offset = 0;
    }

    /* 2. IDTR yapısını hazırla */
    idtr.limit = (uint16_t)(sizeof(idt_gate_t) * IDT_NUM_ENTRIES - 1);  /* 2047 byte */
    idtr.base  = (uint32_t)&idt;

    /* 3. `lidt` instruction'ı ile IDTR'yi CPU'ya yükle
     *
     * inline asm syntax:
     *   "lidt %0" : operand 0 (m constraint = memory)
     *   "m"(idtr) : derleyiciye idtr struct'ının adresini operand olarak vermesini söyler
     */
    __asm__ volatile (
        "lidt %0"
        :
        : "m"(idtr)
        : "memory"
    );

    serial_printf("[IDT] Initialized 256 gates (base: %p, limit: 0x%x)\n",
                  (void*)idtr.base, idtr.limit);
}
