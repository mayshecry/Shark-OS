#ifndef SHARK_CC_PORT_H
#define SHARK_CC_PORT_H
#include <stddef.h>
#define CC_ARENA (8u << 20)
#define CC_OUTPUT (1u << 20)
struct cc_io {
    const void *(*lookup)(const char *, size_t *);
    void (*message)(const char *);
    unsigned char *arena, *output;
    size_t length;
    int link;
    const char **extra;
    unsigned nextra;
};
int sharkcc_compile(struct cc_io *, const char *);
const void *cc_builtin(const char *, size_t *);
int cc_translate(struct cc_io *, const char *);
#endif
