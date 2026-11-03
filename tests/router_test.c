#include "kernel.h"
#include "net.h"
#include "firewall.h"
#include "router.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Host-side test harness for the SharkOS router. It stubs the small kernel
 * surface that router.c / router_cli.c / firewall.c need, then injects real
 * ethernet/IP/UDP frames into the registered service hooks. */

#define TEST_DHCP_PACKET_SIZE 300

volatile uint32_t uptime_ticks;
uint8_t net_mac[6] = {0x52, 0x61, 0x72, 0x6b, 0x00, 0x01};
uint8_t net_ip[4] = {0, 0, 0, 0};
uint8_t net_mask[4] = {0, 0, 0, 0};
uint8_t net_gw[4] = {0, 0, 0, 0};
uint8_t net_dns[4] = {0, 0, 0, 0};
int net_configured = 0;
int net_has_link = 1;
char net_driver_name[32] = "test-nic";

static net_service_hook_t test_service_hook;
static net_arp_hook_t test_arp_hook;
static net_local_ip_hook_t test_local_hook;
static net_forward_hook_t test_forward_hook;
static net_poll_hook_t test_poll_hook;

void net_set_service_hook(net_service_hook_t cb) { test_service_hook = cb; }
void net_set_arp_hook(net_arp_hook_t cb) { test_arp_hook = cb; }
void net_set_local_ip_hook(net_local_ip_hook_t cb) { test_local_hook = cb; }
void net_set_forward_hook(net_forward_hook_t cb) { test_forward_hook = cb; }
void net_set_poll_hook(net_poll_hook_t cb) { test_poll_hook = cb; }
void net_set_rx_callback(net_rx_cb_t cb) { (void)cb; }

int net_has_nic(void) { return 1; }
void terminal_writestring(const char* text) { (void)text; }

/* Fake a machine with an uplink adapter and a second NIC for the LAN. */
static net_port_t test_ports[2];
void net_port_scan(void) {
    memset(test_ports, 0, sizeof(test_ports));
    strcpy(test_ports[0].ifname, "em0");
    strcpy(test_ports[0].driver, "Intel PRO/1000 (e1000)");
    memcpy(test_ports[0].mac, net_mac, 6);
    test_ports[0].vendor = 0x8086; test_ports[0].device = 0x100E;
    test_ports[0].bus = 0; test_ports[0].slot = 3; test_ports[0].func = 0;
    test_ports[0].active = 1; test_ports[0].link = 1;
    strcpy(test_ports[1].ifname, "vtnet0");
    strcpy(test_ports[1].driver, "virtio-net");
    test_ports[1].vendor = 0x1AF4; test_ports[1].device = 0x1000;
    test_ports[1].bus = 0; test_ports[1].slot = 4; test_ports[1].func = 0;
}
uint32_t net_port_count(void) { return 2; }
int net_port_get(uint32_t index, net_port_t* out) {
    if (index >= 2 || !out) return -1;
    *out = test_ports[index];
    return 0;
}

static int test_dhcp_disabled = 0;
void net_dhcp_start(void) { test_dhcp_disabled = 0; }
void net_dhcp_stop(void) { test_dhcp_disabled = 1; }
int net_dhcp_is_disabled(void) { return test_dhcp_disabled; }
const char* net_dhcp_status(void) { return "test"; }
uint32_t net_dhcp_lease_left(void) { return 0; }
int net_dhcp_state(void) { return 0; }

int net_set_static_config(const uint8_t* ip, const uint8_t* mask,
                          const uint8_t* gw, const uint8_t* dns) {
    memcpy(net_ip, ip, 4);
    if (mask) memcpy(net_mask, mask, 4);
    if (gw) memcpy(net_gw, gw, 4);
    if (dns) memcpy(net_dns, dns, 4);
    net_configured = 1;
    test_dhcp_disabled = 1;
    return 0;
}

