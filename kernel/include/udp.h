/* =============================================================================
 * ZeruX OS - UDP (User Datagram Protocol) Header & API
 * File: kernel/include/udp.h
 * =============================================================================
 */

#ifndef UDP_H
#define UDP_H

#include <stdint.h>
#include <stdbool.h>

#pragma pack(push, 1)
typedef struct {
    uint16_t src_port;
    uint16_t dest_port;
    uint16_t length;     /* Header + Data */
    uint16_t checksum;
} udp_hdr_t;
#pragma pack(pop)

/* UDP Dinleyici (Listener) Callback Tipi */
typedef void (*udp_listener_t)(const uint8_t *data, uint32_t len, uint8_t src_ip[4], uint16_t src_port);

/* API Fonksiyonlari */
void udp_init(void);

/* Belirli bir portu dinlemeye alir (Bind) */
bool udp_register_listener(uint16_t port, udp_listener_t callback);

/* Dinlemeyi birakir */
void udp_unregister_listener(uint16_t port);

/* UDP paketi gonderir */
void udp_send(uint8_t target_ip[4], uint16_t src_port, uint16_t dest_port, const uint8_t *payload, uint32_t payload_len);

/* Alt katman (IPv4) tarafindan cagirilan alici fonksiyon */
void udp_receive(const uint8_t *ipv4_packet, const udp_hdr_t *udp, uint32_t total_len);

#endif /* UDP_H */
