#ifndef SHARK_STORE_H
#define SHARK_STORE_H
#include <stdint.h>
void store_boot_option(const char *cmd);
int store_installed(void);
int store_restore(void);
int store_sync(void);
void store_tick(void);
const char *store_status(void);
#endif
