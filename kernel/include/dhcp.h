/* =============================================================================
 * ZeruX OS - DHCP (Dynamic Host Configuration Protocol) Header
 * File: kernel/include/dhcp.h
 * =============================================================================
 */

#ifndef DHCP_H
#define DHCP_H

#include <stdint.h>
#include <stdbool.h>

void dhcp_init(void);
void dhcp_discover(void);
bool dhcp_is_bound(void);

#endif /* DHCP_H */
