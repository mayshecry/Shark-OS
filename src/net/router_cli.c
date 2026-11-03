#include "kernel.h"
#include "net.h"
#include "firewall.h"
#include "router.h"

/* One CLI implementation serves three front ends:
 *   - `rtr ...` / `fw ...` in the SharkOS shell and server console
 *   - `POST /api/cli` on the port-69 management server
 *   - the SSH remote command path (server_execute_remote)
 * Output is captured into a buffer when requested, otherwise printed to the
 * terminal. No libc snprintf is used because this runs freestanding. */

#define CLI_LINE_MAX      512
#define CLI_CONFIG_PATH  "/System/Config/router.conf"
#define CLI_CONFIG_MAX   4096

static char* cli_out;
static uint32_t cli_cap;
static uint32_t cli_used;

static void cli_p(const char* text) {
    if (cli_out) {
        while (*text && cli_used + 1 < cli_cap) cli_out[cli_used++] = *text++;
        cli_out[cli_used] = 0;
    } else terminal_writestring(text);
}

static void cli_num(uint32_t value) {
    char tmp[12];
    uint32_t n = 0;
    if (!value) tmp[n++] = '0';
    while (value && n < sizeof(tmp)) { tmp[n++] = (char)('0' + value % 10); value /= 10; }
    while (n) { char c[2] = { tmp[--n], 0 }; cli_p(c); }
}

static void cli_ip(const uint8_t* ip) {
    char text[16];
    router_format_ip(text, sizeof(text), ip);
    cli_p(text);
}

static void cli_mac(const uint8_t* mac) {
    char text[20];
    router_format_mac(text, sizeof(text), mac);
    cli_p(text);
}

static uint32_t cli_u32(const char* text) {
    uint32_t value = 0;
    if (!text) return 0;
    if (*text < '0' || *text > '9') return 0;
    while (*text >= '0' && *text <= '9') { value = value * 10 + (uint32_t)(*text - '0'); text++; }
    return value;
}

static char* cli_next(char** cursor) {
    char* p = *cursor;
    while (*p == ' ' || *p == '\t') p++;
    if (!*p) { *cursor = p; return 0; }
    char* start;
    if (*p == '"') {
        p++;
        start = p;
        while (*p && *p != '"') p++;
        if (*p) *p++ = 0;
    } else {
        start = p;
        while (*p && *p != ' ' && *p != '\t') p++;
        if (*p) *p++ = 0;
    }
    *cursor = p;
    return start;
}

static int cli_parse_ip(const char* text, uint8_t* out) {
    return text && net_parse_ip4(text, out);
}

static int cli_proto(const char* text) {
    if (!text) return -1;
    if (!strcmp(text, "tcp")) return FIREWALL_PROTOCOL_TCP;
    if (!strcmp(text, "udp")) return FIREWALL_PROTOCOL_UDP;
    if (!strcmp(text, "icmp")) return FIREWALL_PROTOCOL_ICMP;
    if (!strcmp(text, "any")) return FIREWALL_PROTOCOL_ANY;
    return -1;
}

static int cli_dir(const char* text) {
    if (!text) return -1;
    if (!strcmp(text, "in") || !strcmp(text, "input")) return FIREWALL_DIR_IN;
    if (!strcmp(text, "out") || !strcmp(text, "output")) return FIREWALL_DIR_OUT;
    if (!strcmp(text, "forward") || !strcmp(text, "fwd")) return FIREWALL_DIR_FORWARD;
    if (!strcmp(text, "any")) return FIREWALL_DIR_ANY;
    return -1;
}

static int cli_state(const char* text) {
    if (!text) return -1;
    if (!strcmp(text, "new")) return FIREWALL_STATE_NEW;
    if (!strcmp(text, "established") || !strcmp(text, "est")) return FIREWALL_STATE_ESTABLISHED;
    if (!strcmp(text, "any")) return FIREWALL_STATE_ANY;
    return -1;
}

static int cli_action(const char* text) {
    if (!text) return -1;
    if (!strcmp(text, "allow") || !strcmp(text, "accept")) return FIREWALL_ACTION_ALLOW;
    if (!strcmp(text, "deny") || !strcmp(text, "drop") || !strcmp(text, "block")) return FIREWALL_ACTION_DENY;
    return -1;
}

static int cli_port_range(const char* text, uint16_t* start, uint16_t* end) {
    if (!text || !start || !end) return 0;
    uint32_t first = cli_u32(text);
    const char* dash = strchr(text, '-');
    uint32_t last = dash ? cli_u32(dash + 1) : first;
    if (!first || first > 65535 || !last || last > 65535 || last < first) return 0;
    *start = (uint16_t)first;
    *end = (uint16_t)last;
    return 1;
}

/* ------------------------------------------------------------------ */
/* help                                                               */
/* ------------------------------------------------------------------ */

static void cli_rtr_help(void) {
    cli_p("SharkOS Router (basic IPv4 router for the Server image)\n");
    cli_p("  rtr start [port]              start DHCP/DNS/NAT and the web console (default port 69)\n");
    cli_p("  rtr stop | status | stats | help\n");
    cli_p("  rtr lan <ip> <mask>           LAN address of this router\n");
    cli_p("  rtr dhcp pool <start> <end> | lease <seconds> | enable | disable\n");
    cli_p("  rtr dhcp leases | reservations | reserve <mac> <ip> [host] | unreserve <id>\n");
    cli_p("  rtr dhcp release <ip>         force a lease free\n");
    cli_p("  rtr dns enable | disable | upstream <ip> [ip2] | domain <name>\n");
    cli_p("  rtr dns add <name> <ip> | del <name> | records | flush\n");
    cli_p("  rtr wan status | dhcp | renew | static <ip> <mask> <gw> [dns] | down\n");
    cli_p("  rtr ports                      list NICs, uplink adapter and LAN port\n");
    cli_p("  rtr port wan|lan|auto <index>  assign which NIC is uplink / LAN (0-based)\n");
    cli_p("  rtr nat on | off | table      basic NAT44 for LAN clients\n");
    cli_p("  rtr forward add <tcp|udp> <wanport> <lanip> <lanport> [comment] | del <id> | list\n");
    cli_p("  rtr http port <port> | token <value> | profile router\n");
    cli_p("  rtr save [path] | load [path] persist router settings to /System/Config/router.conf\n");
    cli_p("Web console: http://<lan-ip>:69/   API: /api/status /api/fw /api/log /api/cli /api/token\n");
    cli_p("Mutations require header 'X-Shark-Token: <token>' (default: sharkos -- change it!).\n");
}

