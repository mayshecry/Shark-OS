
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

#define NET_VNIC_LIMIT 4
#define NET_VNIC_FRAME_MAX 1518
#define NET_VNIC_STATE_DOWN 0
#define NET_VNIC_STATE_SELECTING 1
#define NET_VNIC_STATE_REQUESTING 2
#define NET_VNIC_STATE_BOUND 3
#define NET_VNIC_STATE_FAILED 4
#define NET_VNIC_STATE_RENEWING 5
#define NET_VNIC_MODE_BRIDGE 0
#define NET_VNIC_MODE_NAT 1

int net_vnic_attach(uint32_t index, const uint8_t mac[6]);
int net_vnic_set_mode(uint32_t index, uint32_t mode);
uint32_t net_vnic_get_mode(uint32_t index);
void net_vnic_detach(uint32_t index);
int net_vnic_dhcp_start(uint32_t index);
int net_vnic_is_attached(uint32_t index);
int net_vnic_get_info(uint32_t index, uint8_t mac[6], uint8_t ip[4], uint8_t mask[4], uint8_t gateway[4], uint8_t dns[4], uint32_t* state);
int net_vnic_send(uint32_t index, const uint8_t* frame, uint32_t len);
int net_vnic_receive(uint32_t index, uint8_t* frame, uint32_t capacity);
int net_vnic_ping(uint32_t index, const uint8_t destination[4], int count);
void net_vnic_rx_frame(const uint8_t* frame, uint32_t len);
void net_vnic_poll(void);

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

int net_tcp_open(const uint8_t* ip, uint16_t port);
int net_tcp_send(const uint8_t* data, uint32_t len);
int net_tcp_listen(uint16_t port);
int net_tcp_server_accept(uint16_t port);
int net_tcp_server_send(int handle, const uint8_t* data, uint32_t len);
int net_tcp_server_recv(int handle, uint8_t* out, uint32_t max, uint32_t timeout_ms);
int net_tcp_server_closed(int handle);
uint32_t net_tcp_server_connection_count(uint16_t port);
void net_tcp_server_close(int handle);
void net_tcp_server_stop(uint16_t port);
int  net_tcp_recv(uint8_t* out, uint32_t max, uint32_t timeout_ms);
int  net_tcp_peer_closed(void);
void net_tcp_close(void);

int net_http_get(const char* url, uint8_t* out_buf, uint32_t out_max);
int net_http_request(const char* method, const char* url, const char* extra_headers, const uint8_t* request_body, uint32_t request_body_len, uint8_t* out_buf, uint32_t out_max);
#define NET_WEBSOCKET_NO_DATA (-2)
#define NET_WEBSOCKET_CLOSED (-3)
int net_websocket_open(const char* url);
int net_websocket_send(const uint8_t* data, uint32_t len);
int net_websocket_recv(uint8_t* out, uint32_t max, uint32_t timeout_ms);
int net_websocket_poll(uint8_t* out, uint32_t max);
void net_websocket_close(void);
void net_http_close_idle(void);
extern char net_http_last_content_type[128];
extern int  net_tls_last_error;
extern char net_tls_peer_cn[64];
extern int  net_http_last_status;
extern char net_http_last_location[1024];

void net_format_mac(char* out);
void net_cmd_ifconfig(void);
void net_cmd_ping(const char* target);
void net_cmd_wget(const char* url);
void net_cmd_dns(const char* host);
void net_cmd_netstat(void);
void net_cmd_dhcp(void);

#endif