#include "kernel.h"
#include "firewall.h"

#define FIREWALL_CONNECTION_LIMIT   48
#define FIREWALL_CONNECTION_TTL    (120u * TICKS_PER_SEC)
#define FIREWALL_RATE_SOURCE_LIMIT 64
#define FIREWALL_RULE_RATE_LIMIT   16

typedef struct {
    uint8_t  active;
    uint8_t  protocol;
    uint8_t  tcp_established;
    uint8_t  local_ip[4];
    uint8_t  remote_ip[4];
    uint16_t local_port;
    uint16_t remote_port;
    uint32_t expires;
} firewall_connection_t;

typedef struct {
    uint8_t  active;
    uint8_t  source[4];
    uint16_t count;
    uint32_t window;
} firewall_rate_source_t;

typedef struct {
    uint8_t  active;
    uint8_t  source[4];
    uint32_t rule_index;
    uint16_t count;
    uint32_t window;
} firewall_rule_rate_t;

typedef struct {
    uint8_t  active;
    uint8_t  protocol;
    uint16_t port;
} firewall_service_t;

typedef struct {
    const uint8_t* frame;
    uint32_t length;
    uint16_t ether_type;
    const uint8_t* ip;
    uint32_t ihl;
    uint32_t total;
    uint8_t  protocol;
    const uint8_t* source;
    const uint8_t* destination;
    uint16_t source_port;
    uint16_t destination_port;
    uint8_t  tcp_flags;
    uint8_t  icmp_type;
} fw_packet_t;

const char* firewall_direction_name(uint8_t direction);
const char* firewall_protocol_name(uint8_t protocol);
const char* firewall_action_name(uint8_t action);

static firewall_rule_t firewall_rules[FIREWALL_RULE_LIMIT];
static firewall_connection_t firewall_connections[FIREWALL_CONNECTION_LIMIT];
static firewall_rate_source_t firewall_rate_sources[FIREWALL_RATE_SOURCE_LIMIT];
static firewall_rule_rate_t firewall_rule_rates[FIREWALL_RULE_RATE_LIMIT];
static firewall_service_t firewall_services[FIREWALL_SERVICE_LIMIT];
static uint8_t firewall_default[3] = {   /* IN, OUT, FORWARD */
    FIREWALL_ACTION_ALLOW, FIREWALL_ACTION_ALLOW, FIREWALL_ACTION_ALLOW
};
static uint16_t firewall_rate_limit;
static uint32_t firewall_rate_drops;
static uint32_t firewall_counters[3][2]; /* [direction][action] */
static int firewall_drop_logging = 1;
static uint32_t firewall_last_drop_log;
static char firewall_log[FIREWALL_LOG_LIMIT][FIREWALL_LOG_ENTRY_SIZE];
static uint32_t firewall_log_head;
static uint32_t firewall_log_used;

/* ------------------------------------------------------------------ */
/* small self-contained helpers (no libc calls beyond kernel.h)        */
/* ------------------------------------------------------------------ */

static uint16_t fw_get16(const uint8_t* value) {
    return (uint16_t)(((uint16_t)value[0] << 8) | value[1]);
}

static int fw_equal4(const uint8_t* left, const uint8_t* right) {
    for (uint32_t i = 0; i < 4; i++) if (left[i] != right[i]) return 0;
    return 1;
}

static void fw_append(char* out, uint32_t cap, uint32_t* used, const char* text) {
    while (*text && *used + 1 < cap) out[(*used)++] = *text++;
    out[*used] = 0;
}

static void fw_dec(uint32_t value, char* out, uint32_t cap, uint32_t* used) {
    char tmp[12];
    uint32_t n = 0;
    if (!value) tmp[n++] = '0';
    while (value && n < sizeof(tmp)) { tmp[n++] = (char)('0' + value % 10); value /= 10; }
    while (n && *used + 1 < cap) out[(*used)++] = tmp[--n];
    out[*used] = 0;
}

static void fw_ip(const uint8_t* ip, char* out, uint32_t cap, uint32_t* used) {
    for (uint32_t i = 0; i < 4; i++) {
        fw_dec(ip[i], out, cap, used);
        if (i < 3 && *used + 1 < cap) out[(*used)++] = '.';
    }
    out[*used] = 0;
}

static int fw_checksum_valid(const uint8_t* header, uint32_t length) {
    uint32_t sum = 0;
    for (uint32_t i = 0; i < length; i += 2) sum += ((uint32_t)header[i] << 8) | header[i + 1];
    while (sum >> 16) sum = (sum & 0xFFFFu) + (sum >> 16);
    return sum == 0xFFFFu;
}