static void cli_fw_help(void) {
    cli_p("SharkOS firewall (ordered, first-match, stateful IPv4)\n");
    cli_p("Classic:  fw allow|deny <tcp|udp|icmp|any> <port|any> [from <ip>]\n");
    cli_p("Extended: fw add <allow|deny> <proto> [dir in|out|forward|any] [from ip/prefix]\n");
    cli_p("                    [to ip/prefix] [dport p|p-p] [sport p|p-p]\n");
    cli_p("                    [state new|established|any] [icmp <type>] [rate <pps>]\n");
    cli_p("                    [log] [comment \"text\"]\n");
    cli_p("Policies: fw policy [in|out|forward|any] allow|deny    Rules: 64 max\n");
    cli_p("Rules:    fw status | delete <id> | enable <id> | disable <id> | move <id> up|down\n");
    cli_p("          fw clear | rate <1-1000|off> | hits <id> | counters [reset]\n");
    cli_p("Profiles: fw profile open|server|lockdown|router\n");
    cli_p("Services: fw service add|del <tcp|udp> <port> | service list\n");
    cli_p("Logs:     fw log [count] | fw log clear | fw logdrops on|off\n");
    cli_p("Direction policies, CIDR matches, port ranges, TCP/ICMP state, per-rule rate\n");
    cli_p("limits, hit counters, logging, enable/disable, reorder and implicit service\n");
    cli_p("ports are supported. Explicit rules always win over implicit service ports.\n");
}

/* ------------------------------------------------------------------ */
/* status printers                                                     */
/* ------------------------------------------------------------------ */

static void cli_fw_status(int verbose) {
    cli_p("policies in=");
    cli_p(firewall_action_name(firewall_get_policy(FIREWALL_DIR_IN)));
    cli_p(" out=");
    cli_p(firewall_action_name(firewall_get_policy(FIREWALL_DIR_OUT)));
    cli_p(" forward=");
    cli_p(firewall_action_name(firewall_get_policy(FIREWALL_DIR_FORWARD)));
    cli_p("  rate=");
    if (firewall_get_rate_limit()) { cli_num(firewall_get_rate_limit()); cli_p("pps"); }
    else cli_p("off");
    cli_p("  rate-drops=");
    cli_num(firewall_rate_drop_count());
    cli_p("\nrules ");
    cli_num(firewall_rule_count());
    cli_p("/64:\n");
    for (uint32_t i = 0; i < FIREWALL_RULE_LIMIT; i++) {
        const firewall_rule_t* r = firewall_get_rule(i);
        if (!r) continue;
        cli_num(i + 1);
        cli_p(r->enabled ? "  " : " !");
        cli_p(firewall_action_name(r->action));
        cli_p(" ");
        cli_p(firewall_protocol_name(r->protocol));
        cli_p(" ");
        cli_p(firewall_direction_name(r->direction));
        cli_p(" ");
        if (r->has_source) {
            cli_ip(r->source);
            cli_p("/");
            cli_num(r->source_prefix);
        } else cli_p("any");
        cli_p(" -> ");
        if (r->has_destination) cli_ip(r->destination);
        else cli_p("any");
        cli_p(" dport ");
        if (r->port) {
            cli_num(r->port);
            if (r->port_end != r->port) { cli_p("-"); cli_num(r->port_end); }
        } else cli_p("any");
        if (r->source_port) { cli_p(" sport "); cli_num(r->source_port); }
        if (r->state == FIREWALL_STATE_NEW) cli_p(" state=new");
        else if (r->state == FIREWALL_STATE_ESTABLISHED) cli_p(" state=established");
        if (r->icmp_type) { cli_p(" icmp="); cli_num(r->icmp_type); }
        if (r->rate_limit) { cli_p(" rate="); cli_num(r->rate_limit); }
        if (r->log) cli_p(" log");
        cli_p(" hits=");
        cli_num(r->hits);
        if (r->comment[0]) { cli_p(" ["); cli_p(r->comment); cli_p("]"); }
        cli_p("\n");
    }
    cli_p("services: ");
    int any = 0;
    for (uint32_t i = 0; i < FIREWALL_SERVICE_LIMIT; i++) {
        uint8_t protocol;
        uint16_t port;
        if (firewall_service_get(i, &protocol, &port) != 0) continue;
        if (any) cli_p(", ");
        cli_p(firewall_protocol_name(protocol));
        cli_p("/");
        cli_num(port);
        any = 1;
    }
    if (!any) cli_p("none");
    cli_p("\ncounters: in ");
    cli_num(firewall_counter(FIREWALL_DIR_IN, FIREWALL_ACTION_ALLOW));
    cli_p(" allow / ");
    cli_num(firewall_counter(FIREWALL_DIR_IN, FIREWALL_ACTION_DENY));
    cli_p(" drop, out ");
    cli_num(firewall_counter(FIREWALL_DIR_OUT, FIREWALL_ACTION_ALLOW));
    cli_p("/");
    cli_num(firewall_counter(FIREWALL_DIR_OUT, FIREWALL_ACTION_DENY));
    cli_p(", forward ");
    cli_num(firewall_counter(FIREWALL_DIR_FORWARD, FIREWALL_ACTION_ALLOW));
    cli_p("/");
    cli_num(firewall_counter(FIREWALL_DIR_FORWARD, FIREWALL_ACTION_DENY));
    cli_p("\n");
    if (verbose) {
        cli_p("recent drops (newest first):\n");
        uint32_t count = firewall_log_count();
        if (!count) cli_p("  none\n");
        for (uint32_t i = 0; i < count && i < 8; i++) {
            cli_p("  ");
            cli_p(firewall_log_entry(i));
            cli_p("\n");
        }
    }
}

static void cli_rtr_status(void) {
    const router_config_t* c = router_get_config();
    const router_stats_t* s = router_get_stats();
    cli_p("SharkOS Router: ");
    cli_p(c->running ? "running" : "stopped");
    cli_p("  web console: ");
    cli_p(c->http_running ? "up on port " : "down (port ");
    cli_num(c->http_port);
    cli_p(c->http_running ? "\n" : ")\n");
    cli_p("LAN  ");
    cli_ip(c->ip);
    cli_p("/");
    cli_ip(c->mask);
    cli_p("  DHCP pool ");
    cli_ip(c->pool_start);
    cli_p(" - ");
    cli_ip(c->pool_end);
    cli_p("  lease ");
    cli_num(c->lease_seconds);
    cli_p("s  leases ");
    cli_num(router_lease_count());
    cli_p("\nWAN  mode=");
    cli_p(c->wan_mode == ROUTER_WAN_DHCP ? "dhcp" : c->wan_mode == ROUTER_WAN_STATIC ? "static" : "disabled");
    cli_p(" ");
    cli_ip(c->wan_ip);
    cli_p(" gw ");
    cli_ip(c->wan_gw);
    cli_p(" dns ");
    cli_ip(c->wan_dns);
    cli_p("  link=");
    cli_p(net_has_link ? "up" : "down");
    cli_p(" dhcp=");
    cli_p(net_dhcp_status());
    cli_p("\nDNS  upstream ");
    cli_ip(c->dns_upstream);
    cli_p(" secondary ");
    cli_ip(c->dns_secondary);
    cli_p(" domain .");
    cli_p(c->domain);
    cli_p(" records ");
    cli_num(router_dns_cache_count());
    cli_p("\nservices dhcp=");
    cli_p(c->dhcp_enabled ? "on" : "off");
    cli_p(" dns=");
    cli_p(c->dns_enabled ? "on" : "off");
    cli_p(" nat=");
    cli_p(c->nat_enabled ? "on" : "off");
    cli_p("\n");
    cli_p("stats dhcp ");
    cli_num(s->dhcp_discover);
    cli_p(" discover / ");
    cli_num(s->dhcp_ack);
    cli_p(" ack / ");
    cli_num(s->dhcp_nak);
    cli_p(" nak, dns ");
    cli_num(s->dns_queries);
    cli_p(" queries / ");
    cli_num(s->dns_forwarded);
    cli_p(" forwarded, nat ");
    cli_num(s->nat_created);
    cli_p(" mappings / ");
    cli_num(s->forwarded_frames);
    cli_p(" frames, http ");
    cli_num(s->http_requests);
    cli_p(" requests\n");
}

