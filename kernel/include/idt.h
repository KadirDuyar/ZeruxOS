/* =============================================================================
 * ZeruX OS — Interrupt Descriptor Table (IDT) Header
 * File: kernel/include/idt.h
 * =============================================================================
 *
 * IDT (Interrupt Descriptor Table), CPU'nun bir interrupt veya exception
 * aldığında hangi adresteki kodu çalıştıracağını belirleyen 256 elemanlı
 * bir kapı (gate) tablosudur.
 *
 * Intel SDM Referansı: Vol. 3A, Chapter 6 — Interrupt and Exception Handling
 *
 * IDT GATE DESCRIPTOR YAPISI (8 Byte):
 *
 *  Bit  31                        16 15  14 13 12 11     8 7          0
 *      ┌────────────────────────────┬───┬─────┬───┬───────┬────────────┐
 *  +4  │ Base Address (Bits 31..16) │ P │ DPL │ 0 │ Type  │ Reserved 0 │
 *      ├────────────────────────────┴───┴─────┴───┴───────┴────────────┤
 *  +0  │ Segment Selector (0x08)    │ Base Address (Bits 15..0)        │
 *      └────────────────────────────┴──────────────────────────────────┘
 *
 * Attribute Flags (Byte 5):
 *   Bit 7   : P (Present) — 1 = Gate geçerli ve bellekte
 *   Bit 6-5 : DPL (Descriptor Privilege Level) — 00 = Ring 0 (Kernel), 11 = Ring 3 (User)
 *   Bit 4   : Storage Segment — 0 (Interrupt/Trap kapıları için her zaman 0)
 *   Bit 3-0 : Gate Type — 0xE (32-bit Interrupt Gate), 0xF (32-bit Trap Gate)
 *
 * INTERRUPT GATE vs TRAP GATE:
 *   - Interrupt Gate (0x0E): Çağrıldığında CPU EFLAGS.IF bitini OTOMATİK SIFIRLAR.
 *     Böylece handler çalışırken başka interrupt gelmez (reentrant koruması).
 *   - Trap Gate (0x0F): EFLAGS.IF bitini DEĞİŞTİRMEZ. Interrupt'lar açık kalır.
 *     Exception'lar genellikle Trap Gate kullanır ama biz güvenli default olarak
 *     Interrupt Gate kullanacağız.
 * =============================================================================
 */

#ifndef IDT_H
#define IDT_H

#include <stdint.h>

/* =============================================================================
 * IDT Gate Attribute Sabitleri (Magic Number Kullanmama İlkesi)
 * =============================================================================
 */
#define IDT_ATTR_PRESENT  0x80   /* Bit 7: Present (1 = aktif gate) */
#define IDT_ATTR_RING0    0x00   /* Bit 6-5: Ring 0 (Kernel yetkisi) */
#define IDT_ATTR_RING3    0x60   /* Bit 6-5: Ring 3 (User-mode yetkisi) */

#define IDT_GATE_INT32    0x0E   /* 32-bit Interrupt Gate (IF otomatik sıfırlanır) */
#define IDT_GATE_TRAP32   0x0F   /* 32-bit Trap Gate (IF değiştirilmez) */

/* Standart Kernel Interrupt Gate Bayrağı: 0x8E (Present | Ring0 | Int32) */
#define IDT_FLAGS_KERNEL_INT (IDT_ATTR_PRESENT | IDT_ATTR_RING0 | IDT_GATE_INT32)

/* User-Mode Accessible Interrupt Gate Bayrağı: 0xEE (Present | Ring3 | Int32) */
#define IDT_FLAGS_USER_INT   (IDT_ATTR_PRESENT | IDT_ATTR_RING3 | IDT_GATE_INT32)

/* Toplam IDT Kapı Sayısı (x86 standardı: 256 vektör) */
#define IDT_NUM_ENTRIES   256

/* =============================================================================
 * IDT Gate Descriptor Struct (8 Byte)
 * =============================================================================
 * __attribute__((packed)) derleyicinin struct elemanları arasına padding
 * eklemesini engeller. CPU bu 8-byte yapıyı tam olarak bu sırada bekler.
 */
typedef struct {
    uint16_t low_offset;   /* Base handler adresinin Düşük 16 biti (0..15) */
    uint16_t selector;     /* Kernel Code Segment Selector (0x08 = GDT Kernel Code) */
    uint8_t  always0;      /* Her zaman 0 olmalıdır (Reserved) */
    uint8_t  flags;        /* Gate erişim bayrakları (Present, Ring, Type) */
    uint16_t high_offset;  /* Base handler adresinin Yüksek 16 biti (16..31) */
} __attribute__((packed)) idt_gate_t;

/* =============================================================================
 * IDTR (IDT Register) Pointer Struct (6 Byte)
 * =============================================================================
 * `lidt` instruction'ı bu 6 byte'lık yapının adresini alır:
 *   - 2 byte limit: Tablo boyutu - 1 (256 * 8 - 1 = 2047 = 0x07FF)
 *   - 4 byte base : IDT tablosunun fiziksel başlangıç adresi
 */
typedef struct {
    uint16_t limit;   /* IDT tablosunun bayt cinsinden boyutu eksi 1 */
    uint32_t base;    /* idt[0] dizisinin 32-bit bellek adresi */
} __attribute__((packed)) idtr_t;

/* =============================================================================
 * IDT Public API Bildirimleri
 * =============================================================================
 */

/* 256 kapılı IDT tablosunu sıfırlar ve `lidt` ile CPU'ya yükler */
void idt_init(void);

/* Belirtilen vektör numarasına handler adresini ve bayraklarını atar */
void idt_set_gate(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags);

#endif /* IDT_H */
