
#ifndef NET_H
#define NET_H

#include <stdint.h>

extern uint8_t net_mac[6];
extern int net_has_link;
extern char net_driver_name[32];

extern uint8_t net_ip[4];
extern uint8_t net_mask[4];
extern uint8_t net_gw[4];
extern uint8_t net_dns[4];
extern int net_configured;

void net_init(void);
int net_has_nic(void);

void net_stack_init(void);

void net_poll(void);

void net_send_raw(const uint8_t* data, uint32_t len);

typedef void (*net_rx_cb_t)(const uint8_t* frame, uint32_t len);
void net_set_rx_callback(net_rx_cb_t cb);

int net_dhcp(void);
void net_dhcp_start(void);
const char* net_dhcp_status(void);
int net_dhcp_state(void);
uint32_t net_dhcp_lease_left(void);

int net_parse_ip4(const char* str, uint8_t* out);
int net_arp_resolve(const uint8_t* ip, uint8_t* mac_out);

int net_dns_lookup(const char* hostname, uint8_t* ip_out);

int net_ping(const char* ip_str, int count);

int net_tcp_connect(const uint8_t* ip, uint16_t port,
                    const uint8_t* send_data, uint32_t send_len,
                    uint8_t* resp_buf, uint32_t resp_max);

int  net_tcp_open(const uint8_t* ip, uint16_t port);
int  net_tcp_send(const uint8_t* data, uint32_t len);
int  net_tcp_recv(uint8_t* out, uint32_t max, uint32_t timeout_ms);
int  net_tcp_peer_closed(void);
void net_tcp_close(void);

int net_http_get(const char* url, uint8_t* out_buf, uint32_t out_max);
extern int  net_tls_last_error;
extern char net_tls_peer_cn[64];
extern int  net_http_last_status;
extern char net_http_last_location[512];

void net_format_mac(char* out);
void net_cmd_ifconfig(void);
void net_cmd_ping(const char* target);
void net_cmd_wget(const char* url);
void net_cmd_dns(const char* host);
void net_cmd_netstat(void);
void net_cmd_dhcp(void);

#endif