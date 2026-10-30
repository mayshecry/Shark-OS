#include "kernel.h"
#include "net.h"

#define VNIC_RX_QUEUE 4
#define VNIC_DHCP_PAYLOAD 300
#define VNIC_ETH_IP 0x0800
#define VNIC_IP_UDP 17
#define VNIC_IP_ICMP 1
#define VNIC_IP_TCP 6
#define VNIC_NAT_CONNECTIONS 64
#define VNIC_NAT_TTL (120u * TICKS_PER_SEC)

struct net_vnic_frame {
    uint16_t length;
    uint8_t bytes[NET_VNIC_FRAME_MAX];
};

typedef struct {
    uint8_t attached;
    uint8_t mode;
    uint8_t mac[6];
    uint8_t ip[4];
    uint8_t mask[4];
    uint8_t gateway[4];
    uint8_t dns[4];
    uint8_t offered_ip[4];
    uint8_t server_ip[4];
    uint32_t state;
    uint32_t xid;
    uint32_t lease_seconds;
    uint32_t bound_tick;
    uint32_t next_tick;
    uint32_t backoff_ticks;
    uint32_t retries;
    uint8_t rx_head;
    uint8_t rx_count;
    struct net_vnic_frame rx[VNIC_RX_QUEUE];
} net_vnic_t;

static net_vnic_t net_vnics[NET_VNIC_LIMIT];

static uint16_t net_vnic_checksum(const uint8_t* bytes, uint32_t length);
static int net_vnic_equal(const uint8_t* left, const uint8_t* right, uint32_t length);
static void net_vnic_put16(uint8_t* out, uint16_t value);
static uint16_t net_vnic_get16(const uint8_t* in);
static void net_vnic_put32(uint8_t* out, uint32_t value);

typedef struct {
    uint8_t active;
    uint8_t index;
    uint8_t protocol;
    uint8_t internal_ip[4];
    uint8_t remote_ip[4];
    uint16_t internal_port;
    uint16_t remote_port;
    uint16_t external_port;
    uint32_t expires;
} net_vnic_nat_entry_t;

static net_vnic_nat_entry_t net_vnic_nat_table[VNIC_NAT_CONNECTIONS];
static uint16_t net_vnic_nat_next_port = 40000;
static const uint8_t net_vnic_nat_gateway[4] = {10,203,0,1};
static const uint8_t net_vnic_nat_mask[4] = {255,255,255,0};
static const uint8_t net_vnic_nat_gateway_mac[6] = {0x02,0x53,0x48,0x00,0x00,0x01};

static void net_vnic_queue_frame(uint32_t index, const uint8_t* frame, uint32_t length) {
    if (index >= NET_VNIC_LIMIT || !net_vnics[index].attached || !frame || length < 14 || length > NET_VNIC_FRAME_MAX) return;
    net_vnic_t* vnic = &net_vnics[index];
    if (vnic->rx_count >= VNIC_RX_QUEUE) return;
    uint32_t slot = (vnic->rx_head + vnic->rx_count) % VNIC_RX_QUEUE;
    vnic->rx[slot].length = (uint16_t)length;
    memcpy(vnic->rx[slot].bytes, frame, length);
    vnic->rx_count++;
}

static void net_vnic_nat_setup_address(uint32_t index) {
    net_vnic_t* vnic = &net_vnics[index];
    memcpy(vnic->ip, (uint8_t[]){10,203,0,(uint8_t)(10 + index)}, 4);
    memcpy(vnic->mask, net_vnic_nat_mask, 4);
    memcpy(vnic->gateway, net_vnic_nat_gateway, 4);
    if (net_dns[0] || net_dns[1] || net_dns[2] || net_dns[3]) memcpy(vnic->dns, net_dns, 4);
    else memcpy(vnic->dns, (uint8_t[]){1,1,1,1}, 4);
    memcpy(vnic->server_ip, net_vnic_nat_gateway, 4);
    memcpy(vnic->offered_ip, vnic->ip, 4);
    vnic->state = NET_VNIC_STATE_BOUND;
    vnic->lease_seconds = 0;
    vnic->bound_tick = uptime_ticks;
    vnic->retries = 0;
}

static void net_vnic_rechecksum_ip(uint8_t* ip, uint32_t ihl) {
    ip[10] = 0;
    ip[11] = 0;
    net_vnic_put16(ip + 10, net_vnic_checksum(ip, ihl));
}

static void net_vnic_rechecksum_transport(uint8_t* ip, uint32_t ihl, uint32_t total, uint8_t protocol) {
    uint8_t* transport = ip + ihl;
    uint32_t length = total - ihl;
    uint32_t checksum_offset;
    if (protocol == VNIC_IP_TCP) checksum_offset = 16;
    else if (protocol == VNIC_IP_UDP) checksum_offset = 6;
    else if (protocol == VNIC_IP_ICMP) {
        if (length < 4) return;
        transport[2] = 0;
        transport[3] = 0;
        net_vnic_put16(transport + 2, net_vnic_checksum(transport, length));
        return;
    } else return;
    if (length < checksum_offset + 2) return;
    uint16_t old_checksum = net_vnic_get16(transport + checksum_offset);
    if (protocol == VNIC_IP_UDP && old_checksum == 0) return;
    transport[checksum_offset] = 0;
    transport[checksum_offset + 1] = 0;
    uint32_t sum = 0;
    for (uint32_t i = 12; i < 20; i += 2) sum += ((uint32_t)ip[i] << 8) | ip[i + 1];
    sum += protocol;
    sum += length;
    for (uint32_t i = 0; i + 1 < length; i += 2) sum += ((uint32_t)transport[i] << 8) | transport[i + 1];
    if (length & 1) sum += (uint32_t)transport[length - 1] << 8;
    while (sum >> 16) sum = (sum & 0xFFFFu) + (sum >> 16);
    uint16_t checksum = (uint16_t)~sum;
    if (protocol == VNIC_IP_UDP && checksum == 0) checksum = 0xFFFF;
    net_vnic_put16(transport + checksum_offset, checksum);
}

static int net_vnic_nat_same_ip(const uint8_t* left, const uint8_t* right) {
    return net_vnic_equal(left, right, 4);
}

static int net_vnic_nat_port_in_use(uint8_t protocol, uint16_t port) {
    for (uint32_t i = 0; i < VNIC_NAT_CONNECTIONS; i++) {
        if (net_vnic_nat_table[i].active && net_vnic_nat_table[i].protocol == protocol && net_vnic_nat_table[i].external_port == port) return 1;
    }
    return 0;
}