int net_parse_ip4(const char* str, uint8_t* out) {
    int oct = 0, val = -1;
    for (const char* p = str; ; p++) {
        if (*p >= '0' && *p <= '9') {
            if (val < 0) val = 0;
            val = val * 10 + (*p - '0');
            if (val > 255) return 0;
        } else if (*p == '.' || *p == '\0') {
            if (val < 0 || oct >= 4) return 0;
            out[oct++] = (uint8_t)val;
            val = -1;
            if (*p == '\0') break;
        } else return 0;
    }
    return oct == 4;
}

#define TEST_SENT_MAX 2048
static uint8_t sent_data[TEST_SENT_MAX];
static uint32_t sent_len;
static uint8_t sent_src_ip[4], sent_dst_ip[4], sent_dst_mac[6];
static uint16_t sent_src_port, sent_dst_port;
static uint32_t sent_count;
static uint8_t raw_frame[2048];
static uint32_t raw_len, raw_count;

int net_send_udp(const uint8_t* dst_mac, const uint8_t* src_ip, const uint8_t* dst_ip,
                 uint16_t src_port, uint16_t dst_port,
                 const uint8_t* data, uint32_t len) {
    sent_len = len > TEST_SENT_MAX ? TEST_SENT_MAX : len;
    memcpy(sent_data, data, sent_len);
    memcpy(sent_src_ip, src_ip, 4);
    memcpy(sent_dst_ip, dst_ip, 4);
    memcpy(sent_dst_mac, dst_mac, 6);
    sent_src_port = src_port;
    sent_dst_port = dst_port;
    sent_count++;
    return (int)len;
}

void net_send_raw(const uint8_t* data, uint32_t len) {
    raw_len = len > sizeof(raw_frame) ? sizeof(raw_frame) : len;
    memcpy(raw_frame, data, raw_len);
    raw_count++;
}

int net_arp_cache_lookup(const uint8_t* ip, uint8_t* mac_out) {
    (void)ip;
    memcpy(mac_out, net_mac, 6);
    return 1;
}

