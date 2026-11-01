#include "kernel.h"
#include "net.h"
#include <stdio.h>

volatile uint32_t uptime_ticks;
uint8_t net_mac[6] = {0x02,0xAA,0xBB,0xCC,0xDD,0x01};
uint8_t net_ip[4] = {192,168,1,20};
uint8_t net_mask[4] = {255,255,255,0};
uint8_t net_gw[4] = {192,168,1,1};
uint8_t net_dns[4] = {1,1,1,1};
int net_configured;
static uint8_t last_frame[1600];
static uint32_t last_length;
static int simulate_ping;
static int simulate_ping_stage;
static int simulate_bridge_noise;

void net_send_raw(const uint8_t* data, uint32_t len) {
    if (len <= sizeof(last_frame)) {
        memcpy(last_frame, data, len);
        last_length = len;
    }
}

int net_arp_resolve(const uint8_t* ip, uint8_t* mac_out) {
    if (ip[0] != 192 || ip[1] != 168 || ip[2] != 1 || ip[3] != 1) return 0;
    memcpy(mac_out, (uint8_t[]){0x02,0xAA,0xBB,0xCC,0xDD,0xFE}, 6);
    return 1;
}

static int equal(const uint8_t* a, const uint8_t* b, uint32_t length) {
    for (uint32_t i = 0; i < length; i++) if (a[i] != b[i]) return 0;
    return 1;
}

static void put16(uint8_t* p, uint16_t value) {
    p[0] = (uint8_t)(value >> 8);
    p[1] = (uint8_t)value;
}

static void put32(uint8_t* p, uint32_t value) {
    p[0] = (uint8_t)(value >> 24);
    p[1] = (uint8_t)(value >> 16);
    p[2] = (uint8_t)(value >> 8);
    p[3] = (uint8_t)value;
}

static uint16_t checksum(const uint8_t* bytes, uint32_t length) {
    uint32_t sum = 0;
    for (uint32_t i = 0; i + 1 < length; i += 2) sum += ((uint32_t)bytes[i] << 8) | bytes[i + 1];
    if (length & 1) sum += (uint32_t)bytes[length - 1] << 8;
    while (sum >> 16) sum = (sum & 0xFFFFu) + (sum >> 16);
    return (uint16_t)~sum;
}

void delay_ms(uint32_t ms) {
    uptime_ticks += ms;
}

void terminal_writestring(const char* text) {
    (void)text;
}

void int_to_string(uint32_t value, char* buffer) {
    char reverse[12];
    uint32_t length = 0;
    do {
        reverse[length++] = (char)('0' + value % 10);
        value /= 10;
    } while (value);
    for (uint32_t i = 0; i < length; i++) buffer[i] = reverse[length - i - 1];
    buffer[length] = 0;
}

void net_poll(void) {
    if (!simulate_ping || !last_length) return;
    if (simulate_ping_stage == 0 && last_length >= 42 && last_frame[12] == 8 && last_frame[13] == 6 && last_frame[20] == 0 && last_frame[21] == 1) {
        if (simulate_bridge_noise) {
            for (uint32_t i = 0; i < 64; i++) {
                uint8_t noise[42] = {0};
                memset(noise, 0xFF, 6);
                noise[12] = 8;
                noise[13] = 6;
                put16(noise + 14, 1);
                put16(noise + 16, 0x0800);
                noise[18] = 6;
                noise[19] = 4;
                put16(noise + 20, 1);
                memcpy(noise + 22, (uint8_t[]){0x02,0x10,0x20,0x30,0x40,0x50}, 6);
                memcpy(noise + 28, (uint8_t[]){192,168,10,5}, 4);
                memcpy(noise + 38, (uint8_t[]){192,168,10,99}, 4);
                net_vnic_rx_frame(noise, sizeof(noise));
            }
        }
        uint8_t reply[42] = {0};
        memcpy(reply, last_frame + 6, 6);
        uint8_t peer_mac[6] = {0x02,0x19,0x28,0x37,0x46,0x55};
        memcpy(reply + 6, peer_mac, 6);
        reply[12] = 8;
        reply[13] = 6;
        uint8_t* arp = reply + 14;
        put16(arp, 1);
        put16(arp + 2, 0x0800);
        arp[4] = 6;
        arp[5] = 4;
        put16(arp + 6, 2);
        memcpy(arp + 8, peer_mac, 6);
        memcpy(arp + 14, last_frame + 38, 4);
        memcpy(arp + 18, last_frame + 22, 6);
        memcpy(arp + 24, last_frame + 28, 4);
        net_vnic_rx_frame(reply, sizeof(reply));
        simulate_ping_stage = 1;
        return;
    }
    if (simulate_ping_stage == 1 && last_length >= 42 && last_frame[12] == 8 && last_frame[13] == 0 && last_frame[23] == 1 && last_frame[34] == 8) {
        uint8_t reply[1600];
        memcpy(reply, last_frame, last_length);
        memcpy(reply, last_frame + 6, 6);
        memcpy(reply + 6, last_frame, 6);
        uint8_t* ip = reply + 14;
        uint8_t temp_ip[4];
        memcpy(temp_ip, ip + 12, 4);
        memcpy(ip + 12, ip + 16, 4);
        memcpy(ip + 16, temp_ip, 4);
        ip[10] = 0;
        ip[11] = 0;
        put16(ip + 10, checksum(ip, 20));
        reply[34] = 0;
        reply[36] = 0;
        reply[37] = 0;
        put16(reply + 36, checksum(reply + 34, last_length - 34));
        net_vnic_rx_frame(reply, last_length);
        simulate_ping_stage = 2;
    }
}