static uint16_t net_vnic_nat_allocate_port(uint8_t protocol) {
    for (uint32_t attempt = 0; attempt < 20001; attempt++) {
        uint16_t candidate = net_vnic_nat_next_port++;
        if (net_vnic_nat_next_port < 40000 || net_vnic_nat_next_port > 60000) net_vnic_nat_next_port = 40000;
        if (!net_vnic_nat_port_in_use(protocol, candidate)) return candidate;
    }
    return 0;
}

static net_vnic_nat_entry_t* net_vnic_nat_out_entry(uint32_t index, uint8_t protocol, uint16_t internal_port, const uint8_t* remote_ip, uint16_t remote_port) {
    net_vnic_nat_entry_t* free_entry = NULL;
    for (uint32_t i = 0; i < VNIC_NAT_CONNECTIONS; i++) {
        net_vnic_nat_entry_t* entry = &net_vnic_nat_table[i];
        if (!entry->active) {
            if (!free_entry) free_entry = entry;
            continue;
        }
        if (entry->index == index && entry->protocol == protocol && entry->internal_port == internal_port &&
            entry->remote_port == remote_port && net_vnic_nat_same_ip(entry->remote_ip, remote_ip)) {
            entry->expires = uptime_ticks + VNIC_NAT_TTL;
            return entry;
        }
    }
    if (!free_entry) return NULL;
    uint16_t port = net_vnic_nat_allocate_port(protocol);
    if (!port) return NULL;
    memset(free_entry, 0, sizeof(*free_entry));
    free_entry->active = 1;
    free_entry->index = (uint8_t)index;
    free_entry->protocol = protocol;
    memcpy(free_entry->internal_ip, net_vnics[index].ip, 4);
    memcpy(free_entry->remote_ip, remote_ip, 4);
    free_entry->internal_port = internal_port;
    free_entry->remote_port = remote_port;
    free_entry->external_port = port;
    free_entry->expires = uptime_ticks + VNIC_NAT_TTL;
    return free_entry;
}

static int net_vnic_nat_is_local_destination(const uint8_t* address) {
    if (!net_configured) return 0;
    for (uint32_t i = 0; i < 4; i++) if ((address[i] & net_mask[i]) != (net_ip[i] & net_mask[i])) return 0;
    return 1;
}

static int net_vnic_nat_get_remote_port(const uint8_t* ip, uint32_t ihl, uint32_t total, uint8_t protocol, uint16_t* port) {
    const uint8_t* transport = ip + ihl;
    if (protocol == VNIC_IP_TCP || protocol == VNIC_IP_UDP) {
        if (total < ihl + 4) return 0;
        *port = net_vnic_get16(transport);
        return 1;
    }
    if (protocol == VNIC_IP_ICMP) {
        if (total < ihl + 8 || transport[0] != 8) return 0;
        *port = net_vnic_get16(transport + 4);
        return 1;
    }
    return 0;
}

static void net_vnic_nat_send_dhcp_reply(uint32_t index, const uint8_t* request, uint32_t request_length) {
    if (index >= NET_VNIC_LIMIT || request_length < 14 + 20 + 8 + 240) return;
    const uint8_t* req_ip = request + 14;
    uint32_t ihl = (uint32_t)(req_ip[0] & 0x0F) * 4;
    if (ihl < 20 || req_ip[9] != VNIC_IP_UDP) return;
    const uint8_t* req_udp = req_ip + ihl;
    uint16_t udp_length = net_vnic_get16(req_udp + 4);
    if (net_vnic_get16(req_udp) != 68 || net_vnic_get16(req_udp + 2) != 67 || udp_length < 8 + 240 || udp_length > request_length - 14 - ihl) return;
    const uint8_t* req_bootp = req_udp + 8;
    if (req_bootp[0] != 1 || req_bootp[2] != 6 || req_bootp[236] != 0x63 || req_bootp[237] != 0x82 || req_bootp[238] != 0x53 || req_bootp[239] != 0x63) return;
    uint8_t requested_type = 0;
    uint32_t option = 240;
    uint32_t options_end = udp_length - 8;
    while (option < options_end) {
        uint8_t code = req_bootp[option++];
        if (code == 0) continue;
        if (code == 255 || option >= options_end) break;
        uint8_t option_length = req_bootp[option++];
        if (option_length > options_end - option) break;
        if (code == 53 && option_length >= 1) requested_type = req_bootp[option];
        option += option_length;
    }
    if (requested_type != 1 && requested_type != 3) return;
    net_vnic_t* vnic = &net_vnics[index];
    uint8_t frame[14 + 20 + 8 + VNIC_DHCP_PAYLOAD];
    memset(frame, 0, sizeof(frame));
    memset(frame, 0xFF, 6);
    memcpy(frame + 6, net_vnic_nat_gateway_mac, 6);
    frame[12] = 0x08;
    frame[13] = 0x00;
    uint8_t* ip = frame + 14;
    ip[0] = 0x45;
    net_vnic_put16(ip + 2, 20 + 8 + VNIC_DHCP_PAYLOAD);
    net_vnic_put16(ip + 4, (uint16_t)vnic->xid);
    ip[8] = 64;
    ip[9] = VNIC_IP_UDP;
    memcpy(ip + 12, net_vnic_nat_gateway, 4);
    memset(ip + 16, 0xFF, 4);
    net_vnic_rechecksum_ip(ip, 20);
    uint8_t* udp = ip + 20;
    net_vnic_put16(udp, 67);
    net_vnic_put16(udp + 2, 68);
    net_vnic_put16(udp + 4, 8 + VNIC_DHCP_PAYLOAD);
    uint8_t* bootp = udp + 8;
    bootp[0] = 2;
    bootp[1] = 1;
    bootp[2] = 6;
    memcpy(bootp + 4, req_bootp + 4, 4);
    memcpy(bootp + 10, req_bootp + 10, 2);
    memcpy(bootp + 16, vnic->ip, 4);
    memcpy(bootp + 20, net_vnic_nat_gateway, 4);
    memcpy(bootp + 28, req_bootp + 28, 16);
    bootp[236] = 0x63;
    bootp[237] = 0x82;
    bootp[238] = 0x53;
    bootp[239] = 0x63;
    option = 240;
    bootp[option++] = 53;
    bootp[option++] = 1;
    bootp[option++] = requested_type == 1 ? 2 : 5;
    bootp[option++] = 1;
    bootp[option++] = 4;
    memcpy(bootp + option, net_vnic_nat_mask, 4);
    option += 4;
    bootp[option++] = 3;
    bootp[option++] = 4;
    memcpy(bootp + option, net_vnic_nat_gateway, 4);
    option += 4;
    bootp[option++] = 6;
    bootp[option++] = 4;
    memcpy(bootp + option, vnic->dns, 4);
    option += 4;
    bootp[option++] = 51;
    bootp[option++] = 4;
    net_vnic_put32(bootp + option, 86400);
    option += 4;
    bootp[option++] = 54;
    bootp[option++] = 4;
    memcpy(bootp + option, net_vnic_nat_gateway, 4);
    option += 4;
    bootp[option] = 255;
    net_vnic_queue_frame(index, frame, sizeof(frame));
}

