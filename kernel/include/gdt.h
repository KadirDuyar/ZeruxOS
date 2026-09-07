/* =============================================================================
 * ZeruX OS — Dynamic Global Descriptor Table (GDT) Header
 * File: kernel/include/gdt.h
 * =============================================================================
 *
 * x86 Protected Mode için dynamic GDT kapılarını (Kernel/User Code & Data, TSS)
 * yönetir ve Ring 3 (User Mode) ile Çok Görevlilik (Multitasking) altyapısını kurar.
 * =============================================================================
 */

#ifndef GDT_H
#define GDT_H

#include <stdint.h>

/* GDT Segment Selector Offset'leri */
#define GDT_KERNEL_CODE_SEL 0x08  /* Index 1: Ring 0 Code (CS) */
#define GDT_KERNEL_DATA_SEL 0x10  /* Index 2: Ring 0 Data (DS, ES, SS) */
#define GDT_USER_CODE_SEL   0x18  /* Index 3: Ring 3 Code (RPL=3 -> 0x1B) */
#define GDT_USER_DATA_SEL   0x20  /* Index 4: Ring 3 Data (RPL=3 -> 0x23) */
#define GDT_TSS_SEL         0x28  /* Index 5: Task State Segment (TR) */

/* 8-Byte GDT Descriptor Yapısı */
typedef struct {
    uint16_t limit_low;     /* Segment Sınırı [0:15] */
    uint16_t base_low;      /* Taban Adres    [0:15] */
    uint8_t  base_middle;   /* Taban Adres    [16:23] */
    uint8_t  access;        /* Erişim Bayrakları (Present, DPL, Exec, RW, etc.) */
    uint8_t  granularity;   /* Granularity + Sınır [16:19] */
    uint8_t  base_high;     /* Taban Adres    [24:31] */
} __attribute__((packed)) gdt_entry_t;

/* GDTR — 6-Byte Register Structure loaded by `lgdt` */
typedef struct {
    uint16_t limit;         /* GDT Boyutu minus 1 */
    uint32_t base;          /* GDT Dizisi Fiziki Adresi */
} __attribute__((packed)) gdt_ptr_t;

/* Public API Bildirimleri */
void gdt_init(void);
void gdt_set_gate(int num, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran);
void dump_gdt(void);

#endif /* GDT_H */
