#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DOOM_ALLOCATIONS 4096

typedef struct {
    void* pointer;
    size_t size;
} doom_allocation_t;

static doom_allocation_t doom_allocations[DOOM_ALLOCATIONS];
static int doom_errno_value;
static FILE* doom_fake_file = (FILE*)(uintptr_t)1;
static FILE* doom_stdout_file = (FILE*)(uintptr_t)2;
static FILE* doom_stderr_file = (FILE*)(uintptr_t)3;
FILE* stdout = (FILE*)(uintptr_t)2;
FILE* stderr = (FILE*)(uintptr_t)3;
FILE* stdin = (FILE*)(uintptr_t)4;

extern void* kmalloc(size_t size);
extern void kfree(void* pointer);
extern void serial_puts(const char* text);

int tolower(int value);
int toupper(int value);
int isalpha(int value);
int isdigit(int value);
int isalnum(int value);
int isspace(int value);
int isprint(int value);
int isupper(int value);
int islower(int value);
int isxdigit(int value);
int ispunct(int value);
int iscntrl(int value);
int isgraph(int value);
int isblank(int value);

int* __errno_location(void) {
    return &doom_errno_value;
}

static void doom_track_alloc(void* pointer, size_t size) {
    if (!pointer) return;
    for (int i = 0; i < DOOM_ALLOCATIONS; i++) {
        if (!doom_allocations[i].pointer) {
            doom_allocations[i].pointer = pointer;
            doom_allocations[i].size = size;
            return;
        }
    }
}

void* malloc(size_t size) {
    void* pointer = kmalloc(size ? size : 1);
    doom_track_alloc(pointer, size);
    return pointer;
}

void free(void* pointer) {
    if (!pointer) return;
    for (int i = 0; i < DOOM_ALLOCATIONS; i++) {
        if (doom_allocations[i].pointer == pointer) {
            doom_allocations[i].pointer = NULL;
            doom_allocations[i].size = 0;
            break;
        }
    }
    kfree(pointer);
}

void* calloc(size_t count, size_t size) {
    if (size && count > SIZE_MAX / size) return NULL;
    size_t total = count * size;
    void* pointer = malloc(total);
    if (pointer) memset(pointer, 0, total);
    return pointer;
}

void* realloc(void* pointer, size_t size) {
    if (!pointer) return malloc(size);
    if (!size) { free(pointer); return NULL; }
    size_t old_size = 0;
    for (int i = 0; i < DOOM_ALLOCATIONS; i++) if (doom_allocations[i].pointer == pointer) { old_size = doom_allocations[i].size; break; }
    void* replacement = malloc(size);
    if (!replacement) return NULL;
    memcpy(replacement, pointer, old_size < size ? old_size : size);
    free(pointer);
    return replacement;
}

char* strdup(const char* text) {
    if (!text) return NULL;
    size_t length = strlen(text) + 1;
    char* copy = malloc(length);
    if (copy) memcpy(copy, text, length);
    return copy;
}

char* strrchr(const char* text, int value) {
    const char* found = NULL;
    do { if (*text == (char)value) found = text; } while (*text++);
    return (char*)found;
}

char* strncpy(char* dest, const char* src, size_t count) {
    size_t i = 0;
    while (i < count && src[i]) { dest[i] = src[i]; i++; }
    while (i < count) dest[i++] = 0;
    return dest;
}

int strncmp(const char* left, const char* right, size_t count) {
    while (count && *left && *left == *right) { left++; right++; count--; }
    if (!count) return 0;
    return (unsigned char)*left - (unsigned char)*right;
}

int strncasecmp(const char* left, const char* right, size_t count) {
    while (count && *left && *right) {
        int a = tolower((unsigned char)*left++);
        int b = tolower((unsigned char)*right++);
        if (a != b) return a - b;
        count--;
    }
    if (!count) return 0;
    return (unsigned char)*left - (unsigned char)*right;
}

static void doom_append(char* output, size_t size, size_t* used, char value) {
    if (*used + 1 < size) output[*used] = value;
    (*used)++;
}