static void cli_rtr_stats(void) {
    const router_stats_t* s = router_get_stats();
    cli_p("router counters\n");
    cli_p("  dhcp discover  "); cli_num(s->dhcp_discover); cli_p("\n");
    cli_p("  dhcp offer     "); cli_num(s->dhcp_offer); cli_p("\n");
    cli_p("  dhcp request   "); cli_num(s->dhcp_request); cli_p("\n");
    cli_p("  dhcp ack       "); cli_num(s->dhcp_ack); cli_p("\n");
    cli_p("  dhcp nak       "); cli_num(s->dhcp_nak); cli_p("\n");
    cli_p("  dhcp release   "); cli_num(s->dhcp_release); cli_p("\n");
    cli_p("  dns queries    "); cli_num(s->dns_queries); cli_p("\n");
    cli_p("  dns local      "); cli_num(s->dns_local); cli_p("\n");
    cli_p("  dns forwarded  "); cli_num(s->dns_forwarded); cli_p("\n");
    cli_p("  dns cached     "); cli_num(s->dns_cached); cli_p("\n");
    cli_p("  dns failed     "); cli_num(s->dns_failed); cli_p("\n");
    cli_p("  nat created    "); cli_num(s->nat_created); cli_p("\n");
    cli_p("  nat hits       "); cli_num(s->nat_hits); cli_p("\n");
    cli_p("  forward hits   "); cli_num(s->forward_hits); cli_p("\n");
    cli_p("  frames fwd     "); cli_num(s->forwarded_frames); cli_p("\n");
    cli_p("  frames dropped "); cli_num(s->dropped_frames); cli_p("\n");
    cli_p("  http requests  "); cli_num(s->http_requests); cli_p("\n");
}

/* ------------------------------------------------------------------ */
/* config persistence                                                 */
/* ------------------------------------------------------------------ */

static int cli_save(const char* path) {
    const router_config_t* c = router_get_config();
    char buffer[CLI_CONFIG_MAX];
    char* saved_out = cli_out;
    uint32_t saved_cap = cli_cap, saved_used = cli_used;
    cli_out = buffer;
    cli_cap = sizeof(buffer);
    cli_used = 0;
    buffer[0] = 0;
    cli_p("# SharkOS Router config - generated by 'rtr save'\n");
    cli_p("lan "); cli_ip(c->ip); cli_p(" "); cli_ip(c->mask); cli_p("\n");
    cli_p("pool "); cli_ip(c->pool_start); cli_p(" "); cli_ip(c->pool_end); cli_p("\n");
    cli_p("lease "); cli_num(c->lease_seconds); cli_p("\n");
    cli_p("domain "); cli_p(c->domain); cli_p("\n");
    cli_p("dns "); cli_ip(c->dns_upstream); cli_p(" "); cli_ip(c->dns_secondary); cli_p("\n");
    cli_p("http "); cli_num(c->http_port); cli_p("\n");
    cli_p("token "); cli_p(c->token); cli_p("\n");
    cli_p("wan ");
    cli_p(c->wan_mode == ROUTER_WAN_DHCP ? "dhcp " : c->wan_mode == ROUTER_WAN_STATIC ? "static " : "down ");
    cli_ip(c->wan_ip); cli_p(" "); cli_ip(c->wan_mask); cli_p(" "); cli_ip(c->wan_gw);
    cli_p(" "); cli_ip(c->wan_dns); cli_p("\n");
    cli_p("ports "); cli_num(c->wan_port); cli_p(" "); cli_num(c->lan_port);
    cli_p(" "); cli_num(c->lan_shared); cli_p("\n");
    for (uint32_t i = 0; i < ROUTER_RESERVATION_LIMIT; i++) {
        const router_reservation_t* r = router_get_reservation(i);
        if (!r) continue;
        cli_p("reserve "); cli_mac(r->mac); cli_p(" "); cli_ip(r->ip); cli_p(" ");
        cli_p(r->hostname[0] ? r->hostname : "-"); cli_p("\n");
    }
    for (uint32_t i = 0; i < ROUTER_DNS_CACHE_LIMIT; i++) {
        const router_dns_entry_t* e = router_get_dns_entry(i);
        if (!e || !e->local) continue;
        cli_p("record "); cli_p(e->name); cli_p(" "); cli_ip(e->ip); cli_p("\n");
    }
    for (uint32_t i = 0; i < ROUTER_FORWARD_LIMIT; i++) {
        const router_forward_t* f = router_get_forward(i);
        if (!f) continue;
        cli_p("forward ");
        cli_p(f->protocol == ROUTER_PROTO_TCP ? "tcp " : "udp ");
        cli_num(f->wan_port); cli_p(" "); cli_ip(f->lan_ip); cli_p(" "); cli_num(f->lan_port); cli_p("\n");
    }
    for (uint32_t d = FIREWALL_DIR_IN; d <= FIREWALL_DIR_FORWARD; d++) {
        cli_p("policy "); cli_p(firewall_direction_name(d)); cli_p(" ");
        cli_p(firewall_action_name(firewall_get_policy(d))); cli_p("\n");
    }
    uint32_t length = cli_used;
    cli_out = saved_out; cli_cap = saved_cap; cli_used = saved_used;
    if (!length || length >= sizeof(buffer)) return -1;
    struct fs_node* node = fs_resolve_path(path);
    if (!node) {
        struct fs_node* parent = fs_resolve_path("/System/Config");
        if (!parent) parent = root;
        node = create_node("router.conf", FS_FILE, parent);
    }
    if (!node || node->type != FS_FILE) return -1;
    if (fs_node_truncate(node, 0) != 0) return -1;
    if (fs_node_write(node, 0, buffer, length) != 0) return -1;
    fs_node_sync(node);
    cli_p("saved ");
    cli_num(length);
    cli_p(" bytes to ");
    cli_p(path);
    cli_p("\n");
    return 0;
}

