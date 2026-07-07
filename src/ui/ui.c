#include "kernel.h"
#include <stdio.h>

// RTC reading
static uint8_t rtc_read(uint8_t reg) {
    outb(0x70, reg);
    return inb(0x71);
}

static void rtc_get_datetime(char* buf, int buflen) {
    (void)buflen;
    uint8_t sec = rtc_read(0x00);
    uint8_t min = rtc_read(0x02);
    uint8_t hour = rtc_read(0x04);
    uint8_t day = rtc_read(0x07);
    uint8_t month = rtc_read(0x08);
    uint8_t year = rtc_read(0x09);

    uint8_t reg_b = rtc_read(0x0B);
    if (!(reg_b & 0x04)) {
        sec = (sec & 0x0F) + ((sec >> 4) * 10);
        min = (min & 0x0F) + ((min >> 4) * 10);
        hour = (hour & 0x0F) + ((hour >> 4) * 10);
        day = (day & 0x0F) + ((day >> 4) * 10);
        month = (month & 0x0F) + ((month >> 4) * 10);
        year = (year & 0x0F) + ((year >> 4) * 10);
    }

    int pos = 0;
    int_to_string(day, buf + pos); pos = strlen(buf);
    buf[pos++] = '/';
    int_to_string(month, buf + pos); pos = strlen(buf);
    buf[pos++] = '/';
    buf[pos++] = '2'; buf[pos++] = '0'; buf[pos++] = '0' + ((year / 10) % 10); buf[pos++] = '0' + (year % 10);
    buf[pos++] = ' ';
    int_to_string(hour, buf + pos); pos = strlen(buf);
    buf[pos++] = ':';
    int_to_string(min, buf + pos); pos = strlen(buf);
    buf[pos++] = ':';
    int_to_string(sec, buf + pos); pos = strlen(buf);
    buf[pos] = '\0';
}

// Scrolling marquee state
int marquee_offset = 0;
static char marquee_text[256];
int marquee_len = 0;

// Draw a glowing horizontal line with effect
static void ui_draw_glow_line(uint32_t x, uint32_t y, uint32_t w, uint32_t color) {
    if (w == 0) return;
    uint32_t stride = (uint32_t)(screen_pitch / 4);
    uint8_t r = (color >> 16) & 0xFF, g = (color >> 8) & 0xFF, b = color & 0xFF;
    for (uint32_t dx = 0; dx < w; dx++) {
        uint32_t* pixel = &lfbptr[y * stride + (x + dx)];
        uint8_t pr = (*pixel >> 16) & 0xFF, pg = (*pixel >> 8) & 0xFF, pb = *pixel & 0xFF;
        uint32_t alpha = 0xAA;
        uint8_t nr = (pr * (255 - alpha) + r * alpha) / 255;
        uint8_t ng = (pg * (255 - alpha) + g * alpha) / 255;
        uint8_t nb = (pb * (255 - alpha) + b * alpha) / 255;
        *pixel = 0xFF000000 | (nr << 16) | (ng << 8) | nb;
    }
}

// Gradient helper
static void ui_draw_gradient_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                                   uint32_t color_top, uint32_t color_bot) {
    if (h == 0) return;
    uint32_t stride = (uint32_t)(screen_pitch / 4);
    uint8_t r_t = (color_top >> 16) & 0xFF, g_t = (color_top >> 8) & 0xFF, b_t = color_top & 0xFF;
    uint8_t r_b = (color_bot >> 16) & 0xFF, g_b = (color_bot >> 8) & 0xFF, b_b = color_bot & 0xFF;
    for (uint32_t dy = 0; dy < h; dy++) {
        uint8_t r = r_t + (r_b - r_t) * dy / h;
        uint8_t g = g_t + (g_b - g_t) * dy / h;
        uint8_t b = b_t + (b_b - b_t) * dy / h;
        uint32_t row_color = 0xFF000000 | (r << 16) | (g << 8) | b;
        for (uint32_t dx = 0; dx < w; dx++) {
            uint32_t* pixel = &lfbptr[(y + dy) * stride + (x + dx)];
            *pixel = row_color;
        }
    }
}

