/* =============================================================================
 * ZeruX OS — Debug & Exception Test Suite Implementasyonu
 * File: kernel/debug/tests.c
 * =============================================================================
 *
 * Bu dosya CPU Exception Handler (#DE, #UD, #BP, #GP, #PF) ve `kernel_panic()`
 * sisteminin eksiksiz çalıştığını canlı ortamda doğrulamak için kullanılır.
 * =============================================================================
 */

#include "tests.h"
#include "serial.h"
#include "irq.h"
#include "cpu.h"
#include <stdint.h>

/* =============================================================================
 * trigger_divide_error() — Vektör 0 (#DE)
 * =============================================================================
 * Bilerek 0'a bölme işlemi yaptırır.
 * CPU anında Vektör 0 (#DE) exception'ı üretir.
 */
void trigger_divide_error(void) {
    serial_printf("[TEST] Triggering #DE Divide-by-Zero Exception...\n");
    volatile int a = 42;
    volatile int b = 0;
    volatile int c = a / b;
    (void)c;
}

/* =============================================================================
 * trigger_invalid_opcode() — Vektör 6 (#UD)
 * =============================================================================
 * `ud2` x86 instruction'ı Intel tarafından özel olarak tanımlanmamış / geçersiz
 * opcode üretmek için tasarlanmıştır (opcode: 0x0F 0x0B).
 */
void trigger_invalid_opcode(void) {
    serial_printf("[TEST] Triggering #UD Invalid Opcode Exception...\n");
    __asm__ volatile ("ud2");
}

/* =============================================================================
 * trigger_breakpoint() — Vektör 3 (#BP)
 * =============================================================================
 * `int $3` (opcode 0xCC) 1-byte breakpoint instruction'ıdır.
 * GDB ve debugger'lar tarafından yazılımsal kesme noktası için kullanılır.
 */
void trigger_breakpoint(void) {
    serial_printf("[TEST] Triggering #BP Breakpoint Exception...\n");
    __asm__ volatile ("int $3");
}

/* =============================================================================
 * trigger_general_protection_fault() — Vektör 13 (#GP)
 * =============================================================================
 * Geçersiz bir segment selector'ı (0x99) DS yazmacına yüklemeye çalışır.
 * GDT sınırları dışında kaldığı için CPU #GP (Vektör 13) üretir.
 */
void trigger_general_protection_fault(void) {
    serial_printf("[TEST] Triggering #GP General Protection Fault...\n");
    __asm__ volatile ("mov %0, %%ds" : : "r"(0x99));
}

/* =============================================================================
 * trigger_page_fault() — Vektör 14 (#PF)
 * =============================================================================
 * Sanal bellekte henüz eşlenmemiş / geçersiz adrese (0xDEADBEEF) yazmaya çalışır.
 * Paging kurulduğunda bu #PF üretecektir; paging kurulmadan önce ise varsayılan
 * olarak bellek alanına bağlı olarak #GP veya #PF üretebilir.
 */
void trigger_page_fault(void) {
    serial_printf("[TEST] Triggering #PF Page Fault Test...\n");
    volatile uint32_t *ptr = (volatile uint32_t*)0xDEADBEEF;
    *ptr = 0xCAFEBABE;
}

/* =============================================================================
 * IRQ0 SMOKE TEST CALLBACK & HARNESS
 * =============================================================================
 */
static volatile uint32_t smoke_test_ticks = 0;

static void smoke_test_irq0_callback(registers_t *regs) {
    (void)regs;
    smoke_test_ticks++;
    serial_printf("[SMOKE TEST] IRQ0 (PIT Timer) Tick #%u received successfully!\n", smoke_test_ticks);

    if (smoke_test_ticks == 5) {
        serial_printf("\n=======================================================\n");
        serial_printf(" [SMOKE TEST PASSED] IRQ Pipeline 100%% Verified!\n");
        serial_printf("   - 8259A PIC Remap (Master 0x20 -> IDT 32)\n");
        serial_printf("   - Assembly IRQ Stub (irq_stub_0 -> irq_common_stub)\n");
        serial_printf("   - C IRQ Dispatcher (irq_handler)\n");
        serial_printf("   - PIC End-of-Interrupt (EOI 0x20)\n");
        serial_printf("=======================================================\n\n");
    }
}

void test_irq0_smoke_test(void) {
    serial_printf("[SMOKE TEST] Registering temporary IRQ0 handler...\n");
    irq_install_handler(IRQ0_TIMER, smoke_test_irq0_callback);

    serial_printf("[SMOKE TEST] Enabling CPU hardware interrupts (sti)...\n");
    cpu_sti();  /* Set EFLAGS.IF = 1 */
}
