/* =============================================================================
 * ZeruX OS — CPU Primitives & Control Register Interface
 * File: kernel/include/cpu.h
 * =============================================================================
 *
 * Bu dosya x86 CPU'nun doğrudan kontrol mekanizmalarını sarar:
 *   - Execution control (hlt, pause, cli, sti)
 *   - Control register erişimi (CR0, CR2, CR3, CR4)
 *   - MSR (Model Specific Register) erişimi
 *
 * Intel SDM Referans:
 *   Vol.1 Section 6.3 — Halting the Processor (HLT instruction)
 *   Vol.2 HLT / CLI / STI / PAUSE opcodes
 *   Vol.3 Section 2.5 — Control Registers (CR0-CR4)
 * =============================================================================
 */

#ifndef CPU_H
#define CPU_H

#include <stdint.h>

/* =============================================================================
 * CR0 — Control Register 0 Bit Tanımları
 * =============================================================================
 */
#define CR0_PE   (1U << 0)   /* Protection Enable: 1 = Protected Mode aktif    */
#define CR0_MP   (1U << 1)   /* Monitor Coprocessor: FPU için                  */
#define CR0_EM   (1U << 2)   /* Emulation: FPU yoksa exception üret            */
#define CR0_TS   (1U << 3)   /* Task Switched: FPU context switch              */
#define CR0_ET   (1U << 4)   /* Extension Type: 387 uyumluluğu (read-only)     */
#define CR0_NE   (1U << 5)   /* Numeric Error: FPU hata raporlama modu         */
#define CR0_WP   (1U << 16)  /* Write Protect: Ring 0'da read-only sayfaları koru */
#define CR0_AM   (1U << 18)  /* Alignment Mask: EFLAGS.AC ile hizalama kontrol */
#define CR0_NW   (1U << 29)  /* Not Write-through: önbellek write politikası    */
#define CR0_CD   (1U << 30)  /* Cache Disable: L1/L2 önbelleği kapat           */
#define CR0_PG   (1U << 31)  /* Paging: 1 = sanal bellek aktif                 */

/* =============================================================================
 * cpu_read_cr0() — CR0 Control Register'ı oku
 * =============================================================================
 */
static inline uint32_t cpu_read_cr0(void) {
    uint32_t cr0;
    __asm__ volatile (
        "mov %%cr0, %0"
        : "=r"(cr0)
    );
    return cr0;
}

/* =============================================================================
 * cpu_read_esp() — Mevcut Stack Pointer'ı oku
 * =============================================================================
 */
static inline uint32_t cpu_read_esp(void) {
    uint32_t esp;
    __asm__ volatile (
        "mov %%esp, %0"
        : "=r"(esp)
    );
    return esp;
}

/* =============================================================================
 * cpu_halt() — CPU'yu sonsuz döngüde durdur (HLT loop)
 * =============================================================================
 */
static inline void cpu_halt(void) {
    for (;;) {
        __asm__ volatile ("hlt");
    }
}

/* =============================================================================
 * cpu_hlt() — Tek HLT instruction (interrupt bekleme)
 * =============================================================================
 */
static inline void cpu_hlt(void) {
    __asm__ volatile ("hlt");
}

/* =============================================================================
 * cpu_cli() — Interrupt'ları devre dışı bırak (EFLAGS.IF = 0)
 * =============================================================================
 */
static inline void cpu_cli(void) {
    __asm__ volatile ("cli" ::: "memory");
}

/* =============================================================================
 * cpu_sti() — Interrupt'ları etkinleştir (EFLAGS.IF = 1)
 * =============================================================================
 */
static inline void cpu_sti(void) {
    __asm__ volatile ("sti" ::: "memory");
}

/* =============================================================================
 * cpu_relax() — Spin-wait döngüsünde PAUSE çalıştır
 * =============================================================================
 */
static inline void cpu_relax(void) {
    __asm__ volatile ("pause");
}

#endif /* CPU_H */
