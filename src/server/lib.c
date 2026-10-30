#include "kernel.h"

void* memset(void* dest, int value, size_t size) {
    uint8_t* out = dest;
    for (size_t i = 0; i < size; i++) out[i] = (uint8_t)value;
    return dest;
}

void* memcpy(void* dest, const void* src, size_t size) {
    uint8_t* out = dest;
    const uint8_t* in = src;
    for (size_t i = 0; i < size; i++) out[i] = in[i];
    return dest;
}

size_t strlen(const char* text) {
    size_t size = 0;
    if (!text) return 0;
    while (text[size]) size++;
    return size;
}

size_t fast_strlen(const char* text) {
    return strlen(text);
}

int strcmp(const char* a, const char* b) {
    while (*a && *a == *b) { a++; b++; }
    return (uint8_t)*a - (uint8_t)*b;
}

static char server_lower(char c) {
    if (c >= 'A' && c <= 'Z') return (char)(c + ('a' - 'A'));
    return c;
}

int strcasecmp(const char* a, const char* b) {
    while (*a && server_lower(*a) == server_lower(*b)) { a++; b++; }
    return (uint8_t)server_lower(*a) - (uint8_t)server_lower(*b);
}

void strcpy(char* dest, const char* src) {
    while ((*dest++ = *src++)) { }
}

char* strchr(const char* text, int value) {
    while (*text) {
        if (*text == (char)value) return (char*)text;
        text++;
    }
    return value == 0 ? (char*)text : NULL;
}

char* strstr(const char* text, const char* find) {
    if (!*find) return (char*)text;
    for (; *text; text++) {
        size_t i = 0;
        while (find[i] && text[i] == find[i]) i++;
        if (!find[i]) return (char*)text;
    }
    return NULL;
}

void hex_to_string(uint32_t value, char* out) {
    static const char digits[] = "0123456789ABCDEF";
    out[0] = '0';
    out[1] = 'x';
    for (uint32_t i = 0; i < 8; i++) out[i + 2] = digits[(value >> (28 - i * 4)) & 15];
    out[10] = 0;
}

void int_to_string(uint32_t value, char* out) {
    char reverse[16];
    uint32_t count = 0;
    do {
        reverse[count++] = (char)('0' + value % 10);
        value /= 10;
    } while (value && count < sizeof(reverse));
    uint32_t i = 0;
    while (count) out[i++] = reverse[--count];
    out[i] = 0;
}

void delay_ms(uint32_t ms) {
    uint32_t start = uptime_ticks;
    while (uptime_ticks - start < ms) asm volatile("pause");
}

void yield(void) {
    asm volatile("sti; hlt");
}

void spin_lock(spinlock_t* lock) {
    while (__sync_lock_test_and_set(lock, 1)) asm volatile("pause");
}

void spin_unlock(spinlock_t* lock) {
    __sync_lock_release(lock);
}

static char* server_capture_buffer;
static uint32_t server_capture_capacity;
static uint32_t server_capture_length;

void server_output_capture_begin(char* output, uint32_t capacity) {
    server_capture_buffer = output;
    server_capture_capacity = capacity;
    server_capture_length = 0;
    if (output && capacity) output[0] = 0;
}

uint32_t server_output_capture_end(void) {
    uint32_t length = server_capture_length;
    if (server_capture_buffer && server_capture_capacity) server_capture_buffer[length] = 0;
    server_capture_buffer = NULL;
    server_capture_capacity = 0;
    server_capture_length = 0;
    return length;
}

int server_output_capture_char(char c) {
    if (!server_capture_buffer || !server_capture_capacity) return 0;
    if (server_capture_length + 1 < server_capture_capacity) {
        server_capture_buffer[server_capture_length++] = c;
        server_capture_buffer[server_capture_length] = 0;
    }
    return 1;
}

int server_output_capture_active(void) {
    return server_capture_buffer != NULL && server_capture_capacity != 0;
}

void terminal_writestring(const char* text) {
    serial_puts(text);
}

void terminal_putchar(char c) {
    serial_putc(c);
}

void terminal_write_direct(const char* text) {
    terminal_writestring(text);
}

void terminal_set_color(uint8_t color) {
    if (active_pane >= 0 && active_pane < MAX_PANES) panes[active_pane].color = color;
}

void terminal_clear(void) {
    serial_puts("\033[2J\033[H");
}

void draw_cursor(void) {
}

long inflate_raw(const uint8_t* in, size_t in_len, uint8_t* out, size_t out_len) {
    (void)in; (void)in_len; (void)out; (void)out_len;
    return -1;
}

long inflate_zlib(const uint8_t* in, size_t in_len, uint8_t* out, size_t out_len) {
    (void)in; (void)in_len; (void)out; (void)out_len;
    return -1;
}

long inflate_gzip(const uint8_t* in, size_t in_len, uint8_t* out, size_t out_len) {
    (void)in; (void)in_len; (void)out; (void)out_len;
    return -1;
}
