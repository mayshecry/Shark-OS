#include "kernel.h"

void lite_kmain(void) {
    terminal_color = vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    terminal_writestring("SharkOS Lite\n");
    tiling_enabled = false;
    mouse_enabled = false;
    init_descriptor_tables();
    outb(0x21, inb(0x21) & ~0x02);
    outb(0x43, 0x36);
    outb(0x40, 0x9C);
    outb(0x40, 0x2E);
    asm volatile("sti");
    fs_initialize();
    terminal_clear();
    pane_count = 1;
    active_pane = 0;
    panes[0].col_start = 0;
    panes[0].col_end = term_cols;
    panes[0].row = content_first_row;
    panes[0].col = 0;
    panes[0].color = vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    panes[0].cmd_index = 0;
    terminal_row = content_first_row;
    terminal_column = 0;
    terminal_writestring(current_dir->name);
    terminal_writestring(" -# ");
    panes[active_pane].prompt_end_col = terminal_column;
    draw_cursor();

    while (1) {
        char c = keyboard_getchar();
        if (c == 0) continue;

        if (c == '\n') {
            command_buffer[command_index] = '\0';
            execute_command(command_buffer);
            command_index = 0;
            command_buffer[0] = '\0';
            terminal_writestring("\n");
            terminal_writestring(current_dir->name);
            terminal_writestring(" -# ");
            panes[active_pane].prompt_end_col = terminal_column;
            draw_cursor();
        } else if (c == '\b') {
            if (command_index > 0) {
                command_index--;
                size_t old_col = terminal_column;
                size_t old_row = terminal_row;
                if (terminal_column > panes[active_pane].col_start) {
                    terminal_column--;
                } else if (terminal_row > content_first_row) {
                    terminal_row--;
                    terminal_column = panes[active_pane].col_end - 1;
                }
                draw_rect(col_px(old_col), row_px(old_row), font_cell_w, font_cell_h, UI_SURFACE);
                draw_rect(col_px(terminal_column), row_px(terminal_row), font_cell_w, font_cell_h, UI_SURFACE);
                draw_cursor();
            }
        } else if (c >= 32 && c < 127) {
            terminal_color = vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
            if (command_index < sizeof(command_buffer) - 1) {
                command_buffer[command_index++] = c;
            }
            terminal_putentryat(c, terminal_color, terminal_column, terminal_row);
            if (++terminal_column >= panes[active_pane].col_end) {
                terminal_column = panes[active_pane].col_start;
                if (++terminal_row >= term_max_row) {
                    terminal_scroll();
                }
            }
            draw_cursor();
        }
    }
}