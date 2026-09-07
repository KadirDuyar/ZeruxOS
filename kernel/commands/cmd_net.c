/* =============================================================================
 * ZeruX OS — Network Shell Commands
 * File: kernel/commands/cmd_net.c
 * =============================================================================
 */

#include "shell_cmd.h"
#include "net.h"
#include "dhcp.h"
#include "dns.h"
#include "firewall.h"
#include "kheap.h"
#include "libc.h"

int cmd_ifconfig(int argc, char **argv, char *out, uint32_t max) {
    (void)argc; (void)argv;
    extern uint32_t g_local_ip;
    extern uint32_t g_subnet_mask;
    extern uint32_t g_gateway_ip;
    extern uint32_t g_dns_ip;

    extern int rtl8168_is_present(void);
    extern void rtl8139_get_mac(uint8_t *mac);
    extern void rtl8168_get_mac(uint8_t *mac);

    uint8_t mac[6];
    if (rtl8168_is_present()) rtl8168_get_mac(mac);
    else rtl8139_get_mac(mac);

    cmd_printf(out, max, "eth0: flags=4163<UP,BROADCAST,RUNNING,MULTICAST>  mtu 1500\n");
    cmd_printf(out, max, "        inet %d.%d.%d.%d  netmask %d.%d.%d.%d  broadcast 0.0.0.0\n",
               (g_local_ip >> 24) & 0xFF, (g_local_ip >> 16) & 0xFF,
               (g_local_ip >> 8) & 0xFF, g_local_ip & 0xFF,
               (g_subnet_mask >> 24) & 0xFF, (g_subnet_mask >> 16) & 0xFF,
               (g_subnet_mask >> 8) & 0xFF, g_subnet_mask & 0xFF);
    cmd_printf(out, max, "        gateway %d.%d.%d.%d  dns %d.%d.%d.%d\n",
               (g_gateway_ip >> 24) & 0xFF, (g_gateway_ip >> 16) & 0xFF,
               (g_gateway_ip >> 8) & 0xFF, g_gateway_ip & 0xFF,
               (g_dns_ip >> 24) & 0xFF, (g_dns_ip >> 16) & 0xFF,
               (g_dns_ip >> 8) & 0xFF, g_dns_ip & 0xFF);
    cmd_printf(out, max, "        ether %02x:%02x:%02x:%02x:%02x:%02x  txqueuelen 1000  (Ethernet)\n\n",
               mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return 0;
}

int cmd_dhcp(int argc, char **argv, char *out, uint32_t max) {
    (void)argc; (void)argv;
    cmd_printf(out, max, "[DHCP] Sending DHCP Discover broadcast...\n");
    dhcp_discover();
    return 0;
}

int cmd_ping(int argc, char **argv, char *out, uint32_t max) {
    if (argc < 2) {
        cmd_printf(out, max, "Usage: ping <ip or hostname>\n");
        return -1;
    }

    const char *target_str = argv[1];
    uint8_t ip[4];

    if (!net_parse_ip(target_str, ip)) {
        if (!dns_resolve(target_str, ip)) {
            cmd_printf(out, max, "ping: unknown host %s\n", target_str);
            return -1;
        }
    }

    cmd_printf(out, max, "PING %d.%d.%d.%d 56(84) bytes of data.\n", ip[0], ip[1], ip[2], ip[3]);
    net_ping(ip[0], ip[1], ip[2], ip[3]);
    return 0;
}

int cmd_netstat(int argc, char **argv, char *out, uint32_t max) {
    (void)argc; (void)argv;
    cmd_printf(out, max, "Active Internet connections (servers and established)\n");
    cmd_printf(out, max, "Proto Recv-Q Send-Q Local Address           Foreign Address         State\n");
    cmd_printf(out, max, "tcp        0      0 0.0.0.0:80              0.0.0.0:*               LISTEN\n");
    cmd_printf(out, max, "udp        0      0 0.0.0.0:68              0.0.0.0:*               LISTEN\n");

    cmd_printf(out, max, "\nNetwork Statistics:\n");
    extern uint32_t g_net_rx_packets, g_net_rx_bytes, g_net_tx_packets, g_net_tx_bytes, g_net_rx_dropped;
    cmd_printf(out, max, "  RX packets: %u  bytes: %u  dropped: %u\n", g_net_rx_packets, g_net_rx_bytes, g_net_rx_dropped);
    cmd_printf(out, max, "  TX packets: %u  bytes: %u\n", g_net_tx_packets, g_net_tx_bytes);
    return 0;
}

int cmd_host(int argc, char **argv, char *out, uint32_t max) {
    if (argc < 2) {
        cmd_printf(out, max, "Usage: host <domain>\n");
        return -1;
    }
    const char *target = argv[1];
    uint8_t ip[4];
    if (dns_resolve(target, ip)) {
        cmd_printf(out, max, "%s has address %d.%d.%d.%d\n", target, ip[0], ip[1], ip[2], ip[3]);
        return 0;
    }
    cmd_printf(out, max, "host: not found: %s\n", target);
    return -1;
}

int cmd_wget(int argc, char **argv, char *out, uint32_t max) {
    (void)out; (void)max;
    if (argc < 3) {
        cmd_printf(out, max, "Usage: wget <host> <path> [outfile]\n");
        return -1;
    }
    extern void http_client_run(int argc, char **argv);
    http_client_run(argc, argv);
    return 0;
}

int cmd_httpserver(int argc, char **argv, char *out, uint32_t max) {
    (void)out; (void)max;
    extern void http_server_run(int argc, char **argv);
    http_server_run(argc, argv);
    return 0;
}

int cmd_tcp(int argc, char **argv, char *out, uint32_t max) {
    if (argc < 3) {
        cmd_printf(out, max, "Usage: tcp <ip> <port>\n");
        return -1;
    }

    uint8_t ip[4];
    if (!net_parse_ip(argv[1], ip)) {
        cmd_printf(out, max, "Invalid IP format.\n");
        return -1;
    }

    uint32_t target = (ip[0] << 24) | (ip[1] << 16) | (ip[2] << 8) | ip[3];
    uint16_t port = (uint16_t)katoi(argv[2]);

    extern int tcp_socket_open(void);
    extern int tcp_connect(int, uint32_t, uint16_t);

    int sock = tcp_socket_open();
    if (sock >= 0) {
        if (tcp_connect(sock, target, port) == 0) {
            cmd_printf(out, max, "[TCP] Connect initiated on socket %d to %s:%u\n", sock, argv[1], port);
            return 0;
        } else {
            cmd_printf(out, max, "[TCP] tcp_connect failed on socket %d.\n", sock);
            return -1;
        }
    }
    cmd_printf(out, max, "[TCP] Connect failed: No free sockets.\n");
    return -1;
}

int cmd_tcpdump(int argc, char **argv, char *out, uint32_t max) {
    (void)argc; (void)argv;
    cmd_printf(out, max, "tcpdump: listening on eth0, capture size 1514 bytes\n");
    extern uint32_t g_pcap_head, g_pcap_count;
    extern pcap_packet_t g_pcap_buffer[];

    if (g_pcap_count == 0) {
        cmd_printf(out, max, "0 packets captured.\n");
    } else {
        uint32_t h = g_pcap_head;
        uint32_t dumped = 0;
        while (dumped < g_pcap_count && dumped < 10) {
            pcap_packet_t *p = &g_pcap_buffer[h];
            cmd_printf(out, max, "[%04u.%03u] IP packet, length %u\n    ",
                       p->timestamp_sec, p->timestamp_msec, p->len);
            for (uint32_t i = 0; i < 16 && i < p->len; i++) {
                cmd_printf(out, max, "%02x ", p->data[i]);
            }
            cmd_printf(out, max, "\n");
            h = (h + 1) % PCAP_MAX_PACKETS;
            dumped++;
        }
        cmd_printf(out, max, "\n%u packets total in ring buffer.\n", g_pcap_count);
    }
    return 0;
}

int cmd_firewall(int argc, char **argv, char *out, uint32_t max) {
    if (argc < 2 || kstrcmp(argv[1], "list") == 0) {
        char *buf = (char*)kmalloc(8192);
        if (buf) {
            fw_list(buf, 8192);
            cmd_printf(out, max, "%s", buf);
            kfree(buf);
            return 0;
        }
        cmd_printf(out, max, "firewall: out of memory\n");
        return -1;
    }

    if (kstrcmp(argv[1], "add") == 0) {
        if (argc < 3) {
            cmd_printf(out, max, "Usage: firewall add <ip>\n");
            return -1;
        }
        uint8_t ip[4];
        if (net_parse_ip(argv[2], ip)) {
            uint32_t target = (ip[0] << 24) | (ip[1] << 16) | (ip[2] << 8) | ip[3];
            int id = fw_add_rule(FW_ACTION_DROP, target, 0xFFFFFFFF, 0, 0);
            if (id >= 0) cmd_printf(out, max, "firewall: added rule %d to DROP %s\n", id, argv[2]);
            else         cmd_printf(out, max, "firewall: rule table full\n");
            return 0;
        } else {
            cmd_printf(out, max, "firewall: invalid IP format\n");
            return -1;
        }
    }

    if (kstrcmp(argv[1], "del") == 0) {
        if (argc < 3) {
            cmd_printf(out, max, "Usage: firewall del <id>\n");
            return -1;
        }
        int id = katoi(argv[2]);
        if (fw_del_rule(id)) cmd_printf(out, max, "firewall: deleted rule %d\n", id);
        else                 cmd_printf(out, max, "firewall: failed to delete rule %d\n", id);
        return 0;
    }

    cmd_printf(out, max, "Usage: firewall list | add <ip> | del <id>\n");
    return -1;
}

int cmd_aurora(int argc, char **argv, char *out, uint32_t max) {
    (void)out; (void)max;
    const char *url = (argc > 1) ? argv[1] : "";
    extern int32_t browser_app_create(int32_t x, int32_t y, int32_t w, int32_t h, const char *url);
    browser_app_create(60, 40, 700, 480, url);
    return 0;
}
