#include "kernel.h"

void irq_handler(struct registers* r) {
    uint32_t irq = r->int_no - 32;

    if (r->int_no == 32) {
        uptime_ticks++;
    } else if (r->int_no == 33) {
        uint8_t scancode = inb(0x60);
        keyboard_handler(scancode);
    } else if (r->int_no == 39) {
        uint8_t isr = inb(0x20);
        if (!(isr & 0x80)) return;
    } else if (r->int_no == 44 && mouse_enabled) {
        mouse_handler();
    } else if (r->int_no == 47) {
        uint8_t isr = inb(0xA0);
        if (!(isr & 0x80)) return;
    }

    if (irq >= 8) outb(0xA0, 0x20);
    outb(0x20, 0x20);
}

void syscall_handler(struct registers* r) {

    task_t* task = task_list;
    while (task) {
        task->syscall_count++;
        task = task->next;
    }

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

    /* A fault must be visible no matter what the terminal was doing. If the
     * desktop Terminal window was capturing output, the old message vanished
     * into the capture buffer and the machine looked "frozen" - drop the
     * capture and paint a panic banner straight onto the hardware
     * framebuffer (lfbptr may be the back buffer in desktop mode). */
    terminal_capture_buffer = NULL;
    terminal_capture_len = 0;
    if (hw_lfbptr && lfbptr != hw_lfbptr) lfbptr = hw_lfbptr;

    static const char* names[] = {
        "Divide by zero", "Debug", "NMI", "Breakpoint", "Overflow",
        "Bound range", "Invalid opcode", "No FPU", "Double fault",
        "Coprocessor overrun", "Invalid TSS", "Segment not present",
        "Stack fault", "General protection fault", "Page fault", "Reserved",
        "x87 FP", "Alignment check", "Machine check", "SIMD FP"
    };
    char buf[11];
    draw_rect(0, 0, (int)screen_width, 8 * (int)font_cell_h + 16, 0xFF800000);
    int y = 8;
    draw_string_px("SharkOS - CPU EXCEPTION, SYSTEM HALTED", 8, y, 0xFFFFFFFF, 0xFF800000);
    y += font_cell_h;
    draw_string_px("Vector: ", 8, y, 0xFFFFFF80, 0xFF800000);
    hex_to_string((uint32_t)r->int_no, buf);
    draw_string_px(buf, 8 + 8 * font_cell_w, y, 0xFFFFFFFF, 0xFF800000);
    if (r->int_no < 20) {
        draw_string_px(names[r->int_no], 8 + 20 * font_cell_w, y, 0xFFFFFFFF, 0xFF800000);
    }
    y += font_cell_h;
    draw_string_px("Error:  ", 8, y, 0xFFFFFF80, 0xFF800000);
    hex_to_string(r->err_code, buf);
    draw_string_px(buf, 8 + 8 * font_cell_w, y, 0xFFFFFFFF, 0xFF800000);
    y += font_cell_h;
    draw_string_px("EIP:    ", 8, y, 0xFFFFFF80, 0xFF800000);
    hex_to_string(r->eip, buf);
    draw_string_px(buf, 8 + 8 * font_cell_w, y, 0xFFFFFFFF, 0xFF800000);
    y += font_cell_h;
    draw_string_px("ESP:    ", 8, y, 0xFFFFFF80, 0xFF800000);
    hex_to_string(r->esp, buf);
    draw_string_px(buf, 8 + 8 * font_cell_w, y, 0xFFFFFFFF, 0xFF800000);
    if (r->int_no == 14) {
        uint32_t cr2;
        asm volatile("mov %%cr2, %0" : "=r"(cr2));
        y += font_cell_h;
        draw_string_px("CR2:    ", 8, y, 0xFFFFFF80, 0xFF800000);
        hex_to_string(cr2, buf);
        draw_string_px(buf, 8 + 8 * font_cell_w, y, 0xFFFFFFFF, 0xFF800000);
    }
    y += font_cell_h * 2;
    draw_string_px("Power off or reset the machine.", 8, y, 0xFFFFFFFF, 0xFF800000);

    while(1) { asm volatile("cli; hlt"); }
}