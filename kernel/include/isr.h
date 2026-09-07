/* =============================================================================
 * ZeruX OS — Interrupt Service Routines (ISR) Header
 * File: kernel/include/isr.h
 * =============================================================================
 *
 * ISR'ler CPU'nun ürettiği istisnaları (Exceptions, Vektör 0–31) yakalayıp işleyen
 * alt seviye yazılım bileşenleridir.
 *
 * x86 CPU EXCEPTION LİSTESİ (Vektör 0–31):
 *   0: #DE  Divide-by-Zero Error
 *   1: #DB  Debug Exception
 *   2: NMI  Non-Maskable Interrupt
 *   3: #BP  Breakpoint Exception (int 3)
 *   4: #OF  Overflow Exception (into)
 *   5: #BR  BOUND Range Exceeded
 *   6: #UD  Invalid Opcode Exception (ud2)
 *   7: #NM  Device Not Available (No Math Coprocessor)
 *   8: #DF  Double Fault  (Error Code Pushed)
 *   9:      Coprocessor Segment Overrun
 *  10: #TS  Invalid TSS  (Error Code Pushed)
 *  11: #NP  Segment Not Present  (Error Code Pushed)
 *  12: #SS  Stack-Segment Fault  (Error Code Pushed)
 *  13: #GP  General Protection Fault  (Error Code Pushed)
 *  14: #PF  Page Fault  (Error Code Pushed + CR2 Linear Address)
 *  15:      Reserved
 *  16: #MF  x87 Floating-Point Exception
 *  17: #AC  Alignment Check Exception  (Error Code Pushed)
 *  18: #MC  Machine Check Exception
 *  19: #XM  SIMD Floating-Point Exception
 *  20: #VE  Virtualization Exception
 *  21–31:   Reserved by Intel
 * =============================================================================
 */

#ifndef ISR_H
#define ISR_H

#include <stdint.h>

/* =============================================================================
 * registers_t Struct — Stack Frame Layout
 * =============================================================================
 * Assembly stub (`isr_common_stub`) tarafından stack'e push edilen tüm
 * register'ların sırasına birebir uyan C veri yapısı.
 *
 * Stack Büyüme Yönü: Yüksek adresten düşük adrese doğru (aşağıya doğru).
 * C struct sıralaması: İlk eleman en düşük adresteki elemandır (en son push edilen).
 */
typedef struct {
    uint32_t ds;                                     /* Data segment selector (en son push edilen) */
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax; /* `pusha` ile saklanan Genel Amaçlı Register'lar */
    uint32_t int_no;                                 /* Interrupt vektör numarası (0..31) */
    uint32_t err_code;                               /* Donanım veya stub tarafından push edilen Error Code */
    uint32_t eip, cs, eflags;                        /* CPU tarafından otomatik push edilen değerler */
    uint32_t useresp, ss;                            /* Ring 3 -> Ring 0 geçişinde CPU tarafından push edilir */
} registers_t;

/* =============================================================================
 * Public ISR API Bildirimleri
 * =============================================================================
 */

/* 0–31 arası tüm CPU exception stub'larını IDT kapılarına kaydeder */
void isr_init(void);

/* Assembly stub'lar tarafından çağrılan ana C dispatcher */
void isr_handler(registers_t *regs);

/*
 * Birleşik Kernel Panic Handler:
 * Hata detaylarını seri porta yazar, VGA'da panik ekranı çizer ve CPU'yu durdurur.
 */
void kernel_panic(const char *reason, registers_t *regs);

#endif /* ISR_H */
