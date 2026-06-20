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
        c = shift_pressed ? keyboard_map_shifted[scancode] : keyboard_map[scancode];
    }

    if (c > 0) {
        int next = (key_head + 1) % 256;
        if (next != key_tail) {
            key_buffer[key_head] = c;
            key_head = next;
        }
    }
}

char keyboard_getchar() {
    if (key_head == key_tail) return 0;
    char c = key_buffer[key_tail];
    key_tail = (key_tail + 1) % 256;
    return c;
}