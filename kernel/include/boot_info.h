/* =============================================================================
 * ZeruX OS — Boot Information & BIOS E820 Memory Map Header
 * File: kernel/include/boot_info.h
 * =============================================================================
 *
 * Bootloader (Real Mode) tarafından INT 15h, AX=E820h çağrısıyla elde edilen
 * fiziksel bellek haritası (E820 Map) ve boot parametrelerinin C yapılarıdır.
 * =============================================================================
 */

#ifndef BOOT_INFO_H
#define BOOT_INFO_H

#include <stdint.h>

#define BOOT_INFO_MAGIC 0x5A455255  /* "ZERU" ASCII magic value */

/* E820 Memory Map Entry Türleri (Type Values) */
#define E820_TYPE_USABLE       1  /* Kullanılabilir Genel RAM (Free Usable RAM) */
#define E820_TYPE_RESERVED     2  /* Donanım / Sistem Tarafından Rezerve Edilmiş */
#define E820_TYPE_ACPI_RECLAIM 3  /* ACPI Reclaimable Memory */
#define E820_TYPE_ACPI_NVS     4  /* ACPI Non-Volatile Storage */
#define E820_TYPE_BAD          5  /* Bozuk / Arızalı Bellek Alanı */

/* BIOS E820 Memory Map Entry Yapısı (24 Byte, Packed) */
typedef struct {
    uint64_t base_addr;     /* Bölgenin 64-bit Fiziksel Başlangıç Adresi */
    uint64_t length;        /* Bölgenin 64-bit Bayt Cinsinden Boyutu */
    uint32_t type;          /* E820 Bölge Türü (1 = Usable, 2 = Reserved, ...) */
    uint32_t acpi_attr;     /* ACPI 3.0 Genişletilmiş Özellikler */
} __attribute__((packed)) e820_entry_t;

/* Bootloader Tarafından 0x7E00 Adresinde Bırakılan Boot Info Yapısı */
typedef struct {
    uint32_t magic;         /* 0x5A455255 ("ZERU") olmalıdır */
    uint32_t e820_count;    /* BIOS tarafından dönen E820 kayıt sayısı */
    uint32_t e820_addr;     /* E820 dizisinin fiziki bellek adresi (0x9000) */

    /* VBE / VESA Parameters (Adim 5) */
    uint32_t vbe_mode;      /* Mode number (0 if failed) */
    uint32_t vbe_phys_base; /* Physical address of Linear Framebuffer */
    uint32_t vbe_pitch;     /* Bytes per scanline */
    uint32_t vbe_width;     /* Width in pixels */
    uint32_t vbe_height;    /* Height in pixels */
    uint32_t vbe_bpp;       /* Bits per pixel */
} __attribute__((packed)) boot_info_t;

/* Public API Bildirimleri */
void boot_info_init(boot_info_t *info);
const boot_info_t* boot_info_get(void);
uint64_t boot_info_get_total_usable_ram(void);

#endif /* BOOT_INFO_H */
