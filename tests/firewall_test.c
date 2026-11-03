#include "kernel.h"
#include "firewall.h"
#include <stdio.h>

volatile uint32_t uptime_ticks;

static void put16(uint8_t* p, uint16_t value) {
    p[0] = (uint8_t)(value >> 8);
    p[1] = (uint8_t)value;
}

static uint16_t checksum16(const uint8_t* data, uint32_t length) {
    uint32_t sum = 0;
    for (uint32_t i = 0; i < length; i += 2) sum += ((uint32_t)data[i] << 8) | data[i + 1];
    while (sum >> 16) sum = (sum & 0xFFFFu) + (sum >> 16);
    return (uint16_t)~sum;
}

static void update_ip_checksum(uint8_t* ip) {
    ip[10] = 0;
    ip[11] = 0;
    put16(ip + 10, checksum16(ip, (uint32_t)(ip[0] & 0x0F) * 4));
}

static void make_ipv4(uint8_t* frame, uint8_t protocol, const uint8_t* source, const uint8_t* destination, uint16_t source_port, uint16_t destination_port) {
    memset(frame, 0, 64);
    frame[12] = 0x08;
    frame[13] = 0x00;
    uint8_t* ip = frame + 14;
    uint8_t* transport = ip + 20;
    uint16_t transport_length = protocol == FIREWALL_PROTOCOL_TCP ? 20 : 8;
    ip[0] = 0x45;
    put16(ip + 2, (uint16_t)(20 + transport_length));
    ip[8] = 64;
    ip[9] = protocol;
    memcpy(ip + 12, source, 4);
    memcpy(ip + 16, destination, 4);
    if (protocol == FIREWALL_PROTOCOL_TCP) {
        transport[12] = 0x50;
        put16(transport, source_port);
        put16(transport + 2, destination_port);
    } else if (protocol == FIREWALL_PROTOCOL_UDP) {
        put16(transport, source_port);
        put16(transport + 2, destination_port);
        put16(transport + 4, 8);
    } else if (protocol == FIREWALL_PROTOCOL_ICMP) {
        transport[0] = 8;
        put16(transport + 4, source_port);
        put16(transport + 6, destination_port);
    }
    update_ip_checksum(ip);
}

