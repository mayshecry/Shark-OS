#ifndef SHARKOS_FIREWALL_H
#define SHARKOS_FIREWALL_H

#include <stdint.h>

#define FIREWALL_RULE_LIMIT 16
#define FIREWALL_ACTION_DENY 0
#define FIREWALL_ACTION_ALLOW 1
#define FIREWALL_PROTOCOL_ANY 0
#define FIREWALL_PROTOCOL_ICMP 1
#define FIREWALL_PROTOCOL_TCP 6
#define FIREWALL_PROTOCOL_UDP 17

typedef struct {
    uint8_t active;
    uint8_t action;
    uint8_t protocol;
    uint8_t has_source;
    uint16_t port;
    uint8_t source[4];
} firewall_rule_t;

void firewall_init(void);
int firewall_input_allowed(const uint8_t* frame, uint32_t length);
void firewall_track_outbound(const uint8_t* frame, uint32_t length);
int firewall_add_rule(uint8_t action, uint8_t protocol, uint16_t port, const uint8_t* source);
int firewall_delete_rule(uint32_t id);
void firewall_clear_rules(void);
int firewall_set_default(uint8_t action);
uint8_t firewall_get_default(void);
const firewall_rule_t* firewall_get_rule(uint32_t index);
uint32_t firewall_rule_count(void);
int firewall_set_rate_limit(uint16_t packets_per_second);
uint16_t firewall_get_rate_limit(void);
uint32_t firewall_rate_drop_count(void);

#endif
