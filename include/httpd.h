#ifndef HTTPD_H
#define HTTPD_H

#include <stdint.h>

#define HTTPD_SERVER_LIMIT 4
int httpd_start(uint16_t port);
int httpd_configure(uint32_t slot, uint16_t port, const char* root, int start);
int httpd_remove(uint32_t slot);
int httpd_server_active(uint32_t slot);
uint16_t httpd_server_port(uint32_t slot);
const char* httpd_server_root(uint32_t slot);
void httpd_stop_server(uint32_t slot);
void httpd_stop(void);
int httpd_is_active(void);
uint16_t httpd_listen_port(void);
void httpd_poll(void);

#endif
