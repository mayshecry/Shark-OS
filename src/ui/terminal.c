#include "kernel.h"

static void ui_init_metrics(void) {
    uint32_t w = (uint32_t)screen_width;
    uint32_t h = (uint32_t)screen_height;

    uint32_t scale_w = w / 960;
    uint32_t scale_h = h / 600;
    font_scale = scale_w > scale_h ? scale_w : scale_h;
    if (font_scale < 1) font_scale = 1;
    if (font_scale > 4) font_scale = 4;

    font_cell_w = 8 * font_scale;
    font_cell_h = 8 * font_scale;
    ui_tab_y = font_cell_h;
    ui_chrome_top = 2 * font_cell_h;
    ui_footer_h = font_cell_h;
    ui_footer_y = h - ui_footer_h;

    term_cols = w / font_cell_w;
    if (term_cols < 40) term_cols = 40;
    term_max_row = (ui_footer_y / font_cell_h) - 1;
    content_first_row = ui_chrome_top / font_cell_h;
}

uint32_t col_px(size_t col) {
    return (uint32_t)(col * font_cell_w);
}

uint32_t row_px(size_t row) {
    return (uint32_t)(row * font_cell_h);
}

void draw_string_px(const char* s, uint32_t x, uint32_t y, uint32_t fg, uint32_t bg) {
    for (size_t i = 0; s[i] != '\0'; i++) {
        draw_char(s[i], x + i * font_cell_w, y, fg, bg);
    }
}

void draw_char(char c, uint32_t x, uint32_t y, uint32_t fg, uint32_t bg) {
    if (c < 32 || c > 126) return;
    uint32_t font_idx = c - 32;
    uint32_t stride = screen_pitch / 4;
    uint32_t scale = font_scale;

    for (int row = 0; row < 8; row++) {
        uint8_t font_byte = font8x8[font_idx][row];
        for (int col = 0; col < 8; col++) {
            uint32_t color = ((font_byte >> (7 - col)) & 1) ? fg : bg;
            for (uint32_t sy = 0; sy < scale; sy++) {
                uint32_t py = y + (uint32_t)row * scale + sy;
                if (py >= screen_height) continue;
                uint32_t* row_ptr = &lfbptr[py * stride + x];
                for (uint32_t sx = 0; sx < scale; sx++) {
                    uint32_t px = (uint32_t)col * scale + sx;
                    if (x + px < screen_width) {
                        row_ptr[px] = color;
                    }
                }
            }
        }
    }
}

void draw_pixel(uint32_t x, uint32_t y, uint32_t color) {
    if (x < screen_width && y < screen_height) {
        uint32_t stride = screen_pitch / 4;
        lfbptr[y * stride + x] = color;
    }
}

void draw_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
    uint32_t stride = screen_pitch / 4;
    for (uint32_t i = 0; i < h; i++) {
        uint32_t* dest = &lfbptr[(y + i) * stride + x];
        for (uint32_t j = 0; j < w; j++) {
            dest[j] = color;
        }
    }
}

void terminal_putentryat(char c, uint8_t color, size_t x, size_t y) {
    uint32_t fg = vga_to_rgb[color & 0x0F];
    uint32_t bg = vga_to_rgb[(color >> 4) & 0x0F];
    draw_char(c, col_px(x), row_px(y), fg, bg);
}

void terminal_set_color(uint8_t color) {
    terminal_color = color;
}

void terminal_scroll() {
    uint32_t start_y_pixels = row_px(content_first_row);
    uint32_t end_y_pixels = row_px(term_max_row);
    uint32_t scroll_height_pixels = end_y_pixels - start_y_pixels;
    uint32_t stride = screen_pitch / 4;
    uint32_t px_start = col_px(panes[active_pane].col_start);
    uint32_t px_width = col_px(panes[active_pane].col_end - panes[active_pane].col_start);

    for (uint32_t y = start_y_pixels; y < start_y_pixels + scroll_height_pixels; y++) {
        memcpy(
            &lfbptr[y * stride + px_start],
            &lfbptr[(y + font_cell_h) * stride + px_start],
            px_width * sizeof(uint32_t)
        );
    }

    draw_rect(px_start, end_y_pixels, px_width, font_cell_h, UI_SURFACE);
    terminal_row = term_max_row - 1;
}

