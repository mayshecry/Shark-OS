#include "kernel.h"

static char key_buffer[256];
static volatile int key_head = 0;
static volatile int key_tail = 0;
static uint8_t extended_scancode = 0;

extern void sharkapi_dispatch_key_event(char key, uint8_t scancode);

void keyboard_handler(uint8_t scancode) {
    if (scancode == 0xE0) {
        extended_scancode = 1;
        return;
    }

    uint8_t base = scancode & 0x7F;
    char mapped = 0;
    if (extended_scancode) {
        if (base == 0x48) mapped = 0x10;
        else if (base == 0x50) mapped = 0x11;
        else if (base == 0x4B) mapped = 0x12;
        else if (base == 0x4D) mapped = 0x13;
    } else if (base < 128) {
        mapped = (char)keyboard_map[base];
    }
    sharkapi_dispatch_key_event(mapped, scancode);

    if (scancode == 0x2A || scancode == 0x36) {
        shift_pressed = true;
        return;
    } else if (scancode == 0xAA || scancode == 0xB6) {
        shift_pressed = false;
        return;
    } else if (scancode == 0x1D) {
        ctrl_pressed = true;
        return;
    } else if (scancode == 0x9D) {
        ctrl_pressed = false;
        return;
    }

    if (scancode & 0x80) {
        extended_scancode = 0;
        return;
    }

    char c = 0;
    if (extended_scancode) {
        if (scancode == 0x48) c = 0x10;
        else if (scancode == 0x50) c = 0x11;
        else if (scancode == 0x4B) c = 0x12;
        else if (scancode == 0x4D) c = 0x13;
        extended_scancode = 0;
    } else {
        if (scancode < 128) {
            c = shift_pressed ? keyboard_map_shifted[scancode] : keyboard_map[scancode];
        } else {
            c = 0;
        }
    }

    if (c > 0) {
        int next = (key_head + 1) % 256;
        if (next != key_tail) {
            key_buffer[key_head] = c;
            key_head = next;
        }
    }
    outb(0x20, 0x20);
}

char keyboard_getchar() {
    int head = key_head;
    int tail = key_tail;
    if (head == tail) return 0;
    char c = key_buffer[tail];
    key_tail = (tail + 1) % 256;
    return c;
}