static int net_vnic_nat_handle_arp(uint32_t index, const uint8_t* frame, uint32_t length) {
    if (length < 42 || frame[12] != 0x08 || frame[13] != 0x06) return 0;
    const uint8_t* arp = frame + 14;
    if (net_vnic_get16(arp) != 1 || net_vnic_get16(arp + 2) != 0x0800 || arp[4] != 6 || arp[5] != 4 || net_vnic_get16(arp + 6) != 1 || !net_vnic_equal(arp + 24, net_vnic_nat_gateway, 4)) return 0;
    uint8_t reply[42] = {0};
    memcpy(reply, arp + 8, 6);
    memcpy(reply + 6, net_vnic_nat_gateway_mac, 6);
    reply[12] = 0x08;
    reply[13] = 0x06;
    uint8_t* out = reply + 14;
    net_vnic_put16(out, 1);
    net_vnic_put16(out + 2, 0x0800);
    out[4] = 6;
    out[5] = 4;
    net_vnic_put16(out + 6, 2);
    memcpy(out + 8, net_vnic_nat_gateway_mac, 6);
    memcpy(out + 14, net_vnic_nat_gateway, 4);
    memcpy(out + 18, arp + 8, 6);
    memcpy(out + 24, arp + 14, 4);
    net_vnic_queue_frame(index, reply, sizeof(reply));
    return 1;
}

static int net_vnic_nat_router_echo(uint32_t index, const uint8_t* frame, uint32_t length, uint32_t ihl, uint32_t total) {
    const uint8_t* ip = frame + 14;
    if (ip[9] != VNIC_IP_ICMP || !net_vnic_equal(ip + 16, net_vnic_nat_gateway, 4) || total < ihl + 8) return 0;
    const uint8_t* icmp = ip + ihl;
    if (icmp[0] != 8 || icmp[1] != 0) return 0;
    uint8_t reply[NET_VNIC_FRAME_MAX];
    memcpy(reply, frame, length);
    memcpy(reply, frame + 6, 6);
    memcpy(reply + 6, net_vnic_nat_gateway_mac, 6);
    uint8_t* out_ip = reply + 14;
    memcpy(out_ip + 12, net_vnic_nat_gateway, 4);
    memcpy(out_ip + 16, ip + 12, 4);
    out_ip[8] = 64;
    net_vnic_rechecksum_ip(out_ip, ihl);
    uint8_t* out_icmp = out_ip + ihl;
    out_icmp[0] = 0;
    net_vnic_rechecksum_transport(out_ip, ihl, total, VNIC_IP_ICMP);
    net_vnic_queue_frame(index, reply, length);
    return 1;
}

static int net_vnic_nat_send(uint32_t index, const uint8_t* frame, uint32_t length) {
    net_vnic_t* vnic = &net_vnics[index];
    if (frame[12] == 0x08 && frame[13] == 0x06) {
        net_vnic_nat_handle_arp(index, frame, length);
        return (int)length;
    }
    if (frame[12] != 0x08 || frame[13] != 0x00 || length < 34) return -1;
    const uint8_t* in_ip = frame + 14;
    if ((in_ip[0] >> 4) != 4 || (in_ip[6] & 0x3F) || in_ip[7]) return -1;
    uint32_t ihl = (uint32_t)(in_ip[0] & 0x0F) * 4;
    uint32_t total = net_vnic_get16(in_ip + 2);
    if (ihl < 20 || total < ihl || total > length - 14) return -1;
    if (in_ip[9] == VNIC_IP_UDP && total >= ihl + 8 && net_vnic_get16(in_ip + ihl + 2) == 67 && net_vnic_get16(in_ip + ihl) == 68) {
        net_vnic_nat_send_dhcp_reply(index, frame, length);
        return (int)length;
    }
    if (net_vnic_nat_router_echo(index, frame, length, ihl, total)) return (int)length;
    if (!net_vnic_nat_same_ip(in_ip + 12, vnic->ip) || in_ip[8] <= 1 || !net_configured) return -1;
    uint8_t protocol = in_ip[9];
    uint16_t internal_port;
    if (!net_vnic_nat_get_remote_port(in_ip, ihl, total, protocol, &internal_port)) return -1;
    uint16_t remote_port = protocol == VNIC_IP_ICMP ? 0 : net_vnic_get16(in_ip + ihl + 2);
    net_vnic_nat_entry_t* entry = net_vnic_nat_out_entry(index, protocol, internal_port, in_ip + 16, remote_port);
    if (!entry) return -1;
    uint8_t next_hop[4];
    if (net_vnic_nat_is_local_destination(in_ip + 16)) memcpy(next_hop, in_ip + 16, 4);
    else memcpy(next_hop, net_gw, 4);
    uint8_t destination_mac[6];
    if (!net_arp_resolve(next_hop, destination_mac)) return -1;
    uint8_t output[NET_VNIC_FRAME_MAX];
    memcpy(output, frame, length);
    memcpy(output, destination_mac, 6);
    memcpy(output + 6, net_mac, 6);
    uint8_t* out_ip = output + 14;
    memcpy(out_ip + 12, net_ip, 4);
    out_ip[8]--;
    if (protocol == VNIC_IP_TCP || protocol == VNIC_IP_UDP) net_vnic_put16(out_ip + ihl, entry->external_port);
    else net_vnic_put16(out_ip + ihl + 4, entry->external_port);
    net_vnic_rechecksum_ip(out_ip, ihl);
    net_vnic_rechecksum_transport(out_ip, ihl, total, protocol);
    net_send_raw(output, length);
    return (int)length;
}

