/* =============================================================================
 * ZeruX OS — Debug & Exception Test Suite
 * File: kernel/include/tests.h
 * =============================================================================
 *
 * Bu dosya Kernel Panic, Register Dump ve ISR Exception Handlers altyapısını
 * sınamak için kontrollü CPU istisnaları üreten test fonksiyonlarını barındırır.
 *
 * `kernel_main()` fonksiyonunu kirletmemek için ayrı bir debug modülü olarak
 * tasarlanmıştır (Linux kernel'daki `lib/test_*.c` gibi).
 * =============================================================================
 */

#ifndef TESTS_H
#define TESTS_H

/* Vektör 0: #DE — Divide-by-Zero Error */
void trigger_divide_error(void);

/* Vektör 6: #UD — Invalid Opcode Exception (ud2 instruction) */
void trigger_invalid_opcode(void);

/* Vektör 3: #BP — Breakpoint Exception (int3 instruction) */
void trigger_breakpoint(void);

/* Vektör 13: #GP — General Protection Fault (Geçersiz segment / privilege ihlali) */
void trigger_general_protection_fault(void);

/* Vektör 14: #PF — Page Fault (Null-pointer dereference / unmapped address write) */
void trigger_page_fault(void);

/* IRQ0 Donanım Kesmesi Smoke Testi (PIC + IDT + IRQ Dispatcher + EOI doğrulaması) */
void test_irq0_smoke_test(void);

#endif /* TESTS_H */
