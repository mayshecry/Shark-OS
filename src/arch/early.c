#include "kernel.h"

/* Pre-video diagnostics: COM1 serial + PC speaker + fail-stop panic.
 *
 * Why this exists: on some machines (notably Ryzen desktops with a
 * discrete GPU and "Above 4G Decoding" enabled) GRUB hands us a
 * framebuffer we cannot use - mapped above 4 GB, or not passed at
 * all. The old code trusted the multiboot struct blindly and the
 * first screen write scribbled over random RAM with no IDT installed
 * yet -> triple fault -> instant reboot with zero information.
 * The validation in kmain() now calls early_panic() instead, which
 * reports over serial, beeps a code on the PC speaker, and halts. */

#define COM1_BASE 0x3F8

void serial_init(void) {
    outb(COM1_BASE + 1, 0x00); /* disable IRQs */
    outb(COM1_BASE + 3, 0x80); /* DLAB on */
    outb(COM1_BASE + 0, 0x01); /* 115200 baud */
    outb(COM1_BASE + 1, 0x00);
    outb(COM1_BASE + 3, 0x03); /* 8N1 */
    outb(COM1_BASE + 2, 0xC7); /* FIFO on */
    outb(COM1_BASE + 4, 0x0B); /* RTS/DTR */
}

static int serial_tx_ready(void) {
    return inb(COM1_BASE + 5) & 0x20;
}

void serial_putc(char c) {
    /* No UART -> reads return 0xFF -> TX-empty -> never hangs. */
    for (int i = 0; i < 100000 && !serial_tx_ready(); i++) { }
    if (!serial_tx_ready()) return;
    outb(COM1_BASE, (uint8_t)c);
}

void serial_puts(const char* s) {
    while (*s) {
        if (*s == '\n') serial_putc('\r');
        serial_putc(*s++);
    }
}

void serial_puthex32(uint32_t v) {
    static const char* h = "0123456789ABCDEF";
    serial_puts("0x");
    for (int i = 7; i >= 0; i--) serial_putc(h[(v >> (i * 4)) & 0xF]);
}

void serial_putdec(uint32_t v) {
    char t[12];
    int_to_string(v, t);
    serial_puts(t);
}

/* Crude busy-wait (no timer this early). Duration varies with CPU
   speed, but that doesn't matter for diagnostic beeps. */
static void early_delay(void) {
    for (volatile unsigned i = 0; i < 4000000u; i++) {
        asm volatile("" ::: "memory");
    }
}

static void speaker_on(void) {
    uint16_t div = 1193182 / 880; /* A5 */
    outb(0x43, 0xB6);             /* PIT ch2, square wave */
    outb(0x42, div & 0xFF);
    outb(0x42, (div >> 8) & 0xFF);
    outb(0x61, inb(0x61) | 0x03);
}

static void speaker_off(void) {
    outb(0x61, inb(0x61) & (uint8_t)~0x03);
}

void speaker_beeps(int n) {
    if (n <= 0 || n > 9) return;
    for (int i = 0; i < n; i++) {
        speaker_on();
        early_delay();
        speaker_off();
        early_delay();
    }
}

int early_panic_code = -1;

void early_panic(int code, const char* msg) {
    early_panic_code = code;
    asm volatile("cli");
    serial_puts("\nSHARKOS EARLY PANIC [code ");
    serial_putdec((uint32_t)code);
    serial_puts("]: ");
    serial_puts(msg);
    serial_puts("\nSystem halted.\n");
    if (code > 0) speaker_beeps(code);
    /* Infinite halt loop: a single HLT could be woken by NMI. */
    for (;;) asm volatile("cli; hlt");
}
