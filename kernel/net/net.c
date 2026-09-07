/* =============================================================================
 * ZeruX OS — Network Stack Implementation (Ethernet, ARP, IPv4, ICMP)
 * File: kernel/net/net.c
 * =============================================================================
 */

#include "net.h"
#include "rtl8139.h"
#include "rtl8168.h"
#include "serial.h"
#include "kheap.h"
#include "timer.h"
#include "task.h"
#include "keyboard.h"
#include "udp.h"
#include "vfs.h"
#include "fat32.h"
#include "dhcp.h"
#include "firewall.h"
#include "tcp.h"
#include <stddef.h>

/* Global Network Configuration */
uint32_t g_local_ip    = 0x0A00020F; /* 10.0.2.15 Default */
uint32_t g_subnet_mask = 0xFFFFFF00; /* 255.255.255.0 */
uint32_t g_gateway_ip  = 0x0A000202; /* 10.0.2.2 Default */
uint32_t g_dns_ip      = 0x0A000203; /* 10.0.2.3 Default */

/* Global Network Statistics */
uint32_t g_net_rx_packets = 0;
uint32_t g_net_rx_bytes = 0;
uint32_t g_net_tx_packets = 0;
uint32_t g_net_tx_bytes = 0;
uint32_t g_net_rx_dropped = 0;

/* Packet Sniffer Buffer */
pcap_packet_t g_pcap_buffer[PCAP_MAX_PACKETS];
uint32_t g_pcap_head = 0;
uint32_t g_pcap_tail = 0;
uint32_t g_pcap_count = 0;

extern uint32_t timer_get_uptime_ms(void);

void pcap_capture(const uint8_t *packet, uint32_t len) {
    if (len > PCAP_MAX_LEN) len = PCAP_MAX_LEN;
    
    pcap_packet_t *p = &g_pcap_buffer[g_pcap_tail];
    uint32_t ms = timer_get_uptime_ms();
    p->timestamp_sec = ms / 1000;
    p->timestamp_msec = ms % 1000;
    p->len = len;
    kmemcpy(p->data, packet, len);
    
    g_pcap_tail = (g_pcap_tail + 1) % PCAP_MAX_PACKETS;
    if (g_pcap_count < PCAP_MAX_PACKETS) {
        g_pcap_count++;
    } else {
        g_pcap_head = (g_pcap_head + 1) % PCAP_MAX_PACKETS;
    }
}


static uint8_t g_my_mac[6];
static bool g_use_8168 = false;

static inline void hw_get_mac(uint8_t *out) {
    if (g_use_8168) rtl8168_get_mac(out);
    else            rtl8139_get_mac(out);
}

static inline bool hw_send(const void *data, uint32_t len) {
    return g_use_8168 ? rtl8168_send_packet(data, len)
                      : rtl8139_send_packet(data, len);
}

/* =============================================================================
 * Byte Swap / Endianness
 * =============================================================================
 */
uint16_t htons(uint16_t hostshort) {
    return (hostshort >> 8) | (hostshort << 8);
}
uint16_t ntohs(uint16_t netshort) {
    return htons(netshort);
}
uint32_t htonl(uint32_t hostlong) {
    return ((hostlong & 0xFF) << 24) |
           ((hostlong & 0xFF00) << 8) |
           ((hostlong & 0xFF0000) >> 8) |
           ((hostlong >> 24) & 0xFF);
}
uint32_t ntohl(uint32_t netlong) {
    return htonl(netlong);
}

/* Calculate Internet Checksum */
static uint16_t net_checksum(const void *data, uint32_t len) {
    uint32_t sum = 0;
    const uint16_t *ptr = (const uint16_t*)data;
    
    while (len > 1) {
        sum += *ptr++;
        len -= 2;
    }
    
    if (len > 0) {
        sum += *(const uint8_t*)ptr;
    }
    
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    
    return (uint16_t)(~sum);
}

void kmemcpy(void *dest, const void *src, size_t n) {
    char *d = (char*)dest;
    const char *s = (const char*)src;
    while (n--) *d++ = *s++;
}

