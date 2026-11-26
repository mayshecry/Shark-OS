#include "app.h"
#include "port.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

static const char *source;
static unsigned messages;
static const void *lookup(const char *p, size_t *n) {
    const char *s = !strcmp(p, "hello.c")    ? source
                    : !strcmp(p, "helper.c") ? "int helper(int x){return x*2;}"
                                             : NULL;
    if (!s)
        return cc_builtin(p, n);
    *n = strlen(s);
    return s;
}
static void say(const char *s) {
    assert(!strcmp(s, "Hello, world!"));
    messages++;
}
static uintptr_t resolve(const char *s) { return strcmp(s, "sharkapi_println") ? 0 : (uintptr_t)say; }
static void report(const char *s) { fprintf(stderr, "%s\n", s); }
static const char *linkit(struct cc_io *io) {
    assert(!cc_translate(io, "hello.c"));
    memcpy(io->output, "SAP1", 4);
    return app_load(io->output, io->length, resolve, NULL, APP_ROOM, NULL);
}
int main(void) {
    const char *extra[] = {"helper.c"};
    struct cc_io io = {lookup, report, malloc(CC_ARENA), malloc(CC_OUTPUT), 0, 1, extra, 1};
    source = "extern void unknown(void); int main(void){unknown();return 0;}";
    const char *err = linkit(&io);
    assert(err && !strcmp(err, "unknown"));
    source = "int x;";
    err = linkit(&io);
    assert(err && strstr(err, "main"));
    source = "#include <sharkapi.h>\nint helper(int); int bss; static char *greeting=\"Hello, world!\";int "
             "main(int argc,char **argv){sharkapi_println(greeting);return bss+++helper(argc)+argv[1][0];}";
    assert(!linkit(&io));
    assert(app_load(io.output, io.length / 2, resolve, NULL, APP_ROOM, NULL));
    assert(app_load(io.output, io.length, resolve, NULL, 100, NULL));
    unsigned char *slots[2];
    for (int i = 0; i < 2; i++) {
        slots[i] = mmap(NULL, APP_ROOM, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        assert(slots[i] != MAP_FAILED);
        for (int repeat = 0; repeat < 2; repeat++) {
            uintptr_t entry;
            assert(!app_load(io.output, io.length, resolve, slots[i], APP_ROOM, &entry));
            char *argv[] = {"hello", "!", NULL};
            assert(((int (*)(int, char **))entry)(2, argv) == 37);
        }
    }
    assert(messages == 4);
    puts(
        "Linked multi-source main ran at two load addresses; imports, argv, globals, BSS and error paths passed");
    return 0;
}