static void doom_append_number(char* output, size_t size, size_t* used, uint64_t value, int negative, unsigned base, int upper, int width, int precision, int zero, int left) {
    char digits[32];
    const char* alphabet = upper ? "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ" : "0123456789abcdefghijklmnopqrstuvwxyz";
    int count = 0;
    if (base < 2 || base > 36) base = 10;
    do { digits[count++] = alphabet[value % base]; value /= base; } while (value && count < (int)sizeof(digits));
    while (count < precision && count < (int)sizeof(digits)) digits[count++] = '0';
    int full = count + (negative ? 1 : 0);
    while (!left && width > full && !(zero && precision < 0)) { doom_append(output, size, used, ' '); width--; }
    if (negative) doom_append(output, size, used, '-');
    while (!left && width > full && zero && precision < 0) { doom_append(output, size, used, '0'); width--; }
    while (count) doom_append(output, size, used, digits[--count]);
    while (left && width > full) { doom_append(output, size, used, ' '); width--; }
}

int vsnprintf(char* output, size_t size, const char* format, va_list args) {
    size_t used = 0;
    if (!format) { if (size) output[0] = 0; return 0; }
    while (*format) {
        if (*format != '%') { doom_append(output, size, &used, *format++); continue; }
        format++;
        if (*format == '%') { doom_append(output, size, &used, *format++); continue; }
        int left = 0, zero = 0, width = 0, precision = -1, length = 0;
        while (*format == '-' || *format == '0' || *format == '+' || *format == ' ' || *format == '#') { if (*format == '-') left = 1; if (*format == '0') zero = 1; format++; }
        if (*format == '*') { width = va_arg(args, int); format++; if (width < 0) { left = 1; width = -width; } }
        else while (*format >= '0' && *format <= '9') { width = width * 10 + (*format++ - '0'); if (width > 1000) width = 1000; }
        if (*format == '.') { format++; precision = 0; if (*format == '*') { precision = va_arg(args, int); format++; } else while (*format >= '0' && *format <= '9') { precision = precision * 10 + (*format++ - '0'); if (precision > 1000) precision = 1000; } }
        if (*format == 'h') { length = 1; format++; if (*format == 'h') { length = 2; format++; } }
        else if (*format == 'l') { length = 3; format++; if (*format == 'l') { length = 4; format++; } }
        else if (*format == 'z' || *format == 't') { length = 5; format++; }
        char specifier = *format ? *format++ : 0;
        if (specifier == 's') {
            const char* value = va_arg(args, const char*); if (!value) value = "(null)";
            size_t n = strlen(value); if (precision >= 0 && n > (size_t)precision) n = (size_t)precision;
            while (!left && width > (int)n) { doom_append(output, size, &used, ' '); width--; }
            for (size_t i = 0; i < n; i++) doom_append(output, size, &used, value[i]);
            while (left && width > (int)n) { doom_append(output, size, &used, ' '); width--; }
        } else if (specifier == 'c') {
            char value = (char)va_arg(args, int); doom_append(output, size, &used, value);
        } else if (specifier == 'p') {
            uintptr_t value = (uintptr_t)va_arg(args, void*); doom_append(output, size, &used, '0'); doom_append(output, size, &used, 'x');
            doom_append_number(output, size, &used, value, 0, 16, 0, width > 2 ? width - 2 : 0, precision < 0 ? 1 : precision, zero, left);
        } else if (specifier == 'd' || specifier == 'i') {
            int64_t value;
            if (length == 4) value = va_arg(args, long long);
            else if (length == 3) value = va_arg(args, long);
            else if (length == 5) value = (int64_t)va_arg(args, ptrdiff_t);
            else value = va_arg(args, int);
            int negative = value < 0; uint64_t magnitude = negative ? (uint64_t)(-(value + 1)) + 1 : (uint64_t)value;
            doom_append_number(output, size, &used, magnitude, negative, 10, 0, width, precision, zero, left);
        } else if (specifier == 'u' || specifier == 'x' || specifier == 'X' || specifier == 'o') {
            uint64_t value;
            if (length == 4) value = va_arg(args, unsigned long long);
            else if (length == 3) value = va_arg(args, unsigned long);
            else if (length == 5) value = va_arg(args, size_t);
            else value = va_arg(args, unsigned int);
            unsigned base = specifier == 'o' ? 8 : (specifier == 'u' ? 10 : 16);
            doom_append_number(output, size, &used, value, 0, base, specifier == 'X', width, precision, zero, left);
        } else if (specifier == 'f' || specifier == 'g' || specifier == 'e') {
            double value = va_arg(args, double); int negative = value < 0; if (negative) value = -value;
            uint64_t whole = (uint64_t)value; uint64_t frac = (uint64_t)((value - (double)whole) * 1000000.0);
            doom_append_number(output, size, &used, whole, negative, 10, 0, width, -1, zero, left);
            if (precision != 0) { doom_append(output, size, &used, '.'); doom_append_number(output, size, &used, frac, 0, 10, 0, 6, 6, 1, 0); }
        } else {
            if (specifier) { doom_append(output, size, &used, '%'); doom_append(output, size, &used, specifier); }
        }
    }
    if (size) output[used < size ? used : size - 1] = 0;
    return used > 2147483647u ? 2147483647 : (int)used;
}