static int net_vnic_nat_receive(uint32_t index, const uint8_t* frame, uint32_t length) {
    if (length < 34 || frame[12] != 0x08 || frame[13] != 0x00) return 0;
    const uint8_t* in_ip = frame + 14;
    if ((in_ip[0] >> 4) != 4 || (in_ip[6] & 0x3F) || in_ip[7] || !net_vnic_nat_same_ip(in_ip + 16, net_ip)) return 0;
    uint32_t ihl = (uint32_t)(in_ip[0] & 0x0F) * 4;
    uint32_t total = net_vnic_get16(in_ip + 2);
    if (ihl < 20 || total < ihl || total > length - 14) return 0;
    uint8_t protocol = in_ip[9];
    uint16_t remote_port = 0;
    uint16_t external_port = 0;
    if (protocol == VNIC_IP_TCP || protocol == VNIC_IP_UDP) {
        if (total < ihl + 4) return 0;
        remote_port = net_vnic_get16(in_ip + ihl);
        external_port = net_vnic_get16(in_ip + ihl + 2);
    } else if (protocol == VNIC_IP_ICMP) {
        if (total < ihl + 8 || in_ip[ihl + 0] != 0) return 0;
        external_port = net_vnic_get16(in_ip + ihl + 4);
    } else return 0;
    net_vnic_nat_entry_t* entry = NULL;
    for (uint32_t i = 0; i < VNIC_NAT_CONNECTIONS; i++) {
        net_vnic_nat_entry_t* candidate = &net_vnic_nat_table[i];
        if (!candidate->active || candidate->protocol != protocol || candidate->external_port != external_port || !net_vnic_nat_same_ip(candidate->remote_ip, in_ip + 12)) continue;
        if (protocol != VNIC_IP_ICMP && candidate->remote_port != remote_port) continue;
        if (candidate->index != index) continue;
        entry = candidate;
        break;
    }
    if (!entry || in_ip[8] <= 1) return 0;
    uint8_t output[NET_VNIC_FRAME_MAX];
    memcpy(output, frame, length);
    memcpy(output, net_vnics[index].mac, 6);
    memcpy(output + 6, net_vnic_nat_gateway_mac, 6);
    uint8_t* out_ip = output + 14;
    memcpy(out_ip + 16, entry->internal_ip, 4);
    out_ip[8]--;
    if (protocol == VNIC_IP_TCP || protocol == VNIC_IP_UDP) net_vnic_put16(out_ip + ihl + 2, entry->internal_port);
    else net_vnic_put16(out_ip + ihl + 4, entry->internal_port);
    net_vnic_rechecksum_ip(out_ip, ihl);
    net_vnic_rechecksum_transport(out_ip, ihl, total, protocol);
    entry->expires = uptime_ticks + VNIC_NAT_TTL;
    net_vnic_queue_frame(index, output, length);
    return 1;
}

static uint16_t net_vnic_checksum(const uint8_t* bytes, uint32_t length) {
    uint32_t sum = 0;
    for (uint32_t i = 0; i + 1 < length; i += 2) sum += ((uint32_t)bytes[i] << 8) | bytes[i + 1];
    if (length & 1) sum += (uint32_t)bytes[length - 1] << 8;
    while (sum >> 16) sum = (sum & 0xFFFFu) + (sum >> 16);
    return (uint16_t)~sum;
}

static int net_vnic_equal(const uint8_t* left, const uint8_t* right, uint32_t length) {
    for (uint32_t i = 0; i < length; i++) if (left[i] != right[i]) return 0;
    return 1;
}

static void net_vnic_put16(uint8_t* out, uint16_t value) {
    out[0] = (uint8_t)(value >> 8);
    out[1] = (uint8_t)value;
}

static uint16_t net_vnic_get16(const uint8_t* in) {
    return (uint16_t)(((uint16_t)in[0] << 8) | in[1]);
}

static uint32_t net_vnic_get32(const uint8_t* in) {
    return ((uint32_t)in[0] << 24) | ((uint32_t)in[1] << 16) | ((uint32_t)in[2] << 8) | in[3];
}

static void net_vnic_put32(uint8_t* out, uint32_t value) {
    out[0] = (uint8_t)(value >> 24);
    out[1] = (uint8_t)(value >> 16);
    out[2] = (uint8_t)(value >> 8);
    out[3] = (uint8_t)value;
}

static void net_vnic_send_dhcp(uint32_t index, uint8_t message_type, int renewal) {
    if (index >= NET_VNIC_LIMIT || !net_vnics[index].attached) return;
    net_vnic_t* vnic = &net_vnics[index];
    uint8_t frame[14 + 20 + 8 + VNIC_DHCP_PAYLOAD];
    memset(frame, 0, sizeof(frame));
    for (uint32_t i = 0; i < 6; i++) frame[i] = 0xFF;
    memcpy(frame + 6, vnic->mac, 6);
    frame[12] = 0x08;
    frame[13] = 0x00;

    uint8_t* ip = frame + 14;
    ip[0] = 0x45;
    net_vnic_put16(ip + 2, 20 + 8 + VNIC_DHCP_PAYLOAD);
    net_vnic_put16(ip + 4, (uint16_t)vnic->xid);
    ip[8] = 64;
    ip[9] = VNIC_IP_UDP;
    if (renewal) memcpy(ip + 12, vnic->ip, 4);
    memset(ip + 16, 0xFF, 4);
    net_vnic_put16(ip + 10, net_vnic_checksum(ip, 20));

    uint8_t* udp = ip + 20;
    net_vnic_put16(udp, 68);
    net_vnic_put16(udp + 2, 67);
    net_vnic_put16(udp + 4, 8 + VNIC_DHCP_PAYLOAD);

    uint8_t* bootp = udp + 8;
    bootp[0] = 1;
    bootp[1] = 1;
    bootp[2] = 6;
    net_vnic_put32(bootp + 4, vnic->xid);
    if (!renewal) {
        bootp[10] = 0x80;
    } else {
        memcpy(bootp + 12, vnic->ip, 4);
    }
    memcpy(bootp + 28, vnic->mac, 6);
    bootp[236] = 0x63;
    bootp[237] = 0x82;
    bootp[238] = 0x53;
    bootp[239] = 0x63;
    uint32_t option = 240;
    bootp[option++] = 53;
    bootp[option++] = 1;
    bootp[option++] = message_type;
    if (!renewal) {
        bootp[option++] = 61;
        bootp[option++] = 7;
        bootp[option++] = 1;
        memcpy(bootp + option, vnic->mac, 6);
        option += 6;
        bootp[option++] = 12;
        bootp[option++] = 7;
        memcpy(bootp + option, "sharkos", 7);
        option += 7;
        bootp[option++] = 55;
        bootp[option++] = 4;
        bootp[option++] = 1;
        bootp[option++] = 3;
        bootp[option++] = 6;
        bootp[option++] = 51;
        bootp[option++] = 57;
        bootp[option++] = 2;
        bootp[option++] = 0x02;
        bootp[option++] = 0x40;
    }
    if (message_type == 3 && !renewal) {
        bootp[option++] = 50;
        bootp[option++] = 4;
        memcpy(bootp + option, vnic->offered_ip, 4);
        option += 4;
        bootp[option++] = 54;
        bootp[option++] = 4;
        memcpy(bootp + option, vnic->server_ip, 4);
        option += 4;
    }
    bootp[option] = 0xFF;
    net_send_raw(frame, sizeof(frame));
}