static uint32_t fw_policy_index(uint8_t direction) {
    if (direction == FIREWALL_DIR_OUT) return 1;
    if (direction == FIREWALL_DIR_FORWARD) return 2;
    return 0;
}

static void fw_count(uint8_t direction, uint8_t action) {
    firewall_counters[fw_policy_index(direction)][action == FIREWALL_ACTION_ALLOW ? 1 : 0]++;
}

static void fw_log_event(uint8_t direction, uint8_t action, const fw_packet_t* p, int32_t rule_id) {
    uint32_t window = uptime_ticks / TICKS_PER_SEC;
    if (action == FIREWALL_ACTION_DENY && (!firewall_drop_logging || window == firewall_last_drop_log)) return;
    if (action == FIREWALL_ACTION_DENY) firewall_last_drop_log = window;
    char* out = firewall_log[firewall_log_head];
    uint32_t used = 0;
    fw_dec(uptime_ticks / TICKS_PER_SEC, out, FIREWALL_LOG_ENTRY_SIZE, &used);
    fw_append(out, FIREWALL_LOG_ENTRY_SIZE, &used, "s ");
    fw_append(out, FIREWALL_LOG_ENTRY_SIZE, &used, firewall_direction_name(direction));
    fw_append(out, FIREWALL_LOG_ENTRY_SIZE, &used, action == FIREWALL_ACTION_ALLOW ? " allow " : " drop ");
    if (p->ether_type == 0x0800) {
        fw_append(out, FIREWALL_LOG_ENTRY_SIZE, &used, firewall_protocol_name(p->protocol));
        fw_append(out, FIREWALL_LOG_ENTRY_SIZE, &used, " ");
        fw_ip(p->source, out, FIREWALL_LOG_ENTRY_SIZE, &used);
        fw_append(out, FIREWALL_LOG_ENTRY_SIZE, &used, ":");
        fw_dec(p->source_port, out, FIREWALL_LOG_ENTRY_SIZE, &used);
        fw_append(out, FIREWALL_LOG_ENTRY_SIZE, &used, "->");
        fw_ip(p->destination, out, FIREWALL_LOG_ENTRY_SIZE, &used);
        fw_append(out, FIREWALL_LOG_ENTRY_SIZE, &used, ":");
        fw_dec(p->destination_port, out, FIREWALL_LOG_ENTRY_SIZE, &used);
    } else {
        fw_append(out, FIREWALL_LOG_ENTRY_SIZE, &used, "eth");
    }
    fw_append(out, FIREWALL_LOG_ENTRY_SIZE, &used, " rule=");
    if (rule_id < 0) fw_append(out, FIREWALL_LOG_ENTRY_SIZE, &used, "policy");
    else fw_dec((uint32_t)rule_id, out, FIREWALL_LOG_ENTRY_SIZE, &used);
    out[used] = 0;
    firewall_log_head = (firewall_log_head + 1) % FIREWALL_LOG_LIMIT;
    if (firewall_log_used < FIREWALL_LOG_LIMIT) firewall_log_used++;
}

/* ------------------------------------------------------------------ */
/* packet parsing                                                      */
/* ------------------------------------------------------------------ */

static int fw_is_fragmented(const uint8_t* ip) {
    return (ip[6] & 0xBF) != 0 || ip[7] != 0;
}

static int fw_parse(const uint8_t* frame, uint32_t length, fw_packet_t* p) {
    memset(p, 0, sizeof(*p));
    if (!frame || length < 14) return 0;
    p->frame = frame;
    p->length = length;
    p->ether_type = fw_get16(frame + 12);
    if (p->ether_type == 0x0806) return 1;               /* ARP always allowed */
    if (p->ether_type != 0x0800) return 2;               /* unknown ethertype  */
    if (length < 34 || (frame[14] >> 4) != 4) return 0;
    p->ihl = (uint32_t)(frame[14] & 0x0F) * 4;
    p->total = fw_get16(frame + 16);
    if (p->ihl < 20 || p->total < p->ihl || p->total > length - 14) return 0;
    p->ip = frame + 14;
    if (!fw_checksum_valid(p->ip, p->ihl) || fw_is_fragmented(p->ip)) return 0;
    p->protocol = p->ip[9];
    p->source = p->ip + 12;
    p->destination = p->ip + 16;
    const uint8_t* transport = p->ip + p->ihl;
    uint32_t payload_length = p->total - p->ihl;
    p->source_port = 0;
    p->destination_port = 0;
    p->tcp_flags = 0;
    if (p->protocol == FIREWALL_PROTOCOL_TCP) {
        if (payload_length < 20) return 0;
        uint32_t header_length = (uint32_t)(transport[12] >> 4) * 4;
        if (header_length < 20 || header_length > payload_length) return 0;
        p->source_port = fw_get16(transport);
        p->destination_port = fw_get16(transport + 2);
        p->tcp_flags = transport[13];
    } else if (p->protocol == FIREWALL_PROTOCOL_UDP) {
        if (payload_length < 8 || fw_get16(transport + 4) != payload_length) return 0;
        p->source_port = fw_get16(transport);
        p->destination_port = fw_get16(transport + 2);
    } else if (p->protocol == FIREWALL_PROTOCOL_ICMP) {
        if (payload_length < 8) return 0;
        p->icmp_type = transport[0];
        p->source_port = fw_get16(transport + 4);   /* echo id */
    }
    return 1;
}

