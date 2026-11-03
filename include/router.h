#ifndef SHARKOS_ROUTER_H
#define SHARKOS_ROUTER_H

#include <stdint.h>
#include "net.h"

/* SharkOS Server basic router: DHCP server, DNS resolver/forwarder, WAN
 * management, NAT with port forwards and the port-69 management console. */

#define ROUTER_DEFAULT_HTTP_PORT   69u
#define ROUTER_LEASE_LIMIT         64
#define ROUTER_RESERVATION_LIMIT   16
#define ROUTER_DNS_RECORD_LIMIT    32
#define ROUTER_DNS_CACHE_LIMIT     64
#define ROUTER_DNS_PENDING_LIMIT   8
#define ROUTER_FORWARD_LIMIT       16
#define ROUTER_NAT_LIMIT           64
#define ROUTER_NAME_SIZE           32
#define ROUTER_DOMAIN_SIZE         48
#define ROUTER_TOKEN_SIZE          32
#define ROUTER_STATUS_SIZE         16384

#define ROUTER_WAN_DISABLED        0
#define ROUTER_WAN_DHCP            1
#define ROUTER_WAN_STATIC          2

#define ROUTER_PROTO_TCP           6
#define ROUTER_PROTO_UDP           17

typedef struct {
    uint8_t  ip[4];
    uint8_t  mask[4];
    uint8_t  pool_start[4];
    uint8_t  pool_end[4];
    uint32_t lease_seconds;
    uint8_t  dns_upstream[4];
    uint8_t  dns_secondary[4];
    char     domain[ROUTER_DOMAIN_SIZE];
    char     token[ROUTER_TOKEN_SIZE];
    uint8_t  wan_mode;
    uint8_t  wan_ip[4];
    uint8_t  wan_mask[4];
    uint8_t  wan_gw[4];
    uint8_t  wan_dns[4];
    uint16_t http_port;
    uint8_t  dhcp_enabled;
    uint8_t  dns_enabled;
    uint8_t  nat_enabled;
    uint8_t  running;
    uint8_t  http_running;
    uint8_t  wan_port;         /* index into net_port inventory (uplink) */
    uint8_t  lan_port;         /* index into net_port inventory (LAN) */
    uint8_t  lan_shared;       /* 1 = one NIC, WAN and LAN share the adapter */
    uint32_t started_at;
} router_config_t;

typedef struct {
    uint8_t  active;
    uint8_t  bound;
    uint8_t  mac[6];
    uint8_t  ip[4];
    char     hostname[ROUTER_NAME_SIZE];
    uint32_t expires;
} router_lease_t;

typedef struct {
    uint8_t  active;
    uint8_t  mac[6];
    uint8_t  ip[4];
    char     hostname[ROUTER_NAME_SIZE];
} router_reservation_t;

typedef struct {
    uint8_t  active;
    uint8_t  ip[4];
    char     name[ROUTER_DOMAIN_SIZE];
    uint32_t expires;
    uint8_t  local;
} router_dns_entry_t;

typedef struct {
    uint8_t  active;
    uint8_t  protocol;
    uint16_t wan_port;
    uint16_t lan_port;
    uint8_t  lan_ip[4];
    char     comment[ROUTER_NAME_SIZE];
    uint32_t hits;
} router_forward_t;

typedef struct {
    uint8_t  active;
    uint8_t  protocol;
    uint8_t  lan_ip[4];
    uint8_t  remote_ip[4];
    uint8_t  mac[6];
    uint16_t lan_port;
    uint16_t wan_port;
    uint16_t remote_port;
    uint32_t expires;
    uint32_t packets;
} router_nat_entry_t;

typedef struct {
    uint32_t dhcp_discover;
    uint32_t dhcp_offer;
    uint32_t dhcp_request;
    uint32_t dhcp_ack;
    uint32_t dhcp_nak;
    uint32_t dhcp_release;
    uint32_t dns_queries;
    uint32_t dns_local;
    uint32_t dns_forwarded;
    uint32_t dns_cached;
    uint32_t dns_failed;
    uint32_t nat_created;
    uint32_t nat_hits;
    uint32_t forward_hits;
    uint32_t forwarded_frames;
    uint32_t dropped_frames;
    uint32_t http_requests;
} router_stats_t;