static int cli_load(const char* path) {
    struct fs_node* node = fs_resolve_path(path);
    if (!node || node->type != FS_FILE || node->content_len <= 0) return -1;
    static char buffer[CLI_CONFIG_MAX];
    uint32_t length = (uint32_t)node->content_len;
    if (length >= sizeof(buffer)) length = sizeof(buffer) - 1;
    memcpy(buffer, fs_node_data(node), length);
    buffer[length] = 0;
    router_clear_static_config();
    char* line = buffer;
    uint32_t count = 0;
    while (*line) {
        char* newline = strchr(line, '\n');
        if (newline) *newline = 0;
        char* p = line;
        char* key = cli_next(&p);
        if (key && key[0] != '#') {
            uint8_t a[4], b[4], c[4], d[4];
            char* s1;
            char* s2;
            count++;
            if (!strcmp(key, "lan") && cli_parse_ip(s1 = cli_next(&p), a) && cli_parse_ip(cli_next(&p), b)) {
                router_lan_set(a, b);
            } else if (!strcmp(key, "pool") && cli_parse_ip(s1 = cli_next(&p), a) && cli_parse_ip(cli_next(&p), b)) {
                router_dhcp_pool(a, b);
            } else if (!strcmp(key, "lease")) {
                router_dhcp_set_lease(cli_u32(cli_next(&p)));
            } else if (!strcmp(key, "domain")) {
                router_dns_set_domain(cli_next(&p));
            } else if (!strcmp(key, "dns")) {
                s1 = cli_next(&p);
                s2 = cli_next(&p);
                if (cli_parse_ip(s1, a)) {
                    if (cli_parse_ip(s2, b)) router_dns_set_upstream(a, b);
                    else router_dns_set_upstream(a, 0);
                }
            } else if (!strcmp(key, "http")) {
                router_set_http_port((uint16_t)cli_u32(cli_next(&p)));
            } else if (!strcmp(key, "token")) {
                router_set_token(cli_next(&p));
            } else if (!strcmp(key, "wan")) {
                char* mode = cli_next(&p);
                if (mode && !strcmp(mode, "static") && cli_parse_ip(cli_next(&p), a) &&
                    cli_parse_ip(cli_next(&p), b) && cli_parse_ip(cli_next(&p), c) &&
                    cli_parse_ip(cli_next(&p), d)) {
                    router_wan_set_static(a, b, c, d);
                } else if (mode && !strcmp(mode, "dhcp")) {
                    router_wan_set_dhcp();
                } else if (mode && !strcmp(mode, "down")) {
                    router_wan_set_down();
                }
            } else if (!strcmp(key, "ports")) {
                uint32_t wan = cli_u32(cli_next(&p));
                uint32_t lan = cli_u32(cli_next(&p));
                uint32_t shared = cli_u32(cli_next(&p));
                if (router_set_ports((int)wan, (int)lan) == 0 && shared)
                    router_set_ports((int)wan, (int)wan);
            } else if (!strcmp(key, "reserve")) {
                uint8_t mac[6];
                char* mac_text = cli_next(&p);
                char* ip_text = cli_next(&p);
                char* host = cli_next(&p);
                if (router_parse_mac(mac_text, mac) && cli_parse_ip(ip_text, a))
                    router_dhcp_reserve(mac, a, host && strcmp(host, "-") != 0 ? host : 0);
            } else if (!strcmp(key, "record")) {
                char* name = cli_next(&p);
                char* ip_text = cli_next(&p);
                if (name && cli_parse_ip(ip_text, a)) router_dns_add_record(name, a, 1);
            } else if (!strcmp(key, "forward")) {
                char* proto = cli_next(&p);
                uint16_t wan_port = (uint16_t)cli_u32(cli_next(&p));
                char* ip_text = cli_next(&p);
                uint16_t lan_port = (uint16_t)cli_u32(cli_next(&p));
                uint8_t protocol = proto && !strcmp(proto, "tcp") ? ROUTER_PROTO_TCP : ROUTER_PROTO_UDP;
                if (cli_parse_ip(ip_text, a)) router_forward_add(protocol, wan_port, a, lan_port, 0);
            } else if (!strcmp(key, "policy")) {
                int direction = cli_dir(cli_next(&p));
                int action = cli_action(cli_next(&p));
                if (direction >= 0 && action >= 0)
                    firewall_set_policy((uint8_t)direction, (uint8_t)action);
            }
        }
        if (!newline) break;
        line = newline + 1;
    }
    cli_p("loaded ");
    cli_num(count);
    cli_p(" settings from ");
    cli_p(path);
    cli_p("\n");
    return 0;
}

/* ------------------------------------------------------------------ */
/* rtr command tree                                                    */
/* ------------------------------------------------------------------ */

static void cli_rtr_dhcp(char** cursor) {
    char* action = cli_next(cursor);
    if (!action || !strcmp(action, "leases")) {
        cli_p("DHCP leases (bound/offered):\n");
        int any = 0;
        for (uint32_t i = 0; i < ROUTER_LEASE_LIMIT; i++) {
            const router_lease_t* l = router_get_lease(i);
            if (!l) continue;
            any = 1;
            cli_p("  "); cli_ip(l->ip); cli_p("  "); cli_mac(l->mac);
            cli_p("  "); cli_p(l->bound ? "bound" : "offered");
            cli_p("  "); cli_p(l->hostname[0] ? l->hostname : "-");
            uint32_t left = (int32_t)(l->expires - uptime_ticks) > 0 ?
                            (l->expires - uptime_ticks) / TICKS_PER_SEC : 0;
            cli_p("  "); cli_num(left); cli_p("s\n");
        }
        if (!any) cli_p("  none\n");
        return;
    }
    if (!strcmp(action, "pool")) {
        uint8_t a[4], b[4];
        char* s = cli_next(cursor);
        char* e = cli_next(cursor);
        if (!cli_parse_ip(s, a) || !cli_parse_ip(e, b) || router_dhcp_pool(a, b) != 0)
            cli_p("usage: rtr dhcp pool <start> <end> (both must be inside the LAN subnet)\n");
        else cli_p("DHCP pool updated\n");
        return;
    }
    if (!strcmp(action, "lease")) {
        uint32_t seconds = cli_u32(cli_next(cursor));
        if (router_dhcp_set_lease(seconds) != 0) cli_p("usage: rtr dhcp lease <60-604800>\n");
        else cli_p("DHCP lease time updated\n");
        return;
    }
    if (!strcmp(action, "enable") || !strcmp(action, "disable")) {
        int on = !strcmp(action, "enable");
        if (router_dhcp_set_enabled(on) != 0) cli_p("start the router first: rtr start\n");
        else { cli_p("DHCP server "); cli_p(on ? "enabled\n" : "disabled\n"); }
        return;
    }
    if (!strcmp(action, "reserve")) {
        uint8_t mac[6], ip[4];
        char* mac_text = cli_next(cursor);
        char* ip_text = cli_next(cursor);
        char* host = cli_next(cursor);
        if (!router_parse_mac(mac_text, mac) || !cli_parse_ip(ip_text, ip) ||
            router_dhcp_reserve(mac, ip, host) != 0)
            cli_p("usage: rtr dhcp reserve <aa:bb:cc:dd:ee:ff> <ip-in-lan> [hostname]\n");
        else cli_p("reservation added\n");
        return;
    }
    if (!strcmp(action, "unreserve")) {
        uint32_t id = cli_u32(cli_next(cursor));
        if (router_dhcp_remove_reservation(id) != 0) cli_p("invalid reservation id\n");
        else cli_p("reservation removed\n");
        return;
    }
    if (!strcmp(action, "reservations")) {
        cli_p("DHCP reservations:\n");
        int any = 0;
        for (uint32_t i = 0; i < ROUTER_RESERVATION_LIMIT; i++) {
            const router_reservation_t* r = router_get_reservation(i);
            if (!r) continue;
            any = 1;
            cli_p("  "); cli_num(i + 1); cli_p("  "); cli_mac(r->mac);
            cli_p("  "); cli_ip(r->ip); cli_p("  ");
            cli_p(r->hostname[0] ? r->hostname : "-"); cli_p("\n");
        }
        if (!any) cli_p("  none\n");
        return;
    }
    if (!strcmp(action, "release")) {
        uint8_t ip[4];
        char* text = cli_next(cursor);
        if (!cli_parse_ip(text, ip) || router_dhcp_release(ip) != 0) cli_p("no active lease for that address\n");
        else cli_p("lease released\n");
        return;
    }
    cli_p("usage: rtr dhcp pool|lease|enable|disable|leases|reservations|reserve|unreserve|release\n");
}

