#include "kernel.h"
#include "net.h"
#include "firewall.h"
#include "router.h"

/* ==================================================================
 * SharkOS Router
 *
 * A small, single-NIC IPv4 router for the SharkOS Server image:
 *   - DHCP server (DISCOVER/OFFER/REQUEST/ACK/NAK/RELEASE/INFORM)
 *   - DNS server with local records, lease names, cache and forwarding
 *   - WAN management (upstream DHCP client or static configuration)
 *   - basic NAT44 with port forwards and ICMP echo identifier mapping
 *   - hooks into net.c so no extra thread or second NIC is required
 * ================================================================== */

#define ROUTER_DHCP_PACKET_SIZE   300
#define ROUTER_DNS_PACKET_SIZE    512
#define ROUTER_NAT_FRAME_SIZE     1518
#define ROUTER_DNS_UPSTREAM_PORT  53000
#define ROUTER_DNS_PENDING_TTL    (3u * TICKS_PER_SEC)
#define ROUTER_NAT_TTL            (120u * TICKS_PER_SEC)
#define ROUTER_OFFER_TTL          (60u * TICKS_PER_SEC)
#define ROUTER_MASK_PREFIX_MAX    32

typedef struct {
    uint8_t  active;
    uint16_t upstream_id;
    uint16_t client_id;
    uint16_t client_port;
    uint16_t query_port;
    uint8_t  client_ip[4];
    uint8_t  client_mac[6];
    uint8_t  local_ip[4];
    uint8_t  question[192];
    uint32_t question_len;
    uint32_t expires;
    uint32_t next_send;
    uint32_t attempts;
} router_dns_pending_t;

static router_config_t        rtr_cfg;
static router_lease_t         rtr_leases[ROUTER_LEASE_LIMIT];
static router_reservation_t   rtr_reservations[ROUTER_RESERVATION_LIMIT];
static router_dns_entry_t     rtr_dns[ROUTER_DNS_CACHE_LIMIT];
static router_dns_pending_t   rtr_pending[ROUTER_DNS_PENDING_LIMIT];
static router_forward_t       rtr_forwards[ROUTER_FORWARD_LIMIT];
static router_nat_entry_t     rtr_nat[ROUTER_NAT_LIMIT];
static router_stats_t         rtr_stats;
static uint8_t                rtr_nat_frame[ROUTER_NAT_FRAME_SIZE];
static uint8_t                rtr_packet[ROUTER_DNS_PACKET_SIZE];
static uint16_t               rtr_nat_next_port = 30000;
static uint16_t               rtr_dns_txid = 0x2A17;
static uint32_t               rtr_last_wan_refresh;
static int                    rtr_dns_custom_upstream;
static int                    rtr_poll_busy;
static char                   rtr_wan_status[64] = "down";

/* ------------------------------------------------------------------ */
/* generic helpers                                                     */
/* ------------------------------------------------------------------ */

static uint16_t rtr_get16(const uint8_t* p) { return (uint16_t)(((uint16_t)p[0] << 8) | p[1]); }
static void rtr_put16(uint8_t* p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static void rtr_put32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);  p[3] = (uint8_t)v;
}

static int rtr_equal4(const uint8_t* a, const uint8_t* b) {
    return a[0] == b[0] && a[1] == b[1] && a[2] == b[2] && a[3] == b[3];
}

static int rtr_equal6(const uint8_t* a, const uint8_t* b) {
    for (uint32_t i = 0; i < 6; i++) if (a[i] != b[i]) return 0;
    return 1;
}

static int rtr_is_zero4(const uint8_t* a) { return !a[0] && !a[1] && !a[2] && !a[3]; }

static uint32_t rtr_ip_u32(const uint8_t* ip) {
    return ((uint32_t)ip[0] << 24) | ((uint32_t)ip[1] << 16) | ((uint32_t)ip[2] << 8) | ip[3];
}

static void rtr_u32_ip(uint32_t value, uint8_t* out) {
    out[0] = (uint8_t)(value >> 24); out[1] = (uint8_t)(value >> 16);
    out[2] = (uint8_t)(value >> 8);  out[3] = (uint8_t)value;
}

static int rtr_same_subnet(const uint8_t* a, const uint8_t* b, const uint8_t* mask) {
    for (uint32_t i = 0; i < 4; i++) if ((a[i] & mask[i]) != (b[i] & mask[i])) return 0;
    return 1;
}

static int rtr_is_lan_ip(const uint8_t* ip) {
    return rtr_same_subnet(ip, rtr_cfg.ip, rtr_cfg.mask);
}

static uint8_t rtr_mask_prefix(const uint8_t* mask) {
    uint8_t bits = 0;
    for (uint32_t i = 0; i < 4; i++) {
        uint8_t byte = mask[i];
        while (byte & 0x80) { bits++; byte = (uint8_t)(byte << 1); }
        if (byte) break;
    }
    return bits;
}

static int rtr_ieq(const char* a, const char* b) {
    while (*a && *b) {
        char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca + 32);
        if (cb >= 'A' && cb <= 'Z') cb = (char)(cb + 32);
        if (ca != cb) return 0;
        a++; b++;
    }
    return *a == 0 && *b == 0;
}

