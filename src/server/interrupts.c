#include "kernel.h"

void irq_handler(struct registers* registers) {
    if (registers->int_no >= 40) outb(0xA0, 0x20);
    outb(0x20, 0x20);
    if (registers->int_no == 32) uptime_ticks++;
    if (registers->int_no >= 40) outb(0x20, 0x20);
}

void isr_handler(struct registers* registers) {
    serial_puts("\nserver: CPU exception ");
    serial_putdec((uint32_t)registers->int_no);
    serial_puts(" at ");
    serial_puthex32((uint32_t)registers->rip);
    serial_puts("\n");
    for (;;) asm volatile("cli; hlt");
}
