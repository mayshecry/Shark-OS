#include "kernel.h"

void ui_init_metrics(void) {
    uint32_t w = (uint32_t)screen_width;
    uint32_t h = (uint32_t)screen_height;
    font_scale = (w >= 1280) ? 2 : 1;
    if (font_scale < 1) font_scale = 1;
    if (font_scale > 3) font_scale = 3;

    font_cell_w = 8 * font_scale;
    font_cell_h = 8 * font_scale;
    ui_tab_y = font_cell_h + 8;
    ui_chrome_top = ui_tab_y + font_cell_h;
    ui_footer_h = font_cell_h + 4;
    ui_footer_y = h - ui_footer_h;

    term_cols = w / font_cell_w;
    if (term_cols < 20) term_cols = 20;
    if (term_cols > 160) term_cols = 160;
    term_max_row = (ui_footer_y / font_cell_h) - 1;
    if (term_max_row < 3) term_max_row = 3;
    content_first_row = ui_chrome_top / font_cell_h;
}

uint32_t col_px(size_t col) {
    return (uint32_t)(col * font_cell_w);
}

uint32_t row_px(size_t row) {
    return (uint32_t)(row * font_cell_h);
}

void draw_string_px(const char* s, int x, int y, uint32_t fg, uint32_t bg) {
    for (size_t i = 0; s[i] != '\0'; i++) {
        draw_char(s[i], x + (int)(i * font_cell_w), y, fg, bg);
    }
}

/* draw_char / draw_pixel / draw_rect take signed coordinates and clip on all
 * four edges. The previous uint32_t versions only tested the right/bottom
 * edge, so a negative x or y wrapped to a huge value, slipped past the
 * `x + px < screen_width` test and wrote outside the framebuffer. */
void draw_char(char c, int x, int y, uint32_t fg, uint32_t bg) {
    if (c < 32 || c > 126) return;

    uint32_t font_idx = (uint32_t)(c - 32);
    uint32_t stride = screen_pitch / 4;
    int scale = (int)font_scale;
    if (scale < 1) scale = 1;

    int sw = (int)screen_width;
    int sh = (int)screen_height;
    int cell = 8 * scale;

    /* Reject cells entirely off screen before touching the framebuffer. */
    if (x >= sw || y >= sh || x + cell <= 0 || y + cell <= 0) return;

    for (int row = 0; row < 8; row++) {
        uint8_t font_byte = font8x8[font_idx][row];
        int py0 = y + row * scale;
        if (py0 < 0 || py0 >= sh) continue;
        int rows = scale;
        if (py0 + rows > sh) rows = sh - py0;
        for (int sy = 0; sy < rows; sy++) {
            uint32_t* row_ptr = &lfbptr[(uint32_t)(py0 + sy) * stride];
            for (int col = 0; col < 8; col++) {
                uint32_t color = ((font_byte >> (7 - col)) & 1) ? fg : bg;
                int px = x + col * scale;
                if (px < 0 || px >= sw) continue;
                int cols = scale;
                if (px + cols > sw) cols = sw - px;
                for (int sx = 0; sx < cols; sx++) {
                    row_ptr[px + sx] = color;
                }
            }
        }
    }
}

void draw_pixel(int x, int y, uint32_t color) {
    if (x < 0 || y < 0) return;
    if ((uint32_t)x >= screen_width || (uint32_t)y >= screen_height) return;
    uint32_t stride = screen_pitch / 4;
    lfbptr[(uint32_t)y * stride + (uint32_t)x] = color;
}

void draw_rect(int x, int y, int w, int h, uint32_t color) {
    if (w <= 0 || h <= 0) return;

    int sw = (int)screen_width;
    int sh = (int)screen_height;

    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + w; if (x1 > sw) x1 = sw;
    int y1 = y + h; if (y1 > sh) y1 = sh;
    if (x0 >= x1 || y0 >= y1) return;

    uint32_t stride = screen_pitch / 4;
    int run = x1 - x0;
    for (int py = y0; py < y1; py++) {
        fill32(&lfbptr[(uint32_t)py * stride + (uint32_t)x0], color, (size_t)run);
    }
}

