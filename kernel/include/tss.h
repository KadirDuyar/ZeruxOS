/* =============================================================================
 * ZeruX OS — Task State Segment (TSS) Header
 * File: kernel/include/tss.h
 * =============================================================================
 *
 * x86 Protected Mode TSS yapısı. User Mode'dan (Ring 3) Kernel Mode'a (Ring 0)
 * kesme veya syscall ile geçildiğinde CPU'nun hangi çekirdek stack'ini (esp0, ss0)
 * yükleyeceğini belirler.
 * =============================================================================
 */

#ifndef TSS_H
#define TSS_H

#include <stdint.h>

/* Intel Vol.3 Ch.7 — 32-bit Task State Segment (104 Bytes Packed) */
typedef struct {
    uint32_t prev_tss;   /* Link to previous TSS (unused in software multitasking) */
    uint32_t esp0;       /* Stack pointer to load when switching to Ring 0 (CRITICAL!) */
    uint32_t ss0;        /* Stack segment to load when switching to Ring 0 (0x10) */
    uint32_t esp1;
    uint32_t ss1;
    uint32_t esp2;
    uint32_t ss2;
    uint32_t cr3;
    uint32_t eip;
    uint32_t eflags;
    uint32_t eax;
    uint32_t ecx;
    uint32_t edx;
    uint32_t ebx;
    uint32_t esp;
    uint32_t ebp;
    uint32_t esi;
    uint32_t edi;
    uint32_t es;
    uint32_t cs;
    uint32_t ss;
    uint32_t ds;
    uint32_t fs;
    uint32_t gs;
    uint32_t ldt;
    uint16_t trap;
    uint16_t iomap_base; /* I/O map base offset (104 = no I/O bitmap) */
} __attribute__((packed)) tss_entry_t;

/* Public API Bildirimleri */
void     tss_init(uint16_t kernel_ss, uint32_t kernel_esp);
void     tss_set_kernel_stack(uint32_t esp0);
uint16_t tss_get_tr(void);
void     dump_tss(void);

#endif /* TSS_H */