int main(void) {
    uint8_t frame[64];
    uint8_t remote[4] = {203,0,113,9};
    uint8_t host[4] = {192,168,1,10};
    firewall_init();
    make_ipv4(frame, FIREWALL_PROTOCOL_TCP, remote, host, 40000, 22);
    if (!firewall_input_allowed(frame, sizeof(frame))) return 1;
    if (firewall_set_default(FIREWALL_ACTION_DENY)) return 2;
    if (firewall_input_allowed(frame, sizeof(frame))) return 3;
    int deny_id = firewall_add_rule(FIREWALL_ACTION_DENY, FIREWALL_PROTOCOL_TCP, 22, NULL);
    if (deny_id != 1 || firewall_input_allowed(frame, sizeof(frame))) return 4;
    if (firewall_rule_count() != 1 || !firewall_get_rule(0)) return 5;
    if (firewall_delete_rule(1) || firewall_rule_count()) return 6;
    int allow_id = firewall_add_rule(FIREWALL_ACTION_ALLOW, FIREWALL_PROTOCOL_TCP, 22, remote);
    if (allow_id != 1 || !firewall_input_allowed(frame, sizeof(frame))) return 7;
    make_ipv4(frame, FIREWALL_PROTOCOL_TCP, (uint8_t[]){198,51,100,2}, host, 40000, 22);
    if (firewall_input_allowed(frame, sizeof(frame))) return 8;
    make_ipv4(frame, FIREWALL_PROTOCOL_TCP, remote, host, 40000, 80);
    if (firewall_input_allowed(frame, sizeof(frame))) return 9;

    firewall_init();
    firewall_set_default(FIREWALL_ACTION_DENY);
    make_ipv4(frame, FIREWALL_PROTOCOL_TCP, host, remote, 51515, 443);
    frame[14 + 20 + 13] = 0x02;
    firewall_track_outbound(frame, sizeof(frame));
    make_ipv4(frame, FIREWALL_PROTOCOL_TCP, remote, host, 443, 51515);
    frame[14 + 20 + 13] = 0x12;
    if (!firewall_input_allowed(frame, sizeof(frame))) return 10;
    make_ipv4(frame, FIREWALL_PROTOCOL_TCP, remote, host, 443, 22222);
    frame[14 + 20 + 13] = 0x12;
    if (firewall_input_allowed(frame, sizeof(frame))) return 11;

    firewall_init();
    firewall_set_default(FIREWALL_ACTION_DENY);
    make_ipv4(frame, FIREWALL_PROTOCOL_TCP, host, remote, 51515, 443);
    firewall_track_outbound(frame, sizeof(frame));
    make_ipv4(frame, FIREWALL_PROTOCOL_TCP, remote, host, 443, 51515);
    frame[14 + 20 + 13] = 0x12;
    if (firewall_input_allowed(frame, sizeof(frame))) return 12;

    firewall_init();
    firewall_set_default(FIREWALL_ACTION_DENY);
    make_ipv4(frame, FIREWALL_PROTOCOL_UDP, host, remote, 53000, 53);
    firewall_track_outbound(frame, sizeof(frame));
    make_ipv4(frame, FIREWALL_PROTOCOL_UDP, remote, host, 53, 53000);
    if (!firewall_input_allowed(frame, sizeof(frame))) return 13;
    make_ipv4(frame, FIREWALL_PROTOCOL_UDP, remote, host, 53, 53001);
    if (firewall_input_allowed(frame, sizeof(frame))) return 14;

    firewall_init();
    firewall_set_default(FIREWALL_ACTION_DENY);
    make_ipv4(frame, FIREWALL_PROTOCOL_ICMP, host, remote, 0x1234, 1);
    firewall_track_outbound(frame, sizeof(frame));
    make_ipv4(frame, FIREWALL_PROTOCOL_ICMP, remote, host, 0x1234, 1);
    frame[14 + 20] = 0;
    if (!firewall_input_allowed(frame, sizeof(frame))) return 15;
    make_ipv4(frame, FIREWALL_PROTOCOL_ICMP, remote, host, 0x5678, 1);
    frame[14 + 20] = 0;
    if (firewall_input_allowed(frame, sizeof(frame))) return 16;

    firewall_init();
    make_ipv4(frame, FIREWALL_PROTOCOL_TCP, remote, host, 40000, 22);
    frame[14 + 8] ^= 1;
    if (firewall_input_allowed(frame, sizeof(frame))) return 17;
    make_ipv4(frame, FIREWALL_PROTOCOL_TCP, remote, host, 40000, 22);
    frame[14 + 6] = 0x20;
    update_ip_checksum(frame + 14);
    if (firewall_input_allowed(frame, sizeof(frame))) return 18;

    memset(frame, 0, sizeof(frame));
    frame[12] = 0x08;
    frame[13] = 0x06;
    firewall_set_default(FIREWALL_ACTION_DENY);
    if (!firewall_input_allowed(frame, sizeof(frame))) return 19;
    make_ipv4(frame, FIREWALL_PROTOCOL_UDP, remote, host, 67, 68);
    if (!firewall_input_allowed(frame, sizeof(frame))) return 20;
    make_ipv4(frame, FIREWALL_PROTOCOL_UDP, remote, host, 1234, 68);
    if (firewall_input_allowed(frame, sizeof(frame))) return 21;
    if (firewall_add_rule(FIREWALL_ACTION_DENY, FIREWALL_PROTOCOL_UDP, 68, NULL) < 0) return 22;
    make_ipv4(frame, FIREWALL_PROTOCOL_UDP, remote, host, 67, 68);
    if (firewall_input_allowed(frame, sizeof(frame))) return 23;

    firewall_init();
    if (firewall_set_rate_limit(1001) == 0 || firewall_set_rate_limit(1) != 0 || firewall_get_rate_limit() != 1) return 24;
    make_ipv4(frame, FIREWALL_PROTOCOL_TCP, remote, host, 40000, 22);
    frame[14 + 20 + 13] = 0x02;
    if (!firewall_input_allowed(frame, sizeof(frame))) return 25;
    make_ipv4(frame, FIREWALL_PROTOCOL_UDP, remote, host, 40000, 53);
    if (firewall_input_allowed(frame, sizeof(frame))) return 26;
    make_ipv4(frame, FIREWALL_PROTOCOL_UDP, (uint8_t[]){198,51,100,4}, host, 40001, 53);
    if (!firewall_input_allowed(frame, sizeof(frame))) return 27;
    if (firewall_rate_drop_count() != 1) return 28;
    uptime_ticks += TICKS_PER_SEC;
    make_ipv4(frame, FIREWALL_PROTOCOL_UDP, remote, host, 40000, 53);
    if (!firewall_input_allowed(frame, sizeof(frame))) return 29;
    make_ipv4(frame, FIREWALL_PROTOCOL_UDP, remote, host, 40000, 53);
    if (firewall_input_allowed(frame, sizeof(frame))) return 30;
    if (firewall_set_rate_limit(0) != 0 || firewall_get_rate_limit() != 0) return 31;

    /* ---- extended rule engine: CIDR, port ranges, direction policies ---- */
    firewall_init();
    firewall_rule_t spec;
    memset(&spec, 0, sizeof(spec));
    spec.enabled = 1;
    spec.action = FIREWALL_ACTION_DENY;
    spec.protocol = FIREWALL_PROTOCOL_TCP;
    spec.direction = FIREWALL_DIR_IN;
    spec.has_source = 1;
    spec.source[0] = 10;
    spec.source_prefix = 8;
    spec.port = 443;
    spec.port_end = 443;
    spec.source_port_end = 0xFFFFu;
    if (firewall_rule_add(&spec) < 0) return 40;
    make_ipv4(frame, FIREWALL_PROTOCOL_TCP, (uint8_t[]){10,1,2,3}, host, 40000, 443);
    if (firewall_input_allowed(frame, sizeof(frame))) return 41;
    make_ipv4(frame, FIREWALL_PROTOCOL_TCP, (uint8_t[]){11,1,2,3}, host, 40000, 443);
    if (!firewall_input_allowed(frame, sizeof(frame))) return 42;
    if (firewall_rule_get(1, &spec) != 0 || spec.source_prefix != 8) return 43;

    firewall_init();
    firewall_set_policy(FIREWALL_DIR_IN, FIREWALL_ACTION_DENY);
    memset(&spec, 0, sizeof(spec));
    spec.enabled = 1;
    spec.action = FIREWALL_ACTION_ALLOW;
    spec.protocol = FIREWALL_PROTOCOL_TCP;
    spec.direction = FIREWALL_DIR_IN;
    spec.port = 8000;
    spec.port_end = 8100;
    spec.source_port_end = 0xFFFFu;
    if (firewall_rule_add(&spec) < 0) return 44;
    make_ipv4(frame, FIREWALL_PROTOCOL_TCP, remote, host, 40000, 8050);
    if (!firewall_input_allowed(frame, sizeof(frame))) return 45;
    make_ipv4(frame, FIREWALL_PROTOCOL_TCP, remote, host, 40000, 9000);
    if (firewall_input_allowed(frame, sizeof(frame))) return 46;

    firewall_init();
    firewall_set_policy(FIREWALL_DIR_OUT, FIREWALL_ACTION_DENY);
    firewall_set_policy(FIREWALL_DIR_FORWARD, FIREWALL_ACTION_DENY);
    make_ipv4(frame, FIREWALL_PROTOCOL_UDP, host, remote, 1234, 9999);
    if (firewall_output_allowed(frame, sizeof(frame))) return 47;
    if (firewall_forward_allowed(frame, sizeof(frame))) return 48;
    firewall_set_policy(FIREWALL_DIR_OUT, FIREWALL_ACTION_ALLOW);
    if (!firewall_output_allowed(frame, sizeof(frame))) return 49;

    /* ---- implicit service ports and explicit-rule precedence ---- */
    firewall_init();
    firewall_set_policy(FIREWALL_DIR_IN, FIREWALL_ACTION_DENY);
    if (firewall_service_add(FIREWALL_PROTOCOL_UDP, 5353) < 0) return 50;
    make_ipv4(frame, FIREWALL_PROTOCOL_UDP, remote, host, 40000, 5353);
    if (!firewall_input_allowed(frame, sizeof(frame))) return 51;
    if (firewall_add_rule(FIREWALL_ACTION_DENY, FIREWALL_PROTOCOL_UDP, 5353, NULL) < 0) return 52;
    if (firewall_input_allowed(frame, sizeof(frame))) return 53;
    if (firewall_service_remove(FIREWALL_PROTOCOL_UDP, 5353) != 0) return 54;
    if (firewall_service_count() != 0) return 55;

    /* ---- enable/disable and rule ordering ---- */
    firewall_init();
    if (firewall_add_rule(FIREWALL_ACTION_DENY, FIREWALL_PROTOCOL_TCP, 0, NULL) != 1) return 56;
    if (firewall_add_rule(FIREWALL_ACTION_ALLOW, FIREWALL_PROTOCOL_TCP, 22, NULL) != 2) return 57;
    make_ipv4(frame, FIREWALL_PROTOCOL_TCP, remote, host, 40000, 22);
    if (firewall_input_allowed(frame, sizeof(frame))) return 58;
    if (firewall_rule_enable(1, 0) != 0) return 59;
    if (!firewall_input_allowed(frame, sizeof(frame))) return 60;
    if (firewall_rule_enable(1, 1) != 0) return 61;
    if (firewall_rule_move(1, 1) != 0) return 62;
    if (!firewall_input_allowed(frame, sizeof(frame))) return 63;
    if (firewall_hit_count(1) < 1) return 64;

    /* ---- presets, counters and drop logging ---- */
    firewall_init();
    if (firewall_apply_preset(FIREWALL_PRESET_SERVER) != 0) return 65;
    if (firewall_get_policy(FIREWALL_DIR_IN) != FIREWALL_ACTION_DENY) return 66;
    if (firewall_get_policy(FIREWALL_DIR_OUT) != FIREWALL_ACTION_ALLOW) return 67;
    if (firewall_rule_count() < 3) return 68;
    firewall_init();
    firewall_set_policy(FIREWALL_DIR_IN, FIREWALL_ACTION_DENY);
    make_ipv4(frame, FIREWALL_PROTOCOL_TCP, remote, host, 40000, 22);
    if (firewall_input_allowed(frame, sizeof(frame))) return 69;
    if (firewall_counter(FIREWALL_DIR_IN, FIREWALL_ACTION_DENY) < 1) return 70;
    if (firewall_log_count() < 1 || !firewall_log_entry(0)) return 71;
    firewall_log_clear();
    if (firewall_log_count() != 0) return 72;

    puts("IPv4 firewall validation, ordered rules, rate limiting, DHCP, fragments and state tracking passed");
    return 0;
}
