#include "port.h"
#include "libtcc.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <setjmp.h>
#include <fcntl.h>
#include <errno.h>
#include <stdint.h>

static struct cc_io *io;
static jmp_buf escape;
static size_t used;
static void *bins[20];
static struct {
    const unsigned char *data;
    size_t pos, size;
} files[40];
static int errnum;
int *__errno_location(void) { return &errnum; }
int *___errno_location(void) { return &errnum; }
static void fail(const char *why) {
    io->message(why);
    longjmp(escape, 1);
}
void exit(int code) {
    (void)code;
    fail("compiler stopped");
    __builtin_unreachable();
}
void __assert_fail(const char *expr, const char *path, int line, const char *fn) {
    (void)path;
    (void)line;
    (void)fn;
    fail(expr);
    __builtin_unreachable();
}
void *malloc(size_t n) {
    size_t bytes = 32, b = 0;
    if (n > CC_ARENA - 16)
        fail("compiler workspace full");
    while (bytes - 16 < n) {
        bytes *= 2;
        b++;
    }
    unsigned char *p = bins[b];
    if (p)
        bins[b] = *(void **)(p + 16);
    else {
        if (bytes > CC_ARENA - used)
            fail("compiler workspace full");
        p = io->arena + used;
        used += bytes;
    }
    *(size_t *)p = b;
    return p + 16;
}
void free(void *p) {
    if (!p)
        return;
    unsigned char *h = (unsigned char *)p - 16;
    size_t b = *(size_t *)h;
    *(void **)p = bins[b];
    bins[b] = h;
}
void *realloc(void *p, size_t n) {
    if (!p)
        return malloc(n);
    size_t cap = ((size_t)32 << *(size_t *)((char *)p - 16)) - 16;
    if (cap >= n)
        return p;
    void *q = malloc(n);
    memcpy(q, p, cap);
    free(p);
    return q;
}
int open(const char *path, int flags, ...) {
    if (flags & O_WRONLY)
        return strcmp(path, "@object") == 0 ? 1 : -1;
    size_t size = 0;
    const void *data = io->lookup(path, &size);
    if (!data)
        return -1;
    for (unsigned i = 3; i < 40; i++)
        if (!files[i].data) {
            files[i].data = data;
            files[i].size = size;
            files[i].pos = 0;
            return i;
        }
    return -1;
}
ssize_t read(int fd, void *p, size_t n) {
    if ((unsigned)fd >= 40 || !files[fd].data)
        return -1;
    if (n > files[fd].size - files[fd].pos)
        n = files[fd].size - files[fd].pos;
    memcpy(p, files[fd].data + files[fd].pos, n);
    files[fd].pos += n;
    return n;
}
int close(int fd) {
    if ((unsigned)fd >= 40)
        return -1;
    files[fd].data = NULL;
    return 0;
}
off_t lseek(int fd, off_t offset, int whence) {
    if ((unsigned)fd >= 40 || !files[fd].data)
        return -1;
    if (whence == SEEK_CUR)
        offset += files[fd].pos;
    if (whence == SEEK_END)
        offset += files[fd].size;
    if (offset < 0 || (size_t)offset > files[fd].size)
        return -1;
    return files[fd].pos = offset;
}
char *getenv(const char *name) {
    (void)name;
    return NULL;
}
char *getcwd(char *buf, size_t n) {
    if (n < 2)
        return NULL;
    strcpy(buf, ".");
    return buf;
}
int unlink(const char *path) {
    (void)path;
    return -1;
}
FILE *const stdout = (FILE *)1, *const stderr = (FILE *)2;
FILE *fdopen(int fd, const char *mode) {
    (void)mode;
    return fd == 1 ? (FILE *)3 : NULL;
}
int fflush(FILE *f) {
    (void)f;
    return 0;
}
int fclose(FILE *f) {
    (void)f;
    return 0;
}
size_t fwrite(const void *p, size_t s, size_t count, FILE *f) {
    if (f != (FILE *)3)
        return 0;
    if (s && count > (CC_OUTPUT - io->length) / s)
        fail("object exceeds 1 MiB");
    if (s && count)
        memcpy(io->output + io->length, p, s * count);
    io->length += s * count;
    return count;
}
int fputc(int c, FILE *f) {
    unsigned char b = c;
    return fwrite(&b, 1, 1, f) ? b : EOF;
}
int fputs(const char *s, FILE *f) {
    (void)f;
    io->message(s);
    return 0;
}
static int print(const char *fmt, va_list ap) {
    char buf[512];
    int n = vsnprintf(buf, sizeof buf, fmt, ap);
    io->message(buf);
    return n;
}
int printf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = print(fmt, ap);
    va_end(ap);
    return n;
}
int fprintf(FILE *f, const char *fmt, ...) {
    (void)f;
    va_list ap;
    va_start(ap, fmt);
    int n = print(fmt, ap);
    va_end(ap);
    return n;
}
int __lockfile(FILE *f) {
    (void)f;
    return 0;
}
void __unlockfile(FILE *f) { (void)f; }
void __stdio_exit_needed(void) {}
int wctomb(char *s, wchar_t wc) {
    if (!s)
        return 0;
    if ((unsigned)wc > 127)
        return -1;
    *s = wc;
    return 1;
}
char *strerror(int code) {
    (void)code;
    return "compiler I/O error";
}
time_t time(time_t *t) {
    if (t)
        *t = 0;
    return 0;
}
struct tm *localtime(const time_t *t) {
    (void)t;
    static struct tm epoch = {.tm_year = 70, .tm_mday = 1, .tm_wday = 4};
    return &epoch;
}
static void diagnostic(void *ctx, const char *s) {
    (void)ctx;
    io->message(s);
}
extern int compile_input(TCCState *, const char *, int);
int compile(struct cc_io *ctx, const char *source) {
    io = ctx;
    io->length = 0;
    if (setjmp(escape))
        return -1;
    TCCState *s = tcc_new();
    tcc_set_error_func(s, NULL, diagnostic);
    if (io->link)
        tcc_set_options(s, "-nostdlib -Wl,-section-alignment=1000");
    tcc_set_output_type(s, io->link ? TCC_OUTPUT_DLL : TCC_OUTPUT_OBJ);
    tcc_undefine_symbol(s, "__STDC_HOSTED__");
    tcc_define_symbol(s, "__STDC_HOSTED__", "0");
    const char *hosted[] = {"__linux__", "__linux", "linux", "__unix__", "__unix", "unix"};
    for (unsigned i = 0; i < sizeof hosted / sizeof hosted[0]; i++)
        tcc_undefine_symbol(s, hosted[i]);
    tcc_define_symbol(s, "__SHARKOS__", "1");
    int rc = 0;
    for (unsigned i = 0; !rc && i <= ctx->nextra; i++) {
        const char *path = i ? ctx->extra[i - 1] : source;
        int fd = open(path, O_RDONLY);
        if (fd < 0) {
            io->message("input not found");
            rc = -1;
            break;
        }
        rc = compile_input(s, path, fd);
        close(fd);
    }
    if (!rc)
        rc = tcc_output_file(s, "@object");
    tcc_delete(s);
    return rc;
}