int snprintf(char* output, size_t size, const char* format, ...) {
    va_list args; va_start(args, format); int result = vsnprintf(output, size, format, args); va_end(args); return result;
}

int vsprintf(char* output, const char* format, va_list args) { return vsnprintf(output, SIZE_MAX, format, args); }

int sprintf(char* output, const char* format, ...) {
    va_list args; va_start(args, format); int result = vsnprintf(output, SIZE_MAX, format, args); va_end(args); return result;
}

int vfprintf(FILE* stream, const char* format, va_list args) {
    char buffer[1024]; int length = vsnprintf(buffer, sizeof(buffer), format, args);
    if (stream == doom_stdout_file || stream == doom_stderr_file) serial_puts(buffer);
    return length;
}

int fprintf(FILE* stream, const char* format, ...) {
    va_list args; va_start(args, format); int result = vfprintf(stream, format, args); va_end(args); return result;
}

int printf(const char* format, ...) {
    va_list args; va_start(args, format); int result = vfprintf(stdout, format, args); va_end(args); return result;
}

int puts(const char* text) { serial_puts(text); serial_puts("\n"); return 0; }
int putchar(int value) { char c = (char)value; serial_puts((char[]){c, 0}); return (unsigned char)c; }
int fputs(const char* text, FILE* stream) { if (stream == stdout || stream == stderr) serial_puts(text); return 0; }
int fflush(FILE* stream) { (void)stream; return 0; }
int fclose(FILE* stream) { (void)stream; return 0; }
int fseek(FILE* stream, long offset, int origin) { (void)stream; (void)offset; (void)origin; return -1; }
long ftell(FILE* stream) { (void)stream; return -1; }
size_t fread(void* buffer, size_t size, size_t count, FILE* stream) { (void)buffer; (void)size; (void)count; (void)stream; return 0; }
size_t fwrite(const void* buffer, size_t size, size_t count, FILE* stream) { (void)buffer; (void)size; (void)count; (void)stream; return 0; }
int feof(FILE* stream) { (void)stream; return 1; }
FILE* fopen(const char* path, const char* mode) {
    (void)mode;
    if (!path) return NULL;
    size_t length = strlen(path);
    if (length >= 13 && strcasecmp(path + length - 13, "freedoom1.wad") == 0) return doom_fake_file;
    return NULL;
}
int remove(const char* path) { (void)path; return -1; }
int rename(const char* old_path, const char* new_path) { (void)old_path; (void)new_path; return -1; }
int access(const char* path, int mode) { (void)path; (void)mode; return -1; }
char* getenv(const char* name) { (void)name; return NULL; }
int system(const char* command) { (void)command; return -1; }
int mkdir(const char* path, unsigned int mode) { (void)path; (void)mode; return -1; }

