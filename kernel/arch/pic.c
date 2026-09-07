/* =============================================================================
 * ZeruX OS — 8259A PIC Driver Implementasyonu
 * File: kernel/arch/pic.c
 * =============================================================================
 *
 * Bu dosya 8259A Master ve Slave PIC çiplerini donanımsal olarak konfigüre eder.
 * `outb_p` ve `inb_p` (I/O with delay) kullanılarak ISA bus üzerindeki yavaş
 * sinyallerin güvenle işlenmesi sağlanır.
 * =============================================================================
 */

#include "pic.h"
#include "ports.h"
#include "serial.h"

/* =============================================================================
 * pic_init() — Master ve Slave PIC'leri Remap Et ve Maskele
 * =============================================================================
 *
 * ICW (Initialization Command Word) Sırası:
 *   1. ICW1: Init komutu (0x11) -> Master (0x20) & Slave (0xA0)
 *   2. ICW2: Vector offset    -> Master = 0x20 (32), Slave = 0x28 (40)
 *   3. ICW3: Cascade yapısı   -> Master IRQ2 (0x04), Slave identity (0x02)
 *   4. ICW4: Mod ayarı        -> 8086 modu (0x01)
 *   5. Maskeleme              -> Tüm IRQ'lar (0xFF) kapatılır
 */
void pic_init(void) {
    serial_printf("[PIC] Remapping 8259A PIC controllers...\n");

    /* ICW1: Initialization komutunu başlat */
    outb_p(PIC1_COMMAND, ICW1_INIT | ICW1_ICW4);
    outb_p(PIC2_COMMAND, ICW1_INIT | ICW1_ICW4);

    /* ICW2: Master ve Slave IRQ vektör offset'lerini tanımla */
    outb_p(PIC1_DATA, PIC1_OFFSET);   /* Master: IRQ 0..7  → Vektör 32..39 */
    outb_p(PIC2_DATA, PIC2_OFFSET);   /* Slave : IRQ 8..15 → Vektör 40..47 */
    serial_printf("[PIC] Master vector offset -> 0x%x (IDT 32..39)\n", PIC1_OFFSET);
    serial_printf("[PIC] Slave  vector offset -> 0x%x (IDT 40..47)\n", PIC2_OFFSET);

    /* ICW3: Master-Slave bağlantı noktalarını tanımla */
    outb_p(PIC1_DATA, 0x04);          /* Master: IRQ2 hattında Slave var (bitmask 00000100b) */
    outb_p(PIC2_DATA, 0x02);          /* Slave : Master IRQ2'ye bağlı (cascade identity 2) */

    /* ICW4: 8086/88 Modunu Aktif Et */
    outb_p(PIC1_DATA, ICW4_8086);
    outb_p(PIC2_DATA, ICW4_8086);

    /* Varsayılan olarak tüm IRQ'ları maskele (sürücüler hazır olana kadar kapalı) */
    outb_p(PIC1_DATA, 0xFF);
    outb_p(PIC2_DATA, 0xFF);

    serial_printf("[PIC] 8259A Remap completed successfully. Initial IRQ mask: 0xFFFF\n");
}

/* =============================================================================
 * pic_send_eoi() — End of Interrupt (EOI) Sinyali Gönder
 * =============================================================================
 * Donanım bir IRQ ürettiğinde, PIC aynı veya daha düşük öncelikli yeni IRQ'ları
 * EOI alana kadar dondurur. Bu yüzden handler bitiminde EOI şarttır.
 */
void pic_send_eoi(uint8_t irq) {
    if (irq >= 8) {
        /* Slave PIC'ten (IRQ 8-15) gelen kesmeler için önce Slave'e EOI gönderilir */
        outb_p(PIC2_COMMAND, PIC_EOI);
    }
    /* Master PIC'e her durumda EOI gönderilir */
    outb_p(PIC1_COMMAND, PIC_EOI);
}

/* =============================================================================
 * pic_unmask_irq() — Belirtilen IRQ'yu Aktif Et
 * =============================================================================
 */
void pic_unmask_irq(uint8_t irq) {
    uint16_t port;
    uint8_t value;

    if (irq < 8) {
        port = PIC1_DATA;
    } else {
        port = PIC2_DATA;
        irq -= 8;
        /* Slave PIC'ten bir IRQ açılıyorsa, Master PIC'teki Cascade (IRQ2) de AÇILMALIDIR! */
        uint8_t master_value = inb(PIC1_DATA) & ~(1 << 2);
        outb_p(PIC1_DATA, master_value);
    }

    value = inb(port) & ~(1 << irq);  /* İlgili bit '0' yapıldığında IRQ unmask olur */
    outb_p(port, value);
}

/* =============================================================================
 * pic_mask_irq() — Belirtilen IRQ'yu Devre Dışı Bırak
 * =============================================================================
 */
void pic_mask_irq(uint8_t irq) {
    uint16_t port;
    uint8_t value;

    if (irq < 8) {
        port = PIC1_DATA;
    } else {
        port = PIC2_DATA;
        irq -= 8;
    }

    value = inb(port) | (1 << irq);   /* İlgili bit '1' yapıldığında IRQ maskelenir */
    outb_p(port, value);
}

/* =============================================================================
 * pic_get_mask() — Mevcut IRQ Maske Durumunu Oku
 * =============================================================================
 */
uint16_t pic_get_mask(void) {
    return (uint16_t)(inb(PIC1_DATA) | (inb(PIC2_DATA) << 8));
}