static void cli_rtr_dns(char** cursor) {
    char* action = cli_next(cursor);
    if (!action || !strcmp(action, "records")) {
        cli_p("DNS records and cache:\n");
        int any = 0;
        for (uint32_t i = 0; i < ROUTER_DNS_CACHE_LIMIT; i++) {
            const router_dns_entry_t* e = router_get_dns_entry(i);
            if (!e) continue;
            any = 1;
            cli_p("  "); cli_p(e->name); cli_p("  "); cli_ip(e->ip);
            cli_p(e->local ? "  static\n" : "  cached\n");
        }
        if (!any) cli_p("  none\n");
        return;
    }
    if (!strcmp(action, "enable") || !strcmp(action, "disable")) {
        int on = !strcmp(action, "enable");
        if (router_dns_set_enabled(on) != 0) cli_p("start the router first: rtr start\n");
        else { cli_p("DNS server "); cli_p(on ? "enabled\n" : "disabled\n"); }
        return;
    }
    if (!strcmp(action, "upstream")) {
        uint8_t a[4], b[4];
        char* s1 = cli_next(cursor);
        char* s2 = cli_next(cursor);
        if (!cli_parse_ip(s1, a)) { cli_p("usage: rtr dns upstream <ip> [secondary-ip]\n"); return; }
        if (s2 && cli_parse_ip(s2, b)) router_dns_set_upstream(a, b);
        else router_dns_set_upstream(a, 0);
        cli_p("DNS upstream updated\n");
        return;
    }
    if (!strcmp(action, "domain")) {
        char* name = cli_next(cursor);
        if (router_dns_set_domain(name) != 0) cli_p("usage: rtr dns domain <name>\n");
        else cli_p("local domain updated\n");
        return;
    }
    if (!strcmp(action, "add")) {
        uint8_t ip[4];
        char* name = cli_next(cursor);
        char* ip_text = cli_next(cursor);
        if (!name || !cli_parse_ip(ip_text, ip) || router_dns_add_record(name, ip, 1) != 0)
            cli_p("usage: rtr dns add <name> <ip>\n");
        else cli_p("DNS record added\n");
        return;
    }
    if (!strcmp(action, "del") || !strcmp(action, "remove")) {
        char* name = cli_next(cursor);
        if (router_dns_remove_record(name) != 0) cli_p("no such record\n");
        else cli_p("DNS record removed\n");
        return;
    }
    if (!strcmp(action, "flush")) {
        router_dns_flush_cache();
        cli_p("DNS cache flushed (static records kept)\n");
        return;
    }
    cli_p("usage: rtr dns enable|disable|upstream|domain|add|del|records|flush\n");
}

static void cli_rtr_wan(char** cursor) {
    char* action = cli_next(cursor);
    if (!action || !strcmp(action, "status")) {
        cli_rtr_status();
        return;
    }
    if (!strcmp(action, "dhcp") || !strcmp(action, "renew")) {
        router_wan_set_dhcp();
        router_wan_refresh();
        cli_p("WAN set to upstream DHCP client; discovering\n");
        return;
    }
    if (!strcmp(action, "static")) {
        uint8_t ip[4], mask[4], gw[4], dns[4];
        char* s1 = cli_next(cursor);
        char* s2 = cli_next(cursor);
        char* s3 = cli_next(cursor);
        char* s4 = cli_next(cursor);
        if (!cli_parse_ip(s1, ip) || !cli_parse_ip(s2, mask) || !cli_parse_ip(s3, gw)) {
            cli_p("usage: rtr wan static <ip> <mask> <gw> [dns]\n");
            return;
        }
        if (s4 && cli_parse_ip(s4, dns)) router_wan_set_static(ip, mask, gw, dns);
        else router_wan_set_static(ip, mask, gw, 0);
        cli_p("static WAN configuration applied\n");
        return;
    }
    if (!strcmp(action, "down")) {
        router_wan_set_down();
        cli_p("WAN administratively down (LAN services keep running)\n");
        return;
    }
    cli_p("usage: rtr wan status|dhcp|renew|static <ip> <mask> <gw> [dns]|down\n");
}

static void cli_rtr_forward(char** cursor) {
    char* action = cli_next(cursor);
    if (!action || !strcmp(action, "list")) {
        cli_p("port forwards:\n");
        int any = 0;
        for (uint32_t i = 0; i < ROUTER_FORWARD_LIMIT; i++) {
            const router_forward_t* f = router_get_forward(i);
            if (!f) continue;
            any = 1;
            cli_p("  "); cli_num(i + 1); cli_p("  ");
            cli_p(f->protocol == ROUTER_PROTO_TCP ? "tcp" : "udp");
            cli_p("  "); cli_num(f->wan_port); cli_p(" -> ");
            cli_ip(f->lan_ip); cli_p(":"); cli_num(f->lan_port);
            cli_p("  hits="); cli_num(f->hits);
            if (f->comment[0]) { cli_p("  "); cli_p(f->comment); }
            cli_p("\n");
        }
        if (!any) cli_p("  none\n");
        return;
    }
    if (!strcmp(action, "add")) {
        char* proto = cli_next(cursor);
        uint16_t wan_port = (uint16_t)cli_u32(cli_next(cursor));
        char* ip_text = cli_next(cursor);
        uint16_t lan_port = (uint16_t)cli_u32(cli_next(cursor));
        char* comment = cli_next(cursor);
        uint8_t ip[4];
        uint8_t protocol = proto && !strcmp(proto, "tcp") ? ROUTER_PROTO_TCP :
                           proto && !strcmp(proto, "udp") ? ROUTER_PROTO_UDP : 0;
        if (!protocol || !wan_port || !lan_port || !cli_parse_ip(ip_text, ip) ||
            router_forward_add(protocol, wan_port, ip, lan_port, comment) != 0)
            cli_p("usage: rtr forward add <tcp|udp> <wanport> <lanip> <lanport> [comment]\n");
        else cli_p("port forward added\n");
        return;
    }
    if (!strcmp(action, "del") || !strcmp(action, "remove")) {
        uint32_t id = cli_u32(cli_next(cursor));
        if (router_forward_remove(id) != 0) cli_p("invalid forward id\n");
        else cli_p("port forward removed\n");
        return;
    }
    cli_p("usage: rtr forward add|del|list\n");
}

static void cli_rtr_ports(void) {
    const router_config_t* c = router_get_config();
    uint32_t count = router_port_count();
    cli_p("interfaces: "); cli_num(count);
    cli_p(c->lan_shared ? " detected (shared single-NIC mode)\n" : " detected (dedicated uplink/LAN)\n");
    for (uint32_t i = 0; i < count; i++) {
        net_port_t port;
        if (router_port_get(i, &port) != 0) continue;
        uint32_t role = router_port_role(i);
        cli_p("  ["); cli_num(i); cli_p("] ");
        cli_p(role == 1 ? "WAN " : role == 2 ? "LAN " : "--- ");
        cli_p(port.ifname); cli_p("  "); cli_p(port.driver); cli_p("  ");
        char mac[20];
        router_format_mac(mac, sizeof(mac), port.mac);
        cli_p(port.mac[0] || port.mac[1] || port.mac[2] || port.mac[3] ||
              port.mac[4] || port.mac[5] ? mac : "mac unknown");
        cli_p(port.active ? "  driver active" : "  detected");
        cli_p(port.link ? "  link up\n" : "  link unknown\n");
    }
    cli_p(c->lan_shared ? "one adapter: LAN shares the uplink (WAN and LAN on the same wire)\n"
                        : "uplink adapter = WAN, second adapter assigned as LAN port\n");
}

