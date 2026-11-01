#include "kernel.h"
#include "firewall.h"

#define FIREWALL_CONNECTION_LIMIT 32
#define FIREWALL_CONNECTION_TTL (120u * TICKS_PER_SEC)
#define FIREWALL_RATE_SOURCE_LIMIT 64
#define FIREWALL_RATE_MAX 1000

typedef struct {
    uint8_t active;
    uint8_t protocol;
    uint8_t tcp_established;
    uint8_t local_ip[4];
    uint8_t remote_ip[4];
    uint16_t local_port;
    uint16_t remote_port;
    uint32_t expires;
} firewall_connection_t;

typedef struct {
    uint8_t active;
    uint8_t source[4];
    uint16_t count;
    uint32_t window;
} firewall_rate_source_t;

static firewall_rule_t firewall_rules[FIREWALL_RULE_LIMIT];
static firewall_connection_t firewall_connections[FIREWALL_CONNECTION_LIMIT];
static firewall_rate_source_t firewall_rate_sources[FIREWALL_RATE_SOURCE_LIMIT];
static uint8_t firewall_default = FIREWALL_ACTION_ALLOW;
static uint16_t firewall_rate_limit;
static uint32_t firewall_rate_drops;

static uint16_t firewall_get16(const uint8_t* value) {
    return (uint16_t)(((uint16_t)value[0] << 8) | value[1]);
}

static int firewall_equal4(const uint8_t* left, const uint8_t* right) {
    for (uint32_t i = 0; i < 4; i++) if (left[i] != right[i]) return 0;
    return 1;
}

static int firewall_checksum_valid(const uint8_t* header, uint32_t length) {
    uint32_t sum = 0;
    for (uint32_t i = 0; i < length; i += 2) sum += ((uint32_t)header[i] << 8) | header[i + 1];
    while (sum >> 16) sum = (sum & 0xFFFFu) + (sum >> 16);
    return sum == 0xFFFFu;
}

static int firewall_parse_ipv4(const uint8_t* frame, uint32_t length, const uint8_t** ip, uint32_t* ihl, uint32_t* total) {
    if (length < 34 || firewall_get16(frame + 12) != 0x0800 || (frame[14] >> 4) != 4) return 0;
    *ihl = (uint32_t)(frame[14] & 0x0F) * 4;
    *total = firewall_get16(frame + 16);
    if (*ihl < 20 || *total < *ihl || *total > length - 14) return 0;
    *ip = frame + 14;
    return firewall_checksum_valid(*ip, *ihl);
}

static int firewall_is_fragmented(const uint8_t* ip) {
    return (ip[6] & 0xBF) != 0 || ip[7] != 0;
}

static int firewall_transport_info(const uint8_t* ip, uint32_t ihl, uint32_t total, uint16_t* source_port, uint16_t* destination_port, uint8_t* tcp_flags) {
    uint8_t protocol = ip[9];
    uint32_t payload_length = total - ihl;
    const uint8_t* transport = ip + ihl;
    *source_port = 0;
    *destination_port = 0;
    *tcp_flags = 0;
    if (protocol == FIREWALL_PROTOCOL_TCP) {
        if (payload_length < 20) return 0;
        uint32_t header_length = (uint32_t)(transport[12] >> 4) * 4;
        if (header_length < 20 || header_length > payload_length) return 0;
        *source_port = firewall_get16(transport);
        *destination_port = firewall_get16(transport + 2);
        *tcp_flags = transport[13];
    } else if (protocol == FIREWALL_PROTOCOL_UDP) {
        if (payload_length < 8 || firewall_get16(transport + 4) != payload_length) return 0;
        *source_port = firewall_get16(transport);
        *destination_port = firewall_get16(transport + 2);
    } else if (protocol == FIREWALL_PROTOCOL_ICMP) {
        if (payload_length < 8) return 0;
    }
    return 1;
}

static int firewall_consume_rate(const uint8_t* source) {
    if (!firewall_rate_limit) return 0;
    uint32_t window = uptime_ticks / TICKS_PER_SEC;
    firewall_rate_source_t* slot = 0;
    for (uint32_t i = 0; i < FIREWALL_RATE_SOURCE_LIMIT; i++) {
        firewall_rate_source_t* entry = &firewall_rate_sources[i];
        if (entry->active && entry->window != window) entry->active = 0;
        if (!entry->active) {
            if (!slot) slot = entry;
            continue;
        }
        if (firewall_equal4(entry->source, source)) {
            slot = entry;
            break;
        }
    }
    if (!slot) {
        firewall_rate_drops++;
        return 1;
    }
    if (!slot->active) {
        slot->active = 1;
        memcpy(slot->source, source, 4);
        slot->count = 0;
        slot->window = window;
    }
    if (slot->count >= firewall_rate_limit) {
        firewall_rate_drops++;
        return 1;
    }
    slot->count++;
    return 0;
}

