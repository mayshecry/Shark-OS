
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

int net_dhcp(void);                 /* blocking: wait for the lease (shell) */
void net_dhcp_start(void);          /* non-blocking restart of the client */
const char* net_dhcp_status(void);  /* "bound 10.0.2.15", "discovering", ... */
int net_dhcp_state(void);           /* 3 = bound, 6 = link-local */
uint32_t net_dhcp_lease_left(void); /* seconds, 0 if not bound */


int net_parse_ip4(const char* str, uint8_t* out);
int net_arp_resolve(const uint8_t* ip, uint8_t* mac_out);


int net_dns_lookup(const char* hostname, uint8_t* ip_out);


int net_ping(const char* ip_str, int count);


int net_tcp_connect(const uint8_t* ip, uint16_t port,
                    const uint8_t* send_data, uint32_t send_len,
                    uint8_t* resp_buf, uint32_t resp_max);

/* Streaming TCP (one connection at a time). */
int  net_tcp_open(const uint8_t* ip, uint16_t port);
int  net_tcp_send(const uint8_t* data, uint32_t len);
int  net_tcp_recv(uint8_t* out, uint32_t max, uint32_t timeout_ms);   /* >0 data, 0 timeout, -1 closed */
int  net_tcp_peer_closed(void);
void net_tcp_close(void);


/* HTTP GET for http:// and https:// URLs (TLS 1.3, no certificate validation). */
int net_http_get(const char* url, uint8_t* out_buf, uint32_t out_max);
extern int  net_tls_last_error;            /* TLS_ERR_* of the last https fetch (0 = ok) */
extern char net_tls_peer_cn[64];           /* subject CN the server presented (unverified) */
extern int  net_http_last_status;          /* e.g. 200, 301, 404 (0 = unknown) */
extern char net_http_last_location[512];   /* Location: header of the last reply */


void net_format_mac(char* out);   /* 18-byte buffer */
void net_cmd_ifconfig(void);
void net_cmd_ping(const char* target);
void net_cmd_wget(const char* url);
void net_cmd_dns(const char* host);
void net_cmd_netstat(void);
void net_cmd_dhcp(void);

#endif