static void net_vnic_copy_option4(uint8_t destination[4], const uint8_t* value, uint8_t length) {
    if (length < 4) return;
    memcpy(destination, value, 4);
}

static void net_vnic_handle_arp(uint32_t index, const uint8_t* frame, uint32_t length) {
    net_vnic_t* vnic = &net_vnics[index];
    if (vnic->state != NET_VNIC_STATE_BOUND || length < 42 || frame[12] != 0x08 || frame[13] != 0x06) return;
    const uint8_t* arp = frame + 14;
    if (net_vnic_get16(arp) != 1 || net_vnic_get16(arp + 2) != 0x0800 || arp[4] != 6 || arp[5] != 4 || net_vnic_get16(arp + 6) != 1) return;
    if (!net_vnic_equal(arp + 24, vnic->ip, 4)) return;
    uint8_t reply[42] = {0};
    memcpy(reply, arp + 8, 6);
    memcpy(reply + 6, vnic->mac, 6);
    reply[12] = 0x08;
    reply[13] = 0x06;
    uint8_t* out = reply + 14;
    net_vnic_put16(out, 1);
    net_vnic_put16(out + 2, 0x0800);
    out[4] = 6;
    out[5] = 4;
    net_vnic_put16(out + 6, 2);
    memcpy(out + 8, vnic->mac, 6);
    memcpy(out + 14, vnic->ip, 4);
    memcpy(out + 18, arp + 8, 6);
    memcpy(out + 24, arp + 14, 4);
    net_vnic_send(index, reply, sizeof(reply));
}

static void net_vnic_handle_icmp(uint32_t index, const uint8_t* frame, uint32_t length) {
    net_vnic_t* vnic = &net_vnics[index];
    if (vnic->state != NET_VNIC_STATE_BOUND || length < 14 + 20 + 8 || frame[12] != 0x08 || frame[13] != 0x00) return;
    const uint8_t* ip = frame + 14;
    if ((ip[0] >> 4) != 4 || ip[9] != 1 || (ip[6] & 0x3F) || ip[7]) return;
    uint32_t ihl = (uint32_t)(ip[0] & 0x0F) * 4;
    uint16_t total = net_vnic_get16(ip + 2);
    if (ihl < 20 || total < ihl + 8 || total > length - 14 || !net_vnic_equal(ip + 16, vnic->ip, 4)) return;
    const uint8_t* icmp = ip + ihl;
    uint32_t icmp_length = total - ihl;
    if (icmp[0] != 8 || icmp[1] != 0) return;
    uint8_t reply[NET_VNIC_FRAME_MAX];
    memcpy(reply, frame, length);
    memcpy(reply, frame + 6, 6);
    memcpy(reply + 6, vnic->mac, 6);
    uint8_t* out_ip = reply + 14;
    memcpy(out_ip + 12, ip + 16, 4);
    memcpy(out_ip + 16, ip + 12, 4);
    out_ip[8] = 64;
    out_ip[10] = 0;
    out_ip[11] = 0;
    net_vnic_put16(out_ip + 10, net_vnic_checksum(out_ip, ihl));
    uint8_t* out_icmp = out_ip + ihl;
    out_icmp[0] = 0;
    out_icmp[2] = 0;
    out_icmp[3] = 0;
    net_vnic_put16(out_icmp + 2, net_vnic_checksum(out_icmp, icmp_length));
    net_vnic_send(index, reply, length);
}

static void net_vnic_handle_frame(uint32_t index, const uint8_t* frame, uint32_t length) {
    net_vnic_handle_arp(index, frame, length);
    net_vnic_handle_icmp(index, frame, length);
}

