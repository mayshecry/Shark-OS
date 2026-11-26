#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "port.h"
static const char *source;
static unsigned errors;
static void report(const char *s) {
    fprintf(stderr, "%s\n", s);
    errors++;
}
static const void *lookup(const char *name, size_t *len) {
    const char *data = !strcmp(name, "demo/test.c")    ? source
                       : !strcmp(name, "demo/local.h") ? "#define SCALE 3\n"
                                                       : NULL;
    if (!data)
        return cc_builtin(name, len);
    *len = strlen(data);
    return data;
}
int main(int argc, char **argv) {
    assert(argc == 2);
    struct cc_io io = {lookup, report, malloc(CC_ARENA), malloc(CC_OUTPUT), 0};
    assert(io.arena && io.output);
    source = "int f( {";
    assert(cc_translate(&io, "demo/test.c") && errors);
    source = "int huge[300000]={1};";
    assert(cc_translate(&io, "demo/test.c"));
    source = "#include <stdint.h>\n#include <sharkapi.h>\n#include \"local.h\"\nstruct point { int "
             "x,y; };\nint main(void){struct point p={4,5}; uint32_t n=0; for(int i=0;i<p.x;i++) "
             "n+=SCALE; return n+p.y+(int)2.5==19?0:1;}\n";
    clock_t start = clock();
    for (int i = 0; i < 100; i++)
        assert(!cc_translate(&io, "demo/test.c"));
    printf("100 compiles in %.3f s (host CPU time)\n", (double)(clock() - start) / CLOCKS_PER_SEC);
    FILE *f = fopen(argv[1], "wb");
    assert(f && fwrite(io.output, 1, io.length, f) == io.length);
    assert(!fclose(f));
    free(io.arena);
    free(io.output);
    return 0;
}
