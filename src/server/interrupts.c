#include "kernel.h"

void irq_handler(struct registers* registers) {
    if (registers->int_no >= 40) outb(0xA0, 0x20);
    outb(0x20, 0x20);
    if (registers->int_no == 32) {
        uptime_ticks++;
        if (server_idle_waiting) server_idle_ticks++;
    }
    if (registers->int_no >= 40) outb(0x20, 0x20);
}

void isr_handler(struct registers* registers) {
    serial_puts("\nserver: CPU exception ");
    serial_putdec((uint32_t)registers->int_no);
    serial_puts(" at ");
#ifdef SHARKOS_32BIT
    serial_puthex32(registers->eip);
    serial_puts(" error=");
    serial_puthex32(registers->err_code);
    serial_puts(" dst=");
    serial_puthex32(registers->edi);
    serial_puts(" src=");
    serial_puthex32(registers->esi);
#else
    serial_puthex32((uint32_t)registers->rip);
    serial_puts(" error=");
    serial_puthex32((uint32_t)registers->err_code);
    serial_puts(" dst=");
    serial_puthex32((uint32_t)registers->rdi);
    serial_puts(" src=");
    serial_puthex32((uint32_t)registers->rsi);
#endif
    serial_puts("\n");
    for (;;) asm volatile("cli; hlt");
}
