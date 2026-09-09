#include "kernel.h"

mouse_state_t mouse_state;
int mouse_cursor_x = 100;
int mouse_cursor_y = 100;
static int mouse_cycle = 0;
static uint8_t mouse_packet[4];
static int mouse_present = 0;

/* Wait until the controller can accept a byte (input buffer empty). */
void mouse_wait(void) {
    uint32_t timeout = 100000;
    while (timeout--) {
        if ((inb(0x64) & 0x02) == 0) return;
    }
}

/* Wait until a byte is available (output buffer full). Returns 0 on timeout
 * so a missing mouse never hangs the boot. */
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

/* Send a command to the mouse and swallow the 0xFA acknowledge. Returns the
 * ACK byte (or 0 on timeout). Leaving the ACK in the output buffer was the
 * old driver's bug: the first IRQ12 after boot delivered 0xFA as "byte 0"
 * of a packet and every packet after that was parsed one byte out of phase
 * until a byte without bit 3 happened to resynchronise it. */
static uint8_t mouse_command(uint8_t cmd) {
    mouse_write(cmd);
    return mouse_read();
}

void mouse_handler(void) {
    uint8_t status = inb(0x64);
    if (!(status & 0x20)) {
        /* Not mouse data: leave it for the keyboard handler. */
        return;
    }

    uint8_t d = inb(0x60);

    if (mouse_cycle == 0) {
        /* Byte 0 always has bit 3 set. Anything else (ACK 0xFA, self-test
         * 0xAA, ID 0x00, or a leftover byte after a hot-plug) is garbage,
         * so drop it and stay aligned. */
        if ((d & 0x08) == 0) return;
        /* Overflow bits set means the packet is unreliable. */
        if (d & 0xC0) return;
    }

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

    /* The controller is configured by polling. Interrupts are already on at
     * this point in kmain(), and with IRQ1/IRQ12 live the IRQ handlers race
     * us for every response byte (the command byte, the ACKs). Losing the
     * command-byte read here is what used to write a config with keyboard
     * interrupts disabled or leave a 0xFA ACK to be parsed as a packet. */
    uint32_t eflags;
    asm volatile("pushf; pop %0; cli" : "=r"(eflags) :: "memory");

    /* Flush anything left in the controller output buffer. */
    for (int i = 0; i < 32 && (inb(0x64) & 0x01); i++) {
        (void)inb(0x60);
    }

    /* Enable the auxiliary (mouse) port. */
    mouse_wait();
    outb(0x64, 0xA8);

    /* Enable IRQ12 in the controller command byte (and keep the keyboard
     * translation bits as they are). */
    mouse_wait();
    outb(0x64, 0x20);
    uint8_t status = mouse_read();
    status |= 0x02;          /* enable IRQ12 */
    status &= (uint8_t)~0x20; /* make sure the mouse clock is not disabled */
    mouse_wait();
    outb(0x64, 0x60);
    mouse_wait();
    outb(0x60, status);

    /* Reset to defaults, then enable streaming. Both commands acknowledge
     * with 0xFA, which is consumed here instead of leaking into the IRQ
     * handler as a bogus packet byte. */
    mouse_command(0xF6);
    uint8_t ack = mouse_command(0xF4);
    mouse_present = (ack == 0xFA);

    /* Drain any stray bytes (e.g. a reset self-test result) so the first
     * real packet starts on a clean boundary. */
    for (int i = 0; i < 8 && (inb(0x64) & 0x01); i++) {
        (void)inb(0x60);
    }
    mouse_cycle = 0;

    if (eflags & 0x200) asm volatile("sti");
}