int net_route_mac(const uint8_t* ip, uint8_t* mac_out) {
    (void)ip;
    uint8_t gateway_mac[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
    memcpy(mac_out, gateway_mac, 6);
    return 1;
}

void net_arp_request(const uint8_t* ip) { (void)ip; }
int net_tcp_is_listening(uint16_t port) { (void)port; return 0; }
int net_tcp_listen(uint16_t port) { (void)port; return 0; }
void net_tcp_server_stop(uint16_t port) { (void)port; }
int net_tcp_server_accept(uint16_t port) { (void)port; return -1; }
int net_tcp_server_send(int handle, const uint8_t* data, uint32_t len) {
    (void)handle; (void)data; return (int)len;
}
int net_tcp_server_recv(int handle, uint8_t* out, uint32_t max, uint32_t timeout_ms) {
    (void)handle; (void)out; (void)max; (void)timeout_ms; return 0;
}
int net_tcp_server_closed(int handle) { (void)handle; return 1; }
void net_tcp_server_close(int handle) { (void)handle; }

/* router_http.c is not linked into this test; these stubs stand in for it. */
int router_http_start(uint16_t port) { (void)port; return 0; }
void router_http_stop(void) { }
void router_http_poll(void) { }
int router_http_is_active(void) { return router_get_config()->http_running; }
uint16_t router_http_port(void) { return router_get_config()->http_port; }

/* Minimal in-memory filesystem for `rtr save` / `rtr load`. */
struct fs_node* root;
static struct fs_node test_root_node;
static struct fs_node test_dir_node;
static struct fs_node test_file_node;
static int test_file_exists;

static void fs_setup(void) {
    memset(&test_root_node, 0, sizeof(test_root_node));
    memset(&test_dir_node, 0, sizeof(test_dir_node));
    memset(&test_file_node, 0, sizeof(test_file_node));
    strcpy(test_root_node.name, "/");
    test_root_node.type = FS_DIRECTORY;
    strcpy(test_dir_node.name, "Config");
    test_dir_node.type = FS_DIRECTORY;
    strcpy(test_file_node.name, "router.conf");
    test_file_node.type = FS_FILE;
    test_file_node.content_capacity = 8192;
    test_file_node.content = (char*)malloc(test_file_node.content_capacity);
    test_file_node.content_len = 0;
    root = &test_root_node;
    test_file_exists = 0;
}

struct fs_node* fs_resolve_path(const char* path) {
    if (!path) return 0;
    if (strcmp(path, "/") == 0) return &test_root_node;
    if (strcmp(path, "/System/Config") == 0) return &test_dir_node;
    if (strstr(path, "router.conf")) return test_file_exists ? &test_file_node : 0;
    return 0;
}

struct fs_node* create_node(const char* name, node_type_t type, struct fs_node* parent) {
    (void)name; (void)type; (void)parent;
    test_file_exists = 1;
    test_file_node.content_len = 0;
    return &test_file_node;
}

int fs_node_truncate(struct fs_node* node, uint32_t length) {
    (void)length;
    if (!node) return -1;
    node->content_len = 0;
    return 0;
}

int fs_node_write(struct fs_node* node, uint32_t offset, const void* data, uint32_t length) {
    if (!node || !node->content) return -1;
    if (offset + length > node->content_capacity) return -1;
    memcpy(node->content + offset, data, length);
    if ((int)(offset + length) > node->content_len) node->content_len = (int)(offset + length);
    return 0;
}

int fs_node_sync(struct fs_node* node) { (void)node; return 0; }
const char* fs_node_data(const struct fs_node* node) { return node ? node->content : 0; }

/* ------------------------------------------------------------------ */
/* frame helpers                                                       */
/* ------------------------------------------------------------------ */

static uint16_t checksum16(const uint8_t* data, uint32_t length) {
    uint32_t sum = 0;
    for (uint32_t i = 0; i < length; i += 2)
        sum += ((uint32_t)data[i] << 8) | ((i + 1 < length) ? data[i + 1] : 0);
    while (sum >> 16) sum = (sum & 0xFFFFu) + (sum >> 16);
    return (uint16_t)~sum;
}

static void put16(uint8_t* p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }

static uint32_t make_udp_frame(uint8_t* frame, const uint8_t* src_mac,
                               const uint8_t* src_ip, const uint8_t* dst_ip,
                               uint16_t sport, uint16_t dport,
                               const uint8_t* payload, uint32_t payload_len) {
    memset(frame, 0, 1536);
    uint8_t dst_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    memcpy(frame, dst_mac, 6);
    memcpy(frame + 6, src_mac, 6);
    frame[12] = 0x08; frame[13] = 0x00;
    uint8_t* ip = frame + 14;
    ip[0] = 0x45;
    put16(ip + 2, (uint16_t)(20 + 8 + payload_len));
    ip[8] = 64; ip[9] = 17;
    memcpy(ip + 12, src_ip, 4);
    memcpy(ip + 16, dst_ip, 4);
    put16(ip + 10, 0);
    put16(ip + 10, checksum16(ip, 20));
    uint8_t* udp = ip + 20;
    put16(udp, sport);
    put16(udp + 2, dport);
    put16(udp + 4, (uint16_t)(8 + payload_len));
    put16(udp + 6, 0);
    if (payload && payload_len) memcpy(udp + 8, payload, payload_len);
    return 14 + 20 + 8 + payload_len;
}

static uint32_t make_tcp_frame(uint8_t* frame, const uint8_t* src_ip,
                               const uint8_t* dst_ip, uint16_t sport, uint16_t dport) {
    memset(frame, 0, 64);
    frame[12] = 0x08; frame[13] = 0x00;
    uint8_t* ip = frame + 14;
    ip[0] = 0x45;
    put16(ip + 2, 40);
    ip[8] = 64; ip[9] = 6;
    memcpy(ip + 12, src_ip, 4);
    memcpy(ip + 16, dst_ip, 4);
    put16(ip + 10, 0);
    put16(ip + 10, checksum16(ip, 20));
    uint8_t* tcp = ip + 20;
    put16(tcp, sport);
    put16(tcp + 2, dport);
    tcp[12] = 0x50;
    tcp[13] = 0x02;
    return 54;
}

static uint32_t make_icmp_frame(uint8_t* frame, const uint8_t* src_mac,
                                const uint8_t* src_ip, const uint8_t* dst_ip,
                                uint8_t type, uint16_t echo_id) {
    memset(frame, 0, 64);
    uint8_t dst_mac[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
    memcpy(frame, dst_mac, 6);
    memcpy(frame + 6, src_mac, 6);
    frame[12] = 0x08; frame[13] = 0x00;
    uint8_t* ip = frame + 14;
    ip[0] = 0x45;
    put16(ip + 2, 28);
    ip[8] = 64; ip[9] = 1;
    memcpy(ip + 12, src_ip, 4);
    memcpy(ip + 16, dst_ip, 4);
    put16(ip + 10, 0);
    put16(ip + 10, checksum16(ip, 20));
    uint8_t* icmp = ip + 20;
    icmp[0] = type; icmp[1] = 0;
    put16(icmp + 4, echo_id);
    put16(icmp + 6, 1);
    put16(icmp + 2, 0);
    put16(icmp + 2, checksum16(icmp, 8));
    return 42;
}

static uint8_t dhcp_msg(uint8_t* buf, uint8_t type, const uint8_t* mac,
                        uint32_t xid, const uint8_t* requested,
                        const uint8_t* server_id, const char* hostname) {
    memset(buf, 0, 300);
    buf[0] = 1; buf[1] = 1; buf[2] = 6;
    put16(buf + 4, 0); /* xid filled below */
    buf[4] = (uint8_t)(xid >> 24); buf[5] = (uint8_t)(xid >> 16);
    buf[6] = (uint8_t)(xid >> 8); buf[7] = (uint8_t)xid;
    put16(buf + 10, 0x8000);
    memcpy(buf + 28, mac, 6);
    buf[236] = 0x63; buf[237] = 0x82; buf[238] = 0x53; buf[239] = 0x63;
    uint32_t o = 240;
    buf[o++] = 53; buf[o++] = 1; buf[o++] = type;
    if (requested) { buf[o++] = 50; buf[o++] = 4; memcpy(buf + o, requested, 4); o += 4; }
    if (server_id) { buf[o++] = 54; buf[o++] = 4; memcpy(buf + o, server_id, 4); o += 4; }
    if (hostname) {
        uint32_t n = (uint32_t)strlen(hostname);
        buf[o++] = 12; buf[o++] = (uint8_t)n; memcpy(buf + o, hostname, n); o += n;
    }
    buf[o++] = 0xFF;
    return (uint8_t)0;
}

static uint8_t dhcp_option_type(const uint8_t* msg, uint32_t len) {
    for (uint32_t p = 240; p + 1 < len; p++) {
        if (msg[p] == 0xFF) break;
        if (msg[p] == 0) { p++; continue; }
        if (msg[p] == 53 && p + 2 < len) return msg[p + 2];
        if (p + 2 + msg[p + 1] > len) break;
        p += 1 + msg[p + 1];
    }
    return 0;
}

static int dhcp_has_option(const uint8_t* msg, uint32_t len, uint8_t code) {
    for (uint32_t p = 240; p + 1 < len; p++) {
        if (msg[p] == 0xFF) break;
        if (msg[p] == 0) { p++; continue; }
        if (msg[p] == code) return 1;
        if (p + 2 + msg[p + 1] > len) break;
        p += 1 + msg[p + 1];
    }
    return 0;
}

static uint32_t dns_query(uint8_t* out, uint16_t id, const char* name, uint16_t qtype) {
    memset(out, 0, 256);
    put16(out, id);
    put16(out + 2, 0x0100);
    put16(out + 4, 1);
    uint32_t pos = 12;
    const char* p = name;
    while (*p) {
        const char* dot = p;
        while (*dot && *dot != '.') dot++;
        uint32_t n = (uint32_t)(dot - p);
        out[pos++] = (uint8_t)n;
        memcpy(out + pos, p, n);
        pos += n;
        p = *dot ? dot + 1 : dot;
    }
    out[pos++] = 0;
    put16(out + pos, qtype); pos += 2;
    put16(out + pos, 1); pos += 2;
    return pos;
}

static uint32_t dns_upstream_answer(uint8_t* out, const uint8_t* query, uint32_t query_len,
                                    const uint8_t* answer_ip) {
    memcpy(out, query, query_len);
    put16(out + 2, 0x8180);
    put16(out + 6, 1);
    uint32_t pos = query_len;
    out[pos++] = 0xC0; out[pos++] = 0x0C;
    put16(out + pos, 1); pos += 2;
    put16(out + pos, 1); pos += 2;
    out[pos++] = 0; out[pos++] = 0; out[pos++] = 0; out[pos++] = 60;
    put16(out + pos, 4); pos += 2;
    memcpy(out + pos, answer_ip, 4); pos += 4;
    return pos;
}

static int cli(const char* text, char* out, uint32_t cap) {
    return router_cli_exec(0, text, out, cap);
}

int main(void) {
    uint8_t frame[1536];
    uint8_t payload[512];
    uint8_t client_mac[6] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
    uint8_t zero[4] = {0, 0, 0, 0};
    uint8_t broadcast[4] = {255, 255, 255, 255};
    uint8_t lan[4] = {192, 168, 10, 1};
    uint8_t pool_ip[4] = {192, 168, 10, 100};
    char buf[8192];

    fs_setup();
    firewall_init();
    router_init();
    if (!test_service_hook || !test_arp_hook || !test_local_hook || !test_forward_hook || !test_poll_hook) return 1;
    if (router_get_config()->http_port != ROUTER_DEFAULT_HTTP_PORT) return 2;
    if (test_local_hook(lan) != 0) return 3;   /* inactive until rtr start */

    if (router_start(69) != 0) return 4;
    if (test_local_hook(lan) != 1) return 53;
    if (!router_is_running() || !router_get_config()->http_running) return 5;
    if (firewall_service_count() < 3) return 6;

    /* ---- DHCP DISCOVER -> OFFER ---- */
    dhcp_msg(payload, 1, client_mac, 0x12345678, 0, 0, "testbox");
    uint32_t frame_len = make_udp_frame(frame, client_mac, zero, broadcast, 68, 67,
                                       payload, TEST_DHCP_PACKET_SIZE);
    sent_count = 0;
    test_service_hook(frame, frame_len, zero, broadcast, 68, 67, 17);
    if (sent_count != 1 || sent_src_port != 67 || sent_dst_port != 68) return 7;
    if (dhcp_option_type(sent_data, sent_len) != 2) return 8;   /* OFFER */
    uint8_t offered[4];
    memcpy(offered, sent_data + 16, 4);
    if (memcmp(offered, pool_ip, 4) != 0) return 9;
    if (!dhcp_has_option(sent_data, sent_len, 1) ||
        !dhcp_has_option(sent_data, sent_len, 3) ||
        !dhcp_has_option(sent_data, sent_len, 6) ||
        !dhcp_has_option(sent_data, sent_len, 51)) return 10;

    /* ---- DHCP REQUEST -> ACK + lease ---- */
    dhcp_msg(payload, 3, client_mac, 0x12345678, offered, lan, "laptop");
    frame_len = make_udp_frame(frame, client_mac, zero, broadcast, 68, 67,
                               payload, TEST_DHCP_PACKET_SIZE);
    sent_count = 0;
    test_service_hook(frame, frame_len, zero, broadcast, 68, 67, 17);
    if (sent_count != 1 || dhcp_option_type(sent_data, sent_len) != 5) return 11;
    const router_lease_t* lease = router_get_lease(0);
    if (!lease || !lease->bound || memcmp(lease->ip, offered, 4) != 0) return 12;
    if (strcmp(lease->hostname, "laptop") != 0) return 13;
    if (router_lease_count() != 1) return 14;

    /* ---- RELEASE ---- */
    dhcp_msg(payload, 7, client_mac, 0xAABBCCDD, offered, lan, 0);
    frame_len = make_udp_frame(frame, client_mac, offered, lan, 68, 67,
                               payload, TEST_DHCP_PACKET_SIZE);
    test_service_hook(frame, frame_len, offered, lan, 68, 67, 17);
    if (router_lease_count() != 0) return 15;

    /* ---- DNS static record ---- */
    uint8_t nas_ip[4] = {192, 168, 10, 10};
    if (router_dns_add_record("nas.lan", nas_ip, 1) != 0) return 16;
    uint32_t qlen = dns_query(payload, 0xBEEF, "nas.lan", 1);
    uint8_t client_ip[4] = {192, 168, 10, 77};
    frame_len = make_udp_frame(frame, client_mac, client_ip, lan, 53001, 53, payload, qlen);
    sent_count = 0;
    test_service_hook(frame, frame_len, client_ip, lan, 53001, 53, 17);
    if (sent_count != 1 || sent_dst_port != 53001) return 17;
    if (sent_len < 12) return 18;
    if ((sent_data[3] & 0x0F) != 0) return 19;                  /* rcode = 0 */
    if (((sent_data[6] << 8) | sent_data[7]) != 1) return 20;  /* one answer */
    if (memcmp(sent_data + sent_len - 4, nas_ip, 4) != 0) return 21;

    /* ---- DNS forwarding + upstream response ---- */
    uint8_t upstream[4] = {9, 9, 9, 9};
    router_dns_set_upstream(upstream, 0);
    qlen = dns_query(payload, 0xCAFE, "example.com", 1);
    frame_len = make_udp_frame(frame, client_mac, client_ip, lan, 53002, 53, payload, qlen);
    sent_count = 0;
    test_service_hook(frame, frame_len, client_ip, lan, 53002, 53, 17);
    if (sent_count != 1 || sent_dst_port != 53) return 22;
    uint8_t upstream_query[256];
    uint32_t upstream_query_len = sent_len;
    memcpy(upstream_query, sent_data, upstream_query_len);
    uint16_t upstream_id = (uint16_t)((upstream_query[0] << 8) | upstream_query[1]);
    uint16_t upstream_sport = sent_src_port;
    uint8_t answered[4] = {93, 184, 216, 34};
    uint8_t reply[512];
    uint32_t reply_len = dns_upstream_answer(reply, upstream_query, upstream_query_len, answered);
    uint8_t wan_ip[4] = {10, 0, 2, 15};
    sent_count = 0;
    frame_len = make_udp_frame(frame, client_mac, upstream, wan_ip, 53, upstream_sport,
                               reply, reply_len);
    test_service_hook(frame, frame_len, upstream, wan_ip, 53, upstream_sport, 17);
    if (sent_count != 1 || sent_dst_port != 53002) return 23;
    if (memcmp(sent_data + sent_len - 4, answered, 4) != 0) return 24;
    int cached = 0;
    for (uint32_t i = 0; i < ROUTER_DNS_CACHE_LIMIT; i++) {
        const router_dns_entry_t* e = router_get_dns_entry(i);
        if (e && strcmp(e->name, "example.com") == 0 && memcmp(e->ip, answered, 4) == 0) cached = 1;
    }
    if (!cached) return 25;
    (void)upstream_id;

    /* ---- WAN static config via CLI ---- */
    buf[0] = 0;
    cli("rtr wan static 10.0.0.5 255.255.255.0 10.0.0.1 1.1.1.1", buf, sizeof(buf));
    if (net_ip[0] != 10 || net_ip[3] != 5 || !net_configured) return 26;
    if (router_get_config()->wan_mode != ROUTER_WAN_STATIC) return 27;

    /* ---- extended firewall rule ---- */
    buf[0] = 0;
    cli("fw add deny tcp dir in from 203.0.113.0/24 dport 22 log comment \"no ssh\"", buf, sizeof(buf));
    if (firewall_rule_count() != 1) return 28;
    firewall_rule_t rule;
    if (firewall_rule_get(1, &rule) != 0) return 29;
    if (rule.protocol != FIREWALL_PROTOCOL_TCP || rule.direction != FIREWALL_DIR_IN ||
        rule.source_prefix != 24 || rule.port != 22 || !rule.log) return 30;
    uint8_t blocked_src[4] = {203, 0, 113, 9};
    uint8_t allowed_src[4] = {198, 51, 100, 2};
    uint32_t tcp_len = make_tcp_frame(frame, blocked_src, lan, 40000, 22);
    if (firewall_input_allowed(frame, tcp_len)) return 31;
    tcp_len = make_tcp_frame(frame, allowed_src, lan, 40000, 22);
    if (!firewall_input_allowed(frame, tcp_len)) return 32;
    tcp_len = make_tcp_frame(frame, blocked_src, lan, 40000, 8080);
    if (!firewall_input_allowed(frame, tcp_len)) return 33;
    if (firewall_hit_count(1) < 1) return 34;
    if (firewall_log_count() < 1) return 35;
    if (!strstr(buf, "added firewall rule")) return 36;

    /* ---- policies, services and profiles ---- */
    buf[0] = 0;
    cli("fw policy forward deny", buf, sizeof(buf));
    if (firewall_get_policy(FIREWALL_DIR_FORWARD) != FIREWALL_ACTION_DENY) return 37;
    cli("fw profile router", buf, sizeof(buf));
    if (firewall_rule_count() < 3) return 38;
    cli("fw service add udp 9999", buf, sizeof(buf));
    if (firewall_service_count() < 4) return 39;
    /* WAN-side DHCP/DNS/console requests must be denied by the profile. */
    uint8_t probe_payload[4] = {0};
    uint8_t probe_wan[4] = {10, 0, 0, 99};
    uint8_t probe_lan[4] = {192, 168, 10, 77};
    uint32_t probe_len = make_udp_frame(frame, client_mac, probe_wan, net_ip,
                                        68, 67, probe_payload, sizeof(probe_payload));
    if (firewall_input_allowed(frame, probe_len)) return 200;
    probe_len = make_udp_frame(frame, client_mac, probe_lan, lan,
                               68, 67, probe_payload, sizeof(probe_payload));
    if (!firewall_input_allowed(frame, probe_len)) return 201;
    uint32_t tcp_probe = make_tcp_frame(frame, probe_wan, net_ip, 40000, 69);
    if (firewall_input_allowed(frame, tcp_probe)) return 202;
    tcp_probe = make_tcp_frame(frame, probe_lan, lan, 40000, 69);
    if (!firewall_input_allowed(frame, tcp_probe)) return 203;
    /* Changing the console port must move the WAN-side deny rule too. */
    cli("rtr http port 6969", buf, sizeof(buf));
    int found = 0;
    for (uint32_t i = 0; i < firewall_rule_count(); i++) {
        const firewall_rule_t* r = firewall_get_rule(i);
        if (r && strcmp(r->comment, "wan console blocked") == 0) {
            if (r->port != 6969) return 204;
            found = 1;
        }
    }
    if (!found) return 205;
    cli("rtr http port 69", buf, sizeof(buf));
    if (router_get_config()->http_port != 69) return 206;

    /* ---- port forward + NAT outbound rewrite ---- */
    uint8_t lan_client[4] = {192, 168, 10, 42};
    if (router_forward_add(ROUTER_PROTO_UDP, 8080, lan_client, 80, "web") != 0) return 40;
    uint8_t remote[4] = {8, 8, 4, 4};
    uint8_t nat_payload[16];
    memset(nat_payload, 0xAB, sizeof(nat_payload));
    uint32_t nat_len = make_udp_frame(frame, client_mac, lan_client, remote, 40000, 12345,
                                      nat_payload, sizeof(nat_payload));
    /* forwarded frames reach the forward hook directly in the kernel */
    raw_count = 0;
    if (!test_forward_hook(frame, nat_len)) return 41;
    if (raw_count != 1 || raw_len != nat_len) return 42;
    if (memcmp(raw_frame + 14 + 12, net_ip, 4) != 0) return 43;   /* SNAT applied */
    const router_nat_entry_t* nat = router_get_nat(0);
    if (!nat || nat->lan_port != 40000) return 44;

    /* ---- ICMP echo NAT (identifier translation) ---- */
    uint8_t icmp_remote[4] = {8, 8, 8, 8};
    uint8_t icmp_client[4] = {192, 168, 10, 43};
    uint32_t icmp_len = make_icmp_frame(frame, client_mac, icmp_client, icmp_remote, 8, 0x1234);
    raw_count = 0;
    if (!test_forward_hook(frame, icmp_len)) return 80;
    if (raw_count != 1 || memcmp(raw_frame + 14 + 12, net_ip, 4) != 0) return 81;
    uint16_t wan_echo_id = (uint16_t)((raw_frame[14 + 20 + 4] << 8) | raw_frame[14 + 20 + 5]);
    if (wan_echo_id == 0x1234) return 82;
    icmp_len = make_icmp_frame(frame, client_mac, icmp_remote, net_ip, 0, wan_echo_id);
    raw_count = 0;
    if (!test_service_hook(frame, icmp_len, icmp_remote, net_ip, wan_echo_id, wan_echo_id, 1)) return 83;
    if (raw_count != 1 || memcmp(raw_frame + 14 + 16, icmp_client, 4) != 0) return 84;
    if (((raw_frame[14 + 20 + 4] << 8) | raw_frame[14 + 20 + 5]) != 0x1234) return 85;

    /* ---- ARP reply for the LAN address ---- */
    raw_count = 0;
    if (test_arp_hook(lan, client_mac, client_ip) != 1) return 86;
    if (raw_count != 1 || raw_len != 42 || raw_frame[21] != 2) return 87;
    if (memcmp(raw_frame + 28, lan, 4) != 0) return 88;

    /* ---- interface inventory / uplink + LAN port ---- */
    if (router_port_count() != 2) return 300;
    if (router_get_config()->wan_port != 0 || router_get_config()->lan_port != 1) return 301;
    if (router_get_config()->lan_shared) return 302;
    net_port_t port;
    if (router_port_get(1, &port) != 0 || strcmp(port.ifname, "vtnet0") != 0) return 303;
    if (router_port_role(0) != 1 || router_port_role(1) != 2) return 304;
    cli("rtr ports", buf, sizeof(buf));
    if (!strstr(buf, "em0") || !strstr(buf, "vtnet0") || !strstr(buf, "WAN")) return 305;
    cli("rtr port lan 0", buf, sizeof(buf));
    if (!router_get_config()->lan_shared) return 306;
    cli("rtr port auto", buf, sizeof(buf));
    if (router_get_config()->lan_shared || router_get_config()->lan_port != 1) return 307;

    /* ---- status JSON ---- */
    buf[0] = 0;
    uint32_t status_len = router_build_status(buf, sizeof(buf));
    if (!status_len || status_len >= sizeof(buf)) return 45;
    if (!strstr(buf, "\"ok\":true") || !strstr(buf, "\"firewall\"") || !strstr(buf, "\"leases\"")) return 46;
    if (!strstr(buf, "\"interfaces\"") || !strstr(buf, "\"uplink\"") ||
        !strstr(buf, "\"mode\":\"dedicated\"") || !strstr(buf, "\"vtnet0\"") ||
        !strstr(buf, "\"role\":\"lan\"")) return 308;
    FILE* f = fopen("/tmp/sharkos-router-status.json", "w");
    if (f) { fwrite(buf, 1, status_len, f); fclose(f); }

    /* ---- save / load ---- */
    buf[0] = 0;
    cli("rtr save", buf, sizeof(buf));
    if (!strstr(buf, "saved")) return 47;
    router_dns_remove_record("nas.lan");
    router_clear_static_config();
    buf[0] = 0;
    cli("rtr load", buf, sizeof(buf));
    if (!strstr(buf, "loaded")) return 48;
    int restored = 0;
    for (uint32_t i = 0; i < ROUTER_DNS_CACHE_LIMIT; i++) {
        const router_dns_entry_t* e = router_get_dns_entry(i);
        if (e && strcmp(e->name, "nas.lan") == 0) restored = 1;
    }
    if (!restored) return 49;

    /* ---- CLI help/status and stop ---- */
    buf[0] = 0;
    cli("rtr help", buf, sizeof(buf));
    if (!strstr(buf, "rtr start")) return 50;
    buf[0] = 0;
    cli("fw status", buf, sizeof(buf));
    if (!strstr(buf, "policies")) return 51;
    router_stop();
    if (router_is_running()) return 52;

    puts("Router DHCP/DNS/WAN/NAT/CLI/firewall/persistence validation passed");
    return 0;
}
