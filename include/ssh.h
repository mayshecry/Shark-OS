#ifndef SSH_H
#define SSH_H

#include <stdint.h>

int ssh_server_init(void);
int ssh_server_set_password(const char* username, const char* password);
int ssh_server_start(uint16_t port);
void ssh_server_stop(void);
void ssh_server_poll(void);
int ssh_server_is_active(void);
int ssh_server_has_password(void);
int ssh_server_has_host_key(void);
uint16_t ssh_server_listen_port(void);

#endif
