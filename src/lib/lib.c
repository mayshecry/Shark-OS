#include "kernel.h"

typedef volatile int spinlock_t;

void spin_lock(spinlock_t *lock) {
    while (__sync_lock_test_and_set(lock, 1));
}

void spin_unlock(spinlock_t *lock) {
    __sync_lock_release(lock);
}

int strcmp(const char* s1, const char* s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++; s2++;
    }
    return *(unsigned char*)s1 - *(unsigned char*)s2;
}

int strcasecmp(const char* s1, const char* s2) {
    while (*s1 && *s2) {
        char c1 = *s1;
        char c2 = *s2;
        if (c1 >= 'A' && c1 <= 'Z') c1 += 32;
        if (c2 >= 'A' && c2 <= 'Z') c2 += 32;
        if (c1 != c2) return (unsigned char)c1 - (unsigned char)c2;
        s1++; s2++;
    }
    if (*s1) return (unsigned char)*s1;
    if (*s2) return -(unsigned char)*s2;
    return 0;
}

void strcpy(char* dest, const char* src) {
    while ((*dest++ = *src++));
}

char* strchr(const char* s, int c) {
    while (*s) {
        if (*s == (char)c) return (char*)s;
        s++;
    }
    if (c == '\0') return (char*)s;
    return NULL;
}

char* strstr(const char* haystack, const char* needle) {
    if (!*needle) return (char*)haystack;
    for (; *haystack; haystack++) {
        const char* h = haystack;
        const char* n = needle;
        while (*h && *n && *h == *n) { h++; n++; }
        if (!*n) return (char*)haystack;
    }
    return NULL;
}

/* memcpy/memset are the hottest routines in the kernel: every frame the
 * desktop composites ~3 MB (1024x768x32) into the back buffer and then
 * copies it to video memory. The old byte loops moved one byte per
 * iteration; `rep movsl`/`rep stosl` move 4 bytes per iteration and are
 * microcoded fast paths on every x86 since the 486, which matters on the
 * low-end machines this kernel is meant to run on. */
void* memcpy(void* dest, const void* src, size_t n) {
    void* ret = dest;
    if (n >= 8) {
        /* Align the destination to 4 bytes first. */
        size_t head = (4 - ((uintptr_t)dest & 3)) & 3;
        if (head) {
            n -= head;
            asm volatile("rep movsb"
                         : "+D"(dest), "+S"(src), "+c"(head)
                         : : "memory");
        }
        size_t words = n >> 2;
        asm volatile("rep movsl"
                     : "+D"(dest), "+S"(src), "+c"(words)
                     : : "memory");
        n &= 3;
    }
    if (n) {
        asm volatile("rep movsb"
                     : "+D"(dest), "+S"(src), "+c"(n)
                     : : "memory");
    }
    return ret;
}

void* memset(void* s, int c, size_t n) {
    void* ret = s;
    uint32_t v = (uint32_t)(uint8_t)c;
    v |= v << 8;
    v |= v << 16;
    if (n >= 8) {
        size_t head = (4 - ((uintptr_t)s & 3)) & 3;
        if (head) {
            n -= head;
            asm volatile("rep stosb"
                         : "+D"(s), "+c"(head)
                         : "a"(v) : "memory");
        }
        size_t words = n >> 2;
        asm volatile("rep stosl"
                     : "+D"(s), "+c"(words)
                     : "a"(v) : "memory");
        n &= 3;
    }
    if (n) {
        asm volatile("rep stosb"
                     : "+D"(s), "+c"(n)
                     : "a"(v) : "memory");
    }
    return ret;
}

/* Fill `count` 32-bit pixels. */
void fill32(uint32_t* dst, uint32_t value, size_t count) {
    asm volatile("rep stosl"
                 : "+D"(dst), "+c"(count)
                 : "a"(value) : "memory");
}

size_t strlen(const char* str) {
    size_t len = 0;
    while (str[len]) len++;
    return len;
}

int fast_strcmp(const char* s1, const char* s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++; s2++;
    }
    return *(unsigned char*)s1 - *(unsigned char*)s2;
}

void hex_to_string(uint32_t value, char* buffer) {
    char hex_chars[] = "0123456789ABCDEF";
    buffer[0] = '0';
    buffer[1] = 'x';
    for (int i = 0; i < 8; i++) {
        buffer[2 + i] = hex_chars[(value >> (28 - i * 4)) & 0xF];
    }
    buffer[10] = '\0';
}

size_t fast_strlen(const char* str) {
    const char* s = str;
    while (*s) s++;
    return (size_t)(s - str);
}

void int_to_string(uint32_t value, char* buffer) {
    char temp[11];
    int i = 0;
    if (value == 0) {
        buffer[0] = '0';
        buffer[1] = '\0';
        return;
    }
    while (value > 0) {
        temp[i++] = (value % 10) + '0';
        value /= 10;
    }
    int j = 0;
    while (i > 0) {
        buffer[j++] = temp[--i];
    }
    buffer[j] = '\0';
}

void delay_ms(uint32_t ms) {
    volatile uint32_t target = uptime_ticks + ms;
    while ((int32_t)(uptime_ticks - target) < 0) {
        asm volatile("hlt");
    }
}

extern uint64_t screen_width;
extern uint64_t screen_height;
extern uint64_t screen_pitch;
extern uint32_t* hw_lfbptr;

void flush_screen_to_hw(void) {
    if (!hw_lfbptr || !lfbptr || hw_lfbptr == lfbptr) return;
    uint32_t fb_size = screen_pitch * screen_height;
    memcpy(hw_lfbptr, lfbptr, fb_size);
}

/* Copy only the rows [y0, y1) of the back buffer to video memory. Used by
 * the cursor-only fast path so moving the mouse does not push 3 MB through
 * the (often uncached, slow) framebuffer aperture every time. */
void flush_rows_to_hw(int y0, int y1) {
    if (!hw_lfbptr || !lfbptr || hw_lfbptr == lfbptr) return;
    if (y0 < 0) y0 = 0;
    if (y1 > (int)screen_height) y1 = (int)screen_height;
    if (y0 >= y1) return;
    uint32_t stride = screen_pitch / 4;
    memcpy(&hw_lfbptr[(uint32_t)y0 * stride], &lfbptr[(uint32_t)y0 * stride],
           (size_t)(y1 - y0) * screen_pitch);
}

