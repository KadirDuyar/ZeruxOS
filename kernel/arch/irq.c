/* =============================================================================
 * ZeruX OS — Hardware IRQ Dispatcher Implementasyonu
 * File: kernel/arch/irq.c
 * =============================================================================
 *
 * Bu dosya donanım kesmelerini (IRQ 0..15) yakalar, ilgili C sürücüsüne (Timer,
 * Keyboard) yönlendirir ve PIC'e EOI (End of Interrupt) sinyali gönderir.
 * =============================================================================
 */

#include "irq.h"
#include "pic.h"
#include "idt.h"
#include "serial.h"
#include <stddef.h>

/* 16 IRQ hattı için C callback handler dizisi */
static irq_handler_t irq_handlers[16] = { NULL };

/* Dış Assembly IRQ Stub Bildirimleri (irq_asm.asm'den gelir) */
extern void irq_stub_0(void);
extern void irq_stub_1(void);
extern void irq_stub_2(void);
extern void irq_stub_3(void);
extern void irq_stub_4(void);
extern void irq_stub_5(void);
extern void irq_stub_6(void);
extern void irq_stub_7(void);
extern void irq_stub_8(void);
extern void irq_stub_9(void);
extern void irq_stub_10(void);
extern void irq_stub_11(void);
extern void irq_stub_12(void);
extern void irq_stub_13(void);
extern void irq_stub_14(void);
extern void irq_stub_15(void);

typedef void (*irq_stub_t)(void);

static const irq_stub_t irq_stubs[16] = {
    irq_stub_0,  irq_stub_1,  irq_stub_2,  irq_stub_3,
    irq_stub_4,  irq_stub_5,  irq_stub_6,  irq_stub_7,
    irq_stub_8,  irq_stub_9,  irq_stub_10, irq_stub_11,
    irq_stub_12, irq_stub_13, irq_stub_14, irq_stub_15
};

/* =============================================================================
 * irq_init() — PIC Remap & IRQ 0..15 IDT Kapılarını Yükle
 * =============================================================================
 */
void irq_init(void) {
    /* 1. 8259A PIC çiplerini remapping et (Master -> 0x20, Slave -> 0x28) */
    pic_init();

    /* 2. IRQ 0..15 stub'larını IDT Kapılarına (32..47) kaydet */
    for (int i = 0; i < 16; i++) {
        idt_set_gate((uint8_t)(32 + i), (uint32_t)irq_stubs[i], 0x08, IDT_FLAGS_KERNEL_INT);
        irq_handlers[i] = NULL;
    }

    serial_printf("[IRQ] Installing IRQ0-IRQ15 stubs to IDT gates 32..47...\n");
    serial_printf("[IRQ] Registered 16 IRQ handlers successfully.\n");
}

/* =============================================================================
 * irq_install_handler() — Donanım Sürücüsü Callback Kaydı
 * =============================================================================
 * Sürücü (örn: Timer veya Keyboard) bu fonksiyon ile kendi handler'ını kaydeder.
 * Kayıt sonrasında ilgili IRQ'nun PIC üzerindeki maskesi otomatik kaldırılır (unmask).
 */
void irq_install_handler(uint8_t irq, irq_handler_t handler) {
    if (irq < 16) {
        irq_handlers[irq] = handler;
        pic_unmask_irq(irq);   /* Sürücü hazır -> IRQ hattını aç */
        serial_printf("[IRQ] Registered handler for IRQ%u and unmasked on PIC.\n", irq);
    }
}

/* =============================================================================
 * irq_uninstall_handler() — Sürücü Callback Kaydını Kaldır
 * =============================================================================
 */
void irq_uninstall_handler(uint8_t irq) {
    if (irq < 16) {
        irq_handlers[irq] = NULL;
        pic_mask_irq(irq);     /* Sürücü kaldırıldı -> IRQ hattını kapat */
        serial_printf("[IRQ] Unregistered handler for IRQ%u and masked on PIC.\n", irq);
    }
}

/* =============================================================================
 * irq_handler() — Ana C IRQ Dispatcher
 * =============================================================================
 * Assembly stub'tan çağrılır. İlgili IRQ callback'ini çalıştırır ve PIC'e EOI gönderir.
 */
void irq_handler(registers_t *regs) {
    if (!regs) return;

    uint8_t irq = (uint8_t)(regs->int_no - 32);

    /* 1. PIC'e End-of-Interrupt (EOI) sinyalini ÖNCE gönder.
     * Context switch yapıldığında `switch_to` stack değiştireceği için bu satıra tekrar dönmeyebilir.
     * EOI önceden gönderilmeli ki 8259A PIC kilitlenmesin!
     */
    pic_send_eoi(irq);

    /* 2. Kayıtlı sürücü handler'ını çalıştır */
    if (irq < 16 && irq_handlers[irq] != NULL) {
        irq_handlers[irq](regs);
    }
}