static int fw_cidr_match(const uint8_t* address, const uint8_t* network, uint8_t prefix) {
    if (prefix > 32) prefix = 32;
    for (uint32_t i = 0; i < 4; i++) {
        uint32_t bits = prefix >= 8 ? 8u : prefix;
        uint8_t mask = (uint8_t)(bits == 0 ? 0 : (0xFFu << (8 - bits)));
        if ((address[i] & mask) != (network[i] & mask)) return 0;
        prefix = prefix > bits ? (uint8_t)(prefix - bits) : 0;
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* connection state                                                    */
/* ------------------------------------------------------------------ */

static void fw_expire_connections(void) {
    for (uint32_t i = 0; i < FIREWALL_CONNECTION_LIMIT; i++) {
        if (firewall_connections[i].active &&
            (int32_t)(uptime_ticks - firewall_connections[i].expires) >= 0)
            firewall_connections[i].active = 0;
    }
}

static int fw_flow_known(const fw_packet_t* p) {
    if (!p->ip) return 0;
    fw_expire_connections();
    for (uint32_t i = 0; i < FIREWALL_CONNECTION_LIMIT; i++) {
        const firewall_connection_t* c = &firewall_connections[i];
        if (!c->active || c->protocol != p->protocol) continue;
        if (c->local_port == p->source_port && c->remote_port == p->destination_port &&
            fw_equal4(c->local_ip, p->source) && fw_equal4(c->remote_ip, p->destination)) return 1;
        if (c->local_port == p->destination_port && c->remote_port == p->source_port &&
            fw_equal4(c->local_ip, p->destination) && fw_equal4(c->remote_ip, p->source)) return 1;
    }
    return 0;
}

static int fw_established_reply(const fw_packet_t* p, int commit) {
    if (!p->ip || (p->protocol != FIREWALL_PROTOCOL_TCP &&
                   p->protocol != FIREWALL_PROTOCOL_UDP &&
                   p->protocol != FIREWALL_PROTOCOL_ICMP)) return 0;
    uint16_t local_port = p->destination_port;
    uint16_t remote_port = p->source_port;
    if (p->protocol == FIREWALL_PROTOCOL_ICMP) {
        if (p->icmp_type != 0) return 0;
        local_port = p->source_port;
        remote_port = 0;
    }
    fw_expire_connections();
    for (uint32_t i = 0; i < FIREWALL_CONNECTION_LIMIT; i++) {
        firewall_connection_t* c = &firewall_connections[i];
        if (!c->active || c->protocol != p->protocol || c->local_port != local_port ||
            c->remote_port != remote_port) continue;
        if (!fw_equal4(c->remote_ip, p->source) || !fw_equal4(c->local_ip, p->destination)) continue;
        if (p->protocol == FIREWALL_PROTOCOL_TCP && !c->tcp_established) {
            if (p->tcp_flags & 0x04) { if (commit) c->active = 0; return 0; }
            if ((p->tcp_flags & 0x12) != 0x12) return 0;
            if (commit) c->tcp_established = 1;
        } else if (p->protocol == FIREWALL_PROTOCOL_TCP &&
                   !(p->tcp_flags & 0x10) && !(p->tcp_flags & 0x04)) return 0;
        if (commit) c->expires = uptime_ticks + FIREWALL_CONNECTION_TTL;
        return 1;
    }
    return 0;
}

void firewall_track_outbound(const uint8_t* frame, uint32_t length) {
    fw_packet_t p;
    if (fw_parse(frame, length, &p) != 1 || !p.ip || fw_is_fragmented(p.ip)) return;
    uint16_t local_port = p.source_port;
    uint16_t remote_port = p.destination_port;
    if (p.protocol == FIREWALL_PROTOCOL_TCP) {
        if (!(p.tcp_flags & 0x02) || (p.tcp_flags & (0x10 | 0x04))) {
            fw_expire_connections();
            for (uint32_t i = 0; i < FIREWALL_CONNECTION_LIMIT; i++) {
                firewall_connection_t* c = &firewall_connections[i];
                if (!c->active || c->protocol != p.protocol ||
                    c->local_port != local_port || c->remote_port != remote_port) continue;
                if (!fw_equal4(c->local_ip, p.source) || !fw_equal4(c->remote_ip, p.destination)) continue;
                if (p.tcp_flags & 0x04) c->active = 0;
                else c->expires = uptime_ticks + FIREWALL_CONNECTION_TTL;
                return;
            }
            return;
        }
    } else if (p.protocol == FIREWALL_PROTOCOL_ICMP) {
        if (p.icmp_type != 8) return;
        remote_port = 0;
    } else if (p.protocol != FIREWALL_PROTOCOL_UDP) return;
    fw_expire_connections();
    firewall_connection_t* slot = 0;
    for (uint32_t i = 0; i < FIREWALL_CONNECTION_LIMIT; i++) {
        firewall_connection_t* c = &firewall_connections[i];
        if (!c->active) { if (!slot) slot = c; continue; }
        if (c->protocol == p.protocol && c->local_port == local_port &&
            c->remote_port == remote_port && fw_equal4(c->local_ip, p.source) &&
            fw_equal4(c->remote_ip, p.destination)) {
            c->expires = uptime_ticks + FIREWALL_CONNECTION_TTL;
            return;
        }
    }
    if (!slot) return;
    memset(slot, 0, sizeof(*slot));
    slot->active = 1;
    slot->protocol = p.protocol;
    memcpy(slot->local_ip, p.source, 4);
    memcpy(slot->remote_ip, p.destination, 4);
    slot->local_port = local_port;
    slot->remote_port = remote_port;
    slot->expires = uptime_ticks + FIREWALL_CONNECTION_TTL;
}

/* ------------------------------------------------------------------ */
/* rate limiting                                                       */
/* ------------------------------------------------------------------ */

static int fw_rate_consume(const uint8_t* source) {
    if (!firewall_rate_limit) return 0;
    uint32_t window = uptime_ticks / TICKS_PER_SEC;
    firewall_rate_source_t* slot = 0;
    for (uint32_t i = 0; i < FIREWALL_RATE_SOURCE_LIMIT; i++) {
        firewall_rate_source_t* entry = &firewall_rate_sources[i];
        if (entry->active && entry->window != window) entry->active = 0;
        if (!entry->active) { if (!slot) slot = entry; continue; }
        if (fw_equal4(entry->source, source)) { slot = entry; break; }
    }
    if (!slot) { firewall_rate_drops++; return 1; }
    if (!slot->active) {
        slot->active = 1;
        memcpy(slot->source, source, 4);
        slot->count = 0;
        slot->window = window;
    }
    if (slot->count >= firewall_rate_limit) { firewall_rate_drops++; return 1; }
    slot->count++;
    return 0;
}

static int fw_rule_rate_consume(uint32_t rule_index, uint16_t limit, const uint8_t* source) {
    if (!limit) return 0;
    uint32_t window = uptime_ticks / TICKS_PER_SEC;
    firewall_rule_rate_t* slot = 0;
    for (uint32_t i = 0; i < FIREWALL_RULE_RATE_LIMIT; i++) {
        firewall_rule_rate_t* entry = &firewall_rule_rates[i];
        if (entry->active && entry->window != window) entry->active = 0;
        if (!entry->active) { if (!slot) slot = entry; continue; }
        if (entry->rule_index == rule_index && fw_equal4(entry->source, source)) { slot = entry; break; }
    }
    if (!slot) return 1;
    if (!slot->active) {
        slot->active = 1;
        slot->rule_index = rule_index;
        memcpy(slot->source, source, 4);
        slot->count = 0;
        slot->window = window;
    }
    if (slot->count >= limit) return 1;
    slot->count++;
    return 0;
}

static int fw_is_new_traffic(const fw_packet_t* p) {
    if (!p->ip) return 0;
    if (p->protocol == FIREWALL_PROTOCOL_TCP)
        return (p->tcp_flags & 0x02) && !(p->tcp_flags & (0x10 | 0x04));
    if (p->protocol == FIREWALL_PROTOCOL_UDP) return 1;
    if (p->protocol == FIREWALL_PROTOCOL_ICMP) return p->icmp_type == 8;
    return 0;
}

/* ------------------------------------------------------------------ */
/* rules & services                                                    */
/* ------------------------------------------------------------------ */

static int fw_rule_match(const firewall_rule_t* rule, const fw_packet_t* p,
                         uint8_t direction, int established) {
    if (!rule->active || !rule->enabled) return 0;
    if (rule->direction != FIREWALL_DIR_ANY && rule->direction != direction) return 0;
    if (rule->protocol != FIREWALL_PROTOCOL_ANY && rule->protocol != p->protocol) return 0;
    if (rule->state == FIREWALL_STATE_NEW && established) return 0;
    if (rule->state == FIREWALL_STATE_ESTABLISHED && !established) return 0;
    if (rule->has_source && !fw_cidr_match(p->source, rule->source, rule->source_prefix)) return 0;
    if (rule->has_destination && !fw_cidr_match(p->destination, rule->destination, rule->destination_prefix)) return 0;
    if (rule->icmp_type && rule->icmp_type != p->icmp_type) return 0;
    if (rule->port && (p->protocol != FIREWALL_PROTOCOL_TCP && p->protocol != FIREWALL_PROTOCOL_UDP)) return 0;
    if (rule->port && (p->destination_port < rule->port || p->destination_port > rule->port_end)) return 0;
    if (rule->source_port && (p->source_port < rule->source_port || p->source_port > rule->source_port_end)) return 0;
    return 1;
}

static int fw_service_match(const fw_packet_t* p) {
    for (uint32_t i = 0; i < FIREWALL_SERVICE_LIMIT; i++) {
        const firewall_service_t* s = &firewall_services[i];
        if (s->active && s->protocol == p->protocol && s->port == p->destination_port) return 1;
    }
    return 0;
}

static void fw_rule_defaults(firewall_rule_t* rule) {
    memset(rule, 0, sizeof(*rule));
    rule->enabled = 1;
    rule->direction = FIREWALL_DIR_ANY;
    rule->source_prefix = 32;
    rule->destination_prefix = 32;
    rule->port_end = 0xFFFFu;
    rule->source_port_end = 0xFFFFu;
    rule->state = FIREWALL_STATE_ANY;
}

/* central decision engine */
static int fw_check(const uint8_t* frame, uint32_t length, uint8_t direction) {
    fw_packet_t p;
    int parsed = fw_parse(frame, length, &p);
    if (parsed == 2) {
        int allowed = firewall_default[fw_policy_index(direction)] == FIREWALL_ACTION_ALLOW;
        fw_count(direction, allowed ? FIREWALL_ACTION_ALLOW : FIREWALL_ACTION_DENY);
        return allowed;
    }
    if (parsed != 1) { fw_count(direction, FIREWALL_ACTION_DENY); return 0; }
    if (p.ether_type == 0x0806) { fw_count(direction, FIREWALL_ACTION_ALLOW); return 1; }

    int is_dhcp_reply = p.protocol == FIREWALL_PROTOCOL_UDP &&
                        p.source_port == 67 && p.destination_port == 68;
    int established = direction == FIREWALL_DIR_IN
                    ? fw_established_reply(&p, 0)
                    : fw_flow_known(&p);
    int matched = 0;
    int allowed = 0;
    int32_t matched_id = -1;
    for (uint32_t i = 0; i < FIREWALL_RULE_LIMIT; i++) {
        firewall_rule_t* rule = &firewall_rules[i];
        if (!fw_rule_match(rule, &p, direction, established)) continue;
        matched = 1;
        matched_id = (int32_t)i + 1;
        rule->hits++;
        allowed = rule->action == FIREWALL_ACTION_ALLOW;
        if (rule->rate_limit && fw_rule_rate_consume(i, rule->rate_limit, p.source)) allowed = 0;
        if (rule->log) fw_log_event(direction, allowed ? FIREWALL_ACTION_ALLOW : FIREWALL_ACTION_DENY, &p, matched_id);
        break;
    }
    if (!matched) {
        if (direction == FIREWALL_DIR_IN && is_dhcp_reply) allowed = 1;
        else if (direction == FIREWALL_DIR_IN && established) allowed = 1;
        else if (direction == FIREWALL_DIR_IN && fw_service_match(&p)) allowed = 1;
        else allowed = firewall_default[fw_policy_index(direction)] == FIREWALL_ACTION_ALLOW;
    }
    if (allowed && direction == FIREWALL_DIR_IN) {
        if (fw_established_reply(&p, 1)) established = 1;
    }
    if (allowed && !established && !is_dhcp_reply &&
        (direction == FIREWALL_DIR_IN || direction == FIREWALL_DIR_FORWARD) &&
        fw_is_new_traffic(&p) && fw_rate_consume(p.source)) allowed = 0;
    fw_count(direction, allowed ? FIREWALL_ACTION_ALLOW : FIREWALL_ACTION_DENY);
    if (!allowed) fw_log_event(direction, FIREWALL_ACTION_DENY, &p, matched_id);
    return allowed;
}

int firewall_input_allowed(const uint8_t* frame, uint32_t length) {
    return fw_check(frame, length, FIREWALL_DIR_IN);
}

int firewall_output_allowed(const uint8_t* frame, uint32_t length) {
    return fw_check(frame, length, FIREWALL_DIR_OUT);
}

int firewall_forward_allowed(const uint8_t* frame, uint32_t length) {
    return fw_check(frame, length, FIREWALL_DIR_FORWARD);
}

void firewall_init(void) {
    memset(firewall_rules, 0, sizeof(firewall_rules));
    memset(firewall_connections, 0, sizeof(firewall_connections));
    memset(firewall_rate_sources, 0, sizeof(firewall_rate_sources));
    memset(firewall_rule_rates, 0, sizeof(firewall_rule_rates));
    memset(firewall_services, 0, sizeof(firewall_services));
    memset(firewall_log, 0, sizeof(firewall_log));
    for (uint32_t i = 0; i < 3; i++) firewall_default[i] = FIREWALL_ACTION_ALLOW;
    memset(firewall_counters, 0, sizeof(firewall_counters));
    firewall_rate_limit = 0;
    firewall_rate_drops = 0;
    firewall_log_head = 0;
    firewall_log_used = 0;
    firewall_last_drop_log = 0xFFFFFFFFu;
    firewall_drop_logging = 1;
}

int firewall_add_rule(uint8_t action, uint8_t protocol, uint16_t port, const uint8_t* source) {
    firewall_rule_t rule;
    fw_rule_defaults(&rule);
    rule.action = action;
    rule.protocol = protocol;
    rule.port = port;
    rule.port_end = port ? port : 0xFFFFu;
    if (source) { rule.has_source = 1; memcpy(rule.source, source, 4); }
    return firewall_rule_add(&rule);
}

int firewall_rule_add(const firewall_rule_t* input) {
    firewall_rule_t normalized;
    const firewall_rule_t* spec = input;
    if (!spec) return -1;
    if (spec->action > FIREWALL_ACTION_ALLOW) return -1;
    if (spec->protocol != FIREWALL_PROTOCOL_ANY && spec->protocol != FIREWALL_PROTOCOL_ICMP &&
        spec->protocol != FIREWALL_PROTOCOL_TCP && spec->protocol != FIREWALL_PROTOCOL_UDP) return -1;
    if (spec->direction > FIREWALL_DIR_FORWARD || spec->state > FIREWALL_STATE_ESTABLISHED) return -1;
    if (spec->source_prefix > 32 || spec->destination_prefix > 32) return -1;
    if (spec->protocol == FIREWALL_PROTOCOL_ICMP) {
        normalized = *spec;
        normalized.port = 0;
        normalized.port_end = 0xFFFFu;
        normalized.source_port = 0;
        normalized.source_port_end = 0xFFFFu;
        spec = &normalized;
    }
    for (uint32_t i = 0; i < FIREWALL_RULE_LIMIT; i++) {
        if (firewall_rules[i].active) continue;
        firewall_rules[i] = *spec;
        firewall_rules[i].active = 1;
        if (!firewall_rules[i].enabled) firewall_rules[i].enabled = 1;
        if (!firewall_rules[i].port_end) firewall_rules[i].port_end = firewall_rules[i].port ? firewall_rules[i].port : 0xFFFFu;
        if (!firewall_rules[i].source_port_end) firewall_rules[i].source_port_end = firewall_rules[i].source_port ? firewall_rules[i].source_port : 0xFFFFu;
        firewall_rules[i].hits = 0;
        return (int)i + 1;
    }
    return -1;
}

int firewall_rule_get(uint32_t id, firewall_rule_t* out) {
    if (!id || id > FIREWALL_RULE_LIMIT || !out) return -1;
    const firewall_rule_t* rule = firewall_get_rule(id - 1);
    if (!rule) return -1;
    *out = *rule;
    return 0;
}

int firewall_rule_enable(uint32_t id, int enabled) {
    if (!id || id > FIREWALL_RULE_LIMIT || !firewall_rules[id - 1].active) return -1;
    firewall_rules[id - 1].enabled = enabled ? 1 : 0;
    return 0;
}

int firewall_rule_move(uint32_t id, int delta) {
    if (!id || id > FIREWALL_RULE_LIMIT || !delta) return -1;
    int target = (int)id - 1 + (delta > 0 ? 1 : -1);
    if (target < 0 || target >= FIREWALL_RULE_LIMIT || !firewall_rules[id - 1].active ||
        !firewall_rules[target].active) return -1;
    firewall_rule_t tmp = firewall_rules[id - 1];
    firewall_rules[id - 1] = firewall_rules[target];
    firewall_rules[target] = tmp;
    return 0;
}

int firewall_delete_rule(uint32_t id) {
    if (!id || id > FIREWALL_RULE_LIMIT || !firewall_rules[id - 1].active) return -1;
    for (uint32_t i = id - 1; i + 1 < FIREWALL_RULE_LIMIT; i++) firewall_rules[i] = firewall_rules[i + 1];
    memset(&firewall_rules[FIREWALL_RULE_LIMIT - 1], 0, sizeof(firewall_rules[0]));
    return 0;
}

void firewall_clear_rules(void) {
    memset(firewall_rules, 0, sizeof(firewall_rules));
}

int firewall_set_default(uint8_t action) {
    return firewall_set_policy(FIREWALL_DIR_IN, action);
}

uint8_t firewall_get_default(void) {
    return firewall_get_policy(FIREWALL_DIR_IN);
}

int firewall_set_policy(uint8_t direction, uint8_t action) {
    if (action > FIREWALL_ACTION_ALLOW) return -1;
    if (direction == FIREWALL_DIR_ANY) {
        for (uint32_t i = 0; i < 3; i++) firewall_default[i] = action;
        return 0;
    }
    if (direction < FIREWALL_DIR_IN || direction > FIREWALL_DIR_FORWARD) return -1;
    firewall_default[fw_policy_index(direction)] = action;
    return 0;
}

uint8_t firewall_get_policy(uint8_t direction) {
    return firewall_default[fw_policy_index(direction)];
}

const firewall_rule_t* firewall_get_rule(uint32_t index) {
    if (index >= FIREWALL_RULE_LIMIT || !firewall_rules[index].active) return 0;
    return &firewall_rules[index];
}

uint32_t firewall_rule_count(void) {
    uint32_t count = 0;
    for (uint32_t i = 0; i < FIREWALL_RULE_LIMIT; i++) if (firewall_rules[i].active) count++;
    return count;
}

int firewall_set_rate_limit(uint16_t packets_per_second) {
    if (packets_per_second > FIREWALL_RATE_MAX) return -1;
    firewall_rate_limit = packets_per_second;
    memset(firewall_rate_sources, 0, sizeof(firewall_rate_sources));
    memset(firewall_rule_rates, 0, sizeof(firewall_rule_rates));
    return 0;
}

uint16_t firewall_get_rate_limit(void) { return firewall_rate_limit; }
uint32_t firewall_rate_drop_count(void) { return firewall_rate_drops; }

int firewall_service_add(uint8_t protocol, uint16_t port) {
    if (!port || (protocol != FIREWALL_PROTOCOL_TCP && protocol != FIREWALL_PROTOCOL_UDP)) return -1;
    for (uint32_t i = 0; i < FIREWALL_SERVICE_LIMIT; i++) {
        if (firewall_services[i].active && firewall_services[i].protocol == protocol &&
            firewall_services[i].port == port) return (int)i + 1;
    }
    for (uint32_t i = 0; i < FIREWALL_SERVICE_LIMIT; i++) {
        if (firewall_services[i].active) continue;
        firewall_services[i].active = 1;
        firewall_services[i].protocol = protocol;
        firewall_services[i].port = port;
        return (int)i + 1;
    }
    return -1;
}

int firewall_service_remove(uint8_t protocol, uint16_t port) {
    for (uint32_t i = 0; i < FIREWALL_SERVICE_LIMIT; i++) {
        if (!firewall_services[i].active || firewall_services[i].protocol != protocol ||
            firewall_services[i].port != port) continue;
        memset(&firewall_services[i], 0, sizeof(firewall_services[i]));
        return 0;
    }
    return -1;
}

int firewall_service_get(uint32_t index, uint8_t* protocol, uint16_t* port) {
    if (index >= FIREWALL_SERVICE_LIMIT || !firewall_services[index].active) return -1;
    if (protocol) *protocol = firewall_services[index].protocol;
    if (port) *port = firewall_services[index].port;
    return 0;
}

uint32_t firewall_service_count(void) {
    uint32_t count = 0;
    for (uint32_t i = 0; i < FIREWALL_SERVICE_LIMIT; i++) if (firewall_services[i].active) count++;
    return count;
}

void firewall_clear_services(void) {
    memset(firewall_services, 0, sizeof(firewall_services));
}

static void fw_preset_rule(uint8_t action, uint8_t protocol, uint16_t port,
                           uint8_t direction, uint8_t state, const char* comment) {
    firewall_rule_t rule;
    fw_rule_defaults(&rule);
    rule.action = action;
    rule.protocol = protocol;
    rule.port = port;
    rule.port_end = port ? port : 0xFFFFu;
    rule.direction = direction;
    rule.state = state;
    if (comment) {
        uint32_t i = 0;
        while (comment[i] && i < FIREWALL_COMMENT_SIZE - 1) { rule.comment[i] = comment[i]; i++; }
    }
    firewall_rule_add(&rule);
}

int firewall_apply_preset(uint8_t preset) {
    if (preset > FIREWALL_PRESET_LOCKDOWN) return -1;
    firewall_clear_rules();
    firewall_reset_counters();
    firewall_set_rate_limit(0);
    if (preset == FIREWALL_PRESET_OPEN) {
        firewall_set_policy(FIREWALL_DIR_ANY, FIREWALL_ACTION_ALLOW);
        return 0;
    }
    firewall_set_policy(FIREWALL_DIR_ANY, FIREWALL_ACTION_DENY);
    fw_preset_rule(FIREWALL_ACTION_ALLOW, FIREWALL_PROTOCOL_ANY, 0, FIREWALL_DIR_IN,
                   FIREWALL_STATE_ESTABLISHED, "established inbound");
    fw_preset_rule(FIREWALL_ACTION_ALLOW, FIREWALL_PROTOCOL_ANY, 0, FIREWALL_DIR_FORWARD,
                   FIREWALL_STATE_ESTABLISHED, "established forward");
    fw_preset_rule(FIREWALL_ACTION_ALLOW, FIREWALL_PROTOCOL_ANY, 0, FIREWALL_DIR_OUT,
                   FIREWALL_STATE_ESTABLISHED, "established outbound");
    if (preset == FIREWALL_PRESET_SERVER) {
        firewall_set_policy(FIREWALL_DIR_OUT, FIREWALL_ACTION_ALLOW);
        firewall_set_policy(FIREWALL_DIR_FORWARD, FIREWALL_ACTION_ALLOW);
        fw_preset_rule(FIREWALL_ACTION_ALLOW, FIREWALL_PROTOCOL_ICMP, 0, FIREWALL_DIR_IN,
                       FIREWALL_STATE_ANY, "ping");
    }
    return 0;
}

uint32_t firewall_counter(uint8_t direction, uint8_t action) {
    return firewall_counters[fw_policy_index(direction)][action == FIREWALL_ACTION_ALLOW ? 1 : 0];
}

uint32_t firewall_hit_count(uint32_t id) {
    if (!id || id > FIREWALL_RULE_LIMIT || !firewall_rules[id - 1].active) return 0;
    return firewall_rules[id - 1].hits;
}

void firewall_reset_counters(void) {
    memset(firewall_counters, 0, sizeof(firewall_counters));
    firewall_rate_drops = 0;
    for (uint32_t i = 0; i < FIREWALL_RULE_LIMIT; i++) firewall_rules[i].hits = 0;
}

void firewall_set_log_drops(int enabled) { firewall_drop_logging = enabled ? 1 : 0; }
int firewall_get_log_drops(void) { return firewall_drop_logging; }
uint32_t firewall_log_count(void) { return firewall_log_used; }

const char* firewall_log_entry(uint32_t index) {
    if (index >= firewall_log_used) return 0;
    uint32_t pos = (firewall_log_head + FIREWALL_LOG_LIMIT - 1 - index) % FIREWALL_LOG_LIMIT;
    return firewall_log[pos];
}

void firewall_log_clear(void) {
    memset(firewall_log, 0, sizeof(firewall_log));
    firewall_log_head = 0;
    firewall_log_used = 0;
}

const char* firewall_direction_name(uint8_t direction) {
    if (direction == FIREWALL_DIR_IN) return "in";
    if (direction == FIREWALL_DIR_OUT) return "out";
    if (direction == FIREWALL_DIR_FORWARD) return "fwd";
    return "any";
}

const char* firewall_protocol_name(uint8_t protocol) {
    if (protocol == FIREWALL_PROTOCOL_TCP) return "tcp";
    if (protocol == FIREWALL_PROTOCOL_UDP) return "udp";
    if (protocol == FIREWALL_PROTOCOL_ICMP) return "icmp";
    return "any";
}

const char* firewall_action_name(uint8_t action) {
    return action == FIREWALL_ACTION_ALLOW ? "allow" : "deny";
}