static void net_vnic_receive_dhcp(net_vnic_t* vnic, const uint8_t* frame, uint32_t length) {
    if (length < 14 + 20 + 8 + 240 || frame[12] != 0x08 || frame[13] != 0x00) return;
    const uint8_t* ip = frame + 14;
    if ((ip[0] >> 4) != 4 || ip[9] != VNIC_IP_UDP) return;
    uint32_t ihl = (uint32_t)(ip[0] & 0x0F) * 4;
    if (ihl < 20 || length < 14 + ihl + 8 + 240) return;
    uint16_t ip_length = net_vnic_get16(ip + 2);
    if (ip_length < ihl + 8 + 240 || ip_length > length - 14) return;
    const uint8_t* udp = ip + ihl;
    if (net_vnic_get16(udp) != 67 || net_vnic_get16(udp + 2) != 68) return;
    uint16_t udp_length = net_vnic_get16(udp + 4);
    if (udp_length < 8 + 240 || udp_length > ip_length - ihl) return;
    const uint8_t* bootp = udp + 8;
    uint32_t payload_length = udp_length - 8;
    if (bootp[0] != 2 || bootp[2] != 6 || net_vnic_get32(bootp + 4) != vnic->xid) return;
    if (!net_vnic_equal(bootp + 28, vnic->mac, 6)) return;
    if (bootp[236] != 0x63 || bootp[237] != 0x82 || bootp[238] != 0x53 || bootp[239] != 0x63) return;

    uint32_t message_type = 0;
    uint8_t mask[4] = {0,0,0,0};
    uint8_t gateway[4] = {0,0,0,0};
    uint8_t dns[4] = {0,0,0,0};
    uint8_t server[4] = {0,0,0,0};
    uint32_t lease = 0;
    uint32_t option = 240;
    while (option + 1 < payload_length) {
        uint8_t code = bootp[option];
        if (code == 0xFF) break;
        if (code == 0) { option++; continue; }
        uint8_t option_length = bootp[option + 1];
        if (option + 2 + option_length > payload_length) break;
        const uint8_t* value = bootp + option + 2;
        if (code == 53 && option_length == 1) message_type = value[0];
        else if (code == 1) net_vnic_copy_option4(mask, value, option_length);
        else if (code == 3) net_vnic_copy_option4(gateway, value, option_length);
        else if (code == 6) net_vnic_copy_option4(dns, value, option_length);
        else if (code == 54) net_vnic_copy_option4(server, value, option_length);
        else if (code == 51 && option_length >= 4) lease = net_vnic_get32(value);
        option += 2 + option_length;
    }
    if (!server[0] && !server[1] && !server[2] && !server[3]) memcpy(server, ip + 12, 4);

    if (message_type == 2 && vnic->state == NET_VNIC_STATE_SELECTING) {
        memcpy(vnic->offered_ip, bootp + 16, 4);
        memcpy(vnic->mask, mask, 4);
        memcpy(vnic->gateway, gateway, 4);
        memcpy(vnic->dns, dns, 4);
        memcpy(vnic->server_ip, server, 4);
        vnic->state = NET_VNIC_STATE_REQUESTING;
        vnic->retries = 1;
        vnic->backoff_ticks = TICKS_PER_SEC;
        vnic->next_tick = uptime_ticks + vnic->backoff_ticks;
        net_vnic_send_dhcp((uint32_t)(vnic - net_vnics), 3, 0);
        return;
    }
    if (message_type == 6 && (vnic->state == NET_VNIC_STATE_REQUESTING || vnic->state == NET_VNIC_STATE_RENEWING)) {
        net_vnic_dhcp_start((uint32_t)(vnic - net_vnics));
        return;
    }
    if (message_type != 5 || (vnic->state != NET_VNIC_STATE_REQUESTING && vnic->state != NET_VNIC_STATE_RENEWING)) return;

    if (bootp[16] || bootp[17] || bootp[18] || bootp[19]) memcpy(vnic->offered_ip, bootp + 16, 4);
    if (mask[0] || mask[1] || mask[2] || mask[3]) memcpy(vnic->mask, mask, 4);
    if (gateway[0] || gateway[1] || gateway[2] || gateway[3]) memcpy(vnic->gateway, gateway, 4);
    if (dns[0] || dns[1] || dns[2] || dns[3]) memcpy(vnic->dns, dns, 4);
    if (lease) vnic->lease_seconds = lease;
    if (!vnic->lease_seconds) vnic->lease_seconds = 3600;
    if (vnic->lease_seconds > 30u * 86400u) vnic->lease_seconds = 30u * 86400u;
    if (!vnic->mask[0] && !vnic->mask[1] && !vnic->mask[2] && !vnic->mask[3]) {
        vnic->mask[0] = 255;
        vnic->mask[1] = 255;
        vnic->mask[2] = 255;
    }
    if (!vnic->dns[0] && !vnic->dns[1] && !vnic->dns[2] && !vnic->dns[3]) memcpy(vnic->dns, vnic->gateway, 4);
    memcpy(vnic->ip, vnic->offered_ip, 4);
    memcpy(vnic->server_ip, server, 4);
    vnic->state = NET_VNIC_STATE_BOUND;
    vnic->bound_tick = uptime_ticks;
    vnic->retries = 0;
}

int net_vnic_attach(uint32_t index, const uint8_t mac[6]) {
    if (index >= NET_VNIC_LIMIT || !mac || (mac[0] & 1)) return -1;
    uint8_t any = 0;
    for (uint32_t i = 0; i < 6; i++) any |= mac[i];
    if (!any) return -1;
    for (uint32_t i = 0; i < NET_VNIC_LIMIT; i++) {
        if (i != index && net_vnics[i].attached && net_vnic_equal(net_vnics[i].mac, mac, 6)) return -1;
    }
    memset(&net_vnics[index], 0, sizeof(net_vnics[index]));
    net_vnics[index].attached = 1;
    net_vnics[index].mode = NET_VNIC_MODE_BRIDGE;
    memcpy(net_vnics[index].mac, mac, 6);
    return 0;
}

int net_vnic_set_mode(uint32_t index, uint32_t mode) {
    if (index >= NET_VNIC_LIMIT || !net_vnics[index].attached || mode > NET_VNIC_MODE_NAT) return -1;
    net_vnics[index].mode = (uint8_t)mode;
    return 0;
}

uint32_t net_vnic_get_mode(uint32_t index) {
    if (index >= NET_VNIC_LIMIT || !net_vnics[index].attached) return NET_VNIC_MODE_BRIDGE;
    return net_vnics[index].mode;
}

void net_vnic_detach(uint32_t index) {
    if (index >= NET_VNIC_LIMIT) return;
    for (uint32_t i = 0; i < VNIC_NAT_CONNECTIONS; i++) {
        if (net_vnic_nat_table[i].active && net_vnic_nat_table[i].index == index) net_vnic_nat_table[i].active = 0;
    }
    memset(&net_vnics[index], 0, sizeof(net_vnics[index]));
}

int net_vnic_dhcp_start(uint32_t index) {
    if (index >= NET_VNIC_LIMIT || !net_vnics[index].attached) return -1;
    net_vnic_t* vnic = &net_vnics[index];
    memset(vnic->ip, 0, sizeof(vnic->ip));
    memset(vnic->mask, 0, sizeof(vnic->mask));
    memset(vnic->gateway, 0, sizeof(vnic->gateway));
    memset(vnic->dns, 0, sizeof(vnic->dns));
    memset(vnic->offered_ip, 0, sizeof(vnic->offered_ip));
    memset(vnic->server_ip, 0, sizeof(vnic->server_ip));
    vnic->xid = 0x53480000u ^ (index << 20) ^ uptime_ticks;
    for (uint32_t i = 0; i < 6; i++) vnic->xid = (vnic->xid * 33u) ^ vnic->mac[i];
    if (vnic->mode == NET_VNIC_MODE_NAT) {
        net_vnic_nat_setup_address(index);
        return 0;
    }
    vnic->lease_seconds = 0;
    vnic->bound_tick = 0;
    vnic->state = NET_VNIC_STATE_SELECTING;
    vnic->backoff_ticks = TICKS_PER_SEC;
    vnic->retries = 1;
    vnic->next_tick = uptime_ticks + vnic->backoff_ticks;
    net_vnic_send_dhcp(index, 1, 0);
    return 0;
}