static void cli_rtr(char** cursor) {
    char* action = cli_next(cursor);
    if (!action || !strcmp(action, "status") || !strcmp(action, "help") || !strcmp(action, "docs")) {
        if (action && (!strcmp(action, "help") || !strcmp(action, "docs"))) { cli_rtr_help(); return; }
        cli_rtr_status();
        return;
    }
    if (!strcmp(action, "start")) {
        char* port_text = cli_next(cursor);
        uint16_t port = port_text ? (uint16_t)cli_u32(port_text) : ROUTER_DEFAULT_HTTP_PORT;
        int result = router_start(port);
        if (result == 0) {
            cli_p("router started; management console at http://");
            cli_ip(router_get_config()->ip);
            cli_p(":");
            cli_num(port);
            cli_p("/  (DHCP, DNS, WAN, NAT enabled)\n");
        } else if (result == -2) cli_p("router services started but the HTTP port is unavailable\n");
        else cli_p("no network adapter detected\n");
        return;
    }
    if (!strcmp(action, "stop")) {
        router_stop();
        cli_p("router stopped\n");
        return;
    }
    if (!strcmp(action, "stats")) { cli_rtr_stats(); return; }
    if (!strcmp(action, "lan")) {
        uint8_t ip[4], mask[4];
        char* s1 = cli_next(cursor);
        char* s2 = cli_next(cursor);
        if (!cli_parse_ip(s1, ip) || !cli_parse_ip(s2, mask) || router_lan_set(ip, mask) != 0)
            cli_p("usage: rtr lan <ip> <mask>\n");
        else cli_p("LAN address updated\n");
        return;
    }
    if (!strcmp(action, "dhcp")) { cli_rtr_dhcp(cursor); return; }
    if (!strcmp(action, "dns")) { cli_rtr_dns(cursor); return; }
    if (!strcmp(action, "wan")) { cli_rtr_wan(cursor); return; }
    if (!strcmp(action, "forward")) { cli_rtr_forward(cursor); return; }
    if (!strcmp(action, "nat")) {
        char* value = cli_next(cursor);
        if (!value || !strcmp(value, "status")) { cli_rtr_status(); return; }
        if (!strcmp(value, "on") || !strcmp(value, "off")) {
            router_nat_set_enabled(!strcmp(value, "on"));
            cli_p("NAT "); cli_p(!strcmp(value, "on") ? "enabled\n" : "disabled\n");
            return;
        }
        if (!strcmp(value, "table")) {
            cli_p("active NAT mappings:\n");
            int any = 0;
            for (uint32_t i = 0; i < ROUTER_NAT_LIMIT; i++) {
                const router_nat_entry_t* n = router_get_nat(i);
                if (!n) continue;
                any = 1;
                cli_p("  "); cli_ip(n->lan_ip); cli_p(":"); cli_num(n->lan_port);
                cli_p(" -> "); cli_num(n->wan_port); cli_p("  remote "); cli_ip(n->remote_ip);
                cli_p("  packets="); cli_num(n->packets); cli_p("\n");
            }
            if (!any) cli_p("  none\n");
            return;
        }
        cli_p("usage: rtr nat on|off|status|table\n");
        return;
    }
    if (!strcmp(action, "http")) {
        char* sub = cli_next(cursor);
        if (!sub || !strcmp(sub, "port")) {
            uint16_t port = (uint16_t)cli_u32(cli_next(cursor));
            if (!port || router_set_http_port(port) != 0) cli_p("usage: rtr http port <1-65535>\n");
            else { cli_p("management console moved to port "); cli_num(port); cli_p("\n"); }
        } else cli_p("usage: rtr http port <port>\n");
        return;
    }
    if (!strcmp(action, "token")) {
        char* value = cli_next(cursor);
        if (router_set_token(value) != 0) cli_p("usage: rtr token <1-31 printable characters>\n");
        else cli_p("API token updated\n");
        return;
    }
    if (!strcmp(action, "profile")) {
        char* value = cli_next(cursor);
        if (value && !strcmp(value, "router")) {
            if (router_firewall_profile() == 0) cli_p("router firewall profile applied\n");
            else cli_p("could not apply profile\n");
        } else cli_p("usage: rtr profile router\n");
        return;
    }
    if (!strcmp(action, "ports") || !strcmp(action, "ifconfig")) {
        cli_rtr_ports();
        return;
    }
    if (!strcmp(action, "port")) {
        char* which = cli_next(cursor);
        char* index_text = cli_next(cursor);
        if (which && !strcmp(which, "auto")) {
            net_port_scan();
            int count = router_port_assign();
            cli_p("auto-assigned "); cli_num((uint32_t)count); cli_p(" interface(s)\n");
            return;
        }
        int index = index_text ? (int)cli_u32(index_text) : -1;
        int ok;
        if (which && !strcmp(which, "wan")) ok = router_set_ports(index, router_get_config()->lan_shared ? index : router_get_config()->lan_port);
        else if (which && !strcmp(which, "lan")) ok = router_set_ports(router_get_config()->wan_port, index);
        else ok = -1;
        if (ok != 0) cli_p("usage: rtr port wan|lan <index>  (see 'rtr ports')\n");
        else { cli_p("interface assignment updated\n"); cli_rtr_ports(); }
        return;
    }
    if (!strcmp(action, "save")) {
        char* path = cli_next(cursor);
        if (cli_save(path ? path : CLI_CONFIG_PATH) != 0) cli_p("could not save config\n");
        return;
    }
    if (!strcmp(action, "load")) {
        char* path = cli_next(cursor);
        if (cli_load(path ? path : CLI_CONFIG_PATH) != 0) cli_p("no saved config found\n");
        return;
    }
    cli_p("unknown rtr command; try 'rtr help'\n");
}

/* ------------------------------------------------------------------ */
/* fw command tree                                                     */
/* ------------------------------------------------------------------ */