// Draw glassmorphism-style panel
static void ui_draw_glass_panel(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    uint32_t stride = (uint32_t)(screen_pitch / 4);
    uint32_t alpha = 0xE0;
    
    for (uint32_t py = y; py < y + h && py < screen_height; py++) {
        for (uint32_t px = x; px < x + w && px < screen_width; px++) {
            uint8_t bg_r = (lfbptr[py * stride + px] >> 16) & 0xFF;
            uint8_t bg_g = (lfbptr[py * stride + px] >> 8) & 0xFF;
            uint8_t bg_b = lfbptr[py * stride + px] & 0xFF;
            uint8_t panel_r = (UI_SURFACE >> 16) & 0xFF;
            uint8_t panel_g = (UI_SURFACE >> 8) & 0xFF;
            uint8_t panel_b = UI_SURFACE & 0xFF;
            uint8_t r = (bg_r * (255 - alpha) + panel_r * alpha) / 255;
            uint8_t g = (bg_g * (255 - alpha) + panel_g * alpha) / 255;
            uint8_t b = (bg_b * (255 - alpha) + panel_b * alpha) / 255;
            lfbptr[py * stride + px] = 0xFF000000 | (r << 16) | (g << 8) | b;
        }
    }
    
    for (uint32_t i = 0; i < 2; i++) {
        uint8_t br = (UI_BORDER >> 16) & 0xFF, bg = (UI_BORDER >> 8) & 0xFF, bb = UI_BORDER & 0xFF;
        uint32_t border_color = 0xFF000000 | (br << 16) | (bg << 8) | bb;
        ui_draw_glow_line(x, y + i, w, border_color);
        ui_draw_glow_line(x, y + h - 1 - i, w, border_color);
        if (h > 2) {
            for (uint32_t py = y + i; py < y + h - i; py++) {
                uint32_t* pleft = &lfbptr[py * stride + x];
                *pleft = border_color;
                if (x + w - 1 < screen_width) {
                    uint32_t* pright = &lfbptr[py * stride + x + w - 1];
                    *pright = border_color;
                }
            }
        }
    }
}

// Draw shadow beneath a panel
static void ui_draw_shadow(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color_bg) {
    (void)color_bg;
    uint32_t stride = (uint32_t)(screen_pitch / 4);
    for (uint32_t dy = 0; dy < 4; dy++) {
        for (uint32_t dx = 0; dx < 4; dx++) {
            uint32_t shadow_x = x + w + dx;
            uint32_t shadow_y = y + h + dy;
            if (shadow_x >= screen_width || shadow_y >= screen_height) continue;
            uint32_t* pixel = &lfbptr[shadow_y * stride + shadow_x];
            uint32_t alpha = 0x30 - dy * 0x0A - dx * 0x0A;
            uint32_t bg = lfbptr[(shadow_y - dy) * stride + (shadow_x - dx)];
            uint8_t br = (bg >> 16) & 0xFF, bg_ = (bg >> 8) & 0xFF, bb = bg & 0xFF;
            uint8_t sr = (br * (255 - alpha)) / 255;
            uint8_t sg = (bg_ * (255 - alpha)) / 255;
            uint8_t sb = (bb * (255 - alpha)) / 255;
            *pixel = 0xFF000000 | (sr << 16) | (sg << 8) | sb;
        }
    }
}

static void ui_fill_content_bg(void) {
    // Content area background - subtle gradient
    uint32_t content_top = row_px(content_first_row);
    uint32_t content_bottom = ui_footer_y;
    ui_draw_gradient_rect(0, content_top, (uint32_t)screen_width, content_bottom - content_top, UI_SURFACE, UI_BG);
    
    // Decorative corner brackets for the entire content area
    uint32_t bracket_size = font_cell_w * 2;
    uint8_t br = (UI_BORDER >> 16) & 0xFF, bg = (UI_BORDER >> 8) & 0xFF, bb = UI_BORDER & 0xFF;
    uint32_t bracket_col = 0xFF000000 | (br << 16) | (bg << 8) | bb;
    
    // Top-left corner bracket
    draw_rect(4, content_top + 4, bracket_size, 2, bracket_col);
    draw_rect(4, content_top + 4, 2, bracket_size, bracket_col);
    
    // Top-right corner bracket
    draw_rect((uint32_t)screen_width - bracket_size - 4, content_top + 4, bracket_size, 2, bracket_col);
    draw_rect((uint32_t)screen_width - 6, content_top + 4, 2, bracket_size, bracket_col);
    
    // Bottom-left corner bracket
    draw_rect(4, content_bottom - bracket_size - 4, bracket_size, 2, bracket_col);
    draw_rect(4, content_bottom - bracket_size - 4, 2, bracket_size, bracket_col);
    
    // Bottom-right corner bracket
    draw_rect((uint32_t)screen_width - bracket_size - 4, content_bottom - bracket_size - 4, bracket_size, 2, bracket_col);
    draw_rect((uint32_t)screen_width - 6, content_bottom - bracket_size - 4, 2, bracket_size, bracket_col);
}

