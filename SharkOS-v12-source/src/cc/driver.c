#include "port.h"
#include <stdint.h>
extern void *memcpy(void *, const void *, size_t);
extern void *memset(void *, int, size_t);
extern size_t strlen(const char *);
extern int strcmp(const char *, const char *);
extern unsigned char sharkcc_data_begin[], sharkcc_data_end[], sharkcc_zero_begin[], sharkcc_zero_end[];
static const struct {
    const char *path, *text;
} headers[] = {
#include "headers.inc"
};
const void *cc_builtin(const char *path, size_t *length) {
    for (size_t i = 0; i < sizeof headers / sizeof headers[0]; i++) {
        if (strcmp(headers[i].path, path))
            continue;
        *length = strlen(headers[i].text);
        return headers[i].text;
    }
    return NULL;
}
int cc_translate(struct cc_io *io, const char *path) {
    static unsigned char saved[4096];
    static int initialized;
    size_t n = (uintptr_t)sharkcc_data_end - (uintptr_t)sharkcc_data_begin;
    if (n > sizeof saved)
        return -1;
    if (!initialized) {
        memcpy(saved, sharkcc_data_begin, n);
        initialized = 1;
    } else
        memcpy(sharkcc_data_begin, saved, n);
    memset(sharkcc_zero_begin, 0, (uintptr_t)sharkcc_zero_end - (uintptr_t)sharkcc_zero_begin);
    return sharkcc_compile(io, path);
}
