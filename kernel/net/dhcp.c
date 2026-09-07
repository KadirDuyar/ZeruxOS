/* =============================================================================
 * ZeruX OS - DHCP (Dynamic Host Configuration Protocol) Client
 * File: kernel/net/dhcp.c
 * =============================================================================
 */

#include "dhcp.h"
#include "udp.h"
#include "net.h"
#include "serial.h"
#include "kheap.h"
#include "rtl8139.h"

#define DHCP_SERVER_PORT 67
#define DHCP_CLIENT_PORT 68

#define DHCP_OP_REQUEST 1
#define DHCP_OP_REPLY   2

#define DHCP_MAGIC_COOKIE 0x63825363

#pragma pack(push, 1)
typedef struct {
    uint8_t  op;
    uint8_t  htype;
    uint8_t  hlen;
    uint8_t  hops;
    uint32_t xid;
    uint16_t secs;
    uint16_t flags;
    uint32_t ciaddr;
    uint32_t yiaddr;
    uint32_t siaddr;
    uint32_t giaddr;
    uint8_t  chaddr[16];
    uint8_t  sname[64];
    uint8_t  file[128];
    uint32_t magic_cookie;
    uint8_t  options[0];
} dhcp_packet_t;
#pragma pack(pop)

static bool g_dhcp_bound = false;
static uint32_t g_dhcp_xid = 0x1337BEEF;

static void dhcp_receive(const uint8_t *data, uint32_t len, uint8_t src_ip[4], uint16_t src_port) {
    (void)src_ip;
    (void)src_port;
    if (len < sizeof(dhcp_packet_t)) return;
    
    dhcp_packet_t *dhcp = (dhcp_packet_t*)data;
    if (dhcp->op != DHCP_OP_REPLY) return;
    if (ntohl(dhcp->magic_cookie) != DHCP_MAGIC_COOKIE) return;
    if (dhcp->xid != htonl(g_dhcp_xid)) return;
    
    /* Parse options */
    uint8_t msg_type = 0;
    uint32_t subnet_mask = 0;
    uint32_t router_ip = 0;
    uint32_t dns_ip = 0;
    
    uint32_t opt_len = len - sizeof(dhcp_packet_t);
    uint8_t *opt = (uint8_t*)dhcp->options;
    
    uint32_t i = 0;
    while (i < opt_len && opt[i] != 255) { /* 255 = End */
        if (opt[i] == 0) { /* Pad */
            i++;
            continue;
        }
        uint8_t type = opt[i];
        uint8_t length = opt[i+1];
        
        if (type == 53 && length == 1) { /* Message Type */
            msg_type = opt[i+2];
        } else if (type == 1 && length == 4) { /* Subnet Mask */
            subnet_mask = ntohl(*(uint32_t*)&opt[i+2]);
        } else if (type == 3 && length >= 4) { /* Router */
            router_ip = ntohl(*(uint32_t*)&opt[i+2]);
        } else if (type == 6 && length >= 4) { /* DNS */
            dns_ip = ntohl(*(uint32_t*)&opt[i+2]);
        }
        i += 2 + length;
    }
    
    if (msg_type == 2) { /* DHCPOFFER */
        serial_printf("[DHCP] Received OFFER: IP %d.%d.%d.%d\n", 
            (ntohl(dhcp->yiaddr) >> 24) & 0xFF, (ntohl(dhcp->yiaddr) >> 16) & 0xFF,
            (ntohl(dhcp->yiaddr) >> 8) & 0xFF, ntohl(dhcp->yiaddr) & 0xFF);
            
        /* Send DHCP Request */
        uint8_t target_ip[4] = {255, 255, 255, 255};
        uint32_t req_len = sizeof(dhcp_packet_t) + 3 + 6 + 1;
        uint8_t *req = kmalloc(req_len);
        kmemset(req, 0, req_len);
        
        dhcp_packet_t *req_pkt = (dhcp_packet_t*)req;
        req_pkt->op = DHCP_OP_REQUEST;
        req_pkt->htype = 1;
        req_pkt->hlen = 6;
        req_pkt->xid = htonl(g_dhcp_xid);
        req_pkt->flags = htons(0x8000); /* Broadcast */
        req_pkt->magic_cookie = htonl(DHCP_MAGIC_COOKIE);
        
        rtl8139_get_mac(req_pkt->chaddr);
        
        req_pkt->options[0] = 53; /* Message Type */
        req_pkt->options[1] = 1;
        req_pkt->options[2] = 3; /* DHCPREQUEST */
        
        req_pkt->options[3] = 50; /* Requested IP */
        req_pkt->options[4] = 4;
        *(uint32_t*)&req_pkt->options[5] = dhcp->yiaddr;
        
        req_pkt->options[9] = 255; /* End */
        
        udp_send(target_ip, DHCP_CLIENT_PORT, DHCP_SERVER_PORT, req, req_len);
        kfree(req);
        
    } else if (msg_type == 5) { /* DHCPACK */
        g_local_ip = ntohl(dhcp->yiaddr);
        if (subnet_mask) g_subnet_mask = subnet_mask;
        if (router_ip) g_gateway_ip = router_ip;
        if (dns_ip) g_dns_ip = dns_ip;
        g_dhcp_bound = true;
        
        serial_printf("[DHCP] ACK Received! Network Configured:\n");
        serial_printf("       IP: %d.%d.%d.%d\n", (g_local_ip >> 24) & 0xFF, (g_local_ip >> 16) & 0xFF, (g_local_ip >> 8) & 0xFF, g_local_ip & 0xFF);
        serial_printf("       Gateway: %d.%d.%d.%d\n", (g_gateway_ip >> 24) & 0xFF, (g_gateway_ip >> 16) & 0xFF, (g_gateway_ip >> 8) & 0xFF, g_gateway_ip & 0xFF);
        serial_printf("       DNS: %d.%d.%d.%d\n", (g_dns_ip >> 24) & 0xFF, (g_dns_ip >> 16) & 0xFF, (g_dns_ip >> 8) & 0xFF, g_dns_ip & 0xFF);
    }
}

