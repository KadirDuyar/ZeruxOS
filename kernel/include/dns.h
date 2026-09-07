/* =============================================================================
 * ZeruX OS - DNS (Domain Name System) Client
 * File: kernel/include/dns.h
 * =============================================================================
 */

#ifndef DNS_H
#define DNS_H

#include <stdint.h>
#include <stdbool.h>

void dns_init(void);

/* Alan adini IP adresine cozer. Ornek: "google.com" -> {142, 250, 190, 46} */
bool dns_resolve(const char *hostname, uint8_t out_ip[4]);

#endif /* DNS_H */
