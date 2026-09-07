/* =============================================================================
 * ZeruX OS — Hardware Interrupt Request (IRQ) Dispatcher Header
 * File: kernel/include/irq.h
 * =============================================================================
 *
 * Bu dosya donanım kesmelerini (IRQ 0..15) yöneten ve sürücülere (Timer, Keyboard)
 * dinamik callback kaydı imkanı tanıyan IRQ altyapısının public arayüzüdür.
 * =============================================================================
 */

#ifndef IRQ_H
#define IRQ_H

#include <stdint.h>
#include "isr.h"   /* registers_t struct tanımı */

/* =============================================================================
 * IRQ Numaralandırma Sabitleri
 * =============================================================================
 */
#define IRQ0_TIMER          0   /* Programmable Interval Timer (PIT) */
#define IRQ1_KEYBOARD       1   /* PS/2 Keyboard Controller */
#define IRQ2_CASCADE        2   /* Cascade signal for Slave PIC */
#define IRQ3_COM2           3   /* Serial Port COM2 / COM4 */
#define IRQ4_COM1           4   /* Serial Port COM1 / COM3 */
#define IRQ5_LPT2           5   /* Parallel Port 2 / Sound Card */
#define IRQ6_FLOPPY         6   /* Floppy Disk Controller */
#define IRQ7_LPT1           7   /* Parallel Port 1 / Spurious Interrupt */
#define IRQ8_RTC            8   /* Real Time Clock (CMOS) */
#define IRQ9_ACPI           9   /* ACPI / PCI Interrupt */
#define IRQ10_PCI          10   /* PCI Network Card / Sound */
#define IRQ11_PCI          11   /* PCI Graphics / USB */
#define IRQ12_MOUSE        12   /* PS/2 Mouse Controller */
#define IRQ13_FPU          13   /* FPU / Coprocessor Error */
#define IRQ14_PRIMARY_ATA  14   /* Primary ATA / IDE Hard Disk */
#define IRQ15_SECONDARY_ATA 15  /* Secondary ATA / IDE Hard Disk */

/* =============================================================================
 * IRQ Handler Callback Typedef'i
 * =============================================================================
 */
typedef void (*irq_handler_t)(registers_t *regs);

/* =============================================================================
 * IRQ Public API Bildirimleri
 * =============================================================================
 */

/* 16 IRQ stub'ını IDT kapılarına (32..47) kaydeder ve PIC'i başlatır */
void irq_init(void);

/* Belirtilen IRQ (0..15) için C sürücü callback fonksiyonunu kaydeder */
void irq_install_handler(uint8_t irq, irq_handler_t handler);

/* Belirtilen IRQ callback kaydını kaldırır */
void irq_uninstall_handler(uint8_t irq);

/* Assembly stub'lar tarafından çağrılan ana C IRQ dispatcher */
void irq_handler(registers_t *regs);

#endif /* IRQ_H */
