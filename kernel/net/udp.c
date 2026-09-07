/* =============================================================================
 * ZeruX OS - UDP (User Datagram Protocol) Implementation
 * File: kernel/net/udp.c
 * =============================================================================
 */

#include "udp.h"
#include "net.h"
#include "serial.h"
#include "kheap.h"

#define MAX_UDP_LISTENERS 64

typedef struct {
    uint16_t port;
    udp_listener_t callback;
    bool active;
} udp_listener_entry_t;

static udp_listener_entry_t g_listeners[MAX_UDP_LISTENERS];

void udp_init(void) {
    for (int i = 0; i < MAX_UDP_LISTENERS; i++) {
        g_listeners[i].active = false;
    }
    serial_printf("[UDP] User Datagram Protocol Layer Initialized.\n");
}

bool udp_register_listener(uint16_t port, udp_listener_t callback) {
    if (!callback) return false;
    
    /* Zaten kayitli mi kontrol et */
    for (int i = 0; i < MAX_UDP_LISTENERS; i++) {
        if (g_listeners[i].active && g_listeners[i].port == port) {
            return false; /* Port is already in use */
        }
    }
    
    /* Bos yer bul */
    for (int i = 0; i < MAX_UDP_LISTENERS; i++) {
        if (!g_listeners[i].active) {
            g_listeners[i].port = port;
            g_listeners[i].callback = callback;
            g_listeners[i].active = true;
            serial_printf("[UDP] Listener registered on port %u\n", port);
            return true;
        }
    }
    return false;
}

void udp_unregister_listener(uint16_t port) {
    for (int i = 0; i < MAX_UDP_LISTENERS; i++) {
        if (g_listeners[i].active && g_listeners[i].port == port) {
            g_listeners[i].active = false;
            serial_printf("[UDP] Listener unregistered from port %u\n", port);
            return;
        }
    }
}

/* UDP Pseudo-Header (for Checksum Calculation) */
#pragma pack(push, 1)
typedef struct {
    uint32_t src_ip;
    uint32_t dest_ip;
    uint8_t zeros;
    uint8_t protocol;
    uint16_t udp_length;
} udp_pseudo_hdr_t;
#pragma pack(pop)

void udp_send(uint8_t target_ip[4], uint16_t src_port, uint16_t dest_port, const uint8_t *payload, uint32_t payload_len) {
    uint32_t udp_total_len = sizeof(udp_hdr_t) + payload_len;
    uint8_t *udp_packet = kmalloc(udp_total_len);
    if (!udp_packet) return;
    
    udp_hdr_t *udp = (udp_hdr_t*)udp_packet;
    udp->src_port = htons(src_port);
    udp->dest_port = htons(dest_port);
    udp->length = htons(udp_total_len);
    udp->checksum = 0; /* Optional in IPv4, but good practice to calculate */
    
    kmemcpy(udp_packet + sizeof(udp_hdr_t), payload, payload_len);
    
    /* Pseudo-header checksum hesaplama (opsiyonel ancak dhcp/dns sunuculari dogrulayabilir) */
    uint32_t src = htonl(g_local_ip);
    uint32_t dst = htonl((target_ip[0] << 24) | (target_ip[1] << 16) | (target_ip[2] << 8) | target_ip[3]);
    
    uint32_t sum = 0;
    sum += (src & 0xFFFF);
    sum += (src >> 16);
    sum += (dst & 0xFFFF);
    sum += (dst >> 16);
    sum += htons(17); /* Protocol 17 (UDP) */
    sum += htons(udp_total_len);
    
    uint16_t *udp_ptr = (uint16_t*)udp_packet;
    for (size_t i = 0; i < udp_total_len / 2; i++) {
        sum += udp_ptr[i];
    }
    
    if (udp_total_len & 1) {
        sum += (udp_packet[udp_total_len - 1] << 8); /* endianness */
    }
    
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    
    udp->checksum = ~(uint16_t)sum;
    if (udp->checksum == 0) udp->checksum = 0xFFFF;
    
    /* IPv4 katmanina pasla */
    net_ipv4_send(target_ip, 17, udp_packet, udp_total_len);
    
    kfree(udp_packet);
}

void udp_receive(const uint8_t *ipv4_packet, const udp_hdr_t *udp, uint32_t total_len) {
    if (total_len < sizeof(udp_hdr_t)) return;
    
    uint16_t dest_port = ntohs(udp->dest_port);
    uint16_t src_port = ntohs(udp->src_port);
    uint16_t udp_len = ntohs(udp->length);
    
    if (udp_len > total_len) udp_len = total_len;
    
    uint32_t payload_len = udp_len - sizeof(udp_hdr_t);
    const uint8_t *payload = (const uint8_t*)udp + sizeof(udp_hdr_t);
    
    const ipv4_hdr_t *ipv4 = (const ipv4_hdr_t*)ipv4_packet;
    uint8_t src_ip[4];
    uint32_t ip_addr = ntohl(ipv4->src_ip);
    src_ip[0] = (ip_addr >> 24) & 0xFF;
    src_ip[1] = (ip_addr >> 16) & 0xFF;
    src_ip[2] = (ip_addr >> 8) & 0xFF;
    src_ip[3] = ip_addr & 0xFF;
    
    /* Listeners'lara gonder */
    for (int i = 0; i < MAX_UDP_LISTENERS; i++) {
        if (g_listeners[i].active && g_listeners[i].port == dest_port) {
            g_listeners[i].callback(payload, payload_len, src_ip, src_port);
            return; /* Paketi isledik */
        }
    }
    
    /* Eger dinleyen yoksa sessizce drop edilir (ICMP Port Unreachable gönderilebilir ama simdilik atliyoruz) */
}
