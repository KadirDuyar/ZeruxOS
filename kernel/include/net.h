/* =============================================================================
 * ZeruX OS — Network Stack Header
 * File: kernel/include/net.h
 * =============================================================================
 */

#ifndef NET_H
#define NET_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* Memory Utility Functions */
void kmemcpy(void *dest, const void *src, size_t n);
void kmemset(void *dest, int val, size_t n);

/* MAC Address Utility */
#define MAC_BROADCAST "\xFF\xFF\xFF\xFF\xFF\xFF"
#define MAC_ZERO      "\x00\x00\x00\x00\x00\x00"

#pragma pack(push, 1)

/* Ethernet II Header (14 bytes) */
typedef struct {
    uint8_t  dest_mac[6];
    uint8_t  src_mac[6];
    uint16_t ethertype;
} eth_hdr_t;

/* ARP Header (28 bytes for IPv4/Ethernet) */
typedef struct {
    uint16_t hw_type;
    uint16_t proto_type;
    uint8_t  hw_len;
    uint8_t  proto_len;
    uint16_t opcode;
    uint8_t  sender_mac[6];
    uint32_t sender_ip;
    uint8_t  target_mac[6];
    uint32_t target_ip;
} arp_hdr_t;

/* IPv4 Header (20 bytes minimum) */
typedef struct {
    uint8_t  ihl_version;   /* Version (4 bits) + IHL (4 bits) */
    uint8_t  tos;
    uint16_t total_len;
    uint16_t id;
    uint16_t frag_offset;
    uint8_t  ttl;
    uint8_t  protocol;
    uint16_t checksum;
    uint32_t src_ip;
    uint32_t dest_ip;
} ipv4_hdr_t;

/* ICMP Header (8 bytes) */
typedef struct {
    uint8_t  type;
    uint8_t  code;
    uint16_t checksum;
    uint16_t identifier;
    uint16_t sequence;
} icmp_hdr_t;

#pragma pack(pop)

/* Network Endianness (Byte Swap) */
uint16_t htons(uint16_t hostshort);
uint16_t ntohs(uint16_t netshort);
uint32_t htonl(uint32_t hostlong);
uint32_t ntohl(uint32_t netlong);

/* Core Network Functions */
void net_init(void);
void net_load_config(void);
void net_receive_packet(const uint8_t *data, uint32_t len);

/* Global Network Statistics */
extern uint32_t g_net_rx_packets;
extern uint32_t g_net_rx_bytes;
extern uint32_t g_net_tx_packets;
extern uint32_t g_net_tx_bytes;
extern uint32_t g_net_rx_dropped;

/* Packet Sniffer (tcpdump) Support */
#define PCAP_MAX_PACKETS 128
#define PCAP_MAX_LEN     1514
typedef struct {
    uint32_t timestamp_sec;
    uint32_t timestamp_msec;
    uint32_t len;
    uint8_t  data[PCAP_MAX_LEN];
} pcap_packet_t;

extern pcap_packet_t g_pcap_buffer[PCAP_MAX_PACKETS];
extern uint32_t g_pcap_head;
extern uint32_t g_pcap_tail;
extern uint32_t g_pcap_count;

void pcap_capture(const uint8_t *packet, uint32_t len);

/* Global Network Configuration */
extern uint32_t g_local_ip;
extern uint32_t g_subnet_mask;
extern uint32_t g_gateway_ip;
extern uint32_t g_dns_ip;

/* Protocol Handlers */
void eth_send(const uint8_t *dest_mac, uint16_t ethertype, const uint8_t *payload, uint32_t payload_len);
void arp_receive(const arp_hdr_t *arp, uint32_t len);
void arp_send_reply(const uint8_t *target_mac, uint32_t target_ip);
void arp_request(const uint8_t *target_ip);
bool arp_resolve(const uint8_t *ip, uint8_t *mac_out);
void ipv4_receive(const ipv4_hdr_t *ipv4, uint32_t len);
void net_ipv4_send(const uint8_t *target_ip, uint8_t protocol, const uint8_t *payload, uint32_t payload_len);
void icmp_receive(const ipv4_hdr_t *ipv4, const icmp_hdr_t *icmp, uint32_t len);

/* Converts IP string to 4-byte array (e.g. "10.0.2.2" -> {10, 0, 2, 2}) */
bool net_parse_ip(const char *ip_str, uint8_t *ip_out);

/* Returns current local IP */
void net_get_ip(uint8_t *ip_out);

/* Helper for ICMP Ping demonstration */
void net_ping(uint8_t ip1, uint8_t ip2, uint8_t ip3, uint8_t ip4);

#endif /* NET_H */