/* ------------------------------------------------------------ capture mode
 *
 * The desktop Terminal window runs shell commands with terminal_capture_buffer
 * pointing at a text buffer. While that is set, nothing here may touch the
 * framebuffer or the CLI cursor position: the shell pane metrics are stale in
 * desktop mode and every stray draw_char() landed somewhere in the back
 * buffer. Previously only terminal_writestring() honoured the capture, while
 * terminal_putchar(), terminal_write_direct(), terminal_clear() and
 * terminal_scroll() drew straight onto the desktop composite. */
int terminal_capture_cap = 0;
bool terminal_capture_cleared = false;

static inline bool terminal_capturing(void) {
    return terminal_capture_buffer != NULL;
}

static void terminal_capture_char(char c) {
    if (!terminal_capture_buffer) return;
    /* Always leave room for the terminating NUL. */
    if (terminal_capture_len < terminal_capture_cap - 1) {
        terminal_capture_buffer[terminal_capture_len++] = c;
        terminal_capture_buffer[terminal_capture_len] = '\0';
    }
}

void terminal_capture_begin(char* buf, int cap) {
    terminal_capture_buffer = buf;
    terminal_capture_cap = cap;
    terminal_capture_len = 0;
    terminal_capture_cleared = false;
    if (buf && cap > 0) buf[0] = '\0';
}

void terminal_capture_end(void) {
    if (terminal_capture_buffer && terminal_capture_cap > 0) {
        if (terminal_capture_len >= terminal_capture_cap) {
            terminal_capture_len = terminal_capture_cap - 1;
        }
        terminal_capture_buffer[terminal_capture_len] = '\0';
    }
    terminal_capture_buffer = NULL;
    terminal_capture_cap = 0;
}

/* Minimal ANSI SGR filter. Several commands (neofetch, sysinfo, ...) emit
 * "\033[0;36m" style colour codes which used to be printed literally as
 * "[0;36m". The sequence is swallowed and, in CLI mode, mapped onto the VGA
 * palette. Returns true when the character was consumed by the parser. */
static bool terminal_ansi_filter(char c) {
    static int state = 0;      /* 0 = idle, 1 = got ESC, 2 = inside CSI */
    static int param = 0;
    static int have_param = 0;

    if (state == 0) {
        if (c == 27) { state = 1; return true; }
        return false;
    }
    if (state == 1) {
        if (c == '[') { state = 2; param = 0; have_param = 0; return true; }
        state = 0;
        return false;
    }
    /* state == 2: parameters until a final byte (0x40..0x7E). */
    if (c >= '0' && c <= '9') { param = param * 10 + (c - '0'); have_param = 1; return true; }
    if (c == ';') {
        if (have_param) {
            if (param == 0) terminal_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
        }
        param = 0; have_param = 0;
        return true;
    }
    if (c == 'm') {
        static const uint8_t ansi_to_vga[8] = {
            VGA_COLOR_BLACK, VGA_COLOR_RED, VGA_COLOR_GREEN, VGA_COLOR_BROWN,
            VGA_COLOR_BLUE, VGA_COLOR_MAGENTA, VGA_COLOR_CYAN, VGA_COLOR_LIGHT_GREY
        };
        if (have_param) {
            uint8_t bg = (terminal_color >> 4) & 0x0F;
            if (param == 0) terminal_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
            else if (param >= 30 && param <= 37) terminal_set_color((uint8_t)(ansi_to_vga[param - 30] | (bg << 4)));
            else if (param >= 90 && param <= 97) terminal_set_color((uint8_t)((ansi_to_vga[param - 90] | 0x08) | (bg << 4)));
        }
        state = 0;
        return true;
    }
    if (c >= 0x40 && c <= 0x7E) { state = 0; return true; }   /* other CSI */
    if ((unsigned char)c < 0x20) { state = 0; return false; }
    return true;
}

void terminal_putentryat(char c, uint8_t color, size_t x, size_t y) {
    if (terminal_capturing()) return;
    uint32_t fg = vga_to_rgb[color & 0x0F];
    uint32_t bg = vga_to_rgb[(color >> 4) & 0x0F];
    draw_char(c, col_px(x), row_px(y), fg, bg);
}

void terminal_set_color(uint8_t color) {
    terminal_color = lite_mode ? vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK) : color;
}

