/* =============================================================================
 * ZeruX OS — RTL8139 PCI Network Interface Controller (NIC) Driver Header
 * File: kernel/include/rtl8139.h
 * =============================================================================
 */

#ifndef RTL8139_H
#define RTL8139_H

#include <stdint.h>
#include <stdbool.h>

void rtl8139_init(void);
bool rtl8139_send_packet(const void *data, uint32_t len);
void rtl8139_get_mac(uint8_t *mac_out);

#endif /* RTL8139_H */