// Draw footer with datetime
void ui_draw_footer(void) {
    char datetime[32];
    rtc_get_datetime(datetime, sizeof(datetime));
    
    uint32_t footer_w = (uint32_t)screen_width;
    uint32_t footer_x = 0;
    uint32_t footer_y = ui_footer_y;
    
    // Clear footer area
    draw_rect(footer_x, footer_y, footer_w, ui_footer_h, UI_BG);
    
    // Draw accent line above footer
    ui_draw_glow_line(0, footer_y, footer_w, UI_BORDER);
    
    // Center the datetime
    uint32_t dt_len = strlen(datetime);
    uint32_t dt_x = (footer_w - dt_len * font_cell_w) / 2;
    uint32_t dt_y = footer_y + 2;
    
    // Draw datetime with subtle background
    draw_rect(dt_x - font_cell_w, dt_y, dt_len * font_cell_w + font_cell_w * 2, font_cell_h, UI_SURFACE);
    draw_string_px(datetime, dt_x, dt_y, UI_TEXT, UI_SURFACE);
}

// Draw top bar with OS name and prompt
void ui_draw_chrome(void) {
    uint32_t top_bar_h = font_cell_h + font_cell_h;
    uint32_t tab_area_h = font_cell_h + 4;
    
    // Clear top area including tab area
    draw_rect(0, 0, (uint32_t)screen_width, top_bar_h + tab_area_h, UI_BG);
    
    // Small subtle yellow accent card at the very top
    draw_rect(16, 0, (uint32_t)screen_width - 32, 1, UI_TITLE);
    
    // Draw prompt on the left (moved down in split mode) - white text
    uint32_t prompt_x = font_cell_w;
    uint32_t prompt_y = (pane_count > 1) ? 135 : 120;
    draw_string_px("Z;/superuser/>", prompt_x, prompt_y, UI_TEXT, UI_BG);

    // Draw OS name centered at top
    const char* os_name = "SharkOS v2.1";
    uint32_t name_len = strlen(os_name);
    uint32_t name_x = ((uint32_t)screen_width - name_len * font_cell_w) / 2;
    uint32_t name_y = 8;
    draw_string_px(os_name, name_x, name_y, UI_TITLE, UI_BG);

    // Draw system info on the right
    char info_buf[128];
    int ip = 0;

    // Kernel
    const char* kern_prefix = "KERN:2.1 ";
    while (*kern_prefix) info_buf[ip++] = *kern_prefix++;

    // Task count as simple CPU indicator
    int task_count = 0;
    task_t* t;
    for (t = task_list; t; t = t->next) task_count++;
    const char* cpu_prefix = "CPU:";
    while (*cpu_prefix) info_buf[ip++] = *cpu_prefix++;
    int_to_string(task_count, info_buf + ip); ip += strlen(info_buf + ip);
    info_buf[ip++] = ' ';

    // Resolution
    const char* res_prefix = "RES:";
    while (*res_prefix) info_buf[ip++] = *res_prefix++;
    int_to_string((uint32_t)screen_width, info_buf + ip); ip += strlen(info_buf + ip);
    info_buf[ip++] = 'x';
    int_to_string((uint32_t)screen_height, info_buf + ip); ip += strlen(info_buf + ip);
    
    info_buf[ip] = '\0';

    int info_len = ip;
    int info_x = (uint32_t)screen_width - info_len * font_cell_w - font_cell_w;
    int info_y = 8;
    draw_string_px(info_buf, info_x, info_y, UI_DIM, UI_BG);
}


