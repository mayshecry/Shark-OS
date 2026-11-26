#ifndef SHARK_INSTALL_H
#define SHARK_INSTALL_H
#include <stdint.h>
void install_ref_set(const void *,uint32_t,const void *,uint32_t);
void install_image_set(const void *, uint32_t);
uint32_t install_size(void);
int install_image_ready(void);
const char *install_disk_confirmed(int, void (*)(const char *, unsigned), int (*)(int,const char *));
const char *install_disk(int, void (*)(const char *, unsigned));
void install_menu(void);
#endif
