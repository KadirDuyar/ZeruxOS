/* =============================================================================
 * ZeruX OS — PCI (Peripheral Component Interconnect) Bus Scanner Driver
 * File: kernel/drivers/pci.c
 * =============================================================================
 */

#include "pci.h"
#include "ports.h"
#include "serial.h"

#define MAX_PCI_DEVICES 32

static pci_device_t g_pci_devices[MAX_PCI_DEVICES];
static uint32_t     g_pci_device_count = 0;

/* I/O Port Helpers using inline assembly */
static inline void pci_outl(uint16_t port, uint32_t value) {
    __asm__ volatile ("outl %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint32_t pci_inl(uint16_t port) {
    uint32_t value;
    __asm__ volatile ("inl %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

uint32_t pci_config_read32(uint8_t bus, uint8_t device, uint8_t func, uint8_t offset) {
    uint32_t lbus  = (uint32_t)bus;
    uint32_t ldev  = (uint32_t)device;
    uint32_t lfunc = (uint32_t)func;
    
    /* Create configuration address as per PCI spec */
    uint32_t address = (uint32_t)((lbus << 16) | (ldev << 11) | (lfunc << 8) | (offset & 0xFC) | ((uint32_t)0x80000000));
    
    pci_outl(PCI_CONFIG_ADDRESS, address);
    return pci_inl(PCI_CONFIG_DATA);
}

void pci_config_write32(uint8_t bus, uint8_t device, uint8_t func, uint8_t offset, uint32_t value) {
    uint32_t lbus  = (uint32_t)bus;
    uint32_t ldev  = (uint32_t)device;
    uint32_t lfunc = (uint32_t)func;
    
    uint32_t address = (uint32_t)((lbus << 16) | (ldev << 11) | (lfunc << 8) | (offset & 0xFC) | ((uint32_t)0x80000000));
    
    pci_outl(PCI_CONFIG_ADDRESS, address);
    pci_outl(PCI_CONFIG_DATA, value);
}

/* Base-16 Integer String Converter for dump */
static void itoa_hex(uint32_t num, char *str) {
    const char *hex_chars = "0123456789ABCDEF";
    int pos = 0;
    
    if (num == 0) {
        str[pos++] = '0';
        str[pos] = '\0';
        return;
    }
    
    char temp[16];
    int tpos = 0;
    while (num > 0) {
        temp[tpos++] = hex_chars[num % 16];
        num /= 16;
    }
    
    while (tpos > 0) {
        str[pos++] = temp[--tpos];
    }
    str[pos] = '\0';
}

void pci_init(void) {
    serial_printf("===========================================\n");
    serial_printf(" ZeruX OS — PCI Bus Scanner\n");
    serial_printf("===========================================\n");
    
    g_pci_device_count = 0;
    
    /* Scan buses (0..7 is sufficient for desktop PCs and QEMU) */
    for (uint16_t bus = 0; bus < 8; bus++) {
        /* Check if bus exists by probing device 0 function 0 */
        if (bus > 0 && pci_config_read32(bus, 0, 0, 0x00) == 0xFFFFFFFF) {
            /* If dev 0 func 0 is completely unresponsive, test if any device exists on this bus */
            bool bus_has_device = false;
            for (uint8_t d = 0; d < 32; d++) {
                if ((pci_config_read32(bus, d, 0, 0x00) & 0xFFFF) != 0xFFFF) {
                    bus_has_device = true;
                    break;
                }
            }
            if (!bus_has_device) continue;
        }

        for (uint8_t device = 0; device < 32; device++) {
            /* Check function 0 first */
            uint32_t dev0_id = pci_config_read32(bus, device, 0, 0x00);
            if ((dev0_id & 0xFFFF) == 0xFFFF) {
                continue; /* Device does not exist */
            }

            /* Read Header Type to see if multi-function */
            uint32_t hdr_reg = pci_config_read32(bus, device, 0, 0x0C);
            uint8_t header_type = (uint8_t)((hdr_reg >> 16) & 0xFF);
            uint8_t max_funcs = (header_type & 0x80) ? 8 : 1;

            for (uint8_t func = 0; func < max_funcs; func++) {
                uint32_t vendor_device = pci_config_read32(bus, device, func, 0x00);
                uint16_t vendor_id = (uint16_t)(vendor_device & 0xFFFF);
                uint16_t device_id = (uint16_t)((vendor_device >> 16) & 0xFFFF);
                
                if (vendor_id == 0xFFFF) {
                    continue;
                }
                
                uint32_t class_info = pci_config_read32(bus, device, func, 0x08);
                uint8_t class_id    = (uint8_t)((class_info >> 24) & 0xFF);
                uint8_t subclass_id = (uint8_t)((class_info >> 16) & 0xFF);
                uint8_t prog_if     = (uint8_t)((class_info >> 8)  & 0xFF);
                
                if (g_pci_device_count < MAX_PCI_DEVICES) {
                    pci_device_t *pdev = &g_pci_devices[g_pci_device_count++];
                    pdev->bus = bus;
                    pdev->device = device;
                    pdev->function = func;
                    pdev->vendor_id = vendor_id;
                    pdev->device_id = device_id;
                    pdev->class_id = class_id;
                    pdev->subclass_id = subclass_id;
                    pdev->prog_if = prog_if;
                    
                    /* Read BARs */
                    pdev->bar0 = pci_config_read32(bus, device, func, 0x10);
                    pdev->bar1 = pci_config_read32(bus, device, func, 0x14);
                    pdev->bar2 = pci_config_read32(bus, device, func, 0x18);
                    pdev->bar3 = pci_config_read32(bus, device, func, 0x1C);
                    pdev->bar4 = pci_config_read32(bus, device, func, 0x20);
                    pdev->bar5 = pci_config_read32(bus, device, func, 0x24);

                    /* 5.1.6: Probe BAR0 size safely ONLY for Memory BARs.
                     * For I/O BARs (bit 0 is 1), writing 0xFFFFFFFF can disable ports or crash devices! */
                    pdev->bar0_size = 0;
                    if (pdev->bar0 != 0 && !(pdev->bar0 & 1)) {
                        uint32_t orig = pdev->bar0;
                        pci_config_write32(bus, device, func, 0x10, 0xFFFFFFFF);
                        uint32_t sz = pci_config_read32(bus, device, func, 0x10);
                        pci_config_write32(bus, device, func, 0x10, orig); /* Restore */
                        sz &= ~0xF;
                        if (sz != 0) {
                            pdev->bar0_size = (~sz) + 1;
                        }
                    }
                    
                    /* Read Interrupt Line (IRQ) and Pin from offset 0x3C */
                    uint32_t intr_info = pci_config_read32(bus, device, func, 0x3C);
                    pdev->interrupt_line = (uint8_t)(intr_info & 0xFF);
                    pdev->interrupt_pin  = (uint8_t)((intr_info >> 8) & 0xFF);
                    
                    char vhex[8], dhex[8], ccl[4];
                    itoa_hex(vendor_id, vhex);
                    itoa_hex(device_id, dhex);
                    itoa_hex(class_id, ccl);
                    
                    serial_printf("[PCI] Found Device %u:%u:%u -> Vendor: 0x%s, Device: 0x%s, Class: 0x%s\n",
                                  bus, device, func, vhex, dhex, ccl);
                }
            }
        }
    }
    serial_printf("===========================================\n\n");
}

pci_device_t* pci_find_device(uint16_t vendor_id, uint16_t device_id) {
    for (uint32_t i = 0; i < g_pci_device_count; i++) {
        if (g_pci_devices[i].vendor_id == vendor_id && g_pci_devices[i].device_id == device_id) {
            return &g_pci_devices[i];
        }
    }
    return 0; /* NULL */
}

pci_device_t* pci_find_device_by_class(uint8_t class_id, uint8_t subclass_id, uint8_t prog_if) {
    for (uint32_t i = 0; i < g_pci_device_count; i++) {
        if (g_pci_devices[i].class_id    == class_id    &&
            g_pci_devices[i].subclass_id == subclass_id &&
            g_pci_devices[i].prog_if     == prog_if) {
            return &g_pci_devices[i];
        }
    }
    return 0; /* NULL */
}


void pci_dump_devices(void) {
    serial_printf("--- System PCI Devices ---\n");
    for (uint32_t i = 0; i < g_pci_device_count; i++) {
        pci_device_t *p = &g_pci_devices[i];
        char vhex[8], dhex[8];
        itoa_hex(p->vendor_id, vhex);
        itoa_hex(p->device_id, dhex);
        
        serial_printf(" Bus %u Dev %u Func %u | Vendor: 0x%s Device: 0x%s | Class: %u Subclass: %u\n",
                      p->bus, p->device, p->function, vhex, dhex, p->class_id, p->subclass_id);
    }
    serial_printf("--------------------------\n");
}
