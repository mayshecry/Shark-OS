#include "kernel.h"
#include "desktop.h"

extern void print_prompt(void);

static void cli_put_char(char c) {
    terminal_color = vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    if (command_index < sizeof(command_buffer) - 1) {
        command_buffer[command_index++] = c;
        command_buffer[command_index] = '\0';
    }
    terminal_putentryat(c, terminal_color, terminal_column, terminal_row);
    if (++terminal_column >= panes[active_pane].col_end) {
        terminal_column = panes[active_pane].col_start;
        if (++terminal_row >= term_max_row) terminal_scroll();
    }
    draw_cursor();
}

static void cli_backspace(void) {
    if (command_index == 0) return;
    command_index--;
    command_buffer[command_index] = '\0';
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

static void cli_submit(void) {
    command_buffer[command_index] = '\0';
    execute_command(command_buffer);
    command_index = 0;
    command_buffer[0] = '\0';
    terminal_writestring("\n");
    print_prompt();
}

static void cli_paste(void) {
    const char* text = clipboard_get();
    int len = clipboard_length();
    for (int i = 0; i < len; i++) {
        char ch = text[i];
        if (ch == '\n') break;
        if (ch < 32 || ch >= 127) continue;
        cli_put_char(ch);
    }
    command_buffer[command_index] = '\0';
}

static void cli_return_to_desktop(void) {
    current_kernel_mode = KERNEL_MODE_DESKTOP;
    desktop.dirty = true;
    desktop_render();
}

void cli_handle_key(char c) {
    if (c == KEY_SUPER) return;

    if (c == 27 && ctrl_pressed) {
        cli_return_to_desktop();
        return;
    }

    if (ctrl_pressed) {
        if (c == 'c' || c == 'C') {
            command_buffer[command_index] = '\0';
            clipboard_set_len(command_buffer, (int)command_index);
            return;
        }
        if (c == 'v' || c == 'V') {
            cli_paste();
            return;
        }
        if (c == 'l' || c == 'L') {
            terminal_clear();
            redraw_all_panes();
            print_prompt();
            return;
        }
    }

    if (c == '\n' || c == '\r') {
        cli_submit();
        return;
    }
    if (c == '\b') {
        cli_backspace();
        return;
    }
    if (c >= 32 && c < 127) {
        cli_put_char(c);
    }
}