static int firewall_is_rate_limited_traffic(const uint8_t* ip, uint32_t ihl, uint8_t protocol, uint8_t tcp_flags, int established, int dhcp_reply) {
    if (established || dhcp_reply) return 0;
    if (protocol == FIREWALL_PROTOCOL_TCP) return (tcp_flags & 0x02) && !(tcp_flags & (0x10 | 0x04));
    if (protocol == FIREWALL_PROTOCOL_UDP) return 1;
    if (protocol == FIREWALL_PROTOCOL_ICMP) {
        const uint8_t* icmp = ip + ihl;
        return icmp[0] == 8 && icmp[1] == 0;
    }
    return 0;
}

static void firewall_expire_connections(void) {
    for (uint32_t i = 0; i < FIREWALL_CONNECTION_LIMIT; i++) {
        if (firewall_connections[i].active && (int32_t)(uptime_ticks - firewall_connections[i].expires) >= 0) firewall_connections[i].active = 0;
    }
}

void firewall_init(void) {
    memset(firewall_rules, 0, sizeof(firewall_rules));
    memset(firewall_connections, 0, sizeof(firewall_connections));
    memset(firewall_rate_sources, 0, sizeof(firewall_rate_sources));
    firewall_default = FIREWALL_ACTION_ALLOW;
    firewall_rate_limit = 0;
    firewall_rate_drops = 0;
}

void firewall_track_outbound(const uint8_t* frame, uint32_t length) {
    if (!frame || length < 14) return;
    const uint8_t* ip;
    uint32_t ihl;
    uint32_t total;
    if (!firewall_parse_ipv4(frame, length, &ip, &ihl, &total) || firewall_is_fragmented(ip)) return;
    uint8_t protocol = ip[9];
    uint16_t local_port;
    uint16_t remote_port;
    uint8_t tcp_flags;
    if (!firewall_transport_info(ip, ihl, total, &local_port, &remote_port, &tcp_flags)) return;
    if (protocol == FIREWALL_PROTOCOL_TCP) {
        if (!(tcp_flags & 0x02) || (tcp_flags & (0x10 | 0x04))) {
            firewall_expire_connections();
            for (uint32_t i = 0; i < FIREWALL_CONNECTION_LIMIT; i++) {
                firewall_connection_t* connection = &firewall_connections[i];
                if (!connection->active || connection->protocol != protocol || connection->local_port != local_port || connection->remote_port != remote_port) continue;
                if (!firewall_equal4(connection->local_ip, ip + 12) || !firewall_equal4(connection->remote_ip, ip + 16)) continue;
                if (tcp_flags & 0x04) connection->active = 0;
                else connection->expires = uptime_ticks + FIREWALL_CONNECTION_TTL;
                return;
            }
            return;
        }
    } else if (protocol == FIREWALL_PROTOCOL_ICMP) {
        const uint8_t* icmp = ip + ihl;
        if (icmp[0] != 8) return;
        local_port = firewall_get16(icmp + 4);
        remote_port = 0;
    } else if (protocol != FIREWALL_PROTOCOL_UDP) return;
    firewall_expire_connections();
    firewall_connection_t* slot = 0;
    for (uint32_t i = 0; i < FIREWALL_CONNECTION_LIMIT; i++) {
        firewall_connection_t* connection = &firewall_connections[i];
        if (!connection->active) {
            if (!slot) slot = connection;
            continue;
        }
        if (connection->protocol == protocol && connection->local_port == local_port && connection->remote_port == remote_port && firewall_equal4(connection->local_ip, ip + 12) && firewall_equal4(connection->remote_ip, ip + 16)) {
            connection->expires = uptime_ticks + FIREWALL_CONNECTION_TTL;
            return;
        }
    }
    if (!slot) return;
    memset(slot, 0, sizeof(*slot));
    slot->active = 1;
    slot->protocol = protocol;
    memcpy(slot->local_ip, ip + 12, 4);
    memcpy(slot->remote_ip, ip + 16, 4);
    slot->local_port = local_port;
    slot->remote_port = remote_port;
    slot->expires = uptime_ticks + FIREWALL_CONNECTION_TTL;
}

