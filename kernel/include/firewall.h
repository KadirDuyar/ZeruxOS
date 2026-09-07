/* =============================================================================
 * ZeruX OS — Layer 4 Stateful Firewall / IDS Rule Engine
 * File: kernel/include/firewall.h
 * =============================================================================
 */

#ifndef FIREWALL_H
#define FIREWALL_H

#include <stdint.h>
#include <stdbool.h>

#define FW_MAX_RULES    64
#define FW_RULE_NAME_LEN 32

typedef enum {
    FW_ACTION_DROP  = 0,
    FW_ACTION_ALLOW = 1,
    FW_ACTION_LOG   = 2
} fw_action_t;

typedef enum {
    FW_DIR_IN   = 0,
    FW_DIR_OUT  = 1,
    FW_DIR_BOTH = 2
} fw_direction_t;

typedef struct {
    bool           active;
    fw_action_t    action;
    fw_direction_t direction;

    uint32_t    src_ip;
    uint32_t    src_mask;

    uint8_t     src_mac[6];
    bool        mac_filter_active;

    uint8_t     protocol;  /* 0=ANY, 1=ICMP, 6=TCP, 17=UDP */

    uint16_t    port_min;
    uint16_t    port_max;

    char        name[FW_RULE_NAME_LEN];
    uint32_t    hit_count;
} fw_rule_t;

typedef struct {
    uint32_t total_in;
    uint32_t total_out;
    uint32_t dropped_in;
    uint32_t dropped_out;
    uint32_t allowed_in;
    uint32_t allowed_out;
    uint32_t logged_count;
} fw_stats_t;

void fw_init(void);

bool fw_check_packet(uint32_t src_ip, const uint8_t *src_mac,
                     uint8_t protocol, uint16_t dest_port, bool is_outgoing);
bool fw_check(uint32_t src_ip, uint8_t protocol, uint16_t dest_port);

int  fw_add_rule_full(fw_action_t action, fw_direction_t dir,
                      uint32_t src_ip, uint32_t src_mask,
                      const uint8_t *src_mac,
                      uint8_t protocol,
                      uint16_t port_min, uint16_t port_max,
                      const char *name);

int  fw_add_rule(fw_action_t action, uint32_t ip, uint32_t mask,
                 uint8_t protocol, uint16_t port);

bool fw_del_rule(int id);
void fw_flush(void);

void        fw_set_default_policy(fw_action_t policy);
fw_action_t fw_get_default_policy(void);

int fw_open_port(uint16_t port, uint8_t protocol);
int fw_close_port(uint16_t port, uint8_t protocol);

int fw_block_ip(uint32_t ip);
int fw_allow_ip(uint32_t ip);

uint32_t        fw_list(char *out, uint32_t max_len);
const fw_rule_t *fw_get_rule(int id);

uint32_t          fw_get_drop_count(void);
const fw_stats_t *fw_get_stats(void);
void              fw_reset_stats(void);

int fw_get_max_rules(void);

#endif /* FIREWALL_H */