void terminal_scroll() {
    if (terminal_capturing()) {
        /* Captured output never scrolls the screen; the window paints it. */
        terminal_row = term_max_row - 1;
        return;
    }
    uint32_t start_y_pixels = row_px(content_first_row);
    uint32_t end_y_pixels = row_px(term_max_row);
    uint32_t stride = screen_pitch / 4;
    uint32_t px_start = col_px(panes[active_pane].col_start);
    uint32_t px_width = col_px(panes[active_pane].col_end - panes[active_pane].col_start);

    /* The source row (y + font_cell_h) must stay inside the framebuffer;
     * on screen heights that are not a multiple of the cell height the
     * old loop read one cell past the end of video memory. */
    uint32_t max_y = (uint32_t)screen_height;
    if (px_start + px_width > (uint32_t)screen_width) {
        px_width = (px_start < (uint32_t)screen_width) ? (uint32_t)screen_width - px_start : 0;
    }
    for (uint32_t y = start_y_pixels; y < end_y_pixels; y++) {
        uint32_t src_y = y + font_cell_h;
        if (src_y >= max_y || y >= max_y) break;
        memcpy(
            &lfbptr[y * stride + px_start],
            &lfbptr[src_y * stride + px_start],
            px_width * sizeof(uint32_t)
        );
    }

    draw_rect(px_start, end_y_pixels, px_width, font_cell_h, UI_SURFACE);
    terminal_row = term_max_row - 1;
}

void terminal_clear(void) {
    if (terminal_capturing()) {
        /* `clear` inside the desktop Terminal: drop what was captured so far
         * and tell the window to wipe its scrollback. */
        terminal_capture_len = 0;
        if (terminal_capture_cap > 0) terminal_capture_buffer[0] = '\0';
        terminal_capture_cleared = true;
        return;
    }
    uint32_t px_start = col_px(panes[active_pane].col_start);
    uint32_t px_width = col_px(panes[active_pane].col_end - panes[active_pane].col_start);
    uint32_t content_h = ui_footer_y - row_px(content_first_row);
    draw_rect(px_start, row_px(content_first_row), px_width, content_h, UI_SURFACE);
    terminal_row = content_first_row;
    terminal_column = panes[active_pane].col_start;
}

