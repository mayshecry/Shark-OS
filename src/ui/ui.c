#include "kernel.h"

static void ui_fill_content_bg(void) {
    draw_rect(0, ui_chrome_top, (uint32_t)screen_width, ui_footer_y - ui_chrome_top, UI_SURFACE);
}

static void ui_draw_chrome(void) {
    uint32_t bd = font_scale > 1 ? font_scale : 2;

    draw_rect(0, 0, (uint32_t)screen_width, (uint32_t)screen_height, UI_BG);
    draw_rect(0, 0, (uint32_t)screen_width, font_cell_h, UI_HEADER);
    draw_rect(0, font_cell_h - bd, (uint32_t)screen_width, bd, UI_BORDER);

    draw_string_px("  The Sharkslayer  ", font_cell_w, 0, UI_TITLE, UI_HEADER);
    draw_string_px(":: SharkOS v0.1  ", font_cell_w + 19 * font_cell_w, 0, UI_BORDER, UI_HEADER);

    size_t deco_col = term_cols > 4 ? term_cols - 3 : 0;
    draw_char('<', col_px(deco_col), 0, UI_ACCENT, UI_HEADER);
    draw_char('>', col_px(deco_col + 1), 0, UI_ACCENT, UI_HEADER);
    draw_char('>', col_px(deco_col + 2), 0, UI_ACCENT, UI_HEADER);

    draw_rect(0, ui_footer_y, (uint32_t)screen_width, ui_footer_h, UI_HEADER);
    uint32_t sep_y = ui_footer_y - bd;  
    if (sep_y < ui_footer_y) {
        draw_rect(0, sep_y, (uint32_t)screen_width, bd, UI_BORDER);
    }
    draw_string_px("  ?=FAQ  TAB=Focus  +=Split  -=Close  s=Settings  help=Commands  poweroff=Shutdown  ",
        font_cell_w, ui_footer_y, UI_DIM, UI_HEADER);
}

static void draw_text_panel(size_t col, size_t row, size_t w, size_t h,
    const char* title, uint32_t border, uint32_t fill, uint32_t title_fg) {
    uint32_t px = col_px(col);
    uint32_t py = row_px(row);
    uint32_t pw = col_px(w);
    uint32_t ph = row_px(h);
    uint32_t bd = font_scale > 1 ? font_scale : 2;

    draw_rect(px, py, pw, ph, fill);
    draw_rect(px, py, pw, bd, border);
    draw_rect(px, py + ph - bd, pw, bd, border);
    draw_rect(px, py, bd, ph, border);
    draw_rect(px + pw - bd, py, bd, ph, border);

    draw_rect(px + bd, py + bd, pw - 2 * bd, font_cell_h, UI_HEADER);
    size_t title_len = strlen(title);
    size_t title_col = col + (w > title_len + 2 ? (w - title_len) / 2 : 1);
    draw_string_px(title, col_px(title_col), py + bd, title_fg, UI_HEADER);
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

    draw_text_panel(box_col, box_row, box_w, box_h,
        "  FAQ — The Sharkslayer  ", UI_BORDER, UI_SURFACE, UI_TITLE);

    size_t text_col = box_col + 2;
    size_t r = box_row + 2;

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

    draw_text_panel(box_col, box_row, box_w, box_h,
        "  Settings  ", UI_BORDER, UI_SURFACE, UI_TITLE);

    size_t text_col = box_col + 2;
    size_t r = box_row + 2;

    uint32_t item_fg = (settings_selected == 0) ? UI_ACCENT : UI_TEXT;
    draw_string_px("[", col_px(text_col), row_px(r), item_fg, UI_SURFACE);
    draw_string_px("]", col_px(text_col + 28), row_px(r), item_fg, UI_SURFACE);
    draw_string_px("Tiling: ", col_px(text_col + 2), row_px(r), UI_LABEL, UI_SURFACE);
    draw_string_px(tiling_enabled ? "ON " : "OFF", col_px(text_col + 10), row_px(r), UI_ANSWER, UI_SURFACE);
    r++;

    item_fg = (settings_selected == 1) ? UI_ACCENT : UI_TEXT;
    draw_string_px("[", col_px(text_col), row_px(r), item_fg, UI_SURFACE);
    draw_string_px("]", col_px(text_col + 28), row_px(r), item_fg, UI_SURFACE);
    draw_string_px("Mouse:  ", col_px(text_col + 2), row_px(r), UI_LABEL, UI_SURFACE);
    draw_string_px(mouse_enabled ? "ON " : "OFF", col_px(text_col + 10), row_px(r), UI_ANSWER, UI_SURFACE);
    r++;

    item_fg = (settings_selected == 2) ? UI_ACCENT : UI_TEXT;
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
    terminal_row = content_first_row;
    terminal_column = panes[active_pane].col_start;
    redraw_all_panes();
    print_prompt();
}

