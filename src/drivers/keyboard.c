#include "kernel.h"

static char key_buffer[256];
static volatile int key_head = 0;
static volatile int key_tail = 0;
static uint8_t extended_scancode = 0;

void keyboard_handler(uint8_t scancode) {
    if (scancode == 0xE0) {
        extended_scancode = 1;
        return;
    }

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
