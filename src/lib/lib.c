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

void strcpy(char* dest, const char* src) {
    while ((*dest++ = *src++));
}

void* memcpy(void* dest, const void* src, size_t n) {
    uint8_t* d = (uint8_t*)dest;
    const uint8_t* s = (const uint8_t*)src;
    for (size_t i = 0; i < n; i++) d[i] = s[i];
    return dest;
}

void* memset(void* s, int c, size_t n) {
    uint8_t* p = (uint8_t*)s;
    for (size_t i = 0; i < n; i++) p[i] = (uint8_t)c;
    return s;
}

size_t strlen(const char* str) {
    size_t len = 0;
    while (str[len]) len++;
    return len;
}

void hex_to_string(uint32_t value, char* buffer) {
    char hex_chars[] = "0123456789ABCDEF";
    buffer[0] = '0';
    buffer[1] = 'x';
    buffer[10] = '\0';
    for (int i = 0; i < 8; i++) {
        buffer[9 - i] = hex_chars[value & 0xF];
        value >>= 4;
    }
}

void int_to_string(uint64_t value, char* buffer) {
    char temp[21];
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