static int firewall_is_established_reply(const uint8_t* ip, uint32_t ihl, uint16_t source_port, uint16_t destination_port, uint8_t tcp_flags) {
    uint8_t protocol = ip[9];
    uint16_t local_port = destination_port;
    uint16_t remote_port = source_port;
    if (protocol == FIREWALL_PROTOCOL_ICMP) {
        const uint8_t* icmp = ip + ihl;
        if (icmp[0] != 0) return 0;
        local_port = firewall_get16(icmp + 4);
        remote_port = 0;
    } else if (protocol != FIREWALL_PROTOCOL_TCP && protocol != FIREWALL_PROTOCOL_UDP) return 0;
    firewall_expire_connections();
    for (uint32_t i = 0; i < FIREWALL_CONNECTION_LIMIT; i++) {
        firewall_connection_t* connection = &firewall_connections[i];
        if (!connection->active || connection->protocol != protocol || connection->local_port != local_port || connection->remote_port != remote_port) continue;
        if (!firewall_equal4(connection->remote_ip, ip + 12) || !firewall_equal4(connection->local_ip, ip + 16)) continue;
        if (protocol == FIREWALL_PROTOCOL_TCP && !connection->tcp_established) {
            if (tcp_flags & 0x04) connection->active = 0;
            else if ((tcp_flags & 0x12) == 0x12) connection->tcp_established = 1;
            else return 0;
        } else if (protocol == FIREWALL_PROTOCOL_TCP && !(tcp_flags & 0x10) && !(tcp_flags & 0x04)) return 0;
        connection->expires = uptime_ticks + FIREWALL_CONNECTION_TTL;
        return 1;
    }
    return 0;
}

int firewall_input_allowed(const uint8_t* frame, uint32_t length) {
    if (!frame || length < 14) return 0;
    uint16_t ether_type = firewall_get16(frame + 12);
    if (ether_type == 0x0806) return 1;
    if (ether_type != 0x0800) return firewall_default == FIREWALL_ACTION_ALLOW;
    const uint8_t* ip;
    uint32_t ihl;
    uint32_t total;
    if (!firewall_parse_ipv4(frame, length, &ip, &ihl, &total) || firewall_is_fragmented(ip)) return 0;
    uint8_t protocol = ip[9];
    uint16_t source_port;
    uint16_t destination_port;
    uint8_t tcp_flags;
    if (!firewall_transport_info(ip, ihl, total, &source_port, &destination_port, &tcp_flags)) return 0;
    const uint8_t* source = ip + 12;
    int is_dhcp_reply = protocol == FIREWALL_PROTOCOL_UDP && source_port == 67 && destination_port == 68;
    int matched = 0;
    int allowed = 0;
    int established = 0;
    for (uint32_t i = 0; i < FIREWALL_RULE_LIMIT; i++) {
        const firewall_rule_t* rule = &firewall_rules[i];
        if (!rule->active) continue;
        if (rule->protocol != FIREWALL_PROTOCOL_ANY && rule->protocol != protocol) continue;
        if (rule->has_source && !firewall_equal4(rule->source, source)) continue;
        if (rule->port && protocol != FIREWALL_PROTOCOL_TCP && protocol != FIREWALL_PROTOCOL_UDP) continue;
        if ((protocol == FIREWALL_PROTOCOL_TCP || protocol == FIREWALL_PROTOCOL_UDP) && rule->port && rule->port != destination_port) continue;
        matched = 1;
        allowed = rule->action == FIREWALL_ACTION_ALLOW;
        break;
    }
    if (!matched) {
        if (is_dhcp_reply) allowed = 1;
        else if (firewall_is_established_reply(ip, ihl, source_port, destination_port, tcp_flags)) {
            allowed = 1;
            established = 1;
        } else allowed = firewall_default == FIREWALL_ACTION_ALLOW;
    } else if (allowed) established = firewall_is_established_reply(ip, ihl, source_port, destination_port, tcp_flags);
    if (!allowed) return 0;
    if (firewall_is_rate_limited_traffic(ip, ihl, protocol, tcp_flags, established, is_dhcp_reply) && firewall_consume_rate(source)) return 0;
    return 1;
}

int firewall_add_rule(uint8_t action, uint8_t protocol, uint16_t port, const uint8_t* source) {
    if (action > FIREWALL_ACTION_ALLOW) return -1;
    if (protocol != FIREWALL_PROTOCOL_ANY && protocol != FIREWALL_PROTOCOL_ICMP && protocol != FIREWALL_PROTOCOL_TCP && protocol != FIREWALL_PROTOCOL_UDP) return -1;
    if (protocol == FIREWALL_PROTOCOL_ICMP) port = 0;
    for (uint32_t i = 0; i < FIREWALL_RULE_LIMIT; i++) {
        if (firewall_rules[i].active) continue;
        firewall_rule_t* rule = &firewall_rules[i];
        memset(rule, 0, sizeof(*rule));
        rule->active = 1;
        rule->action = action;
        rule->protocol = protocol;
        rule->port = port;
        if (source) {
            rule->has_source = 1;
            memcpy(rule->source, source, 4);
        }
        return (int)i + 1;
    }
    return -1;
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
    if (action > FIREWALL_ACTION_ALLOW) return -1;
    firewall_default = action;
    return 0;
}

uint8_t firewall_get_default(void) {
    return firewall_default;
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
    return 0;
}

uint16_t firewall_get_rate_limit(void) {
    return firewall_rate_limit;
}

uint32_t firewall_rate_drop_count(void) {
    return firewall_rate_drops;
}
