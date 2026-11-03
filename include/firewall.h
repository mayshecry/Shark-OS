#ifndef SHARKOS_FIREWALL_H
#define SHARKOS_FIREWALL_H

#include <stdint.h>

/* Ordered, first-match rule table. The legacy layout (active/action/protocol/
 * has_source/port/source) is kept at the front so existing callers keep
 * working; everything after `source` is the extended rule engine. */
#define FIREWALL_RULE_LIMIT      64
#define FIREWALL_SERVICE_LIMIT   12
#define FIREWALL_LOG_LIMIT       24
#define FIREWALL_LOG_ENTRY_SIZE  96
#define FIREWALL_COMMENT_SIZE    32
#define FIREWALL_RATE_MAX       1000

#define FIREWALL_ACTION_DENY     0
#define FIREWALL_ACTION_ALLOW    1

#define FIREWALL_PROTOCOL_ANY    0
#define FIREWALL_PROTOCOL_ICMP   1
#define FIREWALL_PROTOCOL_TCP    6
#define FIREWALL_PROTOCOL_UDP    17

#define FIREWALL_DIR_ANY         0
#define FIREWALL_DIR_IN          1
#define FIREWALL_DIR_OUT         2
#define FIREWALL_DIR_FORWARD     3

#define FIREWALL_STATE_ANY       0
#define FIREWALL_STATE_NEW       1
#define FIREWALL_STATE_ESTABLISHED 2

#define FIREWALL_PRESET_OPEN      0
#define FIREWALL_PRESET_SERVER    1
#define FIREWALL_PRESET_LOCKDOWN  2

typedef struct {
    uint8_t  active;
    uint8_t  action;
    uint8_t  protocol;
    uint8_t  has_source;
    uint16_t port;                 /* legacy: destination port, 0 = any */
    uint8_t  source[4];

    uint8_t  enabled;              /* 0 = disabled but kept in the table */
    uint8_t  direction;            /* FIREWALL_DIR_* */
    uint8_t  log;                  /* log matches */
    uint8_t  source_prefix;        /* CIDR prefix, 0..32 */
    uint8_t  has_destination;
    uint8_t  destination_prefix;
    uint8_t  destination[4];
    uint16_t port_end;             /* inclusive destination port range */
    uint16_t source_port;
    uint16_t source_port_end;
    uint8_t  state;                /* FIREWALL_STATE_* */
    uint8_t  icmp_type;            /* 0 = any ICMP type */
    uint16_t rate_limit;           /* per rule, packets/second/source, 0 = off */
    uint32_t hits;
    char     comment[FIREWALL_COMMENT_SIZE];
} firewall_rule_t;

void   firewall_init(void);
int    firewall_input_allowed(const uint8_t* frame, uint32_t length);
int    firewall_output_allowed(const uint8_t* frame, uint32_t length);
int    firewall_forward_allowed(const uint8_t* frame, uint32_t length);
void   firewall_track_outbound(const uint8_t* frame, uint32_t length);

/* Legacy helper and full-featured rule creation (both return 1-based id). */
int    firewall_add_rule(uint8_t action, uint8_t protocol, uint16_t port, const uint8_t* source);
int    firewall_rule_add(const firewall_rule_t* spec);
int    firewall_rule_get(uint32_t id, firewall_rule_t* out);
int    firewall_rule_enable(uint32_t id, int enabled);
int    firewall_rule_move(uint32_t id, int delta);
int    firewall_delete_rule(uint32_t id);
void   firewall_clear_rules(void);
int    firewall_set_default(uint8_t action);
uint8_t firewall_get_default(void);
int    firewall_set_policy(uint8_t direction, uint8_t action);
uint8_t firewall_get_policy(uint8_t direction);
const firewall_rule_t* firewall_get_rule(uint32_t index);
uint32_t firewall_rule_count(void);

int      firewall_set_rate_limit(uint16_t packets_per_second);
uint16_t firewall_get_rate_limit(void);
uint32_t firewall_rate_drop_count(void);

/* Implicitly allowed local services (e.g. router DHCP/DNS/management).
 * Explicit rules are always evaluated first, so a deny rule can still block
 * a service port. */
int      firewall_service_add(uint8_t protocol, uint16_t port);
int      firewall_service_remove(uint8_t protocol, uint16_t port);
int      firewall_service_get(uint32_t index, uint8_t* protocol, uint16_t* port);
uint32_t firewall_service_count(void);
void     firewall_clear_services(void);

int      firewall_apply_preset(uint8_t preset);
uint32_t firewall_counter(uint8_t direction, uint8_t action);
uint32_t firewall_hit_count(uint32_t id);
void     firewall_reset_counters(void);
void     firewall_set_log_drops(int enabled);
int      firewall_get_log_drops(void);
uint32_t firewall_log_count(void);
const char* firewall_log_entry(uint32_t index);   /* 0 = newest */
void     firewall_log_clear(void);

const char* firewall_direction_name(uint8_t direction);
const char* firewall_protocol_name(uint8_t protocol);
const char* firewall_action_name(uint8_t action);

#endif