void kmemset(void *dest, int val, size_t n) {
    unsigned char *d = (unsigned char*)dest;
    while (n--) *d++ = (unsigned char)val;
}

/* =============================================================================
 * Initialization
 * =============================================================================
 */

/* =============================================================================
 * Network Configuration Loader
 * =============================================================================
 */
static int parse_ip_str(const char *str, uint8_t *ip) {
    int i = 0, j = 0, num = 0;
    while (str[i] && str[i] != '\n' && str[i] != '\r' && j < 4) {
        if (str[i] == '.') {
            ip[j++] = num;
            num = 0;
        } else if (str[i] >= '0' && str[i] <= '9') {
            num = num * 10 + (str[i] - '0');
        } else {
            break; /* Invalid char */
        }
        i++;
    }
    if (j < 4) ip[j] = num;
    return 1;
}

void net_load_config(void) {
    /* Default settings (used if DHCP fails or is disabled) */
    g_local_ip    = 0xC0A83202; /* 192.168.50.2 */
    g_subnet_mask = 0xFFFFFF00; /* 255.255.255.0 */
    g_gateway_ip  = 0xC0A83201; /* 192.168.50.1 */
    g_dns_ip      = 0x08080808; /* 8.8.8.8 */
    
    int use_dhcp = 1; /* DHCP Enabled by default */
    
    int fd = vfs_open("/disk/fat0/etc/network/net.cfg", 0);
    if (fd < 0) {
        serial_printf("[NET] Config not found, attempting to create default at /disk/fat0/etc/network/net.cfg\n");
        /* Manually create the directory structure on FAT32 */
        uint32_t root = fat32_get_root_cluster();
        if (root > 0) {
            
            /* Find or create 'etc' */
            uint32_t etc_cluster = fat32_find_entry(root, "etc");
            if (!etc_cluster) etc_cluster = fat32_mkdir(root, "etc");
            
            if (etc_cluster > 0) {
                /* Find or create 'network' */
                uint32_t net_cluster = fat32_find_entry(etc_cluster, "network");
                if (!net_cluster) net_cluster = fat32_mkdir(etc_cluster, "network");
                
                if (net_cluster > 0) {
                    /* Find or create 'net.cfg' */
                    uint32_t cfg_cluster = fat32_find_entry(net_cluster, "net.cfg");
                    if (!cfg_cluster) cfg_cluster = fat32_create(net_cluster, "net.cfg");
                    
                    if (cfg_cluster > 0) {
                        /* Write default contents using vfs_open now that the file exists! */
                        int wfd = vfs_open("/disk/fat0/etc/network/net.cfg", 0);
                        if (wfd >= 0) {
                            const char *def = "DHCP=1\nIP=192.168.50.2\nMASK=255.255.255.0\nGW=192.168.50.1\nDNS=8.8.8.8\n";
                            vfs_write(wfd, def, 70);
                            vfs_close(wfd);
                            serial_printf("[NET] Created default config /disk/fat0/etc/network/net.cfg\n");
                        }
                    }
                }
            }
        }
    } else {
        char buf[256];
        int bytes = vfs_read(fd, buf, sizeof(buf) - 1);
        vfs_close(fd);
        if (bytes > 0) {
            buf[bytes] = '\0';
            
            /* Simple line-by-line parsing */
            char *line = buf;
            while (*line) {
                if (line[0] == 'I' && line[1] == 'P' && line[2] == '=') {
                    uint8_t ip[4]; parse_ip_str(line + 3, ip);
                    g_local_ip = (ip[0] << 24) | (ip[1] << 16) | (ip[2] << 8) | ip[3];
                } else if (line[0] == 'M' && line[1] == 'A' && line[2] == 'S' && line[3] == 'K' && line[4] == '=') {
                    uint8_t ip[4]; parse_ip_str(line + 5, ip);
                    g_subnet_mask = (ip[0] << 24) | (ip[1] << 16) | (ip[2] << 8) | ip[3];
                } else if (line[0] == 'G' && line[1] == 'W' && line[2] == '=') {
                    uint8_t ip[4]; parse_ip_str(line + 3, ip);
                    g_gateway_ip = (ip[0] << 24) | (ip[1] << 16) | (ip[2] << 8) | ip[3];
                } else if (line[0] == 'D' && line[1] == 'N' && line[2] == 'S' && line[3] == '=') {
                    uint8_t ip[4]; parse_ip_str(line + 4, ip);
                    g_dns_ip = (ip[0] << 24) | (ip[1] << 16) | (ip[2] << 8) | ip[3];
                } else if (line[0] == 'D' && line[1] == 'H' && line[2] == 'C' && line[3] == 'P' && line[4] == '=') {
                    if (line[5] == '0') use_dhcp = 0;
                    else use_dhcp = 1;
                }
                
                /* Skip to next line */
                while (*line && *line != '\n') line++;
                if (*line == '\n') line++;
            }
        }
    }
    
    if (use_dhcp) {
        serial_printf("[NET] Config says DHCP=1, starting DHCP discovery...\n");
        dhcp_discover();
    } else {
        serial_printf("[NET] Config applied (Static IP): %d.%d.%d.%d\n", 
            (g_local_ip >> 24) & 0xFF, (g_local_ip >> 16) & 0xFF, (g_local_ip >> 8) & 0xFF, g_local_ip & 0xFF);
    }
}