static uint16_t transport_checksum(const uint8_t* ip, const uint8_t* transport, uint32_t length, uint8_t protocol) {
    uint32_t sum = 0;
    for (uint32_t i = 12; i < 20; i += 2) sum += ((uint32_t)ip[i] << 8) | ip[i + 1];
    sum += protocol;
    sum += length;
    for (uint32_t i = 0; i + 1 < length; i += 2) sum += ((uint32_t)transport[i] << 8) | transport[i + 1];
    if (length & 1) sum += (uint32_t)transport[length - 1] << 8;
    while (sum >> 16) sum = (sum & 0xFFFFu) + (sum >> 16);
    return (uint16_t)~sum;
}

static void option4(uint8_t* p, uint32_t* offset, uint8_t code, const uint8_t* value) {
    p[(*offset)++] = code;
    p[(*offset)++] = 4;
    memcpy(p + *offset, value, 4);
    *offset += 4;
}

int main(void) {
    uint8_t mac[6] = {0x02,0x53,0x48,0x01,0x12,0x34};
    uint8_t got_mac[6], ip[4], mask[4], gateway[4], dns[4];
    uint32_t state = 0;
    uptime_ticks = 100;
    if (net_vnic_attach(0, mac) || net_vnic_dhcp_start(0)) return 1;
    if (last_length != 342 || last_frame[42] != 1 || last_frame[44] != 6) return 2;

    uint8_t xid[4];
    memcpy(xid, last_frame + 46, 4);
    uint8_t reply[342];
    memcpy(reply, last_frame, sizeof(reply));
    memcpy(reply, mac, 6);
    uint8_t server_mac[6] = {0x02,0xAA,0xBB,0xCC,0xDD,0xEE};
    memcpy(reply + 6, server_mac, 6);
    uint8_t* ip_header = reply + 14;
    memcpy(ip_header + 12, (uint8_t[]){192,168,10,1}, 4);
    memset(ip_header + 16, 255, 4);
    uint8_t* udp = ip_header + 20;
    put16(udp, 67);
    put16(udp + 2, 68);
    put16(udp + 4, 308);
    uint8_t* bootp = udp + 8;
    bootp[0] = 2;
    memcpy(bootp + 4, xid, 4);
    memcpy(bootp + 16, (uint8_t[]){192,168,10,77}, 4);
    memcpy(bootp + 28, mac, 6);
    memset(bootp + 240, 0, 60);
    uint32_t option = 240;
    bootp[option++] = 53;
    bootp[option++] = 1;
    bootp[option++] = 2;
    option4(bootp, &option, 1, (uint8_t[]){255,255,255,0});
    bootp[option++] = 3;
    bootp[option++] = 4;
    memcpy(bootp + option, (uint8_t[]){192,168,10,1}, 4);
    option += 4;
    bootp[option++] = 6;
    bootp[option++] = 4;
    memcpy(bootp + option, (uint8_t[]){1,1,1,1}, 4);
    option += 4;
    bootp[option++] = 51;
    bootp[option++] = 4;
    put32(bootp + option, 3600);
    option += 4;
    option4(bootp, &option, 54, (uint8_t[]){192,168,10,1});
    bootp[option] = 255;

    net_vnic_rx_frame(reply, sizeof(reply));
    if (net_vnic_get_info(0, got_mac, ip, mask, gateway, dns, &state) || state != NET_VNIC_STATE_REQUESTING) return 3;
    if (last_frame[42 + 240 + 2] != 3) return 4;

    bootp[242] = 5;
    net_vnic_rx_frame(reply, sizeof(reply));
    if (net_vnic_get_info(0, got_mac, ip, mask, gateway, dns, &state) || state != NET_VNIC_STATE_BOUND) return 5;
    if (ip[0] != 192 || ip[1] != 168 || ip[2] != 10 || ip[3] != 77 || gateway[3] != 1 || dns[0] != 1) return 6;

    uint8_t arp[42] = {0};
    memset(arp, 0xFF, 6);
    uint8_t client_mac[6] = {0x02,0x10,0x20,0x30,0x40,0x50};
    memcpy(arp + 6, client_mac, 6);
    arp[12] = 8;
    arp[13] = 6;
    uint8_t* arp_body = arp + 14;
    put16(arp_body, 1);
    put16(arp_body + 2, 0x0800);
    arp_body[4] = 6;
    arp_body[5] = 4;
    put16(arp_body + 6, 1);
    memcpy(arp_body + 8, client_mac, 6);
    memcpy(arp_body + 14, (uint8_t[]){192,168,10,5}, 4);
    memcpy(arp_body + 24, ip, 4);
    net_vnic_rx_frame(arp, sizeof(arp));
    if (last_frame[12] != 8 || last_frame[13] != 6 || last_frame[20] != 0 || last_frame[21] != 2 || !equal(last_frame + 22, mac, 6) || !equal(last_frame + 28, ip, 4)) return 7;

    uint8_t echo[42] = {0};
    memcpy(echo, mac, 6);
    memcpy(echo + 6, client_mac, 6);
    echo[12] = 8;
    echo[13] = 0;
    uint8_t* echo_ip = echo + 14;
    echo_ip[0] = 0x45;
    put16(echo_ip + 2, 28);
    echo_ip[8] = 64;
    echo_ip[9] = 1;
    memcpy(echo_ip + 12, (uint8_t[]){192,168,10,5}, 4);
    memcpy(echo_ip + 16, ip, 4);
    put16(echo_ip + 10, checksum(echo_ip, 20));
    uint8_t* echo_icmp = echo_ip + 20;
    echo_icmp[0] = 8;
    put16(echo_icmp + 4, 0x1234);
    put16(echo_icmp + 6, 1);
    put16(echo_icmp + 2, checksum(echo_icmp, 8));
    net_vnic_rx_frame(echo, sizeof(echo));
    if (last_frame[34] != 0 || !equal(last_frame + 26, ip, 4) || checksum(last_frame + 14, 20) != 0 || checksum(last_frame + 34, 8) != 0) return 8;

    uint8_t frame[60] = {0};
    frame[12] = 8;
    memset(frame + 6, 0xAA, 6);
    if (net_vnic_send(0, frame, sizeof(frame)) != 60 || !equal(last_frame + 6, mac, 6)) return 9;
    uint8_t received[NET_VNIC_FRAME_MAX];
    if (net_vnic_receive(0, received, sizeof(received)) != 42) return 10;
    if (net_vnic_receive(0, received, sizeof(received)) != 42) return 32;
    if (net_vnic_receive(0, received, sizeof(received)) != -1) return 33;
    simulate_ping = 1;
    simulate_ping_stage = 0;
    simulate_bridge_noise = 1;
    if (net_vnic_ping(0, (uint8_t[]){1,1,1,1}, 1) != 1 || simulate_ping_stage != 2) return 27;
    if (!equal(last_frame, (uint8_t[]){0x02,0x19,0x28,0x37,0x46,0x55}, 6) || !equal(last_frame + 30, (uint8_t[]){1,1,1,1}, 4)) return 29;
    simulate_ping = 0;
    simulate_bridge_noise = 0;

    uint8_t nat_mac[6] = {0x02,0x53,0x48,0x02,0x56,0x78};
    net_configured = 1;
    if (net_vnic_attach(1, nat_mac) || net_vnic_set_mode(1, NET_VNIC_MODE_NAT) || net_vnic_dhcp_start(1)) return 11;
    uint8_t nat_ip[4], nat_mask[4], nat_gateway[4], nat_dns[4];
    uint32_t nat_state = NET_VNIC_STATE_DOWN;
    if (net_vnic_get_info(1, NULL, nat_ip, nat_mask, nat_gateway, nat_dns, &nat_state) || nat_state != NET_VNIC_STATE_BOUND) return 12;
    if (!equal(nat_ip, (uint8_t[]){10,203,0,11}, 4) || !equal(nat_gateway, (uint8_t[]){10,203,0,1}, 4) || !equal(nat_mask, (uint8_t[]){255,255,255,0}, 4)) return 13;

    uint8_t discover[342] = {0};
    memset(discover, 0xFF, 6);
    memcpy(discover + 6, nat_mac, 6);
    discover[12] = 8;
    discover[13] = 0;
    uint8_t* discover_ip = discover + 14;
    discover_ip[0] = 0x45;
    put16(discover_ip + 2, 328);
    discover_ip[8] = 64;
    discover_ip[9] = 17;
    memset(discover_ip + 16, 0xFF, 4);
    put16(discover_ip + 10, checksum(discover_ip, 20));
    uint8_t* discover_udp = discover_ip + 20;
    put16(discover_udp, 68);
    put16(discover_udp + 2, 67);
    put16(discover_udp + 4, 308);
    uint8_t* discover_bootp = discover_udp + 8;
    discover_bootp[0] = 1;
    discover_bootp[1] = 1;
    discover_bootp[2] = 6;
    memcpy(discover_bootp + 4, (uint8_t[]){0x12,0x34,0x56,0x78}, 4);
    memcpy(discover_bootp + 28, nat_mac, 6);
    memcpy(discover_bootp + 236, (uint8_t[]){0x63,0x82,0x53,0x63}, 4);
    discover_bootp[240] = 53;
    discover_bootp[241] = 1;
    discover_bootp[242] = 1;
    discover_bootp[243] = 255;
    if (net_vnic_send(1, discover, sizeof(discover)) != (int)sizeof(discover)) return 14;
    if (net_vnic_receive(1, received, sizeof(received)) != (int)sizeof(discover) || received[42 + 242] != 2 || !equal(received + 42 + 16, nat_ip, 4)) return 15;

    uint8_t nat_arp[42] = {0};
    memset(nat_arp, 0xFF, 6);
    memcpy(nat_arp + 6, nat_mac, 6);
    nat_arp[12] = 8;
    nat_arp[13] = 6;
    uint8_t* nat_arp_body = nat_arp + 14;
    put16(nat_arp_body, 1);
    put16(nat_arp_body + 2, 0x0800);
    nat_arp_body[4] = 6;
    nat_arp_body[5] = 4;
    put16(nat_arp_body + 6, 1);
    memcpy(nat_arp_body + 8, nat_mac, 6);
    memcpy(nat_arp_body + 14, nat_ip, 4);
    memcpy(nat_arp_body + 24, nat_gateway, 4);
    if (net_vnic_send(1, nat_arp, sizeof(nat_arp)) != (int)sizeof(nat_arp)) return 16;
    if (net_vnic_receive(1, received, sizeof(received)) != 42 || !equal(received + 22, (uint8_t[]){0x02,0x53,0x48,0x00,0x00,0x01}, 6) || !equal(received + 28, nat_gateway, 4)) return 17;

    uint8_t nat_out[42] = {0};
    memcpy(nat_out, (uint8_t[]){0x02,0x53,0x48,0x00,0x00,0x01}, 6);
    memcpy(nat_out + 6, nat_mac, 6);
    nat_out[12] = 8;
    nat_out[13] = 0;
    uint8_t* nat_out_ip = nat_out + 14;
    nat_out_ip[0] = 0x45;
    put16(nat_out_ip + 2, 28);
    nat_out_ip[8] = 64;
    nat_out_ip[9] = 17;
    memcpy(nat_out_ip + 12, nat_ip, 4);
    memcpy(nat_out_ip + 16, (uint8_t[]){8,8,8,8}, 4);
    put16(nat_out_ip + 10, checksum(nat_out_ip, 20));
    uint8_t* nat_out_udp = nat_out_ip + 20;
    put16(nat_out_udp, 12345);
    put16(nat_out_udp + 2, 53);
    put16(nat_out_udp + 4, 8);
    if (net_vnic_send(1, nat_out, sizeof(nat_out)) != (int)sizeof(nat_out)) return 18;
    uint16_t mapped_port = (uint16_t)(((uint16_t)last_frame[34] << 8) | last_frame[35]);
    if (!equal(last_frame + 6, net_mac, 6) || !equal(last_frame + 26, net_ip, 4) || mapped_port < 40000 || checksum(last_frame + 14, 20) != 0) return 19;

    uint8_t nat_reply[42] = {0};
    memcpy(nat_reply, net_mac, 6);
    memcpy(nat_reply + 6, (uint8_t[]){0x02,0xAA,0xBB,0xCC,0xDD,0x08}, 6);
    nat_reply[12] = 8;
    nat_reply[13] = 0;
    uint8_t* nat_reply_ip = nat_reply + 14;
    nat_reply_ip[0] = 0x45;
    put16(nat_reply_ip + 2, 28);
    nat_reply_ip[8] = 64;
    nat_reply_ip[9] = 17;
    memcpy(nat_reply_ip + 12, (uint8_t[]){8,8,8,8}, 4);
    memcpy(nat_reply_ip + 16, net_ip, 4);
    put16(nat_reply_ip + 10, checksum(nat_reply_ip, 20));
    uint8_t* nat_reply_udp = nat_reply_ip + 20;
    put16(nat_reply_udp, 53);
    put16(nat_reply_udp + 2, mapped_port);
    put16(nat_reply_udp + 4, 8);
    net_vnic_rx_frame(nat_reply, sizeof(nat_reply));
    if (net_vnic_receive(1, received, sizeof(received)) != 42 || !equal(received + 30, nat_ip, 4) || (((uint16_t)received[36] << 8) | received[37]) != 12345 || checksum(received + 14, 20) != 0) return 20;

    uint8_t tcp_out[54] = {0};
    memcpy(tcp_out, (uint8_t[]){0x02,0x53,0x48,0x00,0x00,0x01}, 6);
    memcpy(tcp_out + 6, nat_mac, 6);
    tcp_out[12] = 8;
    tcp_out[13] = 0;
    uint8_t* tcp_out_ip = tcp_out + 14;
    tcp_out_ip[0] = 0x45;
    put16(tcp_out_ip + 2, 40);
    tcp_out_ip[8] = 64;
    tcp_out_ip[9] = 6;
    memcpy(tcp_out_ip + 12, nat_ip, 4);
    memcpy(tcp_out_ip + 16, (uint8_t[]){93,184,216,34}, 4);
    put16(tcp_out_ip + 10, checksum(tcp_out_ip, 20));
    uint8_t* tcp_out_segment = tcp_out_ip + 20;
    put16(tcp_out_segment, 23456);
    put16(tcp_out_segment + 2, 443);
    tcp_out_segment[12] = 0x50;
    tcp_out_segment[13] = 0x02;
    put16(tcp_out_segment + 14, 65535);
    put16(tcp_out_segment + 16, transport_checksum(tcp_out_ip, tcp_out_segment, 20, 6));
    if (net_vnic_send(1, tcp_out, sizeof(tcp_out)) != (int)sizeof(tcp_out)) return 21;
    uint16_t tcp_mapped_port = (uint16_t)(((uint16_t)last_frame[34] << 8) | last_frame[35]);
    if (transport_checksum(last_frame + 14, last_frame + 34, 20, 6) != 0) return 22;

    uint8_t tcp_reply[54] = {0};
    memcpy(tcp_reply, net_mac, 6);
    tcp_reply[12] = 8;
    tcp_reply[13] = 0;
    uint8_t* tcp_reply_ip = tcp_reply + 14;
    tcp_reply_ip[0] = 0x45;
    put16(tcp_reply_ip + 2, 40);
    tcp_reply_ip[8] = 64;
    tcp_reply_ip[9] = 6;
    memcpy(tcp_reply_ip + 12, (uint8_t[]){93,184,216,34}, 4);
    memcpy(tcp_reply_ip + 16, net_ip, 4);
    put16(tcp_reply_ip + 10, checksum(tcp_reply_ip, 20));
    uint8_t* tcp_reply_segment = tcp_reply_ip + 20;
    put16(tcp_reply_segment, 443);
    put16(tcp_reply_segment + 2, tcp_mapped_port);
    tcp_reply_segment[12] = 0x50;
    tcp_reply_segment[13] = 0x12;
    put16(tcp_reply_segment + 14, 65535);
    put16(tcp_reply_segment + 16, transport_checksum(tcp_reply_ip, tcp_reply_segment, 20, 6));
    net_vnic_rx_frame(tcp_reply, sizeof(tcp_reply));
    if (net_vnic_receive(1, received, sizeof(received)) != 54 || !equal(received + 30, nat_ip, 4) || (((uint16_t)received[36] << 8) | received[37]) != 23456 || transport_checksum(received + 14, received + 34, 20, 6) != 0) return 23;

    uint8_t icmp_out[42] = {0};
    memcpy(icmp_out, (uint8_t[]){0x02,0x53,0x48,0x00,0x00,0x01}, 6);
    memcpy(icmp_out + 6, nat_mac, 6);
    icmp_out[12] = 8;
    icmp_out[13] = 0;
    uint8_t* icmp_out_ip = icmp_out + 14;
    icmp_out_ip[0] = 0x45;
    put16(icmp_out_ip + 2, 28);
    icmp_out_ip[8] = 64;
    icmp_out_ip[9] = 1;
    memcpy(icmp_out_ip + 12, nat_ip, 4);
    memcpy(icmp_out_ip + 16, (uint8_t[]){1,1,1,1}, 4);
    put16(icmp_out_ip + 10, checksum(icmp_out_ip, 20));
    uint8_t* icmp_out_packet = icmp_out_ip + 20;
    icmp_out_packet[0] = 8;
    put16(icmp_out_packet + 4, 0x2233);
    put16(icmp_out_packet + 6, 7);
    put16(icmp_out_packet + 2, checksum(icmp_out_packet, 8));
    if (net_vnic_send(1, icmp_out, sizeof(icmp_out)) != (int)sizeof(icmp_out)) return 24;
    uint16_t icmp_mapped_id = (uint16_t)(((uint16_t)last_frame[38] << 8) | last_frame[39]);
    if (checksum(last_frame + 34, 8) != 0) return 25;

    uint8_t icmp_reply[42] = {0};
    memcpy(icmp_reply, net_mac, 6);
    icmp_reply[12] = 8;
    icmp_reply[13] = 0;
    uint8_t* icmp_reply_ip = icmp_reply + 14;
    icmp_reply_ip[0] = 0x45;
    put16(icmp_reply_ip + 2, 28);
    icmp_reply_ip[8] = 64;
    icmp_reply_ip[9] = 1;
    memcpy(icmp_reply_ip + 12, (uint8_t[]){1,1,1,1}, 4);
    memcpy(icmp_reply_ip + 16, net_ip, 4);
    put16(icmp_reply_ip + 10, checksum(icmp_reply_ip, 20));
    uint8_t* icmp_reply_packet = icmp_reply_ip + 20;
    icmp_reply_packet[0] = 0;
    put16(icmp_reply_packet + 4, icmp_mapped_id);
    put16(icmp_reply_packet + 6, 7);
    put16(icmp_reply_packet + 2, checksum(icmp_reply_packet, 8));
    net_vnic_rx_frame(icmp_reply, sizeof(icmp_reply));
    if (net_vnic_receive(1, received, sizeof(received)) != 42 || !equal(received + 30, nat_ip, 4) || (((uint16_t)received[38] << 8) | received[39]) != 0x2233 || checksum(received + 34, 8) != 0) return 26;
    simulate_ping = 1;
    simulate_ping_stage = 1;
    if (net_vnic_ping(1, (uint8_t[]){1,1,1,1}, 1) != 1 || simulate_ping_stage != 2) return 28;
    if (!equal(last_frame + 30, (uint8_t[]){1,1,1,1}, 4)) return 30;
    simulate_ping = 0;

    puts("virtual NIC bridge/NAT ICMP echo send-receive, DHCP, ARP, TCP/UDP mapping and frame forwarding test passed");
    return 0;
}
