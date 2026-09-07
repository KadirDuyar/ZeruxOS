/* =============================================================================
 * ZeruX OS - DNS (Domain Name System) Client
 * File: kernel/net/dns.c
 * =============================================================================
 */

#include "dns.h"
#include "udp.h"
#include "net.h"
#include "serial.h"
#include "kheap.h"
#include "task.h"

#define DNS_SERVER_PORT 53
#define DNS_CLIENT_PORT 5353

#pragma pack(push, 1)
typedef struct {
    uint16_t txid;
    uint16_t flags;
    uint16_t questions;
    uint16_t answer_rrs;
    uint16_t authority_rrs;
    uint16_t additional_rrs;
} dns_header_t;
#pragma pack(pop)

static volatile bool g_dns_resolved = false;
static uint8_t g_dns_result_ip[4];
static uint16_t g_dns_txid = 0x1234;

static void dns_receive(const uint8_t *data, uint32_t len, uint8_t src_ip[4], uint16_t src_port) {
    (void)src_ip;
    (void)src_port;
    if (len < sizeof(dns_header_t)) return;
    
    dns_header_t *dns = (dns_header_t*)data;
    if (ntohs(dns->txid) != g_dns_txid) return;
    
    if (ntohs(dns->answer_rrs) > 0) {
        /* Parse answer (skipping questions) */
        uint32_t offset = sizeof(dns_header_t);
        
        /* Skip questions */
        int qcount = ntohs(dns->questions);
        for (int q = 0; q < qcount; q++) {
            while (offset < len && data[offset] != 0) {
                offset += data[offset] + 1;
            }
            offset++; /* null byte */
            offset += 4; /* QTYPE and QCLASS */
        }
        
        /* Parse answers */
        int acount = ntohs(dns->answer_rrs);
        for (int a = 0; a < acount; a++) {
            if (offset >= len) break;
            
            /* Name */
            if ((data[offset] & 0xC0) == 0xC0) { /* Pointer */
                offset += 2;
            } else {
                while (offset < len && data[offset] != 0) offset += data[offset] + 1;
                offset++;
            }
            
            uint16_t type = (data[offset] << 8) | data[offset+1];
            offset += 2;
            
            /* Class */
            offset += 2;
            /* TTL */
            offset += 4;
            
            uint16_t data_len = (data[offset] << 8) | data[offset+1];
            offset += 2;
            
            if (type == 1 && data_len == 4) { /* A Record */
                g_dns_result_ip[0] = data[offset];
                g_dns_result_ip[1] = data[offset+1];
                g_dns_result_ip[2] = data[offset+2];
                g_dns_result_ip[3] = data[offset+3];
                g_dns_resolved = true;
                return;
            }
            offset += data_len;
        }
    }
}

void dns_init(void) {
    udp_register_listener(DNS_CLIENT_PORT, dns_receive);
    serial_printf("[DNS] Client Initialized on UDP Port %u.\n", DNS_CLIENT_PORT);
}

/* Helper to convert "google.com" to "\x06google\x03com\x00" */
static int encode_domain_name(uint8_t *buffer, const char *domain) {
    int i = 0, j = 0;
    int dot_pos = -1;
    
    while (domain[i]) {
        if (domain[i] == '.') {
            buffer[j] = i - (dot_pos + 1);
            int k;
            for (k = 0; k < buffer[j]; k++) {
                buffer[j + 1 + k] = domain[dot_pos + 1 + k];
            }
            j += buffer[j] + 1;
            dot_pos = i;
        }
        i++;
    }
    
    /* Last part */
    buffer[j] = i - (dot_pos + 1);
    int k;
    for (k = 0; k < buffer[j]; k++) {
        buffer[j + 1 + k] = domain[dot_pos + 1 + k];
    }
    j += buffer[j] + 1;
    
    buffer[j] = 0;
    return j + 1;
}

bool dns_resolve(const char *hostname, uint8_t out_ip[4]) {
    g_dns_txid++;
    g_dns_resolved = false;
    
    uint8_t packet[512];
    kmemset(packet, 0, 512);
    
    dns_header_t *dns = (dns_header_t*)packet;
    dns->txid = htons(g_dns_txid);
    dns->flags = htons(0x0100); /* Standard Query */
    dns->questions = htons(1);
    
    int name_len = encode_domain_name(packet + sizeof(dns_header_t), hostname);
    
    uint32_t offset = sizeof(dns_header_t) + name_len;
    packet[offset++] = 0x00; /* Type A */
    packet[offset++] = 0x01;
    packet[offset++] = 0x00; /* Class IN */
    packet[offset++] = 0x01;
    
    uint32_t req_len = offset;
    
    uint8_t target_ip[4];
    target_ip[0] = (g_dns_ip >> 24) & 0xFF;
    target_ip[1] = (g_dns_ip >> 16) & 0xFF;
    target_ip[2] = (g_dns_ip >> 8) & 0xFF;
    target_ip[3] = g_dns_ip & 0xFF;
    
    serial_printf("[DNS] Resolving '%s' via %d.%d.%d.%d\n", hostname, target_ip[0], target_ip[1], target_ip[2], target_ip[3]);
    
    udp_send(target_ip, DNS_CLIENT_PORT, DNS_SERVER_PORT, packet, req_len);
    
    int timeout = 50; /* 50 * 100ms = 5s */
    while (!g_dns_resolved && timeout-- > 0) {
        task_sleep_ms(100);
    }
    
    if (g_dns_resolved) {
        out_ip[0] = g_dns_result_ip[0];
        out_ip[1] = g_dns_result_ip[1];
        out_ip[2] = g_dns_result_ip[2];
        out_ip[3] = g_dns_result_ip[3];
        return true;
    }
    
    return false;
}
