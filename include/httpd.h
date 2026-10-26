#ifndef HTTPD_H
#define HTTPD_H

#include <stdint.h>

int httpd_start(uint16_t port);
void httpd_stop(void);
int httpd_is_active(void);
uint16_t httpd_listen_port(void);
void httpd_poll(void);

#endif