static void faq_draw(void) {
    static size_t faq_backup_capacity = 0;
    size_t box_w = term_cols > 70 ? 66 : term_cols - 4;
    size_t box_h = 22;
    size_t box_col = (term_cols - box_w) / 2;
    size_t box_row = content_first_row + 1;
    if (box_row + box_h > term_max_row) {
        box_h = term_max_row - box_row;
    }

    faq_backup_x = col_px(box_col);
    faq_backup_y = row_px(box_row);
    faq_backup_w = col_px(box_w);
    faq_backup_h = row_px(box_h);

    size_t needed = (size_t)faq_backup_w * faq_backup_h;
    if (!faq_fb_backup || faq_backup_capacity < needed) {
        faq_fb_backup = (uint32_t*)kmalloc(needed * sizeof(uint32_t));
        if (!faq_fb_backup) return;
        faq_backup_capacity = needed;
    }
    if (faq_fb_backup) {
        uint32_t stride = (uint32_t)(screen_pitch / 4);
        for (uint32_t y = 0; y < faq_backup_h; y++) {
            memcpy(
                &faq_fb_backup[y * faq_backup_w],
                &lfbptr[(faq_backup_y + y) * stride + faq_backup_x],
                faq_backup_w * sizeof(uint32_t)
            );
        }
    }

    // Stunning FAQ panel with gradient background
    uint32_t px = col_px(box_col);
    uint32_t py = row_px(box_row);
    uint32_t pw = col_px(box_w);
    uint32_t ph = row_px(box_h);
    
    ui_draw_shadow(px, py, pw, ph, UI_SURFACE);
    ui_draw_gradient_rect(px, py, pw, ph, UI_SURFACE, UI_BG);
    
    // Glowing border
    ui_draw_glow_line(px, py, pw, UI_BORDER);
    ui_draw_glow_line(px, py + ph - 1, pw, UI_BORDER);
    draw_rect(px, py, 1, ph, UI_BORDER);
    draw_rect(px + pw - 1, py, 1, ph, UI_BORDER);
    
    // Corner accents
    draw_char(0xDA, px, py, UI_BORDER, UI_SURFACE);
    draw_char(0xBF, px + pw - font_cell_w, py, UI_BORDER, UI_SURFACE);
    draw_char(0xC0, px, py + ph - font_cell_h, UI_BORDER, UI_SURFACE);
    draw_char(0xD9, px + pw - font_cell_w, py + ph - font_cell_h, UI_BORDER, UI_SURFACE);
    
    // Title area with gradient
    draw_rect(px + 1, py + 1, pw - 2, font_cell_h + 2, UI_HEADER);
    size_t title_len = strlen("  FAQ - The Sharkslayer  ");
    size_t title_col = box_col + (box_w > title_len + 2 ? (box_w - title_len) / 2 : 1);
    draw_string_px("  FAQ - The Sharkslayer  ", col_px(title_col), py + 2, UI_TITLE, UI_HEADER);
    
    // Accent line
    uint32_t accent_y = py + font_cell_h + 4;
    draw_rect(px + 3, accent_y, pw - 6, 1, UI_ACCENT);

    size_t text_col = box_col + 2;
    size_t r = box_row + 3;

    draw_string_px("Q: Who is the developer?", col_px(text_col), row_px(r), UI_LABEL, UI_SURFACE);
    r++;
    draw_string_px("A: Mayshecry", col_px(text_col), row_px(r), UI_ANSWER, UI_SURFACE);
    r += 2;

    draw_string_px("Q: How does SharkOS work?", col_px(text_col), row_px(r), UI_LABEL, UI_SURFACE);
    r++;
    draw_string_px("A: look at our source code on gh", col_px(text_col), row_px(r), UI_ANSWER, UI_SURFACE);
    r += 2;

    draw_string_px("Q: Is templeos better?", col_px(text_col), row_px(r), UI_LABEL, UI_SURFACE);
    r++;
    draw_string_px("A: We're sorry terry davis but unfortunately...", col_px(text_col), row_px(r), UI_ANSWER, UI_SURFACE);
    r++;
    draw_string_px("   no your os was really badly optimized", col_px(text_col), row_px(r), UI_ANSWER, UI_SURFACE);
    r += 2;

    draw_string_px("[  ESC or ? to close  ]", col_px(text_col + 4), row_px(r), UI_ACCENT, UI_SURFACE);
}

void faq_open(void) {
    faq_saved_pane = active_pane;
    current_kernel_mode = KERNEL_MODE_FAQ;
    faq_draw();
}

void faq_close(void) {
    if (faq_fb_backup) {
        uint32_t stride = (uint32_t)(screen_pitch / 4);
        for (uint32_t y = 0; y < faq_backup_h; y++) {
            memcpy(
                &lfbptr[(faq_backup_y + y) * stride + faq_backup_x],
                &faq_fb_backup[y * faq_backup_w],
                faq_backup_w * sizeof(uint32_t)
            );
        }
    }
    current_kernel_mode = KERNEL_MODE_CLI;
    active_pane = faq_saved_pane;
    redraw_all_panes();
    print_prompt();
}

