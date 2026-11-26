#include "kernel.h"

mouse_state_t mouse_state;
int mouse_cursor_x = 100;
int mouse_cursor_y = 100;
static int mouse_cycle = 0;
static int mouse_packet_size = 3;
static uint8_t mouse_packet[4];
static int mouse_present = 0;

void mouse_wait(void) {
    uint32_t timeout = 100000;
    while (timeout--) {
        if ((inb(0x64) & 0x02) == 0) return;
    }
}

static int mouse_wait_data(void) {
    uint32_t timeout = 100000;
    while (timeout--) {
        if (inb(0x64) & 0x01) return 1;
    }
    return 0;
}

uint8_t mouse_read(void) {
    if (!mouse_wait_data()) return 0;
    return inb(0x60);
}

static void mouse_write(uint8_t value) {
    mouse_wait();
    outb(0x64, 0xD4);
    mouse_wait();
    outb(0x60, value);
}

static uint8_t mouse_command(uint8_t cmd) {
    mouse_write(cmd);
    return mouse_read();
}

void mouse_handler(void) {
    uint8_t status = inb(0x64);
    if (!(status & 0x20)) {

        return;
    }

    uint8_t d = inb(0x60);

    if (mouse_cycle == 0) {

        if ((d & 0x08) == 0) return;

        if (d & 0xC0) return;
    }

    mouse_packet[mouse_cycle] = d;
    mouse_cycle++;

    if (mouse_cycle == mouse_packet_size) {
        uint8_t flags = mouse_packet[0];
        int dx = (int)mouse_packet[1];
        int dy = (int)mouse_packet[2];

        if (flags & 0x10) dx -= 256;
        if (flags & 0x20) dy -= 256;

        mouse_state.buttons = flags & 0x07;
        mouse_state.dx = dx;
        mouse_state.dy = -dy;
        int wheel = 0;
        if(mouse_packet_size==4) {
            wheel=mouse_packet[3]&0x0F;
            if(wheel&0x08) wheel-=16;
        }
        mouse_state.wheel = wheel;

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

    uint64_t eflags;
    asm volatile("pushfq; popq %0; cli" : "=r"(eflags) :: "memory");

    for (int i = 0; i < 32 && (inb(0x64) & 0x01); i++) {
        (void)inb(0x60);
    }

    mouse_wait();
    outb(0x64, 0xA8);

    mouse_wait();
    outb(0x64, 0x20);
    uint8_t status = mouse_read();
    status |= 0x02;
    status &= (uint8_t)~0x20;
    mouse_wait();
    outb(0x64, 0x60);
    mouse_wait();
    outb(0x60, status);

    mouse_command(0xF6);
    mouse_packet_size=3;
    const uint8_t rates[3]={200,100,80};
    for(int i=0;i<3;i++) {
        mouse_write(0xF3); (void)mouse_read();
        mouse_write(rates[i]); (void)mouse_read();
    }
    mouse_write(0xF2); (void)mouse_read();
    uint8_t mouse_id=mouse_read();
    if(mouse_id==3 || mouse_id==4) mouse_packet_size=4;
    uint8_t ack = mouse_command(0xF4);
    mouse_present = (ack == 0xFA);

    for (int i = 0; i < 8 && (inb(0x64) & 0x01); i++) {
        (void)inb(0x60);
    }
    mouse_cycle = 0;

    if (eflags & 0x200) asm volatile("sti");
}