void dhcp_init(void) {
    udp_register_listener(DHCP_CLIENT_PORT, dhcp_receive);
    serial_printf("[DHCP] Client Initialized on UDP Port 68.\n");
}

void dhcp_discover(void) {
    g_dhcp_bound = false;
    g_dhcp_xid++;
    
    uint8_t target_ip[4] = {255, 255, 255, 255}; /* Broadcast */
    
    /* Calculate required length with options */
    uint32_t req_len = sizeof(dhcp_packet_t) + 3 + 1;
    uint8_t *req = kmalloc(req_len);
    kmemset(req, 0, req_len);
    
    dhcp_packet_t *dhcp = (dhcp_packet_t*)req;
    dhcp->op = DHCP_OP_REQUEST; /* Boot Request */
    dhcp->htype = 1; /* Ethernet */
    dhcp->hlen = 6;  /* MAC length */
    dhcp->xid = htonl(g_dhcp_xid);
    dhcp->flags = htons(0x8000); /* Broadcast flag */
    dhcp->magic_cookie = htonl(DHCP_MAGIC_COOKIE);
    
    rtl8139_get_mac(dhcp->chaddr);
    
    /* Options */
    dhcp->options[0] = 53; /* Message Type Option */
    dhcp->options[1] = 1;  /* Length 1 */
    dhcp->options[2] = 1;  /* DHCPDISCOVER */
    dhcp->options[3] = 255; /* End Option */
    
    serial_printf("[DHCP] Sending DISCOVER broadcast...\n");
    udp_send(target_ip, DHCP_CLIENT_PORT, DHCP_SERVER_PORT, req, req_len);
    
    kfree(req);
}

bool dhcp_is_bound(void) {
    return g_dhcp_bound;
}