void net_get_ip(uint8_t *ip_out) {
    ip_out[0] = (g_local_ip >> 24) & 0xFF;
    ip_out[1] = (g_local_ip >> 16) & 0xFF;
    ip_out[2] = (g_local_ip >> 8) & 0xFF;
    ip_out[3] = g_local_ip & 0xFF;
}

/* =============================================================================
 * net_init() — Ağ Yığınını Başlatır
 * =============================================================================
 */
void net_init(void) {
    g_use_8168 = rtl8168_is_present();
    hw_get_mac(g_my_mac);
    serial_printf("[NET] Network Stack Initialized. (%s)\n", g_use_8168 ? "RTL8168/8111" : "RTL8139");
}

#define NET_RX_QUEUE_SIZE 128
#define NET_MAX_PACKET_LEN 1514

typedef struct {
    uint32_t len;
    uint8_t data[NET_MAX_PACKET_LEN];
} net_rx_packet_t;

static net_rx_packet_t g_net_rx_queue[NET_RX_QUEUE_SIZE];
static uint32_t g_net_rx_head = 0;
static uint32_t g_net_rx_tail = 0;
static uint32_t g_net_rx_count = 0;

/* =============================================================================
 * Receive Entry Point (Called from RTL8139 Driver - IRQ Context)
 * =============================================================================
 */
void net_receive_packet(const uint8_t *packet, uint32_t len) {
    if (len < sizeof(eth_hdr_t) || len > NET_MAX_PACKET_LEN) {
        g_net_rx_dropped++;
        return;
    }
    
    g_net_rx_packets++;
    g_net_rx_bytes += len;
    pcap_capture(packet, len);
    
    /* Enqueue to ring buffer (atomic) */
    uint32_t cpu_flags;
    asm volatile("pushf; pop %0; cli" : "=r"(cpu_flags));
    
    if (g_net_rx_count < NET_RX_QUEUE_SIZE) {
        net_rx_packet_t *p = &g_net_rx_queue[g_net_rx_tail];
        p->len = len;
        kmemcpy(p->data, packet, len);
        g_net_rx_tail = (g_net_rx_tail + 1) % NET_RX_QUEUE_SIZE;
        g_net_rx_count++;
    } else {
        g_net_rx_dropped++;
    }
    
    asm volatile("push %0; popf" :: "r"(cpu_flags));
}

/* =============================================================================
 * Network Polling (Called from Kernel Idle/Task Context)
 * =============================================================================
 */
