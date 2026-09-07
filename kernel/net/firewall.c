/* =============================================================================
 * ZeruX OS — Layer 4 Stateful Firewall Implementation
 * File: kernel/net/firewall.c
 * =============================================================================
 */

#include "firewall.h"
#include "serial.h"
#include "libc.h"

static fw_rule_t    g_fw_rules[FW_MAX_RULES];
static fw_stats_t   g_fw_stats;
static fw_action_t  g_default_policy = FW_ACTION_ALLOW;

/* ── String helpers ────────────────────────────────────────────────── */
static size_t fw_strlen(const char *s) {
    size_t n = 0; while (s && s[n]) n++; return n;
}

static void fw_strcpy(char *dst, const char *src, size_t maxn) {
    size_t i = 0;
    while (i < maxn - 1 && src && src[i]) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

static int fw_mac_is_zero(const uint8_t *mac) {
    for (int i = 0; i < 6; i++) if (mac[i]) return 0;
    return 1;
}

/* ── Init ──────────────────────────────────────────────────────────── */
void fw_init(void) {
    for (int i = 0; i < FW_MAX_RULES; i++) {
        g_fw_rules[i].active = false;
        g_fw_rules[i].hit_count = 0;
    }
    uint8_t *p = (uint8_t *)&g_fw_stats;
    for (size_t i = 0; i < sizeof(fw_stats_t); i++) p[i] = 0;
    g_default_policy = FW_ACTION_ALLOW;
    serial_printf("[FIREWALL] Layer 4 rule engine initialized (default: ALLOW).\n");
}

/* ── Core packet check ─────────────────────────────────────────────── */
bool fw_check_packet(uint32_t src_ip, const uint8_t *src_mac,
                     uint8_t protocol, uint16_t dest_port, bool is_outgoing) {
    if (is_outgoing) g_fw_stats.total_out++;
    else             g_fw_stats.total_in++;

    for (int i = 0; i < FW_MAX_RULES; i++) {
        fw_rule_t *r = &g_fw_rules[i];
        if (!r->active) continue;

        /* Direction check */
        if (r->direction == FW_DIR_IN  && is_outgoing)  continue;
        if (r->direction == FW_DIR_OUT && !is_outgoing) continue;

        /* IP/CIDR check */
        if (r->src_mask != 0 && (src_ip & r->src_mask) != (r->src_ip & r->src_mask)) continue;

        /* MAC check */
        if (r->mac_filter_active && src_mac) {
            int mac_match = 1;
            for (int m = 0; m < 6; m++) {
                if (r->src_mac[m] != src_mac[m]) { mac_match = 0; break; }
            }
            if (!mac_match) continue;
        }

        /* Protocol check */
        if (r->protocol != 0 && r->protocol != protocol) continue;

        /* Port range check */
        if (r->port_min != 0 || r->port_max != 0) {
            uint16_t lo = r->port_min;
            uint16_t hi = r->port_max ? r->port_max : r->port_min;
            if (dest_port < lo || dest_port > hi) continue;
        }

        /* Match found */
        r->hit_count++;

        if (r->action == FW_ACTION_DROP) {
            if (is_outgoing) g_fw_stats.dropped_out++;
            else             g_fw_stats.dropped_in++;
            serial_printf("[FIREWALL] DROP [%s]: %d.%d.%d.%d proto=%u port=%u\n",
                r->name,
                (src_ip >> 24) & 0xFF, (src_ip >> 16) & 0xFF,
                (src_ip >> 8) & 0xFF,   src_ip & 0xFF,
                protocol, dest_port);
            return false;
        } else if (r->action == FW_ACTION_LOG) {
            g_fw_stats.logged_count++;
            serial_printf("[FIREWALL] LOG [%s]: %d.%d.%d.%d proto=%u port=%u\n",
                r->name,
                (src_ip >> 24) & 0xFF, (src_ip >> 16) & 0xFF,
                (src_ip >> 8) & 0xFF,   src_ip & 0xFF,
                protocol, dest_port);
            /* LOG = pass through */
        } else {
            if (is_outgoing) g_fw_stats.allowed_out++;
            else             g_fw_stats.allowed_in++;
        }
        return true;
    }

    /* Default policy */
    bool allowed = (g_default_policy != FW_ACTION_DROP);
    if (!allowed) {
        if (is_outgoing) g_fw_stats.dropped_out++;
        else             g_fw_stats.dropped_in++;
    } else {
        if (is_outgoing) g_fw_stats.allowed_out++;
        else             g_fw_stats.allowed_in++;
    }
    return allowed;
}

/* Backwards-compat wrapper */
bool fw_check(uint32_t src_ip, uint8_t protocol, uint16_t dest_port) {
    return fw_check_packet(src_ip, NULL, protocol, dest_port, false);
}

/* ── Rule management ───────────────────────────────────────────────── */
int fw_add_rule_full(fw_action_t action, fw_direction_t dir,
                     uint32_t src_ip, uint32_t src_mask,
                     const uint8_t *src_mac,
                     uint8_t protocol,
                     uint16_t port_min, uint16_t port_max,
                     const char *name) {
    for (int i = 0; i < FW_MAX_RULES; i++) {
        if (!g_fw_rules[i].active) {
            fw_rule_t *r = &g_fw_rules[i];
            r->active    = true;
            r->action    = action;
            r->direction = dir;
            r->src_ip    = src_ip;
            r->src_mask  = src_mask;
            r->protocol  = protocol;
            r->port_min  = port_min;
            r->port_max  = port_max;
            r->hit_count = 0;
            if (src_mac && !fw_mac_is_zero(src_mac)) {
                for (int m = 0; m < 6; m++) r->src_mac[m] = src_mac[m];
                r->mac_filter_active = true;
            } else {
                r->mac_filter_active = false;
                for (int m = 0; m < 6; m++) r->src_mac[m] = 0;
            }
            fw_strcpy(r->name, name ? name : "rule", FW_RULE_NAME_LEN);
            return i;
        }
    }
    return -1;
}

int fw_add_rule(fw_action_t action, uint32_t ip, uint32_t mask,
                uint8_t protocol, uint16_t port) {
    return fw_add_rule_full(action, FW_DIR_BOTH, ip, mask,
                            NULL, protocol, port, port, "rule");
}

bool fw_del_rule(int id) {
    if (id < 0 || id >= FW_MAX_RULES || !g_fw_rules[id].active) return false;
    g_fw_rules[id].active = false;
    return true;
}

void fw_flush(void) {
    for (int i = 0; i < FW_MAX_RULES; i++) g_fw_rules[i].active = false;
}

void fw_set_default_policy(fw_action_t policy) {
    g_default_policy = policy;
    serial_printf("[FIREWALL] Default policy set to %s\n",
        policy == FW_ACTION_DROP ? "DROP" : "ALLOW");
}

fw_action_t fw_get_default_policy(void) { return g_default_policy; }

int fw_open_port(uint16_t port, uint8_t protocol) {
    char name[FW_RULE_NAME_LEN];
    /* build name: "allow-port-NNNNN" */
    name[0]='a'; name[1]='l'; name[2]='l'; name[3]='o'; name[4]='w';
    name[5]='-'; name[6]='p'; name[7]='o'; name[8]='r'; name[9]='t';
    name[10]='-';
    int idx = 11;
    uint16_t v = port;
    if (v == 0) { name[idx++] = '0'; }
    else {
        char tmp[8]; int tl = 0;
        while (v) { tmp[tl++] = '0' + (v % 10); v /= 10; }
        for (int i = tl - 1; i >= 0 && idx < FW_RULE_NAME_LEN - 1; i--) name[idx++] = tmp[i];
    }
    name[idx] = '\0';
    return fw_add_rule_full(FW_ACTION_ALLOW, FW_DIR_IN, 0, 0, NULL,
                            protocol, port, port, name);
}

int fw_close_port(uint16_t port, uint8_t protocol) {
    char name[FW_RULE_NAME_LEN];
    name[0]='b'; name[1]='l'; name[2]='o'; name[3]='c'; name[4]='k';
    name[5]='-'; name[6]='p'; name[7]='o'; name[8]='r'; name[9]='t';
    name[10]='-';
    int idx = 11;
    uint16_t v = port;
    if (v == 0) { name[idx++] = '0'; }
    else {
        char tmp[8]; int tl = 0;
        while (v) { tmp[tl++] = '0' + (v % 10); v /= 10; }
        for (int i = tl - 1; i >= 0 && idx < FW_RULE_NAME_LEN - 1; i--) name[idx++] = tmp[i];
    }
    name[idx] = '\0';
    return fw_add_rule_full(FW_ACTION_DROP, FW_DIR_IN, 0, 0, NULL,
                            protocol, port, port, name);
}

int fw_block_ip(uint32_t ip) {
    return fw_add_rule_full(FW_ACTION_DROP, FW_DIR_BOTH, ip, 0xFFFFFFFF,
                            NULL, 0, 0, 0, "block-ip");
}

int fw_allow_ip(uint32_t ip) {
    return fw_add_rule_full(FW_ACTION_ALLOW, FW_DIR_BOTH, ip, 0xFFFFFFFF,
                            NULL, 0, 0, 0, "allow-ip");
}

/* ── fw_list ───────────────────────────────────────────────────────── */
static const char *fw_proto_name(uint8_t p) {
    switch (p) {
        case 0:  return "ANY";
        case 1:  return "ICMP";
        case 6:  return "TCP";
        case 17: return "UDP";
        default: return "?";
    }
}

static const char *fw_dir_name(fw_direction_t d) {
    switch (d) {
        case FW_DIR_IN:   return "IN";
        case FW_DIR_OUT:  return "OUT";
        default:          return "BOTH";
    }
}

/* Simple append helper */
static void fw_append(char *buf, uint32_t max, const char *src) {
    size_t cur = fw_strlen(buf);
    size_t sl  = fw_strlen(src);
    if (cur + sl >= max) return;
    fw_strcpy(buf + cur, src, max - cur);
}

static void fw_append_uint(char *buf, uint32_t max, uint32_t v) {
    char tmp[12]; int l = 0;
    if (v == 0) { tmp[l++] = '0'; }
    else { while (v) { tmp[l++] = '0' + (v % 10); v /= 10; } }
    char rev[12];
    for (int i = 0; i < l; i++) rev[i] = tmp[l - 1 - i];
    rev[l] = '\0';
    fw_append(buf, max, rev);
}

uint32_t fw_list(char *out, uint32_t max_len) {
    if (!out || max_len == 0) return 0;
    out[0] = '\0';
    int count = 0;

    for (int i = 0; i < FW_MAX_RULES; i++) {
        fw_rule_t *r = &g_fw_rules[i];
        if (!r->active) continue;
        count++;

        char line[160];
        line[0] = '\0';
        char *l = line;
        /* [N] ACTION DIR proto IP port hits */
        fw_append(line, sizeof(line), "[");
        fw_append_uint(line, sizeof(line), (uint32_t)i);
        fw_append(line, sizeof(line), "] ");
        fw_append(line, sizeof(line), r->action == FW_ACTION_DROP  ? "DROP " :
                                       r->action == FW_ACTION_LOG   ? "LOG  " : "ALLOW");
        fw_append(line, sizeof(line), " ");
        fw_append(line, sizeof(line), fw_dir_name(r->direction));
        fw_append(line, sizeof(line), " ");
        fw_append(line, sizeof(line), fw_proto_name(r->protocol));
        fw_append(line, sizeof(line), " ");

        if (r->src_mask == 0 && r->src_ip == 0) {
            fw_append(line, sizeof(line), "*.*.*.*");
        } else {
            fw_append_uint(line, sizeof(line), (r->src_ip >> 24) & 0xFF);
            fw_append(line, sizeof(line), ".");
            fw_append_uint(line, sizeof(line), (r->src_ip >> 16) & 0xFF);
            fw_append(line, sizeof(line), ".");
            fw_append_uint(line, sizeof(line), (r->src_ip >> 8) & 0xFF);
            fw_append(line, sizeof(line), ".");
            fw_append_uint(line, sizeof(line), r->src_ip & 0xFF);
            if (r->src_mask != 0xFFFFFFFF) {
                /* Count mask bits */
                uint32_t m = r->src_mask; int bits = 0;
                while (m & 0x80000000) { bits++; m <<= 1; }
                fw_append(line, sizeof(line), "/");
                fw_append_uint(line, sizeof(line), (uint32_t)bits);
            }
        }
        fw_append(line, sizeof(line), " port:");
        if (r->port_min == 0 && r->port_max == 0) {
            fw_append(line, sizeof(line), "any");
        } else {
            fw_append_uint(line, sizeof(line), r->port_min);
            if (r->port_max && r->port_max != r->port_min) {
                fw_append(line, sizeof(line), "-");
                fw_append_uint(line, sizeof(line), r->port_max);
            }
        }
        fw_append(line, sizeof(line), " hits:");
        fw_append_uint(line, sizeof(line), r->hit_count);
        if (r->mac_filter_active) {
            fw_append(line, sizeof(line), " mac:");
            for (int m = 0; m < 6; m++) {
                uint8_t b = r->src_mac[m];
                char hex[3];
                hex[0] = "0123456789ABCDEF"[b >> 4];
                hex[1] = "0123456789ABCDEF"[b & 0xF];
                hex[2] = '\0';
                fw_append(line, sizeof(line), hex);
                if (m < 5) fw_append(line, sizeof(line), ":");
            }
        }
        fw_append(line, sizeof(line), " [");
        fw_append(line, sizeof(line), r->name);
        fw_append(line, sizeof(line), "]\n");
        (void)l;

        if (fw_strlen(out) + fw_strlen(line) < max_len)
            fw_append(out, max_len, line);
    }

    if (count == 0) {
        const char *msg = "Kural yok (default: ALLOW ALL).\n";
        if (fw_strlen(msg) < max_len) fw_append(out, max_len, msg);
    }
    return (uint32_t)fw_strlen(out);
}

const fw_rule_t *fw_get_rule(int id) {
    if (id < 0 || id >= FW_MAX_RULES) return NULL;
    if (!g_fw_rules[id].active) return NULL;
    return &g_fw_rules[id];
}

uint32_t fw_get_drop_count(void) {
    return g_fw_stats.dropped_in + g_fw_stats.dropped_out;
}

const fw_stats_t *fw_get_stats(void) { return &g_fw_stats; }

void fw_reset_stats(void) {
    uint8_t *p = (uint8_t *)&g_fw_stats;
    for (size_t i = 0; i < sizeof(fw_stats_t); i++) p[i] = 0;
    for (int i = 0; i < FW_MAX_RULES; i++) g_fw_rules[i].hit_count = 0;
}

int fw_get_max_rules(void) { return FW_MAX_RULES; }