int net_vnic_is_attached(uint32_t index) {
    return index < NET_VNIC_LIMIT && net_vnics[index].attached;
}

int net_vnic_get_info(uint32_t index, uint8_t mac[6], uint8_t ip[4], uint8_t mask[4], uint8_t gateway[4], uint8_t dns[4], uint32_t* state) {
    if (index >= NET_VNIC_LIMIT || !net_vnics[index].attached) return -1;
    net_vnic_t* vnic = &net_vnics[index];
    if (mac) memcpy(mac, vnic->mac, 6);
    if (ip) memcpy(ip, vnic->ip, 4);
    if (mask) memcpy(mask, vnic->mask, 4);
    if (gateway) memcpy(gateway, vnic->gateway, 4);
    if (dns) memcpy(dns, vnic->dns, 4);
    if (state) *state = vnic->state;
    return 0;
}

int net_vnic_send(uint32_t index, const uint8_t* frame, uint32_t len) {
    if (index >= NET_VNIC_LIMIT || !net_vnics[index].attached || !frame || len < 14 || len > NET_VNIC_FRAME_MAX) return -1;
    if (net_vnics[index].mode == NET_VNIC_MODE_NAT) return net_vnic_nat_send(index, frame, len);
    uint8_t output[NET_VNIC_FRAME_MAX];
    memcpy(output, frame, len);
    memcpy(output + 6, net_vnics[index].mac, 6);
    net_send_raw(output, len);
    return (int)len;
}

int net_vnic_receive(uint32_t index, uint8_t* frame, uint32_t capacity) {
    if (index >= NET_VNIC_LIMIT || !net_vnics[index].attached || !frame || !net_vnics[index].rx_count) return -1;
    net_vnic_t* vnic = &net_vnics[index];
    struct net_vnic_frame* queued = &vnic->rx[vnic->rx_head];
    if (queued->length > capacity) return -1;
    uint16_t length = queued->length;
    memcpy(frame, queued->bytes, length);
    vnic->rx_head = (uint8_t)((vnic->rx_head + 1) % VNIC_RX_QUEUE);
    vnic->rx_count--;
    return length;
}

static int net_vnic_resolve(uint32_t index, const uint8_t* target_ip, uint8_t* target_mac) {
    net_vnic_t* vnic = &net_vnics[index];
    if (vnic->mode == NET_VNIC_MODE_NAT) {
        memcpy(target_mac, net_vnic_nat_gateway_mac, 6);
        return 1;
    }
    uint8_t request[42] = {0};
    memset(request, 0xFF, 6);
    memcpy(request + 6, vnic->mac, 6);
    request[12] = 0x08;
    request[13] = 0x06;
    uint8_t* arp = request + 14;
    net_vnic_put16(arp, 1);
    net_vnic_put16(arp + 2, 0x0800);
    arp[4] = 6;
    arp[5] = 4;
    net_vnic_put16(arp + 6, 1);
    memcpy(arp + 8, vnic->mac, 6);
    memcpy(arp + 14, vnic->ip, 4);
    memcpy(arp + 24, target_ip, 4);
    if (net_vnic_send(index, request, sizeof(request)) < 0) return 0;
    uint8_t incoming[NET_VNIC_FRAME_MAX];
    uint32_t start = uptime_ticks;
    while ((uint32_t)(uptime_ticks - start) < 2000) {
        net_poll();
        int length;
        while ((length = net_vnic_receive(index, incoming, sizeof(incoming))) > 0) {
            if (length < 42 || incoming[12] != 0x08 || incoming[13] != 0x06) continue;
            const uint8_t* response = incoming + 14;
            if (net_vnic_get16(response + 6) != 2 || !net_vnic_equal(response + 14, target_ip, 4) || !net_vnic_equal(response + 24, vnic->ip, 4)) continue;
            memcpy(target_mac, response + 8, 6);
            return 1;
        }
        delay_ms(1);
    }
    return 0;
}