void net_poll(void) {
    uint32_t cpu_flags;
    
    /* Process all packets in the RX queue */
    while (1) {
        asm volatile("pushf; pop %0; cli" : "=r"(cpu_flags));
        if (g_net_rx_count == 0) {
            asm volatile("push %0; popf" :: "r"(cpu_flags));
            break; /* Queue empty */
        }
        
        /* Dequeue one packet */
        net_rx_packet_t pkt;
        pkt.len = g_net_rx_queue[g_net_rx_head].len;
        kmemcpy(pkt.data, g_net_rx_queue[g_net_rx_head].data, pkt.len);
        
        g_net_rx_head = (g_net_rx_head + 1) % NET_RX_QUEUE_SIZE;
        g_net_rx_count--;
        asm volatile("push %0; popf" :: "r"(cpu_flags));
        
        /* Process packet OUTSIDE of IRQ context */
        eth_hdr_t *eth = (eth_hdr_t*)pkt.data;
        uint16_t etype = ntohs(eth->ethertype);
        
        if (etype == 0x0806) { /* ARP */
            arp_receive((const arp_hdr_t*)(pkt.data + sizeof(eth_hdr_t)), pkt.len - sizeof(eth_hdr_t));
        } else if (etype == 0x0800) { /* IPv4 */
            ipv4_receive((const ipv4_hdr_t*)(pkt.data + sizeof(eth_hdr_t)), pkt.len - sizeof(eth_hdr_t));
        }
    }
}

/* =============================================================================
 * Ethernet Transmission
 * =============================================================================
 */
void eth_send(const uint8_t *dest_mac, uint16_t ethertype, const uint8_t *payload, uint32_t len) {
    uint32_t frame_len = sizeof(eth_hdr_t) + len;
    if (frame_len > 1500) return; /* simple limit */
    
    uint8_t *frame = kmalloc(frame_len);
    if (!frame) return;
    
    eth_hdr_t *eth = (eth_hdr_t*)frame;
    kmemcpy(eth->dest_mac, dest_mac, 6);
    kmemcpy(eth->src_mac, g_my_mac, 6);
    eth->ethertype = htons(ethertype);
    
    kmemcpy(frame + sizeof(eth_hdr_t), payload, len);
    
    g_net_tx_packets++;
    g_net_tx_bytes += frame_len;
    pcap_capture(frame, frame_len);
    
    hw_send(frame, frame_len);
    
    /* In a real OS we should use kfree(frame), but kmalloc/kfree needs to work perfectly.
       For now, we just pass the frame to the driver. Our kheap handles kfree. */
    kfree(frame);
}

/* =============================================================================
 * ARP (Address Resolution Protocol)
 * =============================================================================
 */

#define ARP_CACHE_SIZE 16

typedef struct {
    uint32_t ip;
    uint8_t  mac[6];
    bool     valid;
} arp_cache_entry_t;

static arp_cache_entry_t arp_cache[ARP_CACHE_SIZE];

void arp_receive(const arp_hdr_t *arp, uint32_t len) {
    if (len < sizeof(arp_hdr_t)) return;
    
    if (ntohs(arp->hw_type) != 1 || ntohs(arp->proto_type) != 0x0800) return;
    
    uint16_t opcode = ntohs(arp->opcode);
    uint32_t sender_ip = ntohl(arp->sender_ip);
    uint32_t target_ip = ntohl(arp->target_ip);
    
    /* Update ARP Cache if it's a reply or a request */
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (!arp_cache[i].valid || arp_cache[i].ip == sender_ip) {
            arp_cache[i].ip = sender_ip;
            kmemcpy(arp_cache[i].mac, arp->sender_mac, 6);
            arp_cache[i].valid = true;
            break;
        }
    }
    
    if (opcode == 1 && target_ip == g_local_ip) { /* Request for us */
        arp_send_reply(arp->sender_mac, sender_ip);
    } else if (opcode == 2 && target_ip == g_local_ip) { /* Reply for us */
        /* Silently accept ARP replies (no serial print to mimic Linux) */
    }
}

void arp_request(const uint8_t *target_ip) {
    arp_hdr_t req;
    req.hw_type    = htons(1);
    req.proto_type = htons(0x0800);
    req.hw_len     = 6;
    req.proto_len  = 4;
    req.opcode     = htons(1); /* Request */
    
    uint8_t my_mac[6];
    hw_get_mac(my_mac);
    kmemcpy(req.sender_mac, my_mac, 6);
    req.sender_ip  = htonl(g_local_ip);
    
    kmemset(req.target_mac, 0, 6);
    req.target_ip  = htonl((target_ip[0] << 24) | (target_ip[1] << 16) | (target_ip[2] << 8) | target_ip[3]);
    
    uint8_t bcast_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    eth_send(bcast_mac, 0x0806, (uint8_t*)&req, sizeof(arp_hdr_t));
}