void draw_pane_tabs(void) {
    size_t bd = font_scale > 1 ? font_scale : 2;
    uint32_t tab_bar_w = (uint32_t)screen_width;
    draw_rect(0, ui_tab_y, tab_bar_w, font_cell_h, UI_BG);

    for (int i = 0; i < pane_count; i++) {
        uint32_t px = col_px(panes[i].col_start);
        uint32_t pw = col_px(panes[i].col_end - panes[i].col_start);
        uint32_t tab_bg = (i == active_pane) ? UI_TAB_ACTIVE : UI_TAB_INACTIVE;
        draw_rect(px, ui_tab_y, pw, font_cell_h, tab_bg);
        if (i == active_pane) {
            draw_rect(px, ui_tab_y + font_cell_h - bd, pw, bd, UI_SURFACE);
        }
        char label[16];
        label[0] = ' ';
        label[1] = 'S';
        label[2] = 'h';
        label[3] = 'e';
        label[4] = 'l';
        label[5] = 'l';
        label[6] = ' ';
        if (i < 9) {
            label[7] = '1' + i;
            label[8] = ' ';
            label[9] = '\0';
        } else {
            label[7] = '1' + (i / 10);
            label[8] = '0' + (i % 10);
            label[9] = ' ';
            label[10] = '\0';
        }
        uint32_t label_fg = (i == active_pane) ? UI_TITLE : UI_DIM;
        draw_string_px(label, px + font_cell_w / 2, ui_tab_y, label_fg, tab_bg);
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
    }
    pane_count = new_count;
    active_pane = 0;
    terminal_row = content_first_row;
    terminal_column = panes[active_pane].col_start;
    for (int i = 0; i < pane_count; i++) {
        size_t pane_width = panes[i].col_end - panes[i].col_start;
        uint32_t px = col_px(panes[i].col_start) + PANE_GAP * font_cell_w / 2;
        uint32_t pw = col_px(pane_width) - PANE_GAP * font_cell_w;
        uint32_t py = row_px(content_first_row);
        uint32_t ph = ui_footer_y - py;
        draw_rect(px, py, pw, ph, UI_SURFACE);
    }
    redraw_all_panes();
    print_prompt();
}

void close_active_pane(void) {
    if (pane_count <= 1) return;
    int to_remove = active_pane;
    for (int i = to_remove; i < pane_count - 1; i++) {
        panes[i] = panes[i + 1];
    }
    pane_count--;
    if (active_pane >= pane_count) active_pane = pane_count - 1;
    terminal_row = content_first_row;
    terminal_column = panes[active_pane].col_start;
    redraw_all_panes();
    print_prompt();
}

void redraw_all_panes(void) {
    ui_draw_chrome();
    ui_fill_content_bg();
    for (int i = 0; i < pane_count; i++) {
        size_t pane_width = panes[i].col_end - panes[i].col_start;
        uint32_t px = col_px(panes[i].col_start) + PANE_GAP * font_cell_w / 2;
        uint32_t pw = col_px(pane_width) - PANE_GAP * font_cell_w;
        uint32_t py = row_px(content_first_row);
        uint32_t ph = ui_footer_y - py;
        draw_rect(px, py, pw, ph, UI_SURFACE);
        if (i > 0) {
            uint32_t divider_x = col_px(panes[i].col_start) - PANE_GAP * font_cell_w / 2;
            uint32_t divider_w = font_scale > 1 ? font_scale : 2;
            draw_rect(divider_x - divider_w / 2, ui_chrome_top, divider_w, ui_footer_y - ui_chrome_top, UI_BORDER);
        }
    }
    draw_pane_tabs();
}