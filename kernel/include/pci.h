/* =============================================================================
 * ZeruX OS — PCI (Peripheral Component Interconnect) Bus Scanner Header
 * File: kernel/include/pci.h
 * =============================================================================
 */

#ifndef PCI_H
#define PCI_H

#include <stdint.h>

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC

typedef struct {
    uint8_t bus;
    uint8_t device;
    uint8_t function;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t class_id;
    uint8_t subclass_id;
    uint8_t prog_if;
    uint32_t bar0;
    uint32_t bar1;
    uint32_t bar2;
    uint32_t bar3;
    uint32_t bar4;
    uint32_t bar5;
    /* 5.1.6: BAR0 size in bytes (from probing) */
    uint32_t bar0_size;
    uint8_t interrupt_line;
    uint8_t interrupt_pin;
} pci_device_t;

void pci_init(void);
uint32_t pci_config_read32(uint8_t bus, uint8_t device, uint8_t func, uint8_t offset);
void pci_config_write32(uint8_t bus, uint8_t device, uint8_t func, uint8_t offset, uint32_t value);
pci_device_t* pci_find_device(uint16_t vendor_id, uint16_t device_id);
pci_device_t* pci_find_device_by_class(uint8_t class_id, uint8_t subclass_id, uint8_t prog_if);


void pci_dump_devices(void);

#endif /* PCI_H */
