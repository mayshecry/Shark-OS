#include "libtcc.c"
int compile_input(TCCState *s, const char *name, int fd) {
    return tcc_compile(s, AFF_TYPE_C, name, fd);
}