static int cli_fw_add_extended(char** cursor, int action) {
    firewall_rule_t rule;
    memset(&rule, 0, sizeof(rule));
    rule.action = (uint8_t)action;
    rule.enabled = 1;
    rule.direction = FIREWALL_DIR_ANY;
    rule.state = FIREWALL_STATE_ANY;
    rule.source_prefix = 32;
    rule.destination_prefix = 32;
    rule.port_end = 0xFFFFu;
    rule.source_port_end = 0xFFFFu;
    char* proto_text = cli_next(cursor);
    int protocol = cli_proto(proto_text);
    if (protocol < 0) { cli_p("protocol must be tcp, udp, icmp or any\n"); return -1; }
    rule.protocol = (uint8_t)protocol;
    char* token;
    while ((token = cli_next(cursor)) != 0) {
        if (!strcmp(token, "dir")) {
            int value = cli_dir(cli_next(cursor));
            if (value < 0) { cli_p("dir must be in, out, forward or any\n"); return -1; }
            rule.direction = (uint8_t)value;
        } else if (!strcmp(token, "from")) {
            char* text = cli_next(cursor);
            uint8_t prefix;
            if (!router_parse_cidr(text, rule.source, &prefix)) { cli_p("invalid source CIDR\n"); return -1; }
            rule.has_source = 1;
            rule.source_prefix = prefix;
        } else if (!strcmp(token, "to")) {
            char* text = cli_next(cursor);
            uint8_t prefix;
            if (!router_parse_cidr(text, rule.destination, &prefix)) { cli_p("invalid destination CIDR\n"); return -1; }
            rule.has_destination = 1;
            rule.destination_prefix = prefix;
        } else if (!strcmp(token, "dport")) {
            uint16_t start, end;
            if (!cli_port_range(cli_next(cursor), &start, &end)) { cli_p("invalid dport range\n"); return -1; }
            rule.port = start;
            rule.port_end = end;
        } else if (!strcmp(token, "sport")) {
            uint16_t start, end;
            if (!cli_port_range(cli_next(cursor), &start, &end)) { cli_p("invalid sport range\n"); return -1; }
            rule.source_port = start;
            rule.source_port_end = end;
        } else if (!strcmp(token, "state")) {
            int value = cli_state(cli_next(cursor));
            if (value < 0) { cli_p("state must be new, established or any\n"); return -1; }
            rule.state = (uint8_t)value;
        } else if (!strcmp(token, "icmp")) {
            rule.icmp_type = (uint8_t)cli_u32(cli_next(cursor));
        } else if (!strcmp(token, "rate")) {
            uint32_t value = cli_u32(cli_next(cursor));
            if (value > FIREWALL_RATE_MAX) { cli_p("rate must be 0-1000\n"); return -1; }
            rule.rate_limit = (uint16_t)value;
        } else if (!strcmp(token, "log")) {
            rule.log = 1;
        } else if (!strcmp(token, "comment")) {
            char* text = cli_next(cursor);
            if (text) {
                uint32_t i = 0;
                while (text[i] && i < FIREWALL_COMMENT_SIZE - 1) { rule.comment[i] = text[i]; i++; }
                rule.comment[i] = 0;
            }
        } else {
            cli_p("unknown rule option: ");
            cli_p(token);
            cli_p("\n");
            return -1;
        }
    }
    int id = firewall_rule_add(&rule);
    if (id < 0) cli_p("rule table full or invalid rule\n");
    else { cli_p("added firewall rule "); cli_num((uint32_t)id); cli_p("\n"); }
    return id;
}

static void cli_fw_legacy(char** cursor, int action) {
    char* proto_text = cli_next(cursor);
    int protocol = cli_proto(proto_text);
    if (protocol < 0) {
        cli_p("usage: fw allow|deny <tcp|udp|icmp|any> <port|any> [from <ip>]\n");
        return;
    }
    char* port_text = cli_next(cursor);
    char* from = 0;
    uint16_t port = 0;
    if (port_text && !strcmp(port_text, "from")) { from = port_text; port_text = 0; }
    if (protocol == FIREWALL_PROTOCOL_ICMP) {
        if (port_text && strcmp(port_text, "any") != 0) {
            cli_p("ICMP rules are protocol-wide; omit the port or use any\n");
            return;
        }
    } else {
        if (!port_text) { cli_p("port is required; use any for all ports\n"); return; }
        if (strcmp(port_text, "any") != 0) {
            uint32_t value = cli_u32(port_text);
            if (!value || value > 65535) { cli_p("port must be any or 1-65535\n"); return; }
            port = (uint16_t)value;
        }
    }
    if (!from) from = cli_next(cursor);
    uint8_t source[4];
    const uint8_t* source_ptr = 0;
    if (from) {
        char* ip_text = cli_next(cursor);
        if (strcmp(from, "from") != 0 || !cli_parse_ip(ip_text, source)) {
            cli_p("source filter syntax: from <ip>\n");
            return;
        }
        if (cli_next(cursor)) { cli_p("too many arguments\n"); return; }
        source_ptr = source;
    }
    int id = firewall_add_rule((uint8_t)action, (uint8_t)protocol, port, source_ptr);
    if (id < 0) cli_p("rule table is full or invalid\n");
    else { cli_p("added firewall rule "); cli_num((uint32_t)id); cli_p("\n"); }
}