/* lifecycle */
void router_init(void);
int  router_start(uint16_t http_port);
void router_stop(void);
void router_poll(void);
int  router_is_running(void);
void router_clear_static_config(void);
const router_config_t* router_get_config(void);
const router_stats_t* router_get_stats(void);
void router_note_http_request(void);

/* physical interfaces: uplink adapter and auto-assigned LAN port */
#define ROUTER_PORT_AUTO  0xFFu
int      router_port_assign(void);
int      router_set_ports(int wan_index, int lan_index);
uint32_t router_port_count(void);
int      router_port_get(uint32_t index, net_port_t* out);
uint32_t router_port_role(uint32_t index);   /* 1 = WAN, 2 = LAN, 0 = unused */

/* WAN */
int  router_wan_set_dhcp(void);
int  router_wan_set_static(const uint8_t* ip, const uint8_t* mask,
                           const uint8_t* gw, const uint8_t* dns);
int  router_wan_set_down(void);
void router_wan_refresh(void);

/* LAN / DHCP */
int  router_lan_set(const uint8_t* ip, const uint8_t* mask);
int  router_dhcp_pool(const uint8_t* start, const uint8_t* end);
int  router_dhcp_set_lease(uint32_t seconds);
int  router_dhcp_set_enabled(int enabled);
int  router_dhcp_reserve(const uint8_t* mac, const uint8_t* ip, const char* hostname);
int  router_dhcp_remove_reservation(uint32_t id);
int  router_dhcp_release(const uint8_t* ip);
const router_lease_t* router_get_lease(uint32_t index);
const router_reservation_t* router_get_reservation(uint32_t index);
uint32_t router_lease_count(void);

/* DNS */
int  router_dns_set_enabled(int enabled);
int  router_dns_set_upstream(const uint8_t* primary, const uint8_t* secondary);
int  router_dns_set_domain(const char* domain);
int  router_dns_add_record(const char* name, const uint8_t* ip, int local);
int  router_dns_remove_record(const char* name);
void router_dns_flush_cache(void);
const router_dns_entry_t* router_get_dns_entry(uint32_t index);
uint32_t router_dns_cache_count(void);

/* NAT / port forwards */
int  router_nat_set_enabled(int enabled);
int  router_forward_add(uint8_t protocol, uint16_t wan_port,
                        const uint8_t* lan_ip, uint16_t lan_port, const char* comment);
int  router_forward_remove(uint32_t id);
const router_forward_t* router_get_forward(uint32_t index);
const router_nat_entry_t* router_get_nat(uint32_t index);

/* firewall integration */
int  router_firewall_profile(void);
int  router_set_token(const char* token);
int  router_set_http_port(uint16_t port);

/* formatting helpers shared with CLI and HTTP */
void router_format_ip(char* out, uint32_t cap, const uint8_t* ip);
void router_format_mac(char* out, uint32_t cap, const uint8_t* mac);
int  router_parse_mac(const char* text, uint8_t* mac);
int  router_parse_cidr(const char* text, uint8_t* ip, uint8_t* prefix);
uint32_t router_build_status(char* out, uint32_t cap);
uint32_t router_build_firewall_status(char* out, uint32_t cap);
uint32_t router_build_log(char* out, uint32_t cap);

/* CLI (src/net/router_cli.c) */
int  router_cli_exec(const char* command, const char* args, char* out, uint32_t cap);
void router_command(const char* args);
void fw_command(const char* args);

/* HTTP management server (src/net/router_http.c) */
int  router_http_start(uint16_t port);
void router_http_stop(void);
void router_http_poll(void);
int  router_http_is_active(void);
uint16_t router_http_port(void);

#endif