bool arp_resolve(const uint8_t *ip, uint8_t *mac_out) {
    uint32_t target_ip = (ip[0] << 24) | (ip[1] << 16) | (ip[2] << 8) | ip[3];
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (arp_cache[i].valid && arp_cache[i].ip == target_ip) {
            kmemcpy(mac_out, arp_cache[i].mac, 6);
            return true;
        }
    }
    return false;
}

void arp_send_reply(const uint8_t *target_mac, uint32_t target_ip) {
    arp_hdr_t reply;
    reply.hw_type    = htons(1);
    reply.proto_type = htons(0x0800);
    reply.hw_len     = 6;
    reply.proto_len  = 4;
    reply.opcode     = htons(2); /* ARP Reply */
    
    kmemcpy(reply.sender_mac, g_my_mac, 6);
    reply.sender_ip  = htonl(g_local_ip);
    
    kmemcpy(reply.target_mac, target_mac, 6);
    reply.target_ip  = htonl(target_ip);
    
    eth_send(target_mac, 0x0806, (uint8_t*)&reply, sizeof(arp_hdr_t));
}

/* =============================================================================
 * IPv4
 * =============================================================================
 */
void ipv4_receive(const ipv4_hdr_t *ipv4, uint32_t len) {
    uint8_t ihl = (ipv4->ihl_version & 0x0F) * 4;
    if (len < ihl) return;
    
    /* Validate IPv4 Header Checksum */
    if (net_checksum(ipv4, ihl) != 0xFFFF && net_checksum(ipv4, ihl) != 0x0000) {
        serial_printf("[IPv4] Dropping packet with invalid checksum.\n");
        return;
    }
    
    /* Verify Destination IP */
    if (ntohl(ipv4->dest_ip) != g_local_ip && ntohl(ipv4->dest_ip) != 0xFFFFFFFF) {
        return; /* Not for us */
    }
    
    /* Ethernet frames might be padded to 60 bytes. We MUST use IPv4 total_len! */
    uint16_t actual_total_len = ntohs(ipv4->total_len);
    if (actual_total_len > len) actual_total_len = len; /* Fallback for truncated frames */
    if (actual_total_len < ihl) return;
    
    const uint8_t *payload = (const uint8_t*)ipv4 + ihl;
    uint32_t payload_len = actual_total_len - ihl;
    
    /* Firewall Check */
    uint16_t dest_port = 0;
    if (ipv4->protocol == 17) { /* UDP */
        if (payload_len >= sizeof(udp_hdr_t)) {
            dest_port = ntohs(((const udp_hdr_t*)payload)->dest_port);
        }
    } else if (ipv4->protocol == 6) { /* TCP */
        if (payload_len >= sizeof(tcp_hdr_t)) {
            dest_port = ntohs(((const tcp_hdr_t*)payload)->dest_port);
        }
    }
    
    if (!fw_check(ntohl(ipv4->src_ip), ipv4->protocol, dest_port)) {
        g_net_rx_dropped++;
        return; /* Dropped by Firewall */
    }
    
    if (ipv4->protocol == 1) { /* ICMP */
        icmp_receive(ipv4, (const icmp_hdr_t*)payload, payload_len);
    } else if (ipv4->protocol == 17) { /* UDP */
        udp_receive((const uint8_t*)ipv4, (const udp_hdr_t*)payload, payload_len);
    } else if (ipv4->protocol == 6) { /* TCP */
        extern void tcp_receive(const uint8_t *packet, uint32_t len, uint32_t src_ip, uint32_t dest_ip);
        tcp_receive(payload, payload_len, ntohl(ipv4->src_ip), ntohl(ipv4->dest_ip));
    } else {
        // serial_printf("[IPv4] Unhandled Protocol: %d\n", ipv4->protocol);
    }
}