static void cli_fw(char** cursor) {
    char* action = cli_next(cursor);
    if (!action || !strcmp(action, "help") || !strcmp(action, "docs")) { cli_fw_help(); return; }
    if (!strcmp(action, "status") || !strcmp(action, "list")) {
        char* extra = cli_next(cursor);
        cli_fw_status(extra && !strcmp(extra, "-v"));
        return;
    }
    if (!strcmp(action, "policy")) {
        char* first = cli_next(cursor);
        char* second = cli_next(cursor);
        int direction = FIREWALL_DIR_IN;
        char* action_text = first;
        if (second) {
            direction = cli_dir(first);
            action_text = second;
        }
        int value = cli_action(action_text);
        if (direction < 0 || value < 0 || firewall_set_policy((uint8_t)direction, (uint8_t)value) != 0)
            cli_p("usage: fw policy [in|out|forward|any] allow|deny\n");
        else { cli_p("policy set to "); cli_p(firewall_action_name((uint8_t)value)); cli_p("\n"); }
        return;
    }
    if (!strcmp(action, "rate")) {
        char* value = cli_next(cursor);
        if (!value) { cli_p("usage: fw rate <1-1000|off>\n"); return; }
        uint32_t rate = !strcmp(value, "off") ? 0 : cli_u32(value);
        char* extra = cli_next(cursor);
        int valid = !strcmp(value, "off") ? 1 : (rate > 0 && rate <= FIREWALL_RATE_MAX);
        if (!valid || extra) { cli_p("usage: fw rate <1-1000|off>\n"); return; }
        if (firewall_set_rate_limit((uint16_t)rate) != 0) cli_p("invalid rate\n");
        else { cli_p("new-traffic rate limit "); if (rate) { cli_num(rate); cli_p(" pps\n"); } else cli_p("off\n"); }
        return;
    }
    if (!strcmp(action, "clear")) { firewall_clear_rules(); cli_p("firewall rules cleared\n"); return; }
    if (!strcmp(action, "delete") || !strcmp(action, "remove")) {
        uint32_t id = cli_u32(cli_next(cursor));
        if (firewall_delete_rule(id) != 0) cli_p("invalid rule id\n");
        else cli_p("rule deleted\n");
        return;
    }
    if (!strcmp(action, "enable") || !strcmp(action, "disable")) {
        uint32_t id = cli_u32(cli_next(cursor));
        if (firewall_rule_enable(id, !strcmp(action, "enable")) != 0) cli_p("invalid rule id\n");
        else { cli_p("rule "); cli_p(!strcmp(action, "enable") ? "enabled\n" : "disabled\n"); }
        return;
    }
    if (!strcmp(action, "move")) {
        uint32_t id = cli_u32(cli_next(cursor));
        char* direction = cli_next(cursor);
        int delta = direction && !strcmp(direction, "up") ? -1 :
                    direction && !strcmp(direction, "down") ? 1 : 0;
        if (!delta || firewall_rule_move(id, delta) != 0) cli_p("usage: fw move <id> up|down\n");
        else cli_p("rule moved\n");
        return;
    }
    if (!strcmp(action, "hits")) {
        uint32_t id = cli_u32(cli_next(cursor));
        cli_p("rule hits: "); cli_num(firewall_hit_count(id)); cli_p("\n");
        return;
    }
    if (!strcmp(action, "counters")) {
        char* extra = cli_next(cursor);
        if (extra && !strcmp(extra, "reset")) { firewall_reset_counters(); cli_p("counters reset\n"); return; }
        cli_fw_status(0);
        return;
    }
    if (!strcmp(action, "profile")) {
        char* name = cli_next(cursor);
        if (!name) { cli_p("usage: fw profile open|server|lockdown|router\n"); return; }
        if (!strcmp(name, "router")) {
            if (router_firewall_profile() == 0) cli_p("router profile applied\n");
            else cli_p("could not apply profile\n");
        } else if (!strcmp(name, "open")) {
            firewall_apply_preset(FIREWALL_PRESET_OPEN); cli_p("open profile applied\n");
        } else if (!strcmp(name, "server")) {
            firewall_apply_preset(FIREWALL_PRESET_SERVER); cli_p("server profile applied\n");
        } else if (!strcmp(name, "lockdown")) {
            firewall_apply_preset(FIREWALL_PRESET_LOCKDOWN); cli_p("lockdown profile applied\n");
        } else cli_p("usage: fw profile open|server|lockdown|router\n");
        return;
    }
    if (!strcmp(action, "service")) {
        char* sub = cli_next(cursor);
        if (sub && (!strcmp(sub, "list") || !strcmp(sub, "status"))) {
            cli_p("implicit service ports:\n");
            int any = 0;
            for (uint32_t i = 0; i < FIREWALL_SERVICE_LIMIT; i++) {
                uint8_t protocol;
                uint16_t port;
                if (firewall_service_get(i, &protocol, &port) != 0) continue;
                any = 1;
                cli_p("  "); cli_p(firewall_protocol_name(protocol)); cli_p("/"); cli_num(port); cli_p("\n");
            }
            if (!any) cli_p("  none\n");
            return;
        }
        char* proto_text = cli_next(cursor);
        uint16_t port = (uint16_t)cli_u32(cli_next(cursor));
        int protocol = cli_proto(proto_text);
        int valid_sub = sub && (!strcmp(sub, "add") || !strcmp(sub, "del") || !strcmp(sub, "remove"));
        if (!valid_sub || protocol < 0 || !port) {
            cli_p("usage: fw service add|del <tcp|udp> <port> | service list\n");
            return;
        }
        if (!strcmp(sub, "add")) {
            if (firewall_service_add((uint8_t)protocol, port) < 0) cli_p("service table full\n");
            else cli_p("service port added\n");
        } else {
            if (firewall_service_remove((uint8_t)protocol, port) != 0) cli_p("no such service\n");
            else cli_p("service port removed\n");
        }
        return;
    }
    if (!strcmp(action, "log")) {
        char* extra = cli_next(cursor);
        if (extra && !strcmp(extra, "clear")) { firewall_log_clear(); cli_p("log cleared\n"); return; }
        if (extra && !strcmp(extra, "on")) { firewall_set_log_drops(1); cli_p("drop logging on\n"); return; }
        if (extra && !strcmp(extra, "off")) { firewall_set_log_drops(0); cli_p("drop logging off\n"); return; }
        uint32_t limit = extra ? cli_u32(extra) : 12;
        if (!limit || limit > FIREWALL_LOG_LIMIT) limit = FIREWALL_LOG_LIMIT;
        uint32_t count = firewall_log_count();
        if (!count) { cli_p("log empty\n"); return; }
        for (uint32_t i = 0; i < count && i < limit; i++) {
            cli_p(firewall_log_entry(i));
            cli_p("\n");
        }
        return;
    }
    if (!strcmp(action, "logdrops")) {
        char* value = cli_next(cursor);
        if (!value) { cli_p("drop logging "); cli_p(firewall_get_log_drops() ? "on\n" : "off\n"); return; }
        firewall_set_log_drops(!strcmp(value, "on"));
        cli_p("drop logging "); cli_p(!strcmp(value, "on") ? "on\n" : "off\n");
        return;
    }
    if (!strcmp(action, "reset")) {
        firewall_clear_rules();
        firewall_clear_services();
        firewall_set_policy(FIREWALL_DIR_ANY, FIREWALL_ACTION_ALLOW);
        firewall_set_rate_limit(0);
        firewall_log_clear();
        firewall_reset_counters();
        cli_p("firewall reset to factory defaults\n");
        return;
    }
    if (!strcmp(action, "add")) {
        char* value = cli_next(cursor);
        int rule_action = cli_action(value);
        if (rule_action < 0) { cli_p("usage: fw add allow|deny <protocol> [options]\n"); return; }
        cli_fw_add_extended(cursor, rule_action);
        return;
    }
    int legacy_action = cli_action(action);
    if (legacy_action >= 0) { cli_fw_legacy(cursor, legacy_action); return; }
    cli_p("unknown fw command; try 'fw docs'\n");
}

/* ------------------------------------------------------------------ */
/* public entry points                                                 */
/* ------------------------------------------------------------------ */

int router_cli_exec(const char* command, const char* args, char* out, uint32_t cap) {
    char line[CLI_LINE_MAX];
    uint32_t used = 0;
    line[0] = 0;
    if (command) {
        const char* p = command;
        while (*p && used + 1 < sizeof(line)) line[used++] = *p++;
        if (used + 1 < sizeof(line)) line[used++] = ' ';
    }
    if (args) {
        const char* p = args;
        while (*p && used + 1 < sizeof(line)) line[used++] = *p++;
    }
    line[used] = 0;
    char* saved_out = cli_out;
    uint32_t saved_cap = cli_cap, saved_used = cli_used;
    cli_out = (out && cap) ? out : 0;
    cli_cap = cap;
    cli_used = 0;
    if (cli_out) cli_out[0] = 0;
    char* cursor = line;
    char* name = cli_next(&cursor);
    if (name) {
        if (!strcmp(name, "rtr") || !strcmp(name, "router")) cli_rtr(&cursor);
        else if (!strcmp(name, "fw") || !strcmp(name, "firewall")) cli_fw(&cursor);
        else if (!strcmp(name, "help") && (!strcmp(cursor, "rtr") || !strcmp(cursor, "router"))) cli_rtr_help();
        else if (!strcmp(name, "help") && (!strcmp(cursor, "fw") || !strcmp(cursor, "firewall"))) cli_fw_help();
        else cli_p("unknown command; use 'rtr help' or 'fw docs'\n");
    }
    uint32_t produced = cli_used;
    cli_out = saved_out;
    cli_cap = saved_cap;
    cli_used = saved_used;
    return (int)produced;
}

void router_command(const char* args) { router_cli_exec("rtr", args, 0, 0); }
void fw_command(const char* args) { router_cli_exec("fw", args, 0, 0); }