int net_vnic_ping(uint32_t index, const uint8_t destination[4], int count) {
    if (index >= NET_VNIC_LIMIT || !net_vnics[index].attached || !destination) {
        terminal_writestring("PING: virtual adapter is not attached\n");
        return -1;
    }
    net_vnic_t* vnic = &net_vnics[index];
    if (vnic->state != NET_VNIC_STATE_BOUND) {
        terminal_writestring("PING: virtual adapter has no configured address\n");
        return -1;
    }
    if (count < 1) count = 4;
    if (count > 10) count = 10;
    uint8_t next_hop[4];
    if (vnic->mode == NET_VNIC_MODE_NAT) memcpy(next_hop, vnic->gateway, 4);
    else {
        int local = 1;
        for (uint32_t i = 0; i < 4; i++) if ((destination[i] & vnic->mask[i]) != (vnic->ip[i] & vnic->mask[i])) local = 0;
        memcpy(next_hop, local ? destination : vnic->gateway, 4);
    }
    uint8_t destination_mac[6];
    if (!net_vnic_resolve(index, next_hop, destination_mac)) {
        terminal_writestring("PING: unable to resolve virtual adapter gateway or neighbor\n");
        return 0;
    }
    int received = 0;
    for (int sequence = 0; sequence < count; sequence++) {
        uint8_t packet[14 + 20 + 40] = {0};
        memcpy(packet, destination_mac, 6);
        memcpy(packet + 6, vnic->mac, 6);
        packet[12] = 0x08;
        packet[13] = 0x00;
        uint8_t* ip = packet + 14;
        ip[0] = 0x45;
        net_vnic_put16(ip + 2, 60);
        net_vnic_put16(ip + 4, (uint16_t)sequence);
        net_vnic_put16(ip + 6, 0x4000);
        ip[8] = 64;
        ip[9] = VNIC_IP_ICMP;
        memcpy(ip + 12, vnic->ip, 4);
        memcpy(ip + 16, destination, 4);
        net_vnic_rechecksum_ip(ip, 20);
        uint8_t* icmp = ip + 20;
        icmp[0] = 8;
        net_vnic_put16(icmp + 4, (uint16_t)(0x5300 | index));
        net_vnic_put16(icmp + 6, (uint16_t)(sequence + 1));
        for (uint32_t i = 0; i < 32; i++) icmp[8 + i] = (uint8_t)('A' + (i % 26));
        net_vnic_put16(icmp + 2, net_vnic_checksum(icmp, 40));
        if (net_vnic_send(index, packet, sizeof(packet)) < 0) {
            terminal_writestring("PING: unable to transmit echo request\n");
            continue;
        }
        uint32_t start = uptime_ticks;
        int got_reply = 0;
        uint8_t incoming[NET_VNIC_FRAME_MAX];
        while ((uint32_t)(uptime_ticks - start) < 1000) {
            net_poll();
            int length;
            while ((length = net_vnic_receive(index, incoming, sizeof(incoming))) > 0) {
                if (length < 14 + 20 + 8 || incoming[12] != 0x08 || incoming[13] != 0x00) continue;
                const uint8_t* reply_ip = incoming + 14;
                uint32_t ihl = (uint32_t)(reply_ip[0] & 0x0F) * 4;
                uint32_t total = net_vnic_get16(reply_ip + 2);
                if ((reply_ip[0] >> 4) != 4 || reply_ip[9] != VNIC_IP_ICMP || ihl < 20 || total < ihl + 8 || total > (uint32_t)length - 14) continue;
                const uint8_t* reply_icmp = reply_ip + ihl;
                if (reply_icmp[0] != 0 || reply_icmp[1] != 0 || net_vnic_checksum(reply_ip, ihl) != 0 || net_vnic_checksum(reply_icmp, total - ihl) != 0 || net_vnic_get16(reply_icmp + 4) != (uint16_t)(0x5300 | index) || net_vnic_get16(reply_icmp + 6) != (uint16_t)(sequence + 1) || !net_vnic_equal(reply_ip + 12, destination, 4) || !net_vnic_equal(reply_ip + 16, vnic->ip, 4)) continue;
                got_reply = 1;
                break;
            }
            if (got_reply) break;
            delay_ms(1);
        }
        uint32_t elapsed = uptime_ticks - start;
        char number[16];
        if (got_reply) {
            received++;
            terminal_writestring("PING: reply from ");
            for (uint32_t i = 0; i < 4; i++) {
                int_to_string(destination[i], number);
                terminal_writestring(number);
                if (i < 3) terminal_writestring(".");
            }
            terminal_writestring(" seq=");
            int_to_string((uint32_t)(sequence + 1), number);
            terminal_writestring(number);
            terminal_writestring(" time=");
            if (elapsed < 1) terminal_writestring("<1");
            else { int_to_string(elapsed, number); terminal_writestring(number); }
            terminal_writestring(" ms\n");
        } else {
            terminal_writestring("PING: timeout seq=");
            int_to_string((uint32_t)(sequence + 1), number);
            terminal_writestring(number);
            terminal_writestring("\n");
        }
        if (sequence + 1 < count && elapsed < 1000) delay_ms(1000 - elapsed);
    }
    char number[16];
    terminal_writestring("PING: ");
    int_to_string((uint32_t)received, number);
    terminal_writestring(number);
    terminal_writestring("/");
    int_to_string((uint32_t)count, number);
    terminal_writestring(number);
    terminal_writestring(" replies\n");
    return received;
}

void net_vnic_rx_frame(const uint8_t* frame, uint32_t len) {
    if (!frame || len < 14 || len > NET_VNIC_FRAME_MAX) return;
    for (uint32_t index = 0; index < NET_VNIC_LIMIT; index++) {
        if (net_vnics[index].attached && net_vnics[index].mode == NET_VNIC_MODE_NAT) net_vnic_nat_receive(index, frame, len);
    }
    int broadcast = 1;
    for (uint32_t i = 0; i < 6; i++) if (frame[i] != 0xFF) broadcast = 0;
    int multicast = (frame[0] & 1) != 0;
    for (uint32_t index = 0; index < NET_VNIC_LIMIT; index++) {
        net_vnic_t* vnic = &net_vnics[index];
        if (!vnic->attached || vnic->mode == NET_VNIC_MODE_NAT) continue;
        if (!broadcast && !multicast && !net_vnic_equal(frame, vnic->mac, 6)) continue;
        net_vnic_receive_dhcp(vnic, frame, len);
        net_vnic_handle_frame(index, frame, len);
        if (vnic->rx_count >= VNIC_RX_QUEUE) continue;
        uint32_t slot = (vnic->rx_head + vnic->rx_count) % VNIC_RX_QUEUE;
        vnic->rx[slot].length = (uint16_t)len;
        memcpy(vnic->rx[slot].bytes, frame, len);
        vnic->rx_count++;
    }
}

void net_vnic_poll(void) {
    for (uint32_t i = 0; i < VNIC_NAT_CONNECTIONS; i++) {
        if (net_vnic_nat_table[i].active && (int32_t)(uptime_ticks - net_vnic_nat_table[i].expires) >= 0) net_vnic_nat_table[i].active = 0;
    }
    for (uint32_t index = 0; index < NET_VNIC_LIMIT; index++) {
        net_vnic_t* vnic = &net_vnics[index];
        if (!vnic->attached) continue;
        if (vnic->state == NET_VNIC_STATE_BOUND && vnic->lease_seconds &&
            uptime_ticks - vnic->bound_tick >= (vnic->lease_seconds / 2) * TICKS_PER_SEC) {
            vnic->state = NET_VNIC_STATE_RENEWING;
            vnic->retries = 1;
            vnic->backoff_ticks = 2000;
            vnic->next_tick = uptime_ticks + vnic->backoff_ticks;
            net_vnic_send_dhcp(index, 3, 1);
            continue;
        }
        if (vnic->state != NET_VNIC_STATE_SELECTING && vnic->state != NET_VNIC_STATE_REQUESTING && vnic->state != NET_VNIC_STATE_RENEWING) continue;
        if ((int32_t)(uptime_ticks - vnic->next_tick) < 0) continue;
        if (vnic->retries >= 6) {
            if (vnic->state == NET_VNIC_STATE_RENEWING) {
                vnic->state = NET_VNIC_STATE_BOUND;
                vnic->bound_tick = uptime_ticks;
                continue;
            }
            vnic->state = NET_VNIC_STATE_FAILED;
            continue;
        }
        if (vnic->state == NET_VNIC_STATE_SELECTING) net_vnic_send_dhcp(index, 1, 0);
        else net_vnic_send_dhcp(index, 3, vnic->state == NET_VNIC_STATE_RENEWING);
        vnic->retries++;
        if (vnic->backoff_ticks < 8000) vnic->backoff_ticks *= 2;
        if (vnic->backoff_ticks > 8000) vnic->backoff_ticks = 8000;
        vnic->next_tick = uptime_ticks + vnic->backoff_ticks;
    }
}