static void rtr_copy(char* dst, uint32_t cap, const char* src) {
    uint32_t i = 0;
    if (!src) { if (cap) dst[0] = 0; return; }
    while (src[i] && i + 1 < cap) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

static void rtr_append(char* out, uint32_t cap, uint32_t* used, const char* text) {
    while (*text && *used + 1 < cap) out[(*used)++] = *text++;
    out[*used] = 0;
}

static void rtr_num(char* out, uint32_t cap, uint32_t* used, uint32_t value) {
    char tmp[12];
    uint32_t n = 0;
    if (!value) tmp[n++] = '0';
    while (value && n < sizeof(tmp)) { tmp[n++] = (char)('0' + (value % 10)); value /= 10; }
    while (n && *used + 1 < cap) out[(*used)++] = tmp[--n];
    out[*used] = 0;
}

static void rtr_ip_text(char* out, uint32_t cap, uint32_t* used, const uint8_t* ip) {
    for (uint32_t i = 0; i < 4; i++) {
        rtr_num(out, cap, used, ip[i]);
        if (i < 3 && *used + 1 < cap) out[(*used)++] = '.';
    }
    out[*used] = 0;
}

static void rtr_hex(char* out, uint32_t cap, uint32_t* used, uint8_t value) {
    static const char digits[] = "0123456789abcdef";
    if (*used + 2 >= cap) return;
    out[(*used)++] = digits[(value >> 4) & 0xF];
    out[(*used)++] = digits[value & 0xF];
    out[*used] = 0;
}

void router_format_ip(char* out, uint32_t cap, const uint8_t* ip) {
    uint32_t used = 0;
    if (cap) out[0] = 0;
    if (!ip || !cap) return;
    rtr_ip_text(out, cap, &used, ip);
}

void router_format_mac(char* out, uint32_t cap, const uint8_t* mac) {
    uint32_t used = 0;
    if (cap) out[0] = 0;
    if (!mac || !cap) return;
    for (uint32_t i = 0; i < 6; i++) {
        rtr_hex(out, cap, &used, mac[i]);
        if (i < 5 && used + 1 < cap) out[used++] = ':';
    }
    out[used] = 0;
}

static int rtr_hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

int router_parse_mac(const char* text, uint8_t* mac) {
    if (!text || !mac) return 0;
    for (uint32_t i = 0; i < 6; i++) {
        int hi = rtr_hex_value(text[i * 3]);
        int lo = rtr_hex_value(text[i * 3 + 1]);
        if (hi < 0 || lo < 0) return 0;
        if (i < 5 && text[i * 3 + 2] != ':') return 0;
        mac[i] = (uint8_t)((hi << 4) | lo);
    }
    return text[17] == 0;
}

int router_parse_cidr(const char* text, uint8_t* ip, uint8_t* prefix) {
    if (!text || !ip) return 0;
    char buffer[40];
    uint32_t i = 0;
    while (text[i] && i < sizeof(buffer) - 1) { buffer[i] = text[i]; i++; }
    buffer[i] = 0;
    char* slash = strchr(buffer, '/');
    if (slash) *slash = 0;
    if (!net_parse_ip4(buffer, ip)) return 0;
    uint8_t bits = 32;
    if (slash) {
        uint32_t value = 0;
        const char* p = slash + 1;
        if (!*p) return 0;
        while (*p) {
            if (*p < '0' || *p > '9') return 0;
            value = value * 10 + (uint8_t)(*p - '0');
            if (value > 32) return 0;
            p++;
        }
        bits = (uint8_t)value;
    }
    if (prefix) *prefix = bits;
    return 1;
}

static uint16_t rtr_csum(const uint8_t* data, uint32_t len) {
    uint32_t sum = 0;
    for (uint32_t i = 0; i < len; i += 2)
        sum += ((uint32_t)data[i] << 8) | ((i + 1 < len) ? data[i + 1] : 0);
    while (sum >> 16) sum = (sum & 0xFFFFu) + (sum >> 16);
    return (uint16_t)(~sum & 0xFFFFu);
}

/* ------------------------------------------------------------------ */
/* configuration / lifecycle                                          */
/* ------------------------------------------------------------------ */

static int rtr_service_hook(const uint8_t* frame, uint32_t frame_len,
                            const uint8_t* src_ip, const uint8_t* dst_ip,
                            uint16_t sport, uint16_t dport, uint8_t proto);
static void rtr_dns_send_query(router_dns_pending_t* slot);
static int rtr_profile_deny_service(uint8_t protocol, uint16_t port, const char* comment);
static int rtr_dns_upstream_reply(const uint8_t* udp, uint32_t len,
                                  const uint8_t* src_ip, uint16_t dport);
static int rtr_arp_hook(const uint8_t* target_ip, const uint8_t* sender_mac,
                        const uint8_t* sender_ip);
static int rtr_local_ip_hook(const uint8_t* ip);
static int rtr_forward_hook(const uint8_t* frame, uint32_t len);

void router_init(void) {
    memset(&rtr_cfg, 0, sizeof(rtr_cfg));
    memset(rtr_leases, 0, sizeof(rtr_leases));
    memset(rtr_reservations, 0, sizeof(rtr_reservations));
    memset(rtr_dns, 0, sizeof(rtr_dns));
    memset(rtr_pending, 0, sizeof(rtr_pending));
    memset(rtr_forwards, 0, sizeof(rtr_forwards));
    memset(rtr_nat, 0, sizeof(rtr_nat));
    memset(&rtr_stats, 0, sizeof(rtr_stats));
    uint8_t lan[4] = {192,168,10,1};
    uint8_t mask[4] = {255,255,255,0};
    uint8_t pool_start[4] = {192,168,10,100};
    uint8_t pool_end[4] = {192,168,10,200};
    uint8_t dns1[4] = {1,1,1,1};
    uint8_t dns2[4] = {8,8,8,8};
    memcpy(rtr_cfg.ip, lan, 4);
    memcpy(rtr_cfg.mask, mask, 4);
    memcpy(rtr_cfg.pool_start, pool_start, 4);
    memcpy(rtr_cfg.pool_end, pool_end, 4);
    memcpy(rtr_cfg.dns_upstream, dns1, 4);
    memcpy(rtr_cfg.dns_secondary, dns2, 4);
    rtr_cfg.lease_seconds = 3600;
    rtr_cfg.wan_mode = ROUTER_WAN_DHCP;
    rtr_cfg.http_port = ROUTER_DEFAULT_HTTP_PORT;
    rtr_copy(rtr_cfg.domain, sizeof(rtr_cfg.domain), "lan");
    rtr_copy(rtr_cfg.token, sizeof(rtr_cfg.token), "sharkos");
    rtr_copy(rtr_wan_status, sizeof(rtr_wan_status), "waiting for upstream DHCP");
    rtr_nat_next_port = 30000;
    rtr_dns_txid = 0x2A17;
    rtr_dns_custom_upstream = 0;
    rtr_poll_busy = 0;

    net_set_service_hook(rtr_service_hook);
    net_set_arp_hook(rtr_arp_hook);
    net_set_local_ip_hook(rtr_local_ip_hook);
    net_set_forward_hook(rtr_forward_hook);
    net_set_poll_hook(router_poll);
    net_port_scan();
    router_port_assign();
}

/* ------------------------------------------------------------------ */
/* physical interfaces (uplink adapter / LAN port)                    */
/* ------------------------------------------------------------------ */

int router_port_assign(void) {
    uint32_t count = net_port_count();
    rtr_cfg.wan_port = 0;
    rtr_cfg.lan_port = count >= 2 ? 1 : 0;
    rtr_cfg.lan_shared = count >= 2 ? 0 : 1;
    return (int)count;
}

int router_set_ports(int wan_index, int lan_index) {
    uint32_t count = net_port_count();
    if (!count) return -1;
    uint32_t wan = (wan_index < 0 || wan_index == (int)ROUTER_PORT_AUTO) ? 0u : (uint32_t)wan_index;
    uint32_t lan;
    if (lan_index < 0 || lan_index == (int)ROUTER_PORT_AUTO) lan = count >= 2 ? 1u : 0u;
    else lan = (uint32_t)lan_index;
    if (wan >= count || lan >= count) return -1;
    rtr_cfg.wan_port = (uint8_t)wan;
    rtr_cfg.lan_port = (uint8_t)lan;
    rtr_cfg.lan_shared = (lan == wan || count < 2) ? 1 : 0;
    return 0;
}

uint32_t router_port_count(void) { return net_port_count(); }
int router_port_get(uint32_t index, net_port_t* out) { return net_port_get(index, out); }

uint32_t router_port_role(uint32_t index) {
    if (index == rtr_cfg.wan_port) return 1;
    if (!rtr_cfg.lan_shared && index == rtr_cfg.lan_port) return 2;
    return 0;
}

const router_config_t* router_get_config(void) { return &rtr_cfg; }
const router_stats_t* router_get_stats(void) { return &rtr_stats; }
void router_note_http_request(void) { rtr_stats.http_requests++; }
int router_is_running(void) { return rtr_cfg.running ? 1 : 0; }

void router_clear_static_config(void) {
    memset(rtr_reservations, 0, sizeof(rtr_reservations));
    memset(rtr_dns, 0, sizeof(rtr_dns));
    memset(rtr_forwards, 0, sizeof(rtr_forwards));
    memset(rtr_pending, 0, sizeof(rtr_pending));
    memset(rtr_nat, 0, sizeof(rtr_nat));
}

int router_start(uint16_t http_port) {
    if (!net_has_nic()) return -1;
    if (!http_port) http_port = ROUTER_DEFAULT_HTTP_PORT;
    net_port_scan();
    uint32_t ports = net_port_count();
    if (rtr_cfg.wan_port >= ports || rtr_cfg.lan_port >= ports) router_port_assign();
    if (!rtr_cfg.lease_seconds) rtr_cfg.lease_seconds = 3600;
    rtr_cfg.running = 1;
    rtr_cfg.dhcp_enabled = 1;
    rtr_cfg.dns_enabled = 1;
    rtr_cfg.nat_enabled = 1;
    rtr_cfg.http_port = http_port;
    rtr_cfg.started_at = uptime_ticks;
    firewall_service_add(FIREWALL_PROTOCOL_UDP, 67);
    firewall_service_add(FIREWALL_PROTOCOL_UDP, 53);
    firewall_service_add(FIREWALL_PROTOCOL_TCP, http_port);
    if (rtr_cfg.wan_mode == ROUTER_WAN_DHCP && net_dhcp_is_disabled()) net_dhcp_start();
    router_wan_refresh();
    rtr_cfg.http_running = router_http_start(http_port) == 0 ? 1 : 0;
    return rtr_cfg.http_running ? 0 : -2;
}

void router_stop(void) {
    router_http_stop();
    rtr_cfg.http_running = 0;
    rtr_cfg.running = 0;
    rtr_cfg.dhcp_enabled = 0;
    rtr_cfg.dns_enabled = 0;
    rtr_cfg.nat_enabled = 0;
    firewall_service_remove(FIREWALL_PROTOCOL_UDP, 67);
    firewall_service_remove(FIREWALL_PROTOCOL_UDP, 53);
    firewall_service_remove(FIREWALL_PROTOCOL_TCP, rtr_cfg.http_port);
    memset(rtr_nat, 0, sizeof(rtr_nat));
    memset(rtr_pending, 0, sizeof(rtr_pending));
}

int router_set_http_port(uint16_t port) {
    if (!port) return -1;
    firewall_service_remove(FIREWALL_PROTOCOL_TCP, rtr_cfg.http_port);
    router_http_stop();
    rtr_cfg.http_port = port;
    firewall_service_add(FIREWALL_PROTOCOL_TCP, port);
    /* Keep the WAN-side console deny rule (when the router profile is in
     * use) pointed at the new port. */
    for (uint32_t i = 0; i < firewall_rule_count(); i++) {
        const firewall_rule_t* rule = firewall_get_rule(i);
        if (rule && strcmp(rule->comment, "wan console blocked") == 0) {
            firewall_delete_rule(i + 1);
            rtr_profile_deny_service(FIREWALL_PROTOCOL_TCP, port, "wan console blocked");
            break;
        }
    }
    rtr_cfg.http_running = 0;
    if (rtr_cfg.running && router_http_start(port) == 0) rtr_cfg.http_running = 1;
    return rtr_cfg.running && !rtr_cfg.http_running ? -2 : 0;
}

int router_set_token(const char* token) {
    if (!token || !token[0] || strlen(token) >= sizeof(rtr_cfg.token)) return -1;
    for (const char* p = token; *p; p++) {
        if (*p == '"' || *p == '\\' || (uint8_t)*p < 32) return -1;
    }
    rtr_copy(rtr_cfg.token, sizeof(rtr_cfg.token), token);
    return 0;
}

/* ------------------------------------------------------------------ */
/* WAN                                                                 */
/* ------------------------------------------------------------------ */

int router_wan_set_dhcp(void) {
    rtr_cfg.wan_mode = ROUTER_WAN_DHCP;
    rtr_copy(rtr_wan_status, sizeof(rtr_wan_status), "dhcp discovering");
    net_dhcp_start();
    return 0;
}

int router_wan_set_static(const uint8_t* ip, const uint8_t* mask,
                          const uint8_t* gw, const uint8_t* dns) {
    if (!ip || !mask) return -1;
    uint8_t resolved_dns[4];
    if (dns) memcpy(resolved_dns, dns, 4);
    else { resolved_dns[0] = 1; resolved_dns[1] = 1; resolved_dns[2] = 1; resolved_dns[3] = 1; }
    if (net_set_static_config(ip, mask, gw, resolved_dns) != 0) return -1;
    rtr_cfg.wan_mode = ROUTER_WAN_STATIC;
    memcpy(rtr_cfg.wan_ip, ip, 4);
    memcpy(rtr_cfg.wan_mask, mask, 4);
    if (gw) memcpy(rtr_cfg.wan_gw, gw, 4); else memset(rtr_cfg.wan_gw, 0, 4);
    memcpy(rtr_cfg.wan_dns, resolved_dns, 4);
    if (!rtr_dns_custom_upstream) memcpy(rtr_cfg.dns_upstream, resolved_dns, 4);
    rtr_copy(rtr_wan_status, sizeof(rtr_wan_status), "static up");
    return 0;
}

int router_wan_set_down(void) {
    net_dhcp_stop();
    rtr_cfg.wan_mode = ROUTER_WAN_DISABLED;
    net_configured = 0;
    memset(net_ip, 0, 4);
    rtr_copy(rtr_wan_status, sizeof(rtr_wan_status), "administratively down");
    return 0;
}

void router_wan_refresh(void) {
    if (rtr_cfg.wan_mode == ROUTER_WAN_DHCP) {
        if (!net_configured) {
            rtr_copy(rtr_wan_status, sizeof(rtr_wan_status), "waiting for upstream DHCP");
            return;
        }
        memcpy(rtr_cfg.wan_ip, net_ip, 4);
        memcpy(rtr_cfg.wan_mask, net_mask, 4);
        memcpy(rtr_cfg.wan_gw, net_gw, 4);
        memcpy(rtr_cfg.wan_dns, net_dns, 4);
        if (!rtr_dns_custom_upstream && !rtr_is_zero4(net_dns))
            memcpy(rtr_cfg.dns_upstream, net_dns, 4);
        char text[64];
        uint32_t used = 0;
        rtr_append(text, sizeof(text), &used, "dhcp bound ");
        rtr_ip_text(text, sizeof(text), &used, net_ip);
        rtr_copy(rtr_wan_status, sizeof(rtr_wan_status), text);
    } else if (rtr_cfg.wan_mode == ROUTER_WAN_STATIC) {
        rtr_copy(rtr_wan_status, sizeof(rtr_wan_status), "static up");
    } else {
        rtr_copy(rtr_wan_status, sizeof(rtr_wan_status), "administratively down");
    }
}

/* ------------------------------------------------------------------ */
/* LAN / DHCP server                                                   */
/* ------------------------------------------------------------------ */

int router_lan_set(const uint8_t* ip, const uint8_t* mask) {
    if (!ip || !mask || rtr_is_zero4(ip) || rtr_is_zero4(mask)) return -1;
    memcpy(rtr_cfg.ip, ip, 4);
    memcpy(rtr_cfg.mask, mask, 4);
    return 0;
}

int router_dhcp_pool(const uint8_t* start, const uint8_t* end) {
    if (!start || !end || rtr_ip_u32(start) > rtr_ip_u32(end)) return -1;
    if (!rtr_is_lan_ip(start) || !rtr_is_lan_ip(end)) return -1;
    memcpy(rtr_cfg.pool_start, start, 4);
    memcpy(rtr_cfg.pool_end, end, 4);
    return 0;
}

int router_dhcp_set_lease(uint32_t seconds) {
    if (seconds < 60 || seconds > 86400u * 7u) return -1;
    rtr_cfg.lease_seconds = seconds;
    return 0;
}

int router_dhcp_set_enabled(int enabled) {
    if (!rtr_cfg.running) return -1;
    rtr_cfg.dhcp_enabled = enabled ? 1 : 0;
    if (enabled) firewall_service_add(FIREWALL_PROTOCOL_UDP, 67);
    else firewall_service_remove(FIREWALL_PROTOCOL_UDP, 67);
    return 0;
}

static router_lease_t* rtr_lease_by_mac(const uint8_t* mac) {
    for (uint32_t i = 0; i < ROUTER_LEASE_LIMIT; i++)
        if (rtr_leases[i].active && rtr_equal6(rtr_leases[i].mac, mac)) return &rtr_leases[i];
    return 0;
}

static router_lease_t* rtr_lease_by_ip(const uint8_t* ip) {
    for (uint32_t i = 0; i < ROUTER_LEASE_LIMIT; i++)
        if (rtr_leases[i].active && rtr_equal4(rtr_leases[i].ip, ip)) return &rtr_leases[i];
    return 0;
}

static router_reservation_t* rtr_reservation_by_mac(const uint8_t* mac) {
    for (uint32_t i = 0; i < ROUTER_RESERVATION_LIMIT; i++)
        if (rtr_reservations[i].active && rtr_equal6(rtr_reservations[i].mac, mac)) return &rtr_reservations[i];
    return 0;
}

static int rtr_ip_reserved_for_other(const uint8_t* ip, const uint8_t* mac) {
    for (uint32_t i = 0; i < ROUTER_RESERVATION_LIMIT; i++) {
        router_reservation_t* r = &rtr_reservations[i];
        if (r->active && rtr_equal4(r->ip, ip) && !rtr_equal6(r->mac, mac)) return 1;
    }
    return 0;
}

int router_dhcp_reserve(const uint8_t* mac, const uint8_t* ip, const char* hostname) {
    if (!mac || !ip || !rtr_is_lan_ip(ip) || rtr_equal4(ip, rtr_cfg.ip)) return -1;
    router_reservation_t* slot = rtr_reservation_by_mac(mac);
    if (!slot) {
        for (uint32_t i = 0; i < ROUTER_RESERVATION_LIMIT; i++)
            if (!rtr_reservations[i].active) { slot = &rtr_reservations[i]; break; }
    }
    if (!slot) return -1;
    memset(slot, 0, sizeof(*slot));
    slot->active = 1;
    memcpy(slot->mac, mac, 6);
    memcpy(slot->ip, ip, 4);
    rtr_copy(slot->hostname, sizeof(slot->hostname), hostname);
    return 0;
}

int router_dhcp_remove_reservation(uint32_t id) {
    if (!id || id > ROUTER_RESERVATION_LIMIT || !rtr_reservations[id - 1].active) return -1;
    memset(&rtr_reservations[id - 1], 0, sizeof(rtr_reservations[0]));
    return 0;
}

int router_dhcp_release(const uint8_t* ip) {
    router_lease_t* lease = rtr_lease_by_ip(ip);
    if (!lease) return -1;
    memset(lease, 0, sizeof(*lease));
    return 0;
}

const router_lease_t* router_get_lease(uint32_t index) {
    if (index >= ROUTER_LEASE_LIMIT || !rtr_leases[index].active) return 0;
    return &rtr_leases[index];
}

const router_reservation_t* router_get_reservation(uint32_t index) {
    if (index >= ROUTER_RESERVATION_LIMIT || !rtr_reservations[index].active) return 0;
    return &rtr_reservations[index];
}

uint32_t router_lease_count(void) {
    uint32_t count = 0;
    for (uint32_t i = 0; i < ROUTER_LEASE_LIMIT; i++) if (rtr_leases[i].active) count++;
    return count;
}

static void rtr_expire_leases(void) {
    for (uint32_t i = 0; i < ROUTER_LEASE_LIMIT; i++) {
        if (rtr_leases[i].active && (int32_t)(uptime_ticks - rtr_leases[i].expires) >= 0)
            memset(&rtr_leases[i], 0, sizeof(rtr_leases[i]));
    }
}

static router_lease_t* rtr_dhcp_allocate(const uint8_t* mac) {
    rtr_expire_leases();
    router_reservation_t* reservation = rtr_reservation_by_mac(mac);
    router_lease_t* existing = rtr_lease_by_mac(mac);
    router_lease_t* slot = existing;
    uint8_t chosen[4];
    int found = 0;
    if (reservation) {
        router_lease_t* holder = rtr_lease_by_ip(reservation->ip);
        if (!holder || holder == existing) { memcpy(chosen, reservation->ip, 4); found = 1; }
    } else if (existing && rtr_ip_u32(existing->ip) >= rtr_ip_u32(rtr_cfg.pool_start) &&
               rtr_ip_u32(existing->ip) <= rtr_ip_u32(rtr_cfg.pool_end)) {
        memcpy(chosen, existing->ip, 4);
        found = 1;
    }
    if (!found) {
        uint32_t start = rtr_ip_u32(rtr_cfg.pool_start);
        uint32_t end = rtr_ip_u32(rtr_cfg.pool_end);
        for (uint32_t value = start; value <= end; value++) {
            uint8_t candidate[4];
            rtr_u32_ip(value, candidate);
            if (rtr_equal4(candidate, rtr_cfg.ip)) continue;
            if (rtr_ip_reserved_for_other(candidate, mac)) continue;
            router_lease_t* holder = rtr_lease_by_ip(candidate);
            if (holder && holder != existing && holder->bound) continue;
            memcpy(chosen, candidate, 4);
            found = 1;
            break;
        }
    }
    if (!found) return 0;
    if (!slot) {
        for (uint32_t i = 0; i < ROUTER_LEASE_LIMIT; i++)
            if (!rtr_leases[i].active) { slot = &rtr_leases[i]; break; }
    }
    if (!slot) return 0;
    /* Free any other slot that holds the chosen address. */
    for (uint32_t i = 0; i < ROUTER_LEASE_LIMIT; i++) {
        if (&rtr_leases[i] != slot && rtr_leases[i].active && rtr_equal4(rtr_leases[i].ip, chosen))
            memset(&rtr_leases[i], 0, sizeof(rtr_leases[i]));
    }
    int was_bound = slot->bound;
    char hostname[ROUTER_NAME_SIZE];
    memcpy(hostname, slot->hostname, sizeof(hostname));
    memset(slot, 0, sizeof(*slot));
    slot->active = 1;
    slot->bound = was_bound ? 1 : 0;
    memcpy(slot->mac, mac, 6);
    memcpy(slot->ip, chosen, 4);
    if (reservation && reservation->hostname[0])
        rtr_copy(slot->hostname, sizeof(slot->hostname), reservation->hostname);
    else
        memcpy(slot->hostname, hostname, sizeof(slot->hostname));
    slot->expires = uptime_ticks + (slot->bound ? rtr_cfg.lease_seconds * TICKS_PER_SEC : ROUTER_OFFER_TTL);
    return slot;
}

static uint32_t rtr_dhcp_option(uint8_t* out, uint32_t cap, uint32_t used, uint8_t code,
                                const uint8_t* value, uint8_t length) {
    if (used + 2 + length > cap) return used;
    out[used++] = code;
    out[used++] = length;
    for (uint32_t i = 0; i < length; i++) out[used++] = value[i];
    return used;
}

static void rtr_dhcp_send(uint8_t type, router_lease_t* lease, uint32_t xid, uint16_t flags,
                          const uint8_t* client_mac, const uint8_t* ciaddr,
                          const uint8_t* giaddr, const char* hostname_override) {
    uint8_t* d = rtr_packet;
    memset(d, 0, ROUTER_DHCP_PACKET_SIZE);
    d[0] = 2; d[1] = 1; d[2] = 6; d[3] = 0;
    rtr_put32(d + 4, xid);
    rtr_put16(d + 10, flags);
    if (ciaddr) memcpy(d + 12, ciaddr, 4);
    if (lease && type != 6) memcpy(d + 16, lease->ip, 4);
    memcpy(d + 20, rtr_cfg.ip, 4);                 /* siaddr: next server */
    if (giaddr) memcpy(d + 24, giaddr, 4);
    if (client_mac) memcpy(d + 28, client_mac, 6);
    rtr_copy((char*)d + 44, 64, "SharkOS");
    d[236] = 0x63; d[237] = 0x82; d[238] = 0x53; d[239] = 0x63;
    uint32_t o = 240;
    uint8_t one = (uint8_t)type;
    o = rtr_dhcp_option(d, ROUTER_DHCP_PACKET_SIZE, o, 53, &one, 1);
    o = rtr_dhcp_option(d, ROUTER_DHCP_PACKET_SIZE, o, 54, rtr_cfg.ip, 4);
    if (type == 6) {                                /* NAK */
        const char* message = "address unavailable";
        o = rtr_dhcp_option(d, ROUTER_DHCP_PACKET_SIZE, o, 56,
                            (const uint8_t*)message, (uint8_t)strlen(message));
    } else {
        o = rtr_dhcp_option(d, ROUTER_DHCP_PACKET_SIZE, o, 1, rtr_cfg.mask, 4);
        o = rtr_dhcp_option(d, ROUTER_DHCP_PACKET_SIZE, o, 3, rtr_cfg.ip, 4);
        uint8_t dns[8];
        uint8_t dns_count = 0;
        memcpy(dns, rtr_cfg.ip, 4); dns_count += 4;
        if (rtr_cfg.dns_enabled && !rtr_is_zero4(rtr_cfg.dns_upstream) &&
            !rtr_equal4(rtr_cfg.dns_upstream, rtr_cfg.ip)) {
            memcpy(dns + 4, rtr_cfg.dns_upstream, 4); dns_count += 4;
        }
        o = rtr_dhcp_option(d, ROUTER_DHCP_PACKET_SIZE, o, 6, dns, dns_count);
        o = rtr_dhcp_option(d, ROUTER_DHCP_PACKET_SIZE, o, 15,
                            (const uint8_t*)rtr_cfg.domain, (uint8_t)strlen(rtr_cfg.domain));
        uint8_t lease_time[4];
        uint32_t seconds = rtr_cfg.lease_seconds;
        rtr_put32(lease_time, seconds);
        o = rtr_dhcp_option(d, ROUTER_DHCP_PACKET_SIZE, o, 51, lease_time, 4);
        uint8_t renewal[4];
        rtr_put32(renewal, seconds / 2);
        o = rtr_dhcp_option(d, ROUTER_DHCP_PACKET_SIZE, o, 58, renewal, 4);
        rtr_put32(renewal, seconds * 7 / 8);
        o = rtr_dhcp_option(d, ROUTER_DHCP_PACKET_SIZE, o, 59, renewal, 4);
    }
    if (o < ROUTER_DHCP_PACKET_SIZE) d[o++] = 0xFF;
    uint8_t dst_mac[6];
    uint8_t dst_ip[4];
    int broadcast = (flags & 0x8000) || !ciaddr || rtr_is_zero4(ciaddr);
    if (broadcast || type == 6) {
        memset(dst_mac, 0xFF, 6);
        memset(dst_ip, 0xFF, 4);
    } else {
        memcpy(dst_mac, client_mac, 6);
        memcpy(dst_ip, lease ? lease->ip : ciaddr, 4);
    }
    (void)hostname_override;
    net_send_udp(dst_mac, rtr_cfg.ip, dst_ip, 67, 68, d, ROUTER_DHCP_PACKET_SIZE);
}

static void rtr_dhcp_input(const uint8_t* frame, const uint8_t* udp, uint32_t udp_len) {
    if (udp_len < 240 || udp[0] != 1 || udp[1] != 1 || udp[2] != 6) return;
    if (udp[236] != 0x63 || udp[237] != 0x82 || udp[238] != 0x53 || udp[239] != 0x63) return;
    uint32_t xid = ((uint32_t)udp[4] << 24) | ((uint32_t)udp[5] << 16) | ((uint32_t)udp[6] << 8) | udp[7];
    uint16_t flags = rtr_get16(udp + 10);
    const uint8_t* ciaddr = udp + 12;
    const uint8_t* giaddr = udp + 24;
    const uint8_t* mac = udp + 28;
    uint8_t message_type = 0;
    uint8_t requested[4] = {0,0,0,0};
    uint8_t server_id[4] = {0,0,0,0};
    char hostname[ROUTER_NAME_SIZE];
    hostname[0] = 0;
    uint32_t p = 240;
    while (p + 1 < udp_len) {
        uint8_t code = udp[p];
        if (code == 0xFF) break;
        if (code == 0) { p++; continue; }
        uint8_t length = udp[p + 1];
        if (p + 2 + length > udp_len) break;
        const uint8_t* value = udp + p + 2;
        if (code == 53 && length == 1) message_type = value[0];
        else if (code == 50 && length >= 4) memcpy(requested, value, 4);
        else if (code == 54 && length >= 4) memcpy(server_id, value, 4);
        else if (code == 12 && length) {
            uint32_t n = length < sizeof(hostname) - 1 ? length : sizeof(hostname) - 1;
            memcpy(hostname, value, n);
            hostname[n] = 0;
        }
        p += 2 + length;
    }
    if (!message_type) return;
    if (!rtr_is_zero4(giaddr)) {
        /* DHCP relay agents are not supported by this basic server. */
        rtr_stats.dropped_frames++;
        return;
    }
    if (!rtr_is_zero4(server_id) && !rtr_equal4(server_id, rtr_cfg.ip)) {
        router_lease_t* offered = rtr_lease_by_mac(mac);
        if (offered && !offered->bound) memset(offered, 0, sizeof(*offered));
        return;
    }
    const uint8_t* client_mac = frame + 6;
    switch (message_type) {
    case 1: {  /* DISCOVER */
        rtr_stats.dhcp_discover++;
        router_lease_t* lease = rtr_dhcp_allocate(mac);
        if (!lease) { rtr_stats.dropped_frames++; return; }
        if (hostname[0]) rtr_copy(lease->hostname, sizeof(lease->hostname), hostname);
        lease->bound = 0;
        lease->expires = uptime_ticks + ROUTER_OFFER_TTL;
        rtr_dhcp_send(2, lease, xid, flags, client_mac, ciaddr, 0, 0);
        rtr_stats.dhcp_offer++;
        break;
    }
    case 3: {  /* REQUEST */
        rtr_stats.dhcp_request++;
        uint8_t wanted[4];
        if (!rtr_is_zero4(requested)) memcpy(wanted, requested, 4);
        else if (!rtr_is_zero4(ciaddr)) memcpy(wanted, ciaddr, 4);
        else memset(wanted, 0, 4);
        router_reservation_t* reservation = rtr_reservation_by_mac(mac);
        if (reservation && !rtr_equal4(reservation->ip, wanted)) {
            rtr_dhcp_send(6, 0, xid, flags, client_mac, ciaddr, 0, 0);
            rtr_stats.dhcp_nak++;
            return;
        }
        if (!rtr_is_zero4(wanted) &&
            (rtr_ip_u32(wanted) < rtr_ip_u32(rtr_cfg.pool_start) ||
             rtr_ip_u32(wanted) > rtr_ip_u32(rtr_cfg.pool_end) ||
             rtr_equal4(wanted, rtr_cfg.ip) || rtr_ip_reserved_for_other(wanted, mac))) {
            if (!reservation) {
                rtr_dhcp_send(6, 0, xid, flags, client_mac, ciaddr, 0, 0);
                rtr_stats.dhcp_nak++;
                return;
            }
        }
        router_lease_t* holder = rtr_is_zero4(wanted) ? 0 : rtr_lease_by_ip(wanted);
        if (holder && !rtr_equal6(holder->mac, mac)) {
            rtr_dhcp_send(6, 0, xid, flags, client_mac, ciaddr, 0, 0);
            rtr_stats.dhcp_nak++;
            return;
        }
        router_lease_t* lease = rtr_dhcp_allocate(mac);
        if (!lease || (!rtr_is_zero4(wanted) && !rtr_equal4(lease->ip, wanted))) {
            rtr_dhcp_send(6, 0, xid, flags, client_mac, ciaddr, 0, 0);
            rtr_stats.dhcp_nak++;
            return;
        }
        if (hostname[0]) rtr_copy(lease->hostname, sizeof(lease->hostname), hostname);
        lease->bound = 1;
        lease->expires = uptime_ticks + rtr_cfg.lease_seconds * TICKS_PER_SEC;
        rtr_dhcp_send(5, lease, xid, flags, client_mac, ciaddr, 0, 0);
        rtr_stats.dhcp_ack++;
        break;
    }
    case 4:    /* DECLINE */
    case 7: {  /* RELEASE */
        router_lease_t* lease = rtr_lease_by_mac(mac);
        if (lease && rtr_equal6(lease->mac, mac)) memset(lease, 0, sizeof(*lease));
        rtr_stats.dhcp_release++;
        break;
    }
    case 8:    /* INFORM */
        rtr_dhcp_send(5, 0, xid, flags, client_mac, ciaddr, 0, "inform");
        rtr_stats.dhcp_ack++;
        break;
    default:
        break;
    }
}

/* ------------------------------------------------------------------ */
/* DNS server, cache and forwarder                                     */
/* ------------------------------------------------------------------ */

int router_dns_set_enabled(int enabled) {
    if (!rtr_cfg.running) return -1;
    rtr_cfg.dns_enabled = enabled ? 1 : 0;
    if (enabled) firewall_service_add(FIREWALL_PROTOCOL_UDP, 53);
    else firewall_service_remove(FIREWALL_PROTOCOL_UDP, 53);
    return 0;
}

int router_dns_set_upstream(const uint8_t* primary, const uint8_t* secondary) {
    if (primary) memcpy(rtr_cfg.dns_upstream, primary, 4);
    if (secondary) memcpy(rtr_cfg.dns_secondary, secondary, 4);
    rtr_dns_custom_upstream = 1;
    return 0;
}

int router_dns_set_domain(const char* domain) {
    if (!domain || !domain[0] || strlen(domain) >= sizeof(rtr_cfg.domain)) return -1;
    rtr_copy(rtr_cfg.domain, sizeof(rtr_cfg.domain), domain);
    return 0;
}

int router_dns_add_record(const char* name, const uint8_t* ip, int local) {
    if (!name || !name[0] || !ip) return -1;
    router_dns_entry_t* slot = 0;
    for (uint32_t i = 0; i < ROUTER_DNS_CACHE_LIMIT; i++) {
        if (rtr_dns[i].active && rtr_ieq(rtr_dns[i].name, name)) { slot = &rtr_dns[i]; break; }
        if (!rtr_dns[i].active && !slot) slot = &rtr_dns[i];
    }
    if (!slot) return -1;
    memset(slot, 0, sizeof(*slot));
    slot->active = 1;
    slot->local = local ? 1 : 0;
    memcpy(slot->ip, ip, 4);
    rtr_copy(slot->name, sizeof(slot->name), name);
    slot->expires = local ? 0 : uptime_ticks + 300u * TICKS_PER_SEC;
    return 0;
}

int router_dns_remove_record(const char* name) {
    for (uint32_t i = 0; i < ROUTER_DNS_CACHE_LIMIT; i++) {
        if (rtr_dns[i].active && rtr_ieq(rtr_dns[i].name, name)) {
            memset(&rtr_dns[i], 0, sizeof(rtr_dns[i]));
            return 0;
        }
    }
    return -1;
}

void router_dns_flush_cache(void) {
    for (uint32_t i = 0; i < ROUTER_DNS_CACHE_LIMIT; i++)
        if (rtr_dns[i].active && !rtr_dns[i].local) memset(&rtr_dns[i], 0, sizeof(rtr_dns[i]));
}

const router_dns_entry_t* router_get_dns_entry(uint32_t index) {
    if (index >= ROUTER_DNS_CACHE_LIMIT || !rtr_dns[index].active) return 0;
    return &rtr_dns[index];
}

uint32_t router_dns_cache_count(void) {
    uint32_t count = 0;
    for (uint32_t i = 0; i < ROUTER_DNS_CACHE_LIMIT; i++) if (rtr_dns[i].active) count++;
    return count;
}

static void rtr_expire_dns(void) {
    for (uint32_t i = 0; i < ROUTER_DNS_CACHE_LIMIT; i++) {
        if (!rtr_dns[i].active || rtr_dns[i].local) continue;
        if ((int32_t)(uptime_ticks - rtr_dns[i].expires) >= 0) memset(&rtr_dns[i], 0, sizeof(rtr_dns[i]));
    }
}

static uint32_t rtr_dns_name(const uint8_t* msg, uint32_t pos, uint32_t len,
                             char* out, uint32_t cap) {
    uint32_t used = 0;
    uint32_t jumps = 0;
    if (cap) out[0] = 0;
    while (pos < len && jumps < 8) {
        uint8_t label = msg[pos];
        if (label == 0) { pos++; break; }
        if ((label & 0xC0) == 0xC0) { pos += 2; jumps++; break; }
        if ((label & 0xC0) || pos + 1 + label > len) return 0;
        if (used && used + 1 < cap) out[used++] = '.';
        for (uint32_t i = 0; i < label && used + 1 < cap; i++) {
            char c = (char)msg[pos + 1 + i];
            if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
            out[used++] = c;
        }
        pos += 1 + label;
    }
    out[used] = 0;
    return pos;
}

static int rtr_dns_local_lookup(const char* name, uint8_t* ip_out) {
    if (rtr_ieq(name, "router") || rtr_ieq(name, "gateway") || rtr_ieq(name, "sharkos") ||
        rtr_ieq(name, "sharkos.lan")) { memcpy(ip_out, rtr_cfg.ip, 4); return 2; }
    char fqdn[ROUTER_DOMAIN_SIZE * 2];
    uint32_t used = 0;
    rtr_append(fqdn, sizeof(fqdn), &used, "router.");
    rtr_append(fqdn, sizeof(fqdn), &used, rtr_cfg.domain);
    if (rtr_ieq(name, fqdn)) { memcpy(ip_out, rtr_cfg.ip, 4); return 2; }
    for (uint32_t i = 0; i < ROUTER_DNS_CACHE_LIMIT; i++) {
        if (rtr_dns[i].active && rtr_ieq(rtr_dns[i].name, name)) {
            memcpy(ip_out, rtr_dns[i].ip, 4);
            return rtr_dns[i].local ? 1 : 2;
        }
    }
    for (uint32_t i = 0; i < ROUTER_LEASE_LIMIT; i++) {
        router_lease_t* lease = &rtr_leases[i];
        if (!lease->active || !lease->hostname[0]) continue;
        if (rtr_ieq(name, lease->hostname)) { memcpy(ip_out, lease->ip, 4); return 1; }
        used = 0;
        rtr_append(fqdn, sizeof(fqdn), &used, lease->hostname);
        rtr_append(fqdn, sizeof(fqdn), &used, ".");
        rtr_append(fqdn, sizeof(fqdn), &used, rtr_cfg.domain);
        if (rtr_ieq(name, fqdn)) { memcpy(ip_out, lease->ip, 4); return 1; }
    }
    return 0;
}

static void rtr_dns_send(const uint8_t* client_mac, const uint8_t* local_ip,
                         const uint8_t* client_ip, uint16_t client_port, uint16_t id,
                         const uint8_t* question, uint32_t question_len,
                         uint16_t qtype, const uint8_t* answer, uint8_t rcode,
                         const char* ptr_name) {
    uint8_t* out = rtr_packet;
    memset(out, 0, ROUTER_DNS_PACKET_SIZE);
    rtr_put16(out, id);
    rtr_put16(out + 2, (uint16_t)(0x8180 | (rcode & 0xF)));
    rtr_put16(out + 4, 1);
    uint16_t answers = (answer || ptr_name) ? 1 : 0;
    rtr_put16(out + 6, answers);
    uint32_t used = 12;
    if (question_len > ROUTER_DNS_PACKET_SIZE - 64) return;
    memcpy(out + used, question, question_len);
    used += question_len;
    if (answers) {
        out[used++] = 0xC0; out[used++] = 0x0C;
        uint16_t type = ptr_name ? 12 : (qtype == 28 ? 28 : 1);
        rtr_put16(out + used, type); used += 2;
        rtr_put16(out + used, 1); used += 2;
        rtr_put32(out + used, 60); used += 4;
        if (ptr_name) {
            /* Encode the PTR target as DNS labels. */
            const char* p = ptr_name;
            uint32_t cursor = used + 2;
            p = ptr_name;
            while (*p) {
                const char* dot = p;
                while (*dot && *dot != '.') dot++;
                uint32_t length = (uint32_t)(dot - p);
                if (!length || length > 63 || cursor + length + 2 > ROUTER_DNS_PACKET_SIZE) break;
                out[cursor] = (uint8_t)length;
                cursor++;
                memcpy(out + cursor, p, length);
                cursor += length;
                p = *dot ? dot + 1 : dot;
            }
            out[cursor++] = 0;
            rtr_put16(out + used, (uint16_t)(cursor - used - 2));
            used = cursor;
        } else if (qtype == 28) {
            rtr_put16(out + used, 0); used += 2;
        } else {
            rtr_put16(out + used, 4); used += 2;
            memcpy(out + used, answer, 4); used += 4;
        }
    }
    net_send_udp(client_mac, local_ip, client_ip, 53, client_port, out, used);
}

static int rtr_dns_ptr_lookup(const char* name, char* out, uint32_t cap) {
    uint8_t ip[4];
    const char* suffix = ".in-addr.arpa";
    uint32_t name_len = strlen(name);
    uint32_t suffix_len = strlen(suffix);
    if (name_len <= suffix_len || !rtr_ieq(name + name_len - suffix_len, suffix)) return 0;
    uint32_t index = 0;
    const char* p = name;
    for (int part = 3; part >= 0; part--) {
        uint32_t value = 0;
        if (!*p || *p < '0' || *p > '9') return 0;
        while (*p >= '0' && *p <= '9') { value = value * 10 + (uint8_t)(*p - '0'); p++; }
        if (value > 255) return 0;
        ip[part] = (uint8_t)value;
        if (*p != '.') return 0;
        p++;
        index++;
    }
    (void)index;
    router_lease_t* lease = rtr_lease_by_ip(ip);
    if (lease && lease->hostname[0]) {
        uint32_t used = 0;
        rtr_append(out, cap, &used, lease->hostname);
        rtr_append(out, cap, &used, ".");
        rtr_append(out, cap, &used, rtr_cfg.domain);
        return 1;
    }
    for (uint32_t i = 0; i < ROUTER_DNS_CACHE_LIMIT; i++) {
        if (rtr_dns[i].active && rtr_equal4(rtr_dns[i].ip, ip)) {
            rtr_copy(out, cap, rtr_dns[i].name);
            return 1;
        }
    }
    return 0;
}

static void rtr_dns_forward(const uint8_t* frame, const uint8_t* src_ip,
                            const uint8_t* dst_ip, uint16_t client_port,
                            uint16_t client_id, const uint8_t* question,
                            uint32_t question_len, const char* name) {
    (void)name;
    uint32_t slot_index = ROUTER_DNS_PENDING_LIMIT;
    for (uint32_t i = 0; i < ROUTER_DNS_PENDING_LIMIT; i++)
        if (!rtr_pending[i].active) { slot_index = i; break; }
    if (slot_index == ROUTER_DNS_PENDING_LIMIT) { rtr_stats.dns_failed++; return; }
    router_dns_pending_t* slot = &rtr_pending[slot_index];
    memset(slot, 0, sizeof(*slot));
    slot->active = 1;
    slot->client_id = client_id;
    slot->client_port = client_port;
    slot->query_port = (uint16_t)(ROUTER_DNS_UPSTREAM_PORT + slot_index);
    rtr_dns_txid = (uint16_t)(rtr_dns_txid * 1103515245u + 12345u);
    if (!rtr_dns_txid) rtr_dns_txid = 0x2A17;
    slot->upstream_id = rtr_dns_txid;
    memcpy(slot->client_ip, src_ip, 4);
    memcpy(slot->client_mac, frame + 6, 6);
    memcpy(slot->local_ip, dst_ip, 4);
    slot->question_len = question_len;
    if (question_len > sizeof(slot->question)) { slot->active = 0; return; }
    memcpy(slot->question, question, question_len);
    slot->expires = uptime_ticks + ROUTER_DNS_PENDING_TTL;
    slot->next_send = uptime_ticks;
    slot->attempts = 0;
    rtr_dns_send_query(slot);
}

static void rtr_dns_send_query(router_dns_pending_t* slot) {
    uint8_t* out = rtr_packet;
    memset(out, 0, 64 + slot->question_len);
    rtr_put16(out, slot->upstream_id);
    rtr_put16(out + 2, 0x0100);
    rtr_put16(out + 4, 1);
    memcpy(out + 12, slot->question, slot->question_len);
    uint32_t total = 12 + slot->question_len;
    const uint8_t* upstream = rtr_cfg.dns_upstream;
    uint8_t mac[6];
    if (!net_route_mac(upstream, mac)) return;   /* ARP pending, retried in poll */
    const uint8_t* source = net_configured ? net_ip : rtr_cfg.ip;
    net_send_udp(mac, source, upstream, slot->query_port, 53, out, total);
    slot->attempts++;
    rtr_stats.dns_forwarded++;
}

static int rtr_dns_upstream_reply(const uint8_t* udp, uint32_t len,
                                  const uint8_t* src_ip, uint16_t dport) {
    if (len < 12) return 0;
    uint16_t id = rtr_get16(udp);
    for (uint32_t i = 0; i < ROUTER_DNS_PENDING_LIMIT; i++) {
        router_dns_pending_t* slot = &rtr_pending[i];
        if (!slot->active || slot->upstream_id != id ||
            dport != slot->query_port) continue;
        int upstream_ok = rtr_equal4(src_ip, rtr_cfg.dns_upstream) ||
                          rtr_equal4(src_ip, rtr_cfg.dns_secondary);
        if (!upstream_ok) return 0;
        uint8_t answer[4];
        int found = 0;
        uint16_t ancount = rtr_get16(udp + 6);
        uint32_t pos = 12;
        char query_name[ROUTER_DOMAIN_SIZE * 2];
        char answer_name[ROUTER_DOMAIN_SIZE * 2];
        pos = rtr_dns_name(udp, pos, len, query_name, sizeof(query_name));
        if (!pos) return 0;
        pos += 4;
        for (uint16_t a = 0; a < ancount && pos + 12 <= len; a++) {
            if ((udp[pos] & 0xC0) == 0xC0) pos += 2;
            else {
                pos = rtr_dns_name(udp, pos, len, answer_name, sizeof(answer_name));
                if (!pos) return 0;
            }
            if (pos + 10 > len) return 0;
            uint16_t type = rtr_get16(udp + pos);
            uint16_t ttl_lo = rtr_get16(udp + pos + 6);
            uint16_t rdlen = rtr_get16(udp + pos + 8);
            (void)ttl_lo;
            pos += 10;
            if (pos + rdlen > len) return 0;
            if (type == 1 && rdlen == 4 && !found) {
                memcpy(answer, udp + pos, 4);
                found = 1;
            }
            pos += rdlen;
        }
        if (found) {
            router_dns_add_record(query_name, answer, 0);
            rtr_stats.dns_cached++;
            rtr_dns_send(slot->client_mac, slot->local_ip, slot->client_ip,
                         slot->client_port, slot->client_id, slot->question,
                         slot->question_len, 1, answer, 0, 0);
        } else {
            rtr_stats.dns_failed++;
            rtr_dns_send(slot->client_mac, slot->local_ip, slot->client_ip,
                         slot->client_port, slot->client_id, slot->question,
                         slot->question_len, 1, 0, 3, 0);
        }
        memset(slot, 0, sizeof(*slot));
        return 1;
    }
    return 0;
}

static void rtr_dns_input(const uint8_t* frame, const uint8_t* src_ip,
                          const uint8_t* dst_ip, uint16_t sport,
                          const uint8_t* udp, uint32_t len) {
    if (len < 16) return;
    uint16_t flags = rtr_get16(udp + 2);
    if (flags & 0x8000) return;
    if (rtr_get16(udp + 4) != 1) return;
    char name[ROUTER_DOMAIN_SIZE * 2];
    uint32_t pos = rtr_dns_name(udp, 12, len, name, sizeof(name));
    if (!pos || pos + 4 > len) return;
    uint16_t qtype = rtr_get16(udp + pos);
    uint16_t qclass = rtr_get16(udp + pos + 2);
    pos += 4;
    uint32_t question_len = pos - 12;
    rtr_stats.dns_queries++;
    if (qtype == 12) {
        char ptr[ROUTER_DOMAIN_SIZE * 2];
        if (rtr_dns_ptr_lookup(name, ptr, sizeof(ptr)))
            rtr_dns_send(frame + 6, dst_ip, src_ip, sport, rtr_get16(udp),
                         udp + 12, question_len, qtype, 0, 0, ptr);
        else
            rtr_dns_send(frame + 6, dst_ip, src_ip, sport, rtr_get16(udp),
                         udp + 12, question_len, qtype, 0, 3, 0);
        return;
    }
    if (qtype != 1 && qtype != 255 && qtype != 28) {
        rtr_dns_send(frame + 6, dst_ip, src_ip, sport, rtr_get16(udp),
                     udp + 12, question_len, qtype, 0, 0, 0);
        return;
    }
    uint8_t answer[4];
    int kind = rtr_dns_local_lookup(name, answer);
    if (kind) {
        if (kind == 1) rtr_stats.dns_local++; else rtr_stats.dns_cached++;
        rtr_dns_send(frame + 6, dst_ip, src_ip, sport, rtr_get16(udp),
                     udp + 12, question_len, qtype, answer, 0, 0);
        return;
    }
    if (qclass != 1 || rtr_is_zero4(rtr_cfg.dns_upstream)) {
        rtr_stats.dns_failed++;
        rtr_dns_send(frame + 6, dst_ip, src_ip, sport, rtr_get16(udp),
                     udp + 12, question_len, qtype, 0, 3, 0);
        return;
    }
    rtr_dns_forward(frame, src_ip, dst_ip, sport, rtr_get16(udp),
                    udp + 12, question_len, name);
}

static void rtr_poll_dns(void) {
    for (uint32_t i = 0; i < ROUTER_DNS_PENDING_LIMIT; i++) {
        router_dns_pending_t* slot = &rtr_pending[i];
        if (!slot->active) continue;
        if ((int32_t)(uptime_ticks - slot->next_send) >= 0 && slot->attempts < 3) {
            rtr_dns_send_query(slot);
            slot->next_send = uptime_ticks + TICKS_PER_SEC;
        }
        if ((int32_t)(uptime_ticks - slot->expires) >= 0) {
            rtr_stats.dns_failed++;
            rtr_dns_send(slot->client_mac, slot->local_ip, slot->client_ip,
                         slot->client_port, slot->client_id, slot->question,
                         slot->question_len, 1, 0, 2, 0);
            memset(slot, 0, sizeof(*slot));
        }
    }
}

/* ------------------------------------------------------------------ */
/* NAT / port forwarding                                               */
/* ------------------------------------------------------------------ */

int router_nat_set_enabled(int enabled) {
    rtr_cfg.nat_enabled = enabled ? 1 : 0;
    return 0;
}

int router_forward_add(uint8_t protocol, uint16_t wan_port,
                       const uint8_t* lan_ip, uint16_t lan_port, const char* comment) {
    if (!lan_ip || !wan_port || !lan_port) return -1;
    if (protocol != ROUTER_PROTO_TCP && protocol != ROUTER_PROTO_UDP) return -1;
    if (!rtr_is_lan_ip(lan_ip)) return -1;
    router_forward_t* slot = 0;
    for (uint32_t i = 0; i < ROUTER_FORWARD_LIMIT; i++) {
        if (!rtr_forwards[i].active) { slot = &rtr_forwards[i]; break; }
    }
    if (!slot) return -1;
    memset(slot, 0, sizeof(*slot));
    slot->active = 1;
    slot->protocol = protocol;
    slot->wan_port = wan_port;
    slot->lan_port = lan_port;
    memcpy(slot->lan_ip, lan_ip, 4);
    rtr_copy(slot->comment, sizeof(slot->comment), comment);
    firewall_service_add(protocol, wan_port);
    return 0;
}

int router_forward_remove(uint32_t id) {
    if (!id || id > ROUTER_FORWARD_LIMIT || !rtr_forwards[id - 1].active) return -1;
    firewall_service_remove(rtr_forwards[id - 1].protocol, rtr_forwards[id - 1].wan_port);
    memset(&rtr_forwards[id - 1], 0, sizeof(rtr_forwards[0]));
    return 0;
}

const router_forward_t* router_get_forward(uint32_t index) {
    if (index >= ROUTER_FORWARD_LIMIT || !rtr_forwards[index].active) return 0;
    return &rtr_forwards[index];
}

const router_nat_entry_t* router_get_nat(uint32_t index) {
    if (index >= ROUTER_NAT_LIMIT || !rtr_nat[index].active) return 0;
    return &rtr_nat[index];
}

static void rtr_expire_nat(void) {
    for (uint32_t i = 0; i < ROUTER_NAT_LIMIT; i++) {
        if (rtr_nat[i].active && (int32_t)(uptime_ticks - rtr_nat[i].expires) >= 0)
            memset(&rtr_nat[i], 0, sizeof(rtr_nat[i]));
    }
}

static router_forward_t* rtr_forward_by_inside(uint8_t protocol, const uint8_t* lan_ip,
                                               uint16_t lan_port) {
    for (uint32_t i = 0; i < ROUTER_FORWARD_LIMIT; i++) {
        router_forward_t* f = &rtr_forwards[i];
        if (f->active && f->protocol == protocol && f->lan_port == lan_port &&
            rtr_equal4(f->lan_ip, lan_ip)) return f;
    }
    return 0;
}

static router_forward_t* rtr_forward_by_outside(uint8_t protocol, uint16_t wan_port) {
    for (uint32_t i = 0; i < ROUTER_FORWARD_LIMIT; i++) {
        router_forward_t* f = &rtr_forwards[i];
        if (f->active && f->protocol == protocol && f->wan_port == wan_port) return f;
    }
    return 0;
}

static uint16_t rtr_alloc_wan_port(uint8_t protocol) {
    for (uint32_t attempt = 0; attempt < 20000; attempt++) {
        rtr_nat_next_port++;
        if (rtr_nat_next_port < 30000 || rtr_nat_next_port >= 50000) rtr_nat_next_port = 30000;
        uint16_t candidate = rtr_nat_next_port;
        int used = 0;
        for (uint32_t i = 0; i < ROUTER_NAT_LIMIT; i++) {
            if (rtr_nat[i].active && rtr_nat[i].protocol == protocol && rtr_nat[i].wan_port == candidate) {
                used = 1; break;
            }
        }
        if (!used && !rtr_forward_by_outside(protocol, candidate)) return candidate;
    }
    return 0;
}

static router_nat_entry_t* rtr_nat_lookup(uint8_t protocol, const uint8_t* lan_ip,
                                          uint16_t lan_port, const uint8_t* remote_ip,
                                          uint16_t remote_port) {
    for (uint32_t i = 0; i < ROUTER_NAT_LIMIT; i++) {
        router_nat_entry_t* entry = &rtr_nat[i];
        if (!entry->active || entry->protocol != protocol) continue;
        if (rtr_equal4(entry->lan_ip, lan_ip) && entry->lan_port == lan_port &&
            rtr_equal4(entry->remote_ip, remote_ip) && entry->remote_port == remote_port)
            return entry;
    }
    return 0;
}

static router_nat_entry_t* rtr_nat_new(uint8_t protocol) {
    (void)protocol;
    rtr_expire_nat();
    for (uint32_t i = 0; i < ROUTER_NAT_LIMIT; i++)
        if (!rtr_nat[i].active) { memset(&rtr_nat[i], 0, sizeof(rtr_nat[i])); return &rtr_nat[i]; }
    return 0;
}

static void rtr_l4_checksum(uint8_t* ip, uint8_t* l4, uint32_t l4_len, uint8_t protocol) {
    uint32_t checksum_offset = protocol == ROUTER_PROTO_TCP ? 16u : 6u;
    if (l4_len < checksum_offset + 2) return;
    l4[checksum_offset] = 0;
    l4[checksum_offset + 1] = 0;
    uint32_t sum = 0;
    for (uint32_t i = 0; i < 4; i += 2) sum += ((uint32_t)ip[12 + i] << 8) | ip[13 + i];
    for (uint32_t i = 0; i < 4; i += 2) sum += ((uint32_t)ip[16 + i] << 8) | ip[17 + i];
    sum += protocol;
    sum += l4_len;
    for (uint32_t i = 0; i < l4_len; i += 2)
        sum += ((uint32_t)l4[i] << 8) | ((i + 1 < l4_len) ? l4[i + 1] : 0);
    while (sum >> 16) sum = (sum & 0xFFFFu) + (sum >> 16);
    uint16_t checksum = (uint16_t)(~sum & 0xFFFFu);
    if (!checksum && protocol == ROUTER_PROTO_UDP) checksum = 0xFFFFu;
    rtr_put16(l4 + checksum_offset, checksum);
}

static int rtr_rewrite(const uint8_t* frame, uint32_t len, const uint8_t* new_src,
                       const uint8_t* new_dst, uint16_t new_sport, uint16_t new_dport,
                       const uint8_t* dst_mac, uint8_t protocol) {
    if (len < 34 || len > ROUTER_NAT_FRAME_SIZE) return 0;
    uint8_t* out = rtr_nat_frame;
    memcpy(out, frame, len);
    memcpy(out, dst_mac, 6);
    memcpy(out + 6, net_mac, 6);
    uint8_t* ip = out + 14;
    uint32_t ihl = (uint32_t)(ip[0] & 0x0F) * 4;
    if (ihl < 20 || len < 14 + ihl) return 0;
    uint32_t total = rtr_get16(ip + 2);
    if (total < ihl || total > len - 14) return 0;
    if (!ip[8] || ip[8] == 1) return 0;   /* TTL exhausted */
    if (new_src) memcpy(ip + 12, new_src, 4);
    if (new_dst) memcpy(ip + 16, new_dst, 4);
    ip[8]--;
    rtr_put16(ip + 10, 0);
    rtr_put16(ip + 10, rtr_csum(ip, ihl));
    uint8_t* l4 = ip + ihl;
    uint32_t l4_len = total - ihl;
    if (protocol == ROUTER_PROTO_TCP || protocol == ROUTER_PROTO_UDP) {
        if (l4_len < 8) return 0;
        if (new_sport) rtr_put16(l4, new_sport);
        if (new_dport) rtr_put16(l4 + 2, new_dport);
        rtr_l4_checksum(ip, l4, l4_len, protocol);
    } else if (protocol == 1) {
        if (l4_len < 8) return 0;
        if (new_sport) rtr_put16(l4 + 4, new_sport);
        rtr_put16(l4 + 2, 0);
        rtr_put16(l4 + 2, rtr_csum(l4, l4_len));
    } else return 0;
    net_send_raw(out, len);
    rtr_stats.forwarded_frames++;
    return 1;
}

static const uint8_t* rtr_wan_address(void) {
    return net_configured ? net_ip : rtr_cfg.ip;
}

static int rtr_is_wan_address(const uint8_t* ip) {
    if (!ip) return 0;
    if (net_configured && rtr_equal4(ip, net_ip)) return 1;
    if (rtr_cfg.wan_mode == ROUTER_WAN_STATIC && rtr_equal4(ip, rtr_cfg.wan_ip)) return 1;
    return 0;
}

static uint16_t rtr_frame_port(const uint8_t* frame, uint8_t protocol, int source) {
    uint32_t ihl = (uint32_t)(frame[14] & 0x0F) * 4;
    const uint8_t* l4 = frame + 14 + ihl;
    if (protocol == ROUTER_PROTO_TCP || protocol == ROUTER_PROTO_UDP)
        return rtr_get16(l4 + (source ? 0 : 2));
    if (protocol == 1) return rtr_get16(l4 + 4);
    return 0;
}

static int rtr_nat_outbound(const uint8_t* frame, uint32_t len, const uint8_t* src_ip,
                            const uint8_t* dst_ip, uint16_t sport, uint16_t dport,
                            uint8_t protocol) {
    if (!rtr_is_lan_ip(src_ip) || rtr_equal4(src_ip, rtr_cfg.ip)) return 0;
    uint8_t mac[6];
    if (!net_route_mac(dst_ip, mac)) { rtr_stats.dropped_frames++; return 0; }
    router_nat_entry_t* entry = rtr_nat_lookup(protocol, src_ip, sport, dst_ip, dport);
    if (!entry) {
        entry = rtr_nat_new(protocol);
        if (!entry) { rtr_stats.dropped_frames++; return 0; }
        router_forward_t* forward = rtr_forward_by_inside(protocol, src_ip, sport);
        uint16_t wan_port = forward ? forward->wan_port : rtr_alloc_wan_port(protocol);
        if (!wan_port) { memset(entry, 0, sizeof(*entry)); rtr_stats.dropped_frames++; return 0; }
        entry->active = 1;
        entry->protocol = protocol;
        memcpy(entry->lan_ip, src_ip, 4);
        memcpy(entry->remote_ip, dst_ip, 4);
        memcpy(entry->mac, frame + 6, 6);
        entry->lan_port = sport;
        entry->wan_port = wan_port;
        entry->remote_port = dport;
        rtr_stats.nat_created++;
    }
    entry->expires = uptime_ticks + ROUTER_NAT_TTL;
    entry->packets++;
    rtr_stats.nat_hits++;
    return rtr_rewrite(frame, len, rtr_wan_address(), 0,
                       protocol == 1 ? entry->wan_port : entry->wan_port, 0, mac, protocol);
}

static int rtr_client_mac(const uint8_t* lan_ip, uint8_t* mac_out) {
    router_lease_t* lease = rtr_lease_by_ip(lan_ip);
    if (lease) { memcpy(mac_out, lease->mac, 6); return 1; }
    if (net_arp_cache_lookup(lan_ip, mac_out)) return 1;
    memset(mac_out, 0xFF, 6);
    net_arp_request(lan_ip);
    return 0;
}

static int rtr_nat_inbound(const uint8_t* frame, uint32_t len, const uint8_t* src_ip,
                           uint16_t sport, uint16_t dport, uint8_t protocol) {
    router_nat_entry_t* entry = 0;
    rtr_expire_nat();
    for (uint32_t i = 0; i < ROUTER_NAT_LIMIT; i++) {
        router_nat_entry_t* candidate = &rtr_nat[i];
        if (!candidate->active || candidate->protocol != protocol ||
            candidate->wan_port != dport) continue;
        if ((protocol == ROUTER_PROTO_TCP || protocol == ROUTER_PROTO_UDP) &&
            candidate->remote_port && candidate->remote_port != sport) continue;
        if (!rtr_is_zero4(candidate->remote_ip) && !rtr_equal4(candidate->remote_ip, src_ip)) continue;
        entry = candidate;
        break;
    }
    router_forward_t* forward = 0;
    if (!entry) {
        forward = rtr_forward_by_outside(protocol, dport);
        if (!forward) return 0;
        entry = rtr_nat_new(protocol);
        if (!entry) return 0;
        entry->active = 1;
        entry->protocol = protocol;
        memcpy(entry->lan_ip, forward->lan_ip, 4);
        memcpy(entry->remote_ip, src_ip, 4);
        entry->lan_port = forward->lan_port;
        entry->wan_port = forward->wan_port;
        entry->remote_port = sport;
        rtr_stats.nat_created++;
        forward->hits++;
        rtr_stats.forward_hits++;
    }
    entry->expires = uptime_ticks + ROUTER_NAT_TTL;
    entry->packets++;
    rtr_stats.nat_hits++;
    uint8_t client_mac[6];
    if (!rtr_client_mac(entry->lan_ip, client_mac)) { rtr_stats.dropped_frames++; return 0; }
    /* ICMP echo uses the identifier field instead of a destination port. */
    uint16_t new_sport = (protocol == 1) ? entry->lan_port : 0;
    uint16_t new_dport = (protocol == 1) ? 0 : entry->lan_port;
    return rtr_rewrite(frame, len, 0, entry->lan_ip, new_sport, new_dport,
                       client_mac, protocol);
}

static int rtr_forward_hook(const uint8_t* frame, uint32_t len) {
    if (!rtr_cfg.running || !rtr_cfg.nat_enabled || len < 34) return 0;
    const uint8_t* ip = frame + 14;
    if ((ip[0] >> 4) != 4 || (ip[6] & 0x3F) || ip[7]) return 0;
    uint8_t protocol = ip[9];
    if (protocol != 1 && protocol != ROUTER_PROTO_TCP && protocol != ROUTER_PROTO_UDP) return 0;
    const uint8_t* src_ip = ip + 12;
    const uint8_t* dst_ip = ip + 16;
    uint16_t sport = rtr_frame_port(frame, protocol, 1);
    uint16_t dport = rtr_frame_port(frame, protocol, 0);
    if (rtr_is_lan_ip(src_ip) && !rtr_is_lan_ip(dst_ip))
        return rtr_nat_outbound(frame, len, src_ip, dst_ip, sport, dport, protocol);
    if (rtr_is_wan_address(dst_ip))
        return rtr_nat_inbound(frame, len, src_ip, sport, dport, protocol);
    rtr_stats.dropped_frames++;
    return 0;
}

static int rtr_arp_hook(const uint8_t* target_ip, const uint8_t* sender_mac,
                        const uint8_t* sender_ip) {
    if (!rtr_cfg.running) return 0;
    if (!rtr_equal4(target_ip, rtr_cfg.ip) &&
        !(rtr_cfg.wan_mode == ROUTER_WAN_STATIC && rtr_equal4(target_ip, rtr_cfg.wan_ip)))
        return 0;
    uint8_t pkt[42];
    uint32_t off = 0;
    for (uint32_t i = 0; i < 6; i++) pkt[off + i] = sender_mac[i];
    for (uint32_t i = 0; i < 6; i++) pkt[off + 6 + i] = net_mac[i];
    pkt[12] = 0x08; pkt[13] = 0x06;
    pkt[14] = 0; pkt[15] = 1; pkt[16] = 0x08; pkt[17] = 0;
    pkt[18] = 6; pkt[19] = 4; pkt[20] = 0; pkt[21] = 2;
    for (uint32_t i = 0; i < 6; i++) pkt[22 + i] = net_mac[i];
    memcpy(pkt + 28, target_ip, 4);
    memcpy(pkt + 32, sender_mac, 6);
    memcpy(pkt + 38, sender_ip, 4);
    net_send_raw(pkt, 42);
    return 1;
}

static int rtr_local_ip_hook(const uint8_t* ip) {
    if (!rtr_cfg.running) return 0;
    if (rtr_equal4(ip, rtr_cfg.ip)) return 1;
    if (rtr_cfg.wan_mode == ROUTER_WAN_STATIC && rtr_equal4(ip, rtr_cfg.wan_ip)) return 1;
    return 0;
}

static int rtr_service_hook(const uint8_t* frame, uint32_t frame_len,
                            const uint8_t* src_ip, const uint8_t* dst_ip,
                            uint16_t sport, uint16_t dport, uint8_t proto) {
    if (!rtr_cfg.running || frame_len < 34) return 0;
    const uint8_t* ip = frame + 14;
    uint32_t ihl = (uint32_t)(ip[0] & 0x0F) * 4;
    if (ihl < 20 || frame_len < 14 + ihl) return 0;
    const uint8_t* l4 = frame + 14 + ihl;
    uint32_t l4_len = rtr_get16(ip + 2) > ihl ? rtr_get16(ip + 2) - ihl : 0;
    if (l4_len > frame_len - 14 - ihl) l4_len = frame_len - 14 - ihl;

    uint32_t udp_payload_len = 0;
    if (proto == ROUTER_PROTO_UDP && l4_len >= 8) {
        uint16_t ulen = rtr_get16(l4 + 4);
        if (ulen >= 8 && ulen <= l4_len) udp_payload_len = ulen - 8;
    }
    if (proto == ROUTER_PROTO_UDP && dport == 67 && sport == 68 && rtr_cfg.dhcp_enabled) {
        if (!udp_payload_len) return 1;
        rtr_dhcp_input(frame, l4 + 8, udp_payload_len);
        return 1;
    }
    if (proto == ROUTER_PROTO_UDP && dport == 53 && rtr_cfg.dns_enabled) {
        if (!udp_payload_len) return 1;
        rtr_dns_input(frame, src_ip, dst_ip, sport, l4 + 8, udp_payload_len);
        return 1;
    }
    if (proto == ROUTER_PROTO_UDP && rtr_cfg.dns_enabled && udp_payload_len &&
        dport >= ROUTER_DNS_UPSTREAM_PORT &&
        dport < ROUTER_DNS_UPSTREAM_PORT + ROUTER_DNS_PENDING_LIMIT) {
        if (rtr_dns_upstream_reply(l4 + 8, udp_payload_len, src_ip, dport)) return 1;
    }
    if (rtr_cfg.nat_enabled && rtr_is_wan_address(dst_ip) &&
        (proto == ROUTER_PROTO_TCP || proto == ROUTER_PROTO_UDP || proto == 1)) {
        if (rtr_nat_inbound(frame, frame_len, src_ip, sport, dport, proto)) return 1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* firewall profile                                                    */
/* ------------------------------------------------------------------ */

static int rtr_profile_deny_service(uint8_t protocol, uint16_t port, const char* comment) {
    firewall_rule_t rule;
    memset(&rule, 0, sizeof(rule));
    rule.enabled = 1;
    rule.action = FIREWALL_ACTION_DENY;
    rule.protocol = protocol;
    rule.direction = FIREWALL_DIR_IN;
    rule.port = port;
    rule.port_end = port;
    rule.source_port_end = 0xFFFFu;
    rtr_copy(rule.comment, sizeof(rule.comment), comment);
    return firewall_rule_add(&rule);
}

int router_firewall_profile(void) {
    firewall_clear_rules();
    firewall_set_policy(FIREWALL_DIR_ANY, FIREWALL_ACTION_DENY);
    firewall_set_policy(FIREWALL_DIR_OUT, FIREWALL_ACTION_ALLOW);
    firewall_rule_t rule;
    memset(&rule, 0, sizeof(rule));
    rule.enabled = 1;
    rule.action = FIREWALL_ACTION_ALLOW;
    rule.protocol = FIREWALL_PROTOCOL_ANY;
    rule.direction = FIREWALL_DIR_ANY;
    rule.state = FIREWALL_STATE_ESTABLISHED;
    rule.port_end = 0xFFFFu;
    rule.source_port_end = 0xFFFFu;
    rtr_copy(rule.comment, sizeof(rule.comment), "established");
    if (firewall_rule_add(&rule) < 0) return -1;
    memset(&rule, 0, sizeof(rule));
    rule.enabled = 1;
    rule.action = FIREWALL_ACTION_ALLOW;
    rule.protocol = FIREWALL_PROTOCOL_ANY;
    rule.direction = FIREWALL_DIR_FORWARD;
    rule.has_source = 1;
    rule.source_prefix = rtr_mask_prefix(rtr_cfg.mask);
    memcpy(rule.source, rtr_cfg.ip, 4);
    rule.port_end = 0xFFFFu;
    rule.source_port_end = 0xFFFFu;
    rtr_copy(rule.comment, sizeof(rule.comment), "lan to wan");
    if (firewall_rule_add(&rule) < 0) return -1;
    memset(&rule, 0, sizeof(rule));
    rule.enabled = 1;
    rule.action = FIREWALL_ACTION_ALLOW;
    rule.protocol = FIREWALL_PROTOCOL_ANY;
    rule.direction = FIREWALL_DIR_IN;
    rule.has_source = 1;
    rule.source_prefix = rtr_mask_prefix(rtr_cfg.mask);
    memcpy(rule.source, rtr_cfg.ip, 4);
    rule.port_end = 0xFFFFu;
    rule.source_port_end = 0xFFFFu;
    rtr_copy(rule.comment, sizeof(rule.comment), "lan to router");
    if (firewall_rule_add(&rule) < 0) return -1;
    /* Anything not from the LAN subnet is stopped before implicit service
     * ports are consulted, so the WAN side cannot reach DHCP, DNS or the
     * management console. */
    if (rtr_profile_deny_service(FIREWALL_PROTOCOL_UDP, 67, "wan dhcp blocked") < 0) return -1;
    if (rtr_profile_deny_service(FIREWALL_PROTOCOL_UDP, 53, "wan dns blocked") < 0) return -1;
    if (rtr_profile_deny_service(FIREWALL_PROTOCOL_TCP, rtr_cfg.http_port, "wan console blocked") < 0) return -1;
    firewall_service_add(FIREWALL_PROTOCOL_UDP, 67);
    firewall_service_add(FIREWALL_PROTOCOL_UDP, 53);
    firewall_service_add(FIREWALL_PROTOCOL_TCP, rtr_cfg.http_port);
    return 0;
}

/* ------------------------------------------------------------------ */
/* poll                                                                */
/* ------------------------------------------------------------------ */

void router_poll(void) {
    if (rtr_poll_busy) return;
    rtr_poll_busy = 1;
    if (rtr_cfg.running) {
        rtr_expire_leases();
        rtr_expire_dns();
        rtr_expire_nat();
        rtr_poll_dns();
        if (uptime_ticks - rtr_last_wan_refresh >= TICKS_PER_SEC) {
            rtr_last_wan_refresh = uptime_ticks;
            router_wan_refresh();
        }
        if (rtr_cfg.http_running) router_http_poll();
    }
    rtr_poll_busy = 0;
}

/* ------------------------------------------------------------------ */
/* JSON status                                                         */
/* ------------------------------------------------------------------ */

static void rtr_json_str(char* out, uint32_t cap, uint32_t* used, const char* value) {
    rtr_append(out, cap, used, "\"");
    if (value) {
        while (*value && *used + 2 < cap) {
            char c = *value++;
            if (c == '"' || c == '\\') { out[(*used)++] = '\\'; out[(*used)++] = c; }
            else if ((uint8_t)c < 32) out[(*used)++] = ' ';
            else out[(*used)++] = c;
        }
    }
    rtr_append(out, cap, used, "\"");
}

static void rtr_json_ip(char* out, uint32_t cap, uint32_t* used, const uint8_t* ip) {
    char text[16];
    router_format_ip(text, sizeof(text), ip);
    rtr_json_str(out, cap, used, text);
}

static void rtr_json_num(char* out, uint32_t cap, uint32_t* used, uint32_t value) {
    rtr_num(out, cap, used, value);
}

static void rtr_json_bool(char* out, uint32_t cap, uint32_t* used, int value) {
    rtr_append(out, cap, used, value ? "true" : "false");
}

uint32_t router_build_firewall_status(char* out, uint32_t cap) {
    uint32_t used = 0;
    if (cap) out[0] = 0;
    rtr_append(out, cap, &used, "{\"in\":");
    rtr_json_str(out, cap, &used, firewall_action_name(firewall_get_policy(FIREWALL_DIR_IN)));
    rtr_append(out, cap, &used, ",\"out\":");
    rtr_json_str(out, cap, &used, firewall_action_name(firewall_get_policy(FIREWALL_DIR_OUT)));
    rtr_append(out, cap, &used, ",\"forward\":");
    rtr_json_str(out, cap, &used, firewall_action_name(firewall_get_policy(FIREWALL_DIR_FORWARD)));
    rtr_append(out, cap, &used, ",\"rate\":");
    rtr_json_num(out, cap, &used, firewall_get_rate_limit());
    rtr_append(out, cap, &used, ",\"rules\":[");
    int first = 1;
    for (uint32_t i = 0; i < FIREWALL_RULE_LIMIT; i++) {
        const firewall_rule_t* rule = firewall_get_rule(i);
        if (!rule) continue;
        if (!first) rtr_append(out, cap, &used, ",");
        first = 0;
        rtr_append(out, cap, &used, "{\"id\":");
        rtr_json_num(out, cap, &used, i + 1);
        rtr_append(out, cap, &used, ",\"action\":");
        rtr_json_str(out, cap, &used, firewall_action_name(rule->action));
        rtr_append(out, cap, &used, ",\"protocol\":");
        rtr_json_str(out, cap, &used, firewall_protocol_name(rule->protocol));
        rtr_append(out, cap, &used, ",\"dir\":");
        rtr_json_str(out, cap, &used, firewall_direction_name(rule->direction));
        rtr_append(out, cap, &used, ",\"enabled\":");
        rtr_json_bool(out, cap, &used, rule->enabled);
        rtr_append(out, cap, &used, ",\"src\":");
        if (rule->has_source) {
            char text[24];
            uint32_t n = 0;
            rtr_ip_text(text, sizeof(text), &n, rule->source);
            if (n + 1 < sizeof(text)) text[n++] = '/';
            text[n] = 0;
            rtr_num(text, sizeof(text), &n, rule->source_prefix);
            rtr_json_str(out, cap, &used, text);
        } else rtr_json_str(out, cap, &used, "any");
        rtr_append(out, cap, &used, ",\"dst\":");
        if (rule->has_destination) rtr_json_ip(out, cap, &used, rule->destination);
        else rtr_json_str(out, cap, &used, "any");
        rtr_append(out, cap, &used, ",\"dport\":");
        rtr_json_num(out, cap, &used, rule->port);
        rtr_append(out, cap, &used, ",\"dport_end\":");
        rtr_json_num(out, cap, &used, rule->port_end);
        rtr_append(out, cap, &used, ",\"sport\":");
        rtr_json_num(out, cap, &used, rule->source_port);
        rtr_append(out, cap, &used, ",\"state\":");
        rtr_json_num(out, cap, &used, rule->state);
        rtr_append(out, cap, &used, ",\"log\":");
        rtr_json_bool(out, cap, &used, rule->log);
        rtr_append(out, cap, &used, ",\"hits\":");
        rtr_json_num(out, cap, &used, rule->hits);
        rtr_append(out, cap, &used, ",\"comment\":");
        rtr_json_str(out, cap, &used, rule->comment);
        rtr_append(out, cap, &used, "}");
    }
    rtr_append(out, cap, &used, "],\"services\":[");
    first = 1;
    for (uint32_t i = 0; i < FIREWALL_SERVICE_LIMIT; i++) {
        uint8_t protocol;
        uint16_t port;
        if (firewall_service_get(i, &protocol, &port) != 0) continue;
        if (!first) rtr_append(out, cap, &used, ",");
        first = 0;
        rtr_append(out, cap, &used, "{\"proto\":");
        rtr_json_str(out, cap, &used, firewall_protocol_name(protocol));
        rtr_append(out, cap, &used, ",\"port\":");
        rtr_json_num(out, cap, &used, port);
        rtr_append(out, cap, &used, "}");
    }
    rtr_append(out, cap, &used, "],\"counters\":{\"in_allow\":");
    rtr_json_num(out, cap, &used, firewall_counter(FIREWALL_DIR_IN, FIREWALL_ACTION_ALLOW));
    rtr_append(out, cap, &used, ",\"in_drop\":");
    rtr_json_num(out, cap, &used, firewall_counter(FIREWALL_DIR_IN, FIREWALL_ACTION_DENY));
    rtr_append(out, cap, &used, ",\"out_allow\":");
    rtr_json_num(out, cap, &used, firewall_counter(FIREWALL_DIR_OUT, FIREWALL_ACTION_ALLOW));
    rtr_append(out, cap, &used, ",\"out_drop\":");
    rtr_json_num(out, cap, &used, firewall_counter(FIREWALL_DIR_OUT, FIREWALL_ACTION_DENY));
    rtr_append(out, cap, &used, ",\"fwd_allow\":");
    rtr_json_num(out, cap, &used, firewall_counter(FIREWALL_DIR_FORWARD, FIREWALL_ACTION_ALLOW));
    rtr_append(out, cap, &used, ",\"fwd_drop\":");
    rtr_json_num(out, cap, &used, firewall_counter(FIREWALL_DIR_FORWARD, FIREWALL_ACTION_DENY));
    rtr_append(out, cap, &used, ",\"rate_drops\":");
    rtr_json_num(out, cap, &used, firewall_rate_drop_count());
    rtr_append(out, cap, &used, "}}");
    return used;
}

uint32_t router_build_log(char* out, uint32_t cap) {
    uint32_t used = 0;
    if (cap) out[0] = 0;
    rtr_append(out, cap, &used, "[");
    uint32_t count = firewall_log_count();
    for (uint32_t i = 0; i < count && i < 16; i++) {
        if (i) rtr_append(out, cap, &used, ",");
        rtr_json_str(out, cap, &used, firewall_log_entry(i));
    }
    rtr_append(out, cap, &used, "]");
    return used;
}

static void rtr_json_port(char* out, uint32_t cap, uint32_t* used, uint32_t index) {
    net_port_t port;
    if (net_port_get(index, &port) != 0) { rtr_append(out, cap, used, "null"); return; }
    char mac[20];
    char pci[12];
    static const char hex[] = "0123456789abcdef";
    router_format_mac(mac, sizeof(mac), port.mac);
    pci[0] = hex[(port.bus >> 4) & 0xF]; pci[1] = hex[port.bus & 0xF];
    pci[2] = ':';
    pci[3] = hex[(port.slot >> 4) & 0xF]; pci[4] = hex[port.slot & 0xF];
    pci[5] = '.'; pci[6] = hex[port.func & 0xF]; pci[7] = 0;
    rtr_append(out, cap, used, "{\"ifname\":");
    rtr_json_str(out, cap, used, port.ifname);
    rtr_append(out, cap, used, ",\"driver\":");
    rtr_json_str(out, cap, used, port.driver);
    rtr_append(out, cap, used, ",\"mac\":");
    rtr_json_str(out, cap, used, port.mac[0] || port.mac[1] || port.mac[2] ||
                                  port.mac[3] || port.mac[4] || port.mac[5] ? mac : "unknown");
    rtr_append(out, cap, used, ",\"pci\":");
    rtr_json_str(out, cap, used, pci);
    rtr_append(out, cap, used, ",\"active\":");
    rtr_json_bool(out, cap, used, port.active);
    rtr_append(out, cap, used, ",\"link\":");
    rtr_json_bool(out, cap, used, port.link);
    rtr_append(out, cap, used, ",\"role\":");
    uint32_t role = router_port_role(index);
    rtr_json_str(out, cap, used, role == 1 ? "wan" : role == 2 ? "lan" : "unused");
    rtr_append(out, cap, used, "}");
}

uint32_t router_build_status(char* out, uint32_t cap) {
    uint32_t used = 0;
    if (cap) out[0] = 0;
    rtr_append(out, cap, &used, "{\"ok\":true,\"product\":\"SharkOS Router\",");
    uint32_t port_count = net_port_count();
    rtr_append(out, cap, &used, "\"interfaces\":{\"count\":");
    rtr_json_num(out, cap, &used, port_count);
    rtr_append(out, cap, &used, ",\"mode\":");
    rtr_json_str(out, cap, &used, rtr_cfg.lan_shared ? "shared" : "dedicated");
    rtr_append(out, cap, &used, ",\"lan_shared\":");
    rtr_json_bool(out, cap, &used, rtr_cfg.lan_shared);
    rtr_append(out, cap, &used, ",\"wan\":");
    rtr_json_num(out, cap, &used, rtr_cfg.wan_port);
    rtr_append(out, cap, &used, ",\"lan\":");
    rtr_json_num(out, cap, &used, rtr_cfg.lan_port);
    rtr_append(out, cap, &used, ",\"uplink\":");
    rtr_json_port(out, cap, &used, rtr_cfg.wan_port);
    rtr_append(out, cap, &used, ",\"lan_port\":");
    if (!rtr_cfg.lan_shared) rtr_json_port(out, cap, &used, rtr_cfg.lan_port);
    else rtr_append(out, cap, &used, "null");
    rtr_append(out, cap, &used, ",\"ports\":[");
    for (uint32_t i = 0; i < port_count; i++) {
        if (i) rtr_append(out, cap, &used, ",");
        rtr_json_port(out, cap, &used, i);
    }
    rtr_append(out, cap, &used, "]}");
    rtr_append(out, cap, &used, ",\"running\":");
    rtr_json_bool(out, cap, &used, rtr_cfg.running);
    rtr_append(out, cap, &used, ",\"uptime\":");
    rtr_json_num(out, cap, &used, uptime_ticks / TICKS_PER_SEC);
    rtr_append(out, cap, &used, ",\"router_uptime\":");
    rtr_json_num(out, cap, &used, rtr_cfg.running ? (uptime_ticks - rtr_cfg.started_at) / TICKS_PER_SEC : 0);
    rtr_append(out, cap, &used, ",\"lan\":{\"ip\":");
    rtr_json_ip(out, cap, &used, rtr_cfg.ip);
    rtr_append(out, cap, &used, ",\"mask\":");
    rtr_json_ip(out, cap, &used, rtr_cfg.mask);
    rtr_append(out, cap, &used, ",\"dhcp\":{\"enabled\":");
    rtr_json_bool(out, cap, &used, rtr_cfg.dhcp_enabled);
    rtr_append(out, cap, &used, ",\"pool_start\":");
    rtr_json_ip(out, cap, &used, rtr_cfg.pool_start);
    rtr_append(out, cap, &used, ",\"pool_end\":");
    rtr_json_ip(out, cap, &used, rtr_cfg.pool_end);
    rtr_append(out, cap, &used, ",\"lease\":");
    rtr_json_num(out, cap, &used, rtr_cfg.lease_seconds);
    rtr_append(out, cap, &used, ",\"leases\":[");
    int first = 1;
    for (uint32_t i = 0; i < ROUTER_LEASE_LIMIT; i++) {
        const router_lease_t* lease = router_get_lease(i);
        if (!lease) continue;
        if (!first) rtr_append(out, cap, &used, ",");
        first = 0;
        rtr_append(out, cap, &used, "{\"ip\":");
        rtr_json_ip(out, cap, &used, lease->ip);
        rtr_append(out, cap, &used, ",\"mac\":");
        char mac[20];
        router_format_mac(mac, sizeof(mac), lease->mac);
        rtr_json_str(out, cap, &used, mac);
        rtr_append(out, cap, &used, ",\"host\":");
        rtr_json_str(out, cap, &used, lease->hostname);
        rtr_append(out, cap, &used, ",\"bound\":");
        rtr_json_bool(out, cap, &used, lease->bound);
        uint32_t left = 0;
        if ((int32_t)(lease->expires - uptime_ticks) > 0)
            left = (lease->expires - uptime_ticks) / TICKS_PER_SEC;
        rtr_append(out, cap, &used, ",\"expires\":");
        rtr_json_num(out, cap, &used, left);
        rtr_append(out, cap, &used, "}");
    }
    rtr_append(out, cap, &used, "],\"reservations\":[");
    first = 1;
    for (uint32_t i = 0; i < ROUTER_RESERVATION_LIMIT; i++) {
        const router_reservation_t* r = router_get_reservation(i);
        if (!r) continue;
        if (!first) rtr_append(out, cap, &used, ",");
        first = 0;
        rtr_append(out, cap, &used, "{\"id\":");
        rtr_json_num(out, cap, &used, i + 1);
        rtr_append(out, cap, &used, ",\"ip\":");
        rtr_json_ip(out, cap, &used, r->ip);
        rtr_append(out, cap, &used, ",\"mac\":");
        char mac[20];
        router_format_mac(mac, sizeof(mac), r->mac);
        rtr_json_str(out, cap, &used, mac);
        rtr_append(out, cap, &used, ",\"host\":");
        rtr_json_str(out, cap, &used, r->hostname);
        rtr_append(out, cap, &used, "}");
    }
    rtr_append(out, cap, &used, "]}}");
    rtr_append(out, cap, &used, ",\"wan\":{\"mode\":");
    rtr_json_str(out, cap, &used, rtr_cfg.wan_mode == ROUTER_WAN_DHCP ? "dhcp" :
                                  rtr_cfg.wan_mode == ROUTER_WAN_STATIC ? "static" : "disabled");
    rtr_append(out, cap, &used, ",\"status\":");
    rtr_json_str(out, cap, &used, rtr_wan_status);
    rtr_append(out, cap, &used, ",\"ip\":");
    rtr_json_ip(out, cap, &used, rtr_cfg.wan_ip);
    rtr_append(out, cap, &used, ",\"mask\":");
    rtr_json_ip(out, cap, &used, rtr_cfg.wan_mask);
    rtr_append(out, cap, &used, ",\"gw\":");
    rtr_json_ip(out, cap, &used, rtr_cfg.wan_gw);
    rtr_append(out, cap, &used, ",\"dns\":");
    rtr_json_ip(out, cap, &used, rtr_cfg.wan_dns);
    rtr_append(out, cap, &used, ",\"link\":");
    rtr_json_bool(out, cap, &used, net_has_link);
    rtr_append(out, cap, &used, ",\"dhcp_state\":");
    rtr_json_str(out, cap, &used, net_dhcp_status());
    rtr_append(out, cap, &used, ",\"lease_left\":");
    rtr_json_num(out, cap, &used, net_dhcp_lease_left());
    rtr_append(out, cap, &used, "}");
    rtr_append(out, cap, &used, ",\"dns\":{\"enabled\":");
    rtr_json_bool(out, cap, &used, rtr_cfg.dns_enabled);
    rtr_append(out, cap, &used, ",\"upstream\":");
    rtr_json_ip(out, cap, &used, rtr_cfg.dns_upstream);
    rtr_append(out, cap, &used, ",\"secondary\":");
    rtr_json_ip(out, cap, &used, rtr_cfg.dns_secondary);
    rtr_append(out, cap, &used, ",\"domain\":");
    rtr_json_str(out, cap, &used, rtr_cfg.domain);
    rtr_append(out, cap, &used, ",\"records\":[");
    first = 1;
    for (uint32_t i = 0; i < ROUTER_DNS_CACHE_LIMIT; i++) {
        const router_dns_entry_t* e = router_get_dns_entry(i);
        if (!e) continue;
        if (!first) rtr_append(out, cap, &used, ",");
        first = 0;
        rtr_append(out, cap, &used, "{\"name\":");
        rtr_json_str(out, cap, &used, e->name);
        rtr_append(out, cap, &used, ",\"ip\":");
        rtr_json_ip(out, cap, &used, e->ip);
        rtr_append(out, cap, &used, ",\"local\":");
        rtr_json_bool(out, cap, &used, e->local);
        rtr_append(out, cap, &used, "}");
    }
    rtr_append(out, cap, &used, "]}");
    rtr_append(out, cap, &used, ",\"nat\":{\"enabled\":");
    rtr_json_bool(out, cap, &used, rtr_cfg.nat_enabled);
    rtr_append(out, cap, &used, ",\"forwards\":[");
    first = 1;
    for (uint32_t i = 0; i < ROUTER_FORWARD_LIMIT; i++) {
        const router_forward_t* f = router_get_forward(i);
        if (!f) continue;
        if (!first) rtr_append(out, cap, &used, ",");
        first = 0;
        rtr_append(out, cap, &used, "{\"id\":");
        rtr_json_num(out, cap, &used, i + 1);
        rtr_append(out, cap, &used, ",\"proto\":");
        rtr_json_str(out, cap, &used, f->protocol == ROUTER_PROTO_TCP ? "tcp" : "udp");
        rtr_append(out, cap, &used, ",\"wan_port\":");
        rtr_json_num(out, cap, &used, f->wan_port);
        rtr_append(out, cap, &used, ",\"lan_port\":");
        rtr_json_num(out, cap, &used, f->lan_port);
        rtr_append(out, cap, &used, ",\"lan_ip\":");
        rtr_json_ip(out, cap, &used, f->lan_ip);
        rtr_append(out, cap, &used, ",\"hits\":");
        rtr_json_num(out, cap, &used, f->hits);
        rtr_append(out, cap, &used, ",\"comment\":");
        rtr_json_str(out, cap, &used, f->comment);
        rtr_append(out, cap, &used, "}");
    }
    rtr_append(out, cap, &used, "],\"mappings\":[");
    first = 1;
    for (uint32_t i = 0; i < ROUTER_NAT_LIMIT; i++) {
        const router_nat_entry_t* n = router_get_nat(i);
        if (!n) continue;
        if (!first) rtr_append(out, cap, &used, ",");
        first = 0;
        rtr_append(out, cap, &used, "{\"proto\":");
        rtr_json_num(out, cap, &used, n->protocol);
        rtr_append(out, cap, &used, ",\"lan_ip\":");
        rtr_json_ip(out, cap, &used, n->lan_ip);
        rtr_append(out, cap, &used, ",\"lan_port\":");
        rtr_json_num(out, cap, &used, n->lan_port);
        rtr_append(out, cap, &used, ",\"wan_port\":");
        rtr_json_num(out, cap, &used, n->wan_port);
        rtr_append(out, cap, &used, ",\"remote_ip\":");
        rtr_json_ip(out, cap, &used, n->remote_ip);
        rtr_append(out, cap, &used, ",\"packets\":");
        rtr_json_num(out, cap, &used, n->packets);
        rtr_append(out, cap, &used, "}");
    }
    rtr_append(out, cap, &used, "]}");
    rtr_append(out, cap, &used, ",\"http\":{\"port\":");
    rtr_json_num(out, cap, &used, rtr_cfg.http_port);
    rtr_append(out, cap, &used, ",\"running\":");
    rtr_json_bool(out, cap, &used, rtr_cfg.http_running);
    rtr_append(out, cap, &used, ",\"auth\":true}");
    rtr_append(out, cap, &used, ",\"stats\":{\"dhcp_discover\":");
    rtr_json_num(out, cap, &used, rtr_stats.dhcp_discover);
    rtr_append(out, cap, &used, ",\"dhcp_offer\":");
    rtr_json_num(out, cap, &used, rtr_stats.dhcp_offer);
    rtr_append(out, cap, &used, ",\"dhcp_request\":");
    rtr_json_num(out, cap, &used, rtr_stats.dhcp_request);
    rtr_append(out, cap, &used, ",\"dhcp_ack\":");
    rtr_json_num(out, cap, &used, rtr_stats.dhcp_ack);
    rtr_append(out, cap, &used, ",\"dhcp_nak\":");
    rtr_json_num(out, cap, &used, rtr_stats.dhcp_nak);
    rtr_append(out, cap, &used, ",\"dns_queries\":");
    rtr_json_num(out, cap, &used, rtr_stats.dns_queries);
    rtr_append(out, cap, &used, ",\"dns_local\":");
    rtr_json_num(out, cap, &used, rtr_stats.dns_local);
    rtr_append(out, cap, &used, ",\"dns_forwarded\":");
    rtr_json_num(out, cap, &used, rtr_stats.dns_forwarded);
    rtr_append(out, cap, &used, ",\"dns_cached\":");
    rtr_json_num(out, cap, &used, rtr_stats.dns_cached);
    rtr_append(out, cap, &used, ",\"dns_failed\":");
    rtr_json_num(out, cap, &used, rtr_stats.dns_failed);
    rtr_append(out, cap, &used, ",\"nat_mappings\":");
    rtr_json_num(out, cap, &used, rtr_stats.nat_created);
    rtr_append(out, cap, &used, ",\"forwarded_frames\":");
    rtr_json_num(out, cap, &used, rtr_stats.forwarded_frames);
    rtr_append(out, cap, &used, ",\"dropped_frames\":");
    rtr_json_num(out, cap, &used, rtr_stats.dropped_frames);
    rtr_append(out, cap, &used, ",\"http_requests\":");
    rtr_json_num(out, cap, &used, rtr_stats.http_requests);
    rtr_append(out, cap, &used, "},\"firewall\":");
    used += router_build_firewall_status(out + used, cap > used ? cap - used : 0);
    rtr_append(out, cap, &used, ",\"log\":");
    used += router_build_log(out + used, cap > used ? cap - used : 0);
    rtr_append(out, cap, &used, "}");
    return used;
}