static uint32_t* settings_fb_backup = 0;
static uint32_t settings_backup_x = 0;
static uint32_t settings_backup_y = 0;
static uint32_t settings_backup_w = 0;
static uint32_t settings_backup_h = 0;
static int settings_saved_pane = 0;

void apply_theme(int theme_idx) {
    if (theme_idx < 0 || theme_idx >= MAX_THEMES) return;
    selected_theme = theme_idx;
    UI_BG = themes[theme_idx].bg;
    UI_SURFACE = themes[theme_idx].surface;
    UI_HEADER = themes[theme_idx].header;
    UI_TAB_ACTIVE = themes[theme_idx].tab_active;
    UI_TAB_INACTIVE = themes[theme_idx].tab_inactive;
    UI_BORDER = themes[theme_idx].border;
    UI_TITLE = themes[theme_idx].title;
    UI_TEXT = themes[theme_idx].text;
    UI_DIM = themes[theme_idx].dim;
    UI_ACCENT = themes[theme_idx].accent;
    UI_LABEL = themes[theme_idx].label;
    UI_ANSWER = themes[theme_idx].answer;
}

void settings_draw(void) {
    size_t box_w = term_cols > 50 ? 46 : term_cols - 4;
    size_t box_h = 12;
    size_t box_col = (term_cols - box_w) / 2;
    size_t box_row = content_first_row + 2;

    settings_backup_x = col_px(box_col);
    settings_backup_y = row_px(box_row);
    settings_backup_w = col_px(box_w);
    settings_backup_h = row_px(box_h);

    size_t needed = (size_t)settings_backup_w * settings_backup_h;
    if (!settings_fb_backup) {
        settings_fb_backup = (uint32_t*)kmalloc(needed * sizeof(uint32_t));
        if (!settings_fb_backup) return;
    }
    if (settings_fb_backup) {
        uint32_t stride = (uint32_t)(screen_pitch / 4);
        for (uint32_t y = 0; y < settings_backup_h; y++) {
            memcpy(
                &settings_fb_backup[y * settings_backup_w],
                &lfbptr[(settings_backup_y + y) * stride + settings_backup_x],
                settings_backup_w * sizeof(uint32_t)
            );
        }
    }

    // Modern settings panel
    uint32_t px = col_px(box_col);
    uint32_t py = row_px(box_row);
    uint32_t pw = col_px(box_w);
    uint32_t ph = row_px(box_h);
    
    ui_draw_shadow(px, py, pw, ph, UI_SURFACE);
    ui_draw_gradient_rect(px, py, pw, ph, UI_SURFACE, UI_BG);
    
    // Glowing border
    ui_draw_glow_line(px, py, pw, UI_BORDER);
    ui_draw_glow_line(px, py + ph - 1, pw, UI_BORDER);
    draw_rect(px, py, 1, ph, UI_BORDER);
    draw_rect(px + pw - 1, py, 1, ph, UI_BORDER);
    
    // Title
    draw_rect(px + 1, py + 1, pw - 2, font_cell_h + 2, UI_HEADER);
    size_t title_len = strlen("  Settings  ");
    size_t title_col = box_col + (box_w > title_len + 2 ? (box_w - title_len) / 2 : 1);
    draw_string_px("  Settings  ", col_px(title_col), py + 2, UI_TITLE, UI_HEADER);
    
    // Accent underline
    draw_rect(px + 3, py + font_cell_h + 4, pw - 6, 1, UI_ACCENT);

    size_t text_col = box_col + 2;
    size_t r = box_row + 3;

    uint32_t item_fg = (settings_selected == 0) ? UI_ACCENT : UI_TEXT;
    draw_rect(col_px(text_col), row_px(r), font_cell_w, font_cell_h, UI_SURFACE);
    draw_string_px("[", col_px(text_col), row_px(r), item_fg, UI_SURFACE);
    draw_string_px("]", col_px(text_col + 28), row_px(r), item_fg, UI_SURFACE);
    draw_string_px("Tiling: ", col_px(text_col + 2), row_px(r), UI_LABEL, UI_SURFACE);
    draw_string_px(tiling_enabled ? "ON " : "OFF", col_px(text_col + 10), row_px(r), UI_ANSWER, UI_SURFACE);
    r++;

    item_fg = (settings_selected == 1) ? UI_ACCENT : UI_TEXT;
    draw_rect(col_px(text_col), row_px(r), font_cell_w, font_cell_h, UI_SURFACE);
    draw_string_px("[", col_px(text_col), row_px(r), item_fg, UI_SURFACE);
    draw_string_px("]", col_px(text_col + 28), row_px(r), item_fg, UI_SURFACE);
    draw_string_px("Mouse:  ", col_px(text_col + 2), row_px(r), UI_LABEL, UI_SURFACE);
    draw_string_px(mouse_enabled ? "ON " : "OFF", col_px(text_col + 10), row_px(r), UI_ANSWER, UI_SURFACE);
    r++;

    item_fg = (settings_selected == 2) ? UI_ACCENT : UI_TEXT;
    draw_rect(col_px(text_col), row_px(r), font_cell_w, font_cell_h, UI_SURFACE);
    draw_string_px("[", col_px(text_col), row_px(r), item_fg, UI_SURFACE);
    draw_string_px("]", col_px(text_col + 28), row_px(r), item_fg, UI_SURFACE);
    draw_string_px("Theme:  ", col_px(text_col + 2), row_px(r), UI_LABEL, UI_SURFACE);
    if (selected_theme == THEME_SHARKOS) draw_string_px("SharkOS", col_px(text_col + 10), row_px(r), UI_ANSWER, UI_SURFACE);
    else if (selected_theme == THEME_BLUE) draw_string_px("Blue  ", col_px(text_col + 10), row_px(r), UI_ANSWER, UI_SURFACE);
    else if (selected_theme == THEME_TEMPLEOS) draw_string_px("Temple", col_px(text_col + 10), row_px(r), UI_ANSWER, UI_SURFACE);
    r++;

    r++;
    draw_string_px("ENTER=Toggle  UP/DOWN=Move  ESC=Close", col_px(text_col), row_px(r), UI_DIM, UI_SURFACE);
}

