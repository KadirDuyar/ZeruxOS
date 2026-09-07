/* =============================================================================
 * ZeruX OS — RTL8168/8111 (Gigabit) PCI Network Interface Controller Driver Header
 * File: kernel/include/rtl8168.h
 * =============================================================================
 * Covers the RTL8168/8111/8411/RTL811x "C+ mode" MAC family, including the
 * RTL8111DL (PCI Device ID 0x8168). This is a distinct chip generation from
 * the RTL8139 (10/100) — it is descriptor-ring + MMIO based, not the legacy
 * port-I/O ring buffer used by RTL8139.
 * =============================================================================
 */
#ifndef RTL8168_H
#define RTL8168_H

#include <stdint.h>
#include <stdbool.h>

/* Returns true if an RTL8168/8111-family NIC was found and initialized. */
bool rtl8168_init(void);

/* Queues a raw Ethernet frame for transmission. Returns false on failure/no-link-yet. */
bool rtl8168_send_packet(const void *data, uint32_t len);

/* Copies the 6-byte station MAC address into mac_out. */
void rtl8168_get_mac(uint8_t *mac_out);

/* True once rtl8168_init() has found and brought up the card. */
bool rtl8168_is_present(void);

#endif /* RTL8168_H */