int tolower(int value) { return value >= 'A' && value <= 'Z' ? value + 32 : value; }
int toupper(int value) { return value >= 'a' && value <= 'z' ? value - 32 : value; }
int isalpha(int value) { return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z'); }
int isdigit(int value) { return value >= '0' && value <= '9'; }
int isalnum(int value) { return isalpha(value) || isdigit(value); }
int isspace(int value) { return value == ' ' || value == '\t' || value == '\n' || value == '\r' || value == '\f' || value == '\v'; }
int isprint(int value) { return value >= 32 && value < 127; }
int isupper(int value) { return value >= 'A' && value <= 'Z'; }
int islower(int value) { return value >= 'a' && value <= 'z'; }
int isxdigit(int value) { return isdigit(value) || (tolower(value) >= 'a' && tolower(value) <= 'f'); }
int ispunct(int value) { return isprint(value) && !isspace(value) && !isalnum(value); }
int iscntrl(int value) { return value < 32 || value == 127; }
int isgraph(int value) { return isprint(value) && !isspace(value); }
int isblank(int value) { return value == ' ' || value == '\t'; }

static int32_t doom_toupper_table[384];
static int32_t doom_tolower_table[384];
static const int32_t* doom_toupper_pointer = doom_toupper_table + 128;
static const int32_t* doom_tolower_pointer = doom_tolower_table + 128;
static int doom_ctype_ready;

const int32_t** __ctype_toupper_loc(void) {
    if (!doom_ctype_ready) {
        for (int i = -128; i < 256; i++) {
            doom_toupper_table[i + 128] = toupper(i);
            doom_tolower_table[i + 128] = tolower(i);
        }
        doom_ctype_ready = 1;
    }
    return &doom_toupper_pointer;
}

const int32_t** __ctype_tolower_loc(void) {
    if (!doom_ctype_ready) __ctype_toupper_loc();
    return &doom_tolower_pointer;
}

int sscanf(const char* input, const char* format, ...) {
    int assigned = 0;
    va_list args;
    va_start(args, format);
    while (*format) {
        if (isspace((unsigned char)*format)) {
            while (isspace((unsigned char)*format)) format++;
            while (isspace((unsigned char)*input)) input++;
            continue;
        }
        if (*format != '%') {
            if (*input != *format) break;
            input++; format++;
            continue;
        }
        format++;
        int base = 10;
        if (*format == 'x' || *format == 'X') { base = 16; format++; }
        else if (*format == 'o') { base = 8; format++; }
        else if (*format == 'i') { base = 0; format++; }
        else if (*format == 'd' || *format == 'u') format++;
        else break;
        while (isspace((unsigned char)*input)) input++;
        char* end = (char*)input;
        long value = strtol(input, &end, base);
        if (end == input) break;
        *va_arg(args, int*) = (int)value;
        input = end;
        assigned++;
    }
    va_end(args);
    return assigned;
}

void exit(int status) {
    (void)status;
    extern void doom_engine_exit(void);
    doom_engine_exit();
    __builtin_unreachable();
}

int atoi(const char* text) { return (int)strtol(text, NULL, 10); }
long atol(const char* text) { return strtol(text, NULL, 10); }
long strtol(const char* text, char** end, int base) {
    int negative = 0; long value = 0;
    while (isspace((unsigned char)*text)) text++;
    if (*text == '-') { negative = 1; text++; } else if (*text == '+') text++;
    if (base == 0) { base = 10; if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) { base = 16; text += 2; } else if (text[0] == '0') base = 8; }
    while (*text) { int d = isdigit((unsigned char)*text) ? *text - '0' : tolower((unsigned char)*text) - 'a' + 10; if (d < 0 || d >= base) break; value = value * base + d; text++; }
    if (end) *end = (char*)text;
    return negative ? -value : value;
}
unsigned long strtoul(const char* text, char** end, int base) { return (unsigned long)strtol(text, end, base); }
long long strtoll(const char* text, char** end, int base) { return (long long)strtol(text, end, base); }
unsigned long long strtoull(const char* text, char** end, int base) { return (unsigned long long)strtoul(text, end, base); }
double strtod(const char* text, char** end) { long whole = strtol(text, end, 10); return (double)whole; }

int abs(int value) { return value < 0 ? -value : value; }
long labs(long value) { return value < 0 ? -value : value; }
double fabs(double value) { return value < 0 ? -value : value; }
double floor(double value) { long whole = (long)value; return (double)(whole > value ? whole - 1 : whole); }
double ceil(double value) { long whole = (long)value; return (double)(whole < value ? whole + 1 : whole); }
double sqrt(double value) { if (value <= 0) return 0; double guess = value > 1 ? value : 1; for (int i = 0; i < 24; i++) guess = (guess + value / guess) * 0.5; return guess; }
double pow(double base, double exponent) { if (exponent == 2) return base * base; if (exponent == 0) return 1; return base; }

void qsort(void* base, size_t count, size_t size, int (*compare)(const void*, const void*)) {
    uint8_t* bytes = base;
    for (size_t i = 1; i < count; i++) for (size_t j = i; j > 0 && compare(bytes + (j - 1) * size, bytes + j * size) > 0; j--) {
        for (size_t k = 0; k < size; k++) { uint8_t t = bytes[(j - 1) * size + k]; bytes[(j - 1) * size + k] = bytes[j * size + k]; bytes[j * size + k] = t; }
    }
}