void settings_open(void) {
    settings_saved_pane = active_pane;
    current_kernel_mode = KERNEL_MODE_SETTINGS;
    settings_selected = 0;
    settings_draw();
}

void settings_close(void) {
    if (current_kernel_mode != KERNEL_MODE_SETTINGS) return;

    if (settings_fb_backup) {
        uint32_t stride = (uint32_t)(screen_pitch / 4);
        for (uint32_t y = 0; y < settings_backup_h; y++) {
            memcpy(
                &lfbptr[(settings_backup_y + y) * stride + settings_backup_x],
                &settings_fb_backup[y * settings_backup_w],
                settings_backup_w * sizeof(uint32_t)
            );
        }
    }
    current_kernel_mode = KERNEL_MODE_CLI;
    active_pane = settings_saved_pane;
    ui_draw_chrome();
    ui_draw_footer();
    // Advance marquee
    marquee_offset++;
    if (marquee_offset >= marquee_len) marquee_offset = 0;
    // Redraw panes with proper dividers
    for (int i = 0; i < pane_count; i++) {
        size_t pane_width = panes[i].col_end - panes[i].col_start;
        uint32_t px = col_px(panes[i].col_start) + PANE_GAP * font_cell_w / 2;
        uint32_t pw = col_px(pane_width) - PANE_GAP * font_cell_w;
        uint32_t py = row_px(content_first_row);
        uint32_t ph = ui_footer_y - py;
        draw_rect(px, py, pw, ph, UI_SURFACE);
    }
    draw_pane_tabs();
    terminal_row = content_first_row;
    terminal_column = panes[active_pane].col_start;
    print_prompt();
}

void draw_pane_tabs(void) {
    uint32_t tab_bar_y = ui_tab_y;
    uint32_t tab_h = font_cell_h + 4;
    
    // Draw tab bar background
    draw_rect(0, tab_bar_y, (uint32_t)screen_width, tab_h, UI_SURFACE);
    
    for (int i = 0; i < pane_count; i++) {
        size_t pane_width = panes[i].col_end - panes[i].col_start;
        uint32_t px = col_px(panes[i].col_start) + PANE_GAP * font_cell_w / 2;
        uint32_t pw = col_px(pane_width) - PANE_GAP * font_cell_w;
        char label[32];
        label[0] = '1' + i;
        label[1] = '\0';
        
        if (i == active_pane) {
            // Active tab - number turns red, no background block
            draw_string_px(label, px + font_cell_w * 2, tab_bar_y + 3, 0xFFFF0000, UI_SURFACE);
        } else {
            // Inactive tab - dimmed, no background block
            draw_string_px(label, px + font_cell_w * 2, tab_bar_y + 3, UI_DIM, UI_SURFACE);
        }
    }
}