void terminal_write_char_internal(char c) {
    if (terminal_ansi_filter(c)) return;
    if (terminal_capturing()) {
        terminal_capture_char(c);
        return;
    }
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

void terminal_write_direct(const char* data) {
    if (terminal_capturing()) {
        for (size_t i = 0; data[i] != '\0'; i++) {
            if (!terminal_ansi_filter(data[i])) terminal_capture_char(data[i]);
        }
        return;
    }
    for (size_t i = 0; data[i] != '\0'; i++) {
        char c = data[i];
        if (c == '\n') {
            size_t pane_start = panes[active_pane].col_start;
            terminal_column = pane_start;
            if (++terminal_row >= term_max_row) {
                terminal_scroll();
            }
            continue;
        }
        terminal_putentryat(c, terminal_color, terminal_column, terminal_row);
        size_t pane_end = panes[active_pane].col_end;
        if (++terminal_column >= pane_end) {
            terminal_column = panes[active_pane].col_start;
            if (++terminal_row >= term_max_row) {
                terminal_scroll();
            }
        }
    }
}

void draw_cursor(void) {
    if (terminal_capturing()) return;
    uint32_t x = col_px(terminal_column);
    uint32_t y = row_px(terminal_row);
    uint32_t stride = screen_pitch / 4;

    for (uint32_t sy = 0; sy < font_cell_h; sy++) {
        uint32_t py = y + sy;
        if (py >= screen_height) continue;
        uint32_t* row_ptr = &lfbptr[py * stride + x];
        for (uint32_t sx = 0; sx < font_cell_w; sx++) {
            if (x + sx < screen_width) {
                row_ptr[sx] = 0xFF00FF66;
            }
        }
    }
}

void terminal_putchar_cli(char c) {
    if (c == '\n') {
        terminal_write_char_internal('\n');
        draw_cursor();
        return;
    }
    if (c == '\b') {
        if (command_index > 0) {
            command_index--;
            draw_rect(col_px(terminal_column), row_px(terminal_row), font_cell_w, font_cell_h, UI_SURFACE);
            if (terminal_column > panes[active_pane].col_start) {
                terminal_column--;
            } else if (terminal_row > content_first_row) {
                terminal_row--;
                terminal_column = panes[active_pane].col_end - 1;
            }
            draw_rect(col_px(terminal_column), row_px(terminal_row), font_cell_w, font_cell_h, UI_SURFACE);
        } else {
            draw_rect(col_px(terminal_column), row_px(terminal_row), font_cell_w, font_cell_h, UI_SURFACE);
        }
        draw_cursor();
        return;
    }
    if (c == '\t') {
        return;
    }

    if (command_index < sizeof(command_buffer) - 1) {
        command_buffer[command_index++] = c;
    }
    terminal_write_char_internal(c);
    draw_cursor();
}

void terminal_putchar_editor(char c) {
    if (c == '\n') {
        if (editor_buffer_idx < sizeof(editor_buffer) - 1) {
            editor_buffer[editor_buffer_idx++] = '\n';
        }
        terminal_column = panes[active_pane].col_start;
        if (++terminal_row >= term_max_row) {
            terminal_scroll();
        }
        return;
    }
    if (c == '\b') {
        draw_rect(col_px(terminal_column), row_px(terminal_row), font_cell_w, font_cell_h, UI_SURFACE);
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
        draw_cursor();
        return;
    }
    if (c == '\t') {
        return;
    }

    if (editor_buffer_idx < sizeof(editor_buffer) - 1) {
        editor_buffer[editor_buffer_idx++] = c;
    }
    terminal_write_char_internal(c);
    draw_cursor();
}

void terminal_putchar(char c) {
    terminal_write_char_internal(c);
}

void scrollback_save_line(const char* text, uint8_t color) {
    if (scrollback.count >= SCROLLBACK_LINES) {
        scrollback.top = (scrollback.top + 1) % SCROLLBACK_LINES;
        scrollback.count--;
    }
    int idx = (scrollback.top + scrollback.count) % SCROLLBACK_LINES;
    int len = 0;
    while (text[len] != '\0' && len < SCROLLBACK_COLS - 1) {
        scrollback.lines[idx][len] = text[len];
        scrollback.colors[idx][len] = color;
        len++;
    }
    scrollback.lines[idx][len] = '\0';
    scrollback.colors[idx][len] = color;
    scrollback.count++;
}

void terminal_writestring(const char* data) {
    static char line_buf[SCROLLBACK_COLS];
    static int line_len = 0;
    static uint8_t line_color = VGA_COLOR_WHITE;
    line_len = 0;
    line_color = terminal_color;

    for (size_t i = 0; data[i] != '\0'; i++) {
        if (data[i] == '\n') {
            line_buf[line_len] = '\0';
            if (line_len > 0) {
                scrollback_save_line(line_buf, line_color);
            }
            line_len = 0;
            line_color = terminal_color;
        } else {
            if (line_len < SCROLLBACK_COLS - 1) {
                line_buf[line_len++] = data[i];
            }
        }
        
        
        /* Capture-aware: appends to terminal_capture_buffer in the desktop
         * Terminal, draws to the CLI pane otherwise. */
        terminal_write_char_internal(data[i]);
    }
}

void terminal_draw_scrollback(void) {
    if (scrollback_offset == 0) return;

    uint32_t px_start = col_px(panes[active_pane].col_start);
    uint32_t px_width = col_px(panes[active_pane].col_end - panes[active_pane].col_start);
    uint32_t content_h = ui_footer_y - row_px(content_first_row);
    draw_rect(px_start, row_px(content_first_row), px_width, content_h, UI_SURFACE);

    int visible_rows = term_max_row - content_first_row;
    int start_line = scrollback.count - scrollback_offset - visible_rows + 1;
    if (start_line < 0) start_line = 0;

    int row = content_first_row;
    for (int i = start_line; i < scrollback.count && row < (int)term_max_row; i++) {
        int idx = (scrollback.top + i) % SCROLLBACK_LINES;
        for (int c = 0; c < SCROLLBACK_COLS && scrollback.lines[idx][c] != '\0'; c++) {
            if (c >= (int)(panes[active_pane].col_end - panes[active_pane].col_start)) break;
            uint8_t col = scrollback.colors[idx][c];
            terminal_putentryat(scrollback.lines[idx][c], col, panes[active_pane].col_start + c, row);
        }
        row++;
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
    panes[0].prompt_end_col = 0;

    redraw_all_panes();
    ui_draw_footer();
}
