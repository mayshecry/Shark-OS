#ifndef SHARK_APP_H
#define SHARK_APP_H
#include <stddef.h>
#include <stdint.h>
#define APP_ROOM (2u << 20)
int app_tag(const unsigned char *p, size_t size);
const char *app_load(const unsigned char *file, size_t size, uintptr_t (*resolve)(const char *),
                     unsigned char *image, size_t room, uintptr_t *entry);
#endif
