#include "kernel.h"

mouse_state_t mouse_state;
int mouse_cursor_x = 100;
int mouse_cursor_y = 100;
static int mouse_cycle = 0;
static uint8_t mouse_packet[4];

void mouse_wait(void) {
    uint32_t timeout = 10000;
    while (timeout--) {
        if ((inb(0x64) & 0x02) == 0) return;
    }
}

uint8_t mouse_read(void) {
    mouse_wait();
    return inb(0x60);
}

void mouse_handler(void) {
    uint8_t status = inb(0x64);
    if (!(status & 0x20)) return;

    uint8_t d = inb(0x60);
    if (mouse_cycle == 0 && (d & 0x08) == 0) return;

    mouse_packet[mouse_cycle] = d;
    mouse_cycle++;

    if (mouse_cycle == 3) {
        uint8_t flags = mouse_packet[0];
        int dx = (int)mouse_packet[1];
        int dy = (int)mouse_packet[2];

        if (flags & 0x10) dx -= 256;
        if (flags & 0x20) dy -= 256;

        mouse_state.buttons = flags & 0x07;
        mouse_state.dx = dx;
        mouse_state.dy = -dy;
        mouse_state.wheel = 0;

        mouse_cursor_x += dx;
        mouse_cursor_y -= dy;

        if (mouse_cursor_x < 0) mouse_cursor_x = 0;
        if (mouse_cursor_y < 0) mouse_cursor_y = 0;
        if ((uint32_t)mouse_cursor_x >= screen_width) mouse_cursor_x = (int)screen_width - 1;
        if ((uint32_t)mouse_cursor_y >= screen_height) mouse_cursor_y = (int)screen_height - 1;

        mouse_state.x = mouse_cursor_x;
        mouse_state.y = mouse_cursor_y;

        mouse_cycle = 0;
    }
}

void mouse_init(void) {
    mouse_state.x = mouse_cursor_x;
    mouse_state.y = mouse_cursor_y;
    mouse_state.buttons = 0;
    mouse_state.dx = 0;
    mouse_state.dy = 0;
    mouse_cycle = 0;

    mouse_wait();
    outb(0x64, 0xA8);

    mouse_wait();
    outb(0x64, 0x20);
    uint8_t status = inb(0x60);
    status |= 0x02;
    mouse_wait();
    outb(0x64, 0x60);
    outb(0x60, status);

    mouse_wait();
    outb(0x64, 0xD4);
    mouse_wait();
    outb(0x60, 0xF4);
    mouse_read();
}