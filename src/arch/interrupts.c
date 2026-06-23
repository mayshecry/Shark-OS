#include "kernel.h"

void irq_handler(struct registers* r) {
    if (r->int_no >= 40) outb(0xA0, 0x20);
    outb(0x20, 0x20);

    if (rtl_io_base != 0 && r->int_no == (uint32_t)(rtl_irq + 32)) {
        uint16_t status = inw(rtl_io_base + 0x3E);
        outw(rtl_io_base + 0x3E, status);
        if (status & 0x01) {
            terminal_writestring("\n[Network] Packet Received!");
        }
    }

    if (r->int_no == 33) {
        uint8_t scancode = inb(0x60);
        keyboard_handler(scancode);
    }
    if (r->int_no == 44 && mouse_enabled) {
        mouse_handler();
    }
    if (r->int_no == 32) {
        uptime_ticks++;
    }
}

void syscall_handler(struct registers* r) {
    if (r->eax == 1) {
        terminal_writestring("\nProcess exited.\n");
    } else if (r->eax == 3) {
        char* name = (char*)r->ebx;
        char* buf = (char*)r->ecx;
        for (int i = 0; i < pool_index; i++) {
            if (strcmp(node_pool[i].name, name) == 0 && node_pool[i].type == FS_FILE) {
                strcpy(buf, node_pool[i].content);
                r->eax = (uint32_t)node_pool[i].content_len;
                return;
            }
        }
        r->eax = (uint32_t)-1;
    } else if (r->eax == 4) {
        const char* buf = (const char*)r->ecx;
        uint32_t len = r->edx;
        for (uint32_t i = 0; i < len; i++) {
            terminal_putchar(buf[i]);
        }
    } else if (r->eax == 24) {
        yield();
    }
}

void isr_handler(struct registers* r) {
    if (r->int_no == 0x80) {
        syscall_handler(r);
        return;
    }

    char buf[11];
    terminal_writestring("\nCPU EXCEPTION: ");
    hex_to_string((uint32_t)r->int_no, buf);
    terminal_writestring(buf);
    terminal_writestring(". SYSTEM HALTED.");

    while(1) { asm volatile("hlt"); }
}