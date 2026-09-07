/* =============================================================================
 * ZeruX OS — BIOS E820 Memory Map Parser Implementasyonu
 * File: kernel/arch/boot_info.c
 * =============================================================================
 *
 * Bootloader tarafından Real Mode'da (INT 15h, AX=E820h) toplanan ve 0x7E00 /
 * 0x8000 adreslerinde bırakılan fiziksel bellek haritasını okur, doğrular ve
 * seri porta dökümünü çıkarır (Adım 2.0 doğrulaması).
 * =============================================================================
 */

#include "boot_info.h"
#include "serial.h"
#include <stddef.h>

static const boot_info_t *g_boot_info = NULL;
static uint64_t g_total_usable_ram     = 0;

static const char *e820_type_to_string(uint32_t type) {
    switch (type) {
        case E820_TYPE_USABLE:       return "Usable RAM";
        case E820_TYPE_RESERVED:     return "Reserved";
        case E820_TYPE_ACPI_RECLAIM: return "ACPI Reclaimable";
        case E820_TYPE_ACPI_NVS:     return "ACPI NVS";
        case E820_TYPE_BAD:          return "Bad Memory";
        default:                     return "Unknown / Reserved";
    }
}

/* =============================================================================
 * boot_info_init() — E820 Bellek Haritasını Çözümle ve Logla
 * =============================================================================
 */
void boot_info_init(boot_info_t *info) {
    g_total_usable_ram = 0;

    serial_printf("===========================================\n");
    serial_printf(" ZeruX OS — BIOS E820 Physical Memory Map\n");
    serial_printf("===========================================\n");

    if (!info || info->magic != BOOT_INFO_MAGIC) {
        serial_printf("[E820] WARNING: Invalid boot_info magic (expected 0x%x, got 0x%x).\n",
                      BOOT_INFO_MAGIC, info ? info->magic : 0);
        serial_printf("[E820] Memory map detection failed or unsupported by BIOS!\n");
        return;
    }

    g_boot_info = info;
    e820_entry_t *map = (e820_entry_t*)info->e820_addr;

    serial_printf("[E820] Total Entries: %u (Map Address: %p)\n\n", info->e820_count, (void*)info->e820_addr);

    for (uint32_t i = 0; i < info->e820_count; i++) {
        uint32_t base_low  = (uint32_t)(map[i].base_addr & 0xFFFFFFFF);
        uint32_t base_high = (uint32_t)(map[i].base_addr >> 32);
        uint32_t len_low   = (uint32_t)(map[i].length & 0xFFFFFFFF);
        uint32_t size_kb   = len_low / 1024;

        if (map[i].type == E820_TYPE_USABLE) {
            g_total_usable_ram += map[i].length;
        }

        if (base_high == 0) {
            serial_printf("[E820] Entry #%u: Base=0x%x  Length=0x%x (%u KB)  Type=%u (%s)\n",
                          i, base_low, len_low, size_kb, map[i].type, e820_type_to_string(map[i].type));
        } else {
            serial_printf("[E820] Entry #%u: Base=0x%x%x  Length=0x%x  Type=%u (%s)\n",
                          i, base_high, base_low, len_low, map[i].type, e820_type_to_string(map[i].type));
        }
    }

    uint32_t total_mb = (uint32_t)(g_total_usable_ram / (1024 * 1024));
    uint32_t total_kb = (uint32_t)(g_total_usable_ram / 1024);

    serial_printf("-------------------------------------------\n");
    serial_printf("[E820] Total Usable System RAM: %u MB (%u KB)\n", total_mb, total_kb);
    serial_printf("===========================================\n\n");
}

/* =============================================================================
 * boot_info_get() — Boot Info Struct Pointer'ını Döner
 * =============================================================================
 */
const boot_info_t* boot_info_get(void) {
    return g_boot_info;
}

/* =============================================================================
 * boot_info_get_total_usable_ram() — Toplam Kullanılabilir RAM (Byte)
 * =============================================================================
 */
uint64_t boot_info_get_total_usable_ram(void) {
    return g_total_usable_ram;
}