void terminal_clear(void) {
    uint32_t px_start = col_px(panes[active_pane].col_start);
    uint32_t px_width = col_px(panes[active_pane].col_end - panes[active_pane].col_start);
    uint32_t content_h = ui_footer_y - row_px(content_first_row);
    draw_rect(px_start, row_px(content_first_row), px_width, content_h, UI_SURFACE);
    terminal_row = content_first_row;
    terminal_column = panes[active_pane].col_start;
}

void terminal_write_char_internal(char c) {
    size_t pane_start = panes[active_pane].col_start;
    size_t pane_end = panes[active_pane].col_end;
    if (c == '\n') {
        terminal_column = pane_start;
        if (++terminal_row >= term_max_row) {
            terminal_scroll();
        }
        return;
    }
    if (c == '\t') {
        terminal_column = ((terminal_column - pane_start + 4) & ~3) + pane_start;
        if (terminal_column >= pane_end) {
            terminal_column = pane_start;
            if (++terminal_row >= term_max_row) {
                terminal_scroll();
            }
        }
        return;
    }
    terminal_putentryat(c, terminal_color, terminal_column, terminal_row);
    if (++terminal_column >= pane_end) {
        terminal_column = pane_start;
        if (++terminal_row >= term_max_row) {
            terminal_scroll();
        }
    }
}

void terminal_putchar_cli(char c) {
    if (c == '\n') {
    }
    if (c == '\b') {
        if (command_index > 0) {
            command_index--;
            if (terminal_column > panes[active_pane].col_start) {
                terminal_column--;
            } else if (terminal_row > content_first_row) {
                terminal_row--;
                terminal_column = panes[active_pane].col_end - 1;
            }
            draw_rect(col_px(terminal_column), row_px(terminal_row), font_cell_w, font_cell_h, UI_SURFACE);
        }
        return;
    }
    if (c == '\t') {
        return;
    }

    if (command_index < sizeof(command_buffer) - 1) {
        command_buffer[command_index++] = c;
    }
    terminal_write_char_internal(c);
}

void terminal_putchar_editor(char c) {
    if (c == '\n') {
        if (editor_buffer_idx < sizeof(editor_buffer) - 1) {
            editor_buffer[editor_buffer_idx++] = '\n';
        }
        terminal_column = panes[active_pane].col_start;
        if (++terminal_row >= term_max_row) terminal_row = content_first_row;
        return;
    }
    if (c == '\b') {
        if (editor_buffer_idx > 0) {
            editor_buffer_idx--;
        }
        if (terminal_column > panes[active_pane].col_start) {
            terminal_column--;
        } else if (terminal_row > content_first_row) {
            terminal_row--;
            terminal_column = panes[active_pane].col_end - 1;
        }
        draw_rect(col_px(terminal_column), row_px(terminal_row), font_cell_w, font_cell_h, UI_SURFACE);
        return;
    }
    if (c == '\t') {
        return;
    }

    if (editor_buffer_idx < sizeof(editor_buffer) - 1) {
        editor_buffer[editor_buffer_idx++] = c;
    }
    terminal_write_char_internal(c);
}

void terminal_putchar(char c) {
    terminal_write_char_internal(c);
}

void terminal_writestring(const char* data) {
    for (size_t i = 0; data[i] != '\0'; i++) {
        terminal_write_char_internal(data[i]);
    }
}

void terminal_initialize(void) {
    ui_init_metrics();
    pane_count = 1;
    active_pane = 0;

    panes[0].col_start = 0;
    panes[0].col_end = term_cols;
    panes[0].row = content_first_row;
    panes[0].col = 0;
    panes[0].color = vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    panes[0].cmd_index = 0;

    redraw_all_panes();
}