void split_active_pane(void) {
    if (pane_count >= MAX_PANES) {
        terminal_writestring("\nMax panes reached.\n");
        print_prompt();
        return;
    }
    int new_count = pane_count + 1;
    size_t total_gaps = (size_t)(new_count - 1) * PANE_GAP;
    size_t usable_cols = term_cols - total_gaps;
    size_t pane_w = usable_cols / new_count;
    if (pane_w < 6) {
        terminal_writestring("\nPane too small to split.\n");
        print_prompt();
        return;
    }
    for (int i = 0; i < new_count; i++) {
        size_t cs = i * (pane_w + PANE_GAP);
        size_t ce = cs + pane_w;
        if (ce > term_cols) ce = term_cols;
        panes[i].col_start = cs;
        panes[i].col_end = ce;
        panes[i].row = content_first_row;
        panes[i].col = cs;
        panes[i].color = vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        panes[i].cmd_index = 0;
        panes[i].prompt_end_col = cs;
    }
    pane_count = new_count;
    int saved_active = active_pane;
    active_pane = 0;
    
    // Redraw everything cleanly
    redraw_all_panes();
    
    for (int i = 0; i < pane_count; i++) {
        active_pane = i;
        terminal_row = panes[i].row;
        terminal_column = panes[i].col_start;
        print_prompt();
    }
    active_pane = saved_active;
    terminal_row = panes[active_pane].row;
    terminal_column = panes[active_pane].prompt_end_col + panes[active_pane].cmd_index;
}

void close_active_pane(void) {
    if (pane_count <= 1) return;
    int to_remove = active_pane;

    for (int i = to_remove; i < pane_count - 1; i++) {
        panes[i] = panes[i + 1];
    }
    pane_count--;

    if (active_pane >= pane_count) {
        active_pane = pane_count - 1;
    } else if (active_pane > to_remove) {
        active_pane--;
    }

    size_t total_gaps = (size_t)(pane_count - 1) * PANE_GAP;
    size_t usable_cols = term_cols - total_gaps;
    size_t pane_w = usable_cols / pane_count;
    for (int i = 0; i < pane_count; i++) {
        size_t cs = i * (pane_w + PANE_GAP);
        size_t ce = cs + pane_w;
        if (ce > term_cols) ce = term_cols;
        panes[i].col_start = cs;
        panes[i].col_end = ce;
        panes[i].row = content_first_row;
        panes[i].col = cs;
        panes[i].cmd_index = 0;
        panes[i].prompt_end_col = cs;
    }

    // Redraw everything cleanly
    redraw_all_panes();
    terminal_row = content_first_row;
    terminal_column = panes[active_pane].col_start;
    print_prompt();
}

void redraw_all_panes(void) {
    ui_draw_chrome();
    ui_fill_content_bg();
    ui_draw_footer();
    // Advance marquee
    marquee_offset++;
    if (marquee_offset >= marquee_len) marquee_offset = 0;
    for (int i = 0; i < pane_count; i++) {
        size_t pane_width = panes[i].col_end - panes[i].col_start;
        uint32_t px = col_px(panes[i].col_start) + PANE_GAP * font_cell_w / 2;
        uint32_t pw = col_px(pane_width) - PANE_GAP * font_cell_w;
        uint32_t py = row_px(content_first_row);
        uint32_t ph = ui_footer_y - py;
        ui_draw_gradient_rect(px, py, pw, ph, UI_SURFACE, UI_BG);
        
        // Draw full-height vertical divider lines
        if (i > 0) {
            uint32_t divider_x = col_px(panes[i].col_start) - PANE_GAP * font_cell_w / 2;
            uint32_t divider_w = 2;
            // Full height yellow divider from chrome to footer
            draw_rect(divider_x - divider_w/2, ui_chrome_top, divider_w, ui_footer_y - ui_chrome_top, UI_TITLE);
        }
    }
    draw_pane_tabs();
    // Restore terminal content
    terminal_draw_scrollback();
}