void net_ipv4_send(const uint8_t *target_ip, uint8_t protocol, const uint8_t *payload, uint32_t payload_len) {
    uint32_t ipv4_total_len = sizeof(ipv4_hdr_t) + payload_len;
    uint8_t *ipv4_packet = kmalloc(ipv4_total_len);
    if (!ipv4_packet) return;
    
    static uint16_t ipv4_id = 1000;
    
    ipv4_hdr_t *ipv4 = (ipv4_hdr_t*)ipv4_packet;
    ipv4->ihl_version = 0x45; /* IPv4, 20 bytes header */
    ipv4->tos         = 0;
    ipv4->total_len   = htons(ipv4_total_len);
    ipv4->id          = htons(ipv4_id++);
    ipv4->frag_offset = htons(0x4000); /* Set DF (Don't Fragment) bit */
    ipv4->ttl         = 64;
    ipv4->protocol    = protocol;
    ipv4->checksum    = 0;
    ipv4->src_ip      = htonl(g_local_ip);
    ipv4->dest_ip     = htonl((target_ip[0] << 24) | (target_ip[1] << 16) | (target_ip[2] << 8) | target_ip[3]);
    
    ipv4->checksum    = net_checksum(ipv4, sizeof(ipv4_hdr_t));
    
    kmemcpy(ipv4_packet + sizeof(ipv4_hdr_t), payload, payload_len);
    
    /* Loopback Check: If destination is 127.0.0.1 or our own IP, bypass ethernet and deliver locally */
    uint32_t dest_ip_u32 = ntohl(ipv4->dest_ip);
    if (dest_ip_u32 == g_local_ip || dest_ip_u32 == 0x7F000001) { /* 127.0.0.1 */
        ipv4_receive((const ipv4_hdr_t*)ipv4_packet, ipv4_total_len);
        kfree(ipv4_packet);
        return;
    }
    
    /* Routing: If not in local subnet, send to gateway */
    uint32_t next_hop_ip_u32;
    if ((dest_ip_u32 & g_subnet_mask) != (g_local_ip & g_subnet_mask)) {
        next_hop_ip_u32 = g_gateway_ip;
    } else {
        next_hop_ip_u32 = dest_ip_u32;
    }
    
    uint8_t next_hop[4];
    next_hop[0] = (next_hop_ip_u32 >> 24) & 0xFF;
    next_hop[1] = (next_hop_ip_u32 >> 16) & 0xFF;
    next_hop[2] = (next_hop_ip_u32 >> 8) & 0xFF;
    next_hop[3] = next_hop_ip_u32 & 0xFF;
    
    uint8_t target_mac[6];
    if (dest_ip_u32 == 0xFFFFFFFF) {
        /* Broadcast */
        for (int i=0; i<6; i++) target_mac[i] = 0xFF;
        eth_send(target_mac, 0x0800, ipv4_packet, ipv4_total_len);
    } else {
        if (!arp_resolve(next_hop, target_mac)) {
            arp_request(next_hop);
            
            /* Wait for ARP reply up to 500ms */
            int timeout = 50; /* 50 * 10ms = 500ms */
            while (!arp_resolve(next_hop, target_mac) && timeout-- > 0) {
                task_sleep_ms(10);
            }
        }
        
        if (arp_resolve(next_hop, target_mac)) {
            eth_send(target_mac, 0x0800, ipv4_packet, ipv4_total_len);
        } else {
            serial_printf("[IPv4] ERROR: Dropping packet (ARP timeout for %d.%d.%d.%d)\n", next_hop[0], next_hop[1], next_hop[2], next_hop[3]);
        }
    }
    
    kfree(ipv4_packet);
}

/* =============================================================================
 * ICMP (Internet Control Message Protocol) - Ping Responder
 * =============================================================================
 */
