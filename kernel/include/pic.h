/* =============================================================================
 * ZeruX OS — 8259A Programmable Interrupt Controller (PIC) Driver Header
 * File: kernel/include/pic.h
 * =============================================================================
 *
 * 8259A PIC, x86 sistemlerinde harici donanımlardan (klavye, timer, disk vb.)
 * gelen kesme sinyallerini (IRQ) CPU'ya ileten çiptir.
 *
 * MİMARİ İZOLASYON:
 * Bu dosya SADECE 8259A donanımı ile ilgilenir. İleride APIC (Advanced PIC)
 * mimarisine geçildiğinde bu modül kaldırılarak `apic.c` ile değiştirilecektir.
 * =============================================================================
 */

#ifndef PIC_H
#define PIC_H

#include <stdint.h>

/* =============================================================================
 * 8259A Port Sabitleri (Magic Number Kullanmama İlkesi)
 * =============================================================================
 */
#define PIC1_COMMAND   0x20   /* Master PIC Command / Status Port */
#define PIC1_DATA      0x21   /* Master PIC Data / Mask Port */

#define PIC2_COMMAND   0xA0   /* Slave PIC Command / Status Port */
#define PIC2_DATA      0xA1   /* Slave PIC Data / Mask Port */

#define PIC_EOI        0x20   /* End-of-Interrupt (EOI) komutu */

/* IDT Vektör Offset'leri (Protected Mode için güvenli 32–47 aralığı) */
#define PIC1_OFFSET    0x20   /* Master PIC IRQ 0..7  → Vektör 32..39 (0x20..0x27) */
#define PIC2_OFFSET    0x28   /* Slave  PIC IRQ 8..15 → Vektör 40..47 (0x28..0x2F) */

/* ICW (Initialization Command Word) Sabitleri */
#define ICW1_ICW4      0x01   /* ICW4 gerekli uyarısı */
#define ICW1_SINGLE    0x02   /* Single (1) vs Cascade (0) modu */
#define ICW1_INTERVAL4 0x04   /* Call interval 4 vs 8 */
#define ICW1_LEVEL     0x08   /* Level triggered vs Edge triggered */
#define ICW1_INIT      0x10   /* Initialization komutu — ICW1 başlatır */

#define ICW4_8086      0x01   /* 8086/88 modu (0x01: x86 mimarisi) */
#define ICW4_AUTO      0x02   /* Auto EOI modu */

/* =============================================================================
 * PIC Public API Bildirimleri
 * =============================================================================
 */

/* Master & Slave PIC'leri remapping eder ve tüm IRQ'ları varsayılan olarak maskeler */
void pic_init(void);

/* Belirtilen IRQ tamamlandığında PIC'lere EOI sinyali gönderir */
void pic_send_eoi(uint8_t irq);

/* Tek bir IRQ'nun maskesini kaldırarak aktif eder (0 = unmask) */
void pic_unmask_irq(uint8_t irq);

/* Tek bir IRQ'yu maskeleyerek devre dışı bırakır (1 = mask) */
void pic_mask_irq(uint8_t irq);

/* Mevcut Master (düşük 8 bit) ve Slave (yüksek 8 bit) maske durumunu döner */
uint16_t pic_get_mask(void);

#endif /* PIC_H */