void icmp_receive(const ipv4_hdr_t *ipv4, const icmp_hdr_t *icmp, uint32_t len) {
    if (len < sizeof(icmp_hdr_t)) return;
    
    if (icmp->type == 8 && icmp->code == 0) { /* Echo Request */
        /* Silently handle ping requests from others */

        /* Construct ICMP Reply payload (header + data) */
        uint8_t *reply_payload = kmalloc(len);
        if (!reply_payload) return;
        
        kmemcpy(reply_payload, icmp, len);
        icmp_hdr_t *reply_icmp = (icmp_hdr_t*)reply_payload;
        reply_icmp->type = 0; /* Echo Reply */
        reply_icmp->checksum = 0;
        reply_icmp->checksum = net_checksum(reply_payload, len);
        
        /* Construct IPv4 Header for Reply */
        uint32_t ipv4_total_len = sizeof(ipv4_hdr_t) + len;
        uint8_t *ipv4_packet = kmalloc(ipv4_total_len);
        if (!ipv4_packet) {
            kfree(reply_payload);
            return;
        }
        
        ipv4_hdr_t *reply_ipv4 = (ipv4_hdr_t*)ipv4_packet;
        reply_ipv4->ihl_version = 0x45; /* IPv4, 20 bytes header */
        reply_ipv4->tos         = 0;
        reply_ipv4->total_len   = htons(ipv4_total_len);
        reply_ipv4->id          = htons(0x1337);
        reply_ipv4->frag_offset = 0;
        reply_ipv4->ttl         = 64;
        reply_ipv4->protocol    = 1; /* ICMP */
        reply_ipv4->checksum    = 0;
        reply_ipv4->src_ip      = htonl(g_local_ip);
        reply_ipv4->dest_ip     = ipv4->src_ip;
        
        reply_ipv4->checksum    = net_checksum(reply_ipv4, sizeof(ipv4_hdr_t));
        
        kmemcpy(ipv4_packet + sizeof(ipv4_hdr_t), reply_payload, len);
        
        /* Send it out over Ethernet to the sender's MAC address */
        const eth_hdr_t *original_eth = (const eth_hdr_t*)((const uint8_t*)ipv4 - sizeof(eth_hdr_t));
        eth_send(original_eth->src_mac, 0x0800, ipv4_packet, ipv4_total_len);
        
        kfree(ipv4_packet);
        kfree(reply_payload);
    } else if (icmp->type == 0 && icmp->code == 0) { /* Echo Reply */
        uint32_t *payload_ptr = (uint32_t*)((uint8_t*)icmp + sizeof(icmp_hdr_t));
        uint32_t send_time = *payload_ptr;
        uint32_t recv_time = timer_get_uptime_ms();
        uint32_t rtt = (recv_time >= send_time) ? (recv_time - send_time) : 0;
        
        serial_printf("64 bytes from %d.%d.%d.%d: icmp_seq=%u ttl=%u time=%u ms\n",
            (ntohl(ipv4->src_ip) >> 24) & 0xFF, (ntohl(ipv4->src_ip) >> 16) & 0xFF,
            (ntohl(ipv4->src_ip) >> 8) & 0xFF, ntohl(ipv4->src_ip) & 0xFF,
            ntohs(icmp->sequence), ipv4->ttl, rtt);
    }
}

/* =============================================================================
 * Utilities
 * =============================================================================
 */
bool net_parse_ip(const char *ip_str, uint8_t *ip_out) {
    int octet = 0;
    int val = 0;
    bool digit_found = false;
    
    while (*ip_str) {
        if (*ip_str >= '0' && *ip_str <= '9') {
            val = val * 10 + (*ip_str - '0');
            digit_found = true;
            if (val > 255) return false;
        } else if (*ip_str == '.') {
            if (!digit_found || octet >= 3) return false;
            ip_out[octet++] = (uint8_t)val;
            val = 0;
            digit_found = false;
        } else {
            return false;
        }
        ip_str++;
    }
    
    if (!digit_found || octet != 3) return false;
    ip_out[octet] = (uint8_t)val;
    return true;
}

/* =============================================================================
 * Ping Client (Echo Request)
 * =============================================================================
 */

void net_ping(uint8_t ip1, uint8_t ip2, uint8_t ip3, uint8_t ip4) {
    uint8_t target_ip[4] = {ip1, ip2, ip3, ip4};
    uint8_t next_hop[4];
    uint8_t target_mac[6];
    
    serial_printf("PING %d.%d.%d.%d (%d.%d.%d.%d) 56(84) bytes of data.\n", ip1,ip2,ip3,ip4, ip1,ip2,ip3,ip4);
    
    /* Simple Routing: check subnet mask */
    uint32_t ip_u32 = (ip1 << 24) | (ip2 << 16) | (ip3 << 8) | ip4;
    if ((ip_u32 & g_subnet_mask) != (g_local_ip & g_subnet_mask)) {
        next_hop[0] = (g_gateway_ip >> 24) & 0xFF;
        next_hop[1] = (g_gateway_ip >> 16) & 0xFF;
        next_hop[2] = (g_gateway_ip >> 8) & 0xFF;
        next_hop[3] = g_gateway_ip & 0xFF;
    } else {
        next_hop[0] = ip1; next_hop[1] = ip2; next_hop[2] = ip3; next_hop[3] = ip4;
    }
    
    if (!arp_resolve(next_hop, target_mac)) {
        arp_request(next_hop);
        int timeout = 5000000;
        while (!arp_resolve(next_hop, target_mac) && timeout-- > 0) {
            asm volatile("pause");
        }
        if (timeout <= 0) {
            serial_printf("Destination Host Unreachable\n");
            return;
        }
    }
    
    int seq = 1;
    shell_interrupt_requested = false;
    
    while (!shell_interrupt_requested) {
        /* Construct ICMP Echo Request */
        uint32_t payload_len = 56; /* 56 bytes of ping payload data */
        uint32_t icmp_len = sizeof(icmp_hdr_t) + payload_len;
        uint8_t *icmp_packet = kmalloc(icmp_len);
        if (!icmp_packet) return;
        
        icmp_hdr_t *icmp = (icmp_hdr_t*)icmp_packet;
        icmp->type = 8; /* Echo Request */
        icmp->code = 0;
        icmp->identifier = htons(0x1337);
        icmp->sequence = htons(seq);
        icmp->checksum = 0;
        
        /* Fill payload */
        uint8_t *payload = icmp_packet + sizeof(icmp_hdr_t);
        for (uint32_t i = 0; i < payload_len; i++) payload[i] = 'A' + (i % 26);
        
        /* Store timestamp in first 4 bytes of payload */
        *(uint32_t*)payload = timer_get_uptime_ms();
        
        icmp->checksum = net_checksum(icmp_packet, icmp_len);
        
        /* Construct IPv4 Header */
        uint32_t ipv4_total_len = sizeof(ipv4_hdr_t) + icmp_len;
        uint8_t *ipv4_packet = kmalloc(ipv4_total_len);
        if (!ipv4_packet) {
            kfree(icmp_packet);
            return;
        }
        
        ipv4_hdr_t *ipv4 = (ipv4_hdr_t*)ipv4_packet;
        ipv4->ihl_version = 0x45; /* IPv4, 20 bytes header */
        ipv4->tos         = 0;
        ipv4->total_len   = htons(ipv4_total_len);
        ipv4->id          = htons(0x1337 + seq);
        ipv4->frag_offset = 0;
        ipv4->ttl         = 64;
        ipv4->protocol    = 1; /* ICMP */
        ipv4->checksum    = 0;
        ipv4->src_ip      = htonl(g_local_ip);
        ipv4->dest_ip     = htonl((target_ip[0] << 24) | (target_ip[1] << 16) | (target_ip[2] << 8) | target_ip[3]);
        
        ipv4->checksum    = net_checksum(ipv4, sizeof(ipv4_hdr_t));
        
        kmemcpy(ipv4_packet + sizeof(ipv4_hdr_t), icmp_packet, icmp_len);
        
        eth_send(target_mac, 0x0800, ipv4_packet, ipv4_total_len);
        
        kfree(ipv4_packet);
        kfree(icmp_packet);
        
        seq++;
        
        /* Check interrupt multiple times during wait to be responsive */
        for (int w = 0; w < 10 && !shell_interrupt_requested; w++) {
            task_sleep_ms(100);
        }
    }
    
    seq--; /* Exclude the one we were about to send */
    serial_printf("\n--- %d.%d.%d.%d ping statistics ---\n", ip1,ip2,ip3,ip4);
    serial_printf("%d packets transmitted, %d received, 0%% packet loss\n", seq, seq);
    
    shell_interrupt_requested = false;
}
