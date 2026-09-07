/* appwindows.c - Win98-styled client areas for the SharkOS desktop apps.
 *
 * Behaviour is unchanged from the previous version (buffers, key handlers,
 * file manager navigation); only the painting now follows the Windows 98
 * system palette. Every colour comes from include/win98_theme.h.
 */

#include "kernel.h"
#include "desktop.h"
#include "win98_theme.h"
#include "doom.h"
#include "flappybird.h"
#include "smb.h"
#include "pong.h"
#include "geometrydash.h"
#include "net.h"
#include "icon_data.h"

static void kmemmove(void* dst, const void* src, int n) {
    char* d = (char*)dst;
    const char* s = (const char*)src;
    for (int i = 0; i < n; i++) d[i] = s[i];
}
static void kstrcat(char* dst, const char* src) {
    while (*dst) dst++;
    while ((*dst++ = *src++));
}

/* Label/value rows used by the info-style windows. */
static void label_value(int lx, int vx, int y, const char* label,
                        const char* value, uint32_t vcolor) {
    w98_text_bold(label, lx, y, W98_BTNTEXT, W98_BTNFACE, 1, NULL);
    w98_text(value, vx, y, vcolor, W98_BTNFACE, 1, NULL);
}

/* ----------------------------------------------------------------- terminal */

static char terminal_buffer[256] = "";
static int terminal_buf_len = 0;
static char terminal_output[4096] = "";
static int terminal_output_len = 0;

void app_window_draw_terminal(window_t* w) {
    int cx = w->rect.client_x;
    int cy = w->rect.client_y;
    int cw = w->rect.client_w;
    int ch = w->rect.client_h;

    /* Black console inside a sunken well, on a button-face body. */
    w98_fill(cx, cy, cw, ch, W98_BTNFACE);
    w98_surface(cx + 2, cy + 2, cw - 4, ch - 4, W98_BEVEL_SUNKEN,
                W98_BTNDKSHADOW);

    int tx = cx + 6, ty = cy + 6;
    int tw = cw - 12, th = ch - 12;

    int max_chars_per_line = (tw - 8) / 6;
    if (max_chars_per_line > 90) max_chars_per_line = 90;
    int max_lines = (th - 8) / 10;
    if (max_lines > 60) max_lines = 60;

    w98_rect_t clip;
    clip.x = tx + 2; clip.y = ty + 2; clip.w = tw - 4; clip.h = th - 4;

    int current_line = 0;
    if (terminal_output_len > 0) {
        for (int i = 0; i < terminal_output_len && current_line < max_lines;
             i++) {
            int start = i;
            int len = 0;
            while (i < terminal_output_len && terminal_output[i] != '\n' &&
                   len < max_chars_per_line) {
                i++;
                len++;
            }
            char line_buf[91];
            for (int j = 0; j < len && j < 90; j++) {
                line_buf[j] = terminal_output[start + j];
            }
            line_buf[len] = '\0';
            w98_text(line_buf, tx + 4, ty + 4 + current_line * 10,
                     0xFFAAAAAA, W98_BTNDKSHADOW, 1, &clip);
            i++;
            current_line++;
        }
    }

    int prompt_y = ty + 4 + current_line * 10;
    w98_text("$ ", tx + 4, prompt_y, 0xFF55FF55, W98_BTNDKSHADOW, 1, &clip);

    char temp[256];
    int i;
    for (i = 0; i < terminal_buf_len && i < max_chars_per_line - 2; i++) {
        temp[i] = terminal_buffer[i];
    }
    temp[i] = '\0';
    w98_text(temp, tx + 4 + 12, prompt_y, 0xFFFFFF55, W98_BTNDKSHADOW, 1,
             &clip);

    static int blink = 0;
    if (blink < 30) {
        w98_fill(tx + 4 + 12 + i * 6, prompt_y, 6, 8, 0xFF55FF55);
    }
    if (blink++ >= 60) blink = 0;
}

/* ------------------------------------------------------------------- games */

void app_window_draw_doom(window_t* w) {
    doom_set_window_rect(w->rect.client_x, w->rect.client_y,
                         w->rect.client_w, w->rect.client_h);
    doom_draw_frame();
}
void app_window_draw_flappybird(window_t* w) {
    flappybird_set_window_rect(w->rect.client_x, w->rect.client_y,
                               w->rect.client_w, w->rect.client_h);
    flappybird_draw_frame();
}
void app_window_draw_smb(window_t* w) {
    smb_set_window_rect(w->rect.client_x, w->rect.client_y,
                        w->rect.client_w, w->rect.client_h);
    smb_draw_frame();
}
void app_window_draw_pong(window_t* w) {
    pong_set_window_rect(w->rect.client_x, w->rect.client_y,
                         w->rect.client_w, w->rect.client_h);
    pong_draw_frame();
}
void app_window_draw_gdash(window_t* w) {
    gd_set_window_rect(w->rect.client_x, w->rect.client_y,
                       w->rect.client_w, w->rect.client_h);
    gd_draw_frame();
}

/* ----------------------------------------------------------------- settings */

typedef struct {
    uint32_t color;
    const char* label;
} settings_color_opt_t;

static const settings_color_opt_t settings_colors[] = {
    { W98_DESKTOP,        "Teal (Default)" },
    { 0xFF1A0A0Au,        "Dark Red" },
    { 0xFF0A1A0Au,        "Dark Green" },
    { 0xFF1A1A0Au,        "Amber" },
    { 0xFF0A0A0Au,        "Pure Black" },
    { 0xFF2A1A0Au,        "Brown" },
    { 0xFF1A0A2Au,        "Purple" },
    { 0xFF0A1A2Au,        "Teal Blue" },
};
#define SETTINGS_COLOR_COUNT 8

#define SETTINGS_ROW_H 16

static int settings_hover = -1;   /* 0..2 radio, 10..17 swatch, 99 shutdown */

/* Shared layout so the painter and the mouse handler agree.
 * Returns the y position of the shutdown button. */
static int settings_layout(window_t* w, int* radio_y, int* swatch_y,
                           int* btn_y) {
    int cy = w->rect.client_y;
    int ch = w->rect.client_h;

    *radio_y = cy + 26;
    *swatch_y = *radio_y + 3 * SETTINGS_ROW_H + 22;
    *btn_y = cy + ch - 30;
    return *btn_y;
}

static void settings_draw_radio(int x, int y, bool on, const char* label,
                                bool hovered) {
    /* 12x12 sunken white circle-ish square, black dot when selected. */
    w98_surface(x, y, 12, 12, W98_BEVEL_SUNKEN, W98_WINDOW);
    if (on) {
        w98_fill(x + 4, y + 4, 4, 4, W98_BTNTEXT);
    }
    uint32_t bg = hovered ? W98_BTNFACE : W98_BTNFACE;
    w98_text(label, x + 18, y + 2, W98_BTNTEXT, bg, 1, NULL);
}

void app_window_draw_settings(window_t* w) {
    int cx = w->rect.client_x;
    int cy = w->rect.client_y;
    int cw = w->rect.client_w;

    int radio_y, swatch_y, btn_y;
    settings_layout(w, &radio_y, &swatch_y, &btn_y);

    /* Hover is derived from the pointer every paint, so it cannot drift from
     * what the click handler tests. */
    settings_hover = -1;
    int mxp = desktop_mouse_x, myp = desktop_mouse_y;
    for (int i = 0; i < 3; i++) {
        int ry = radio_y + i * SETTINGS_ROW_H;
        if (mxp >= cx && mxp < cx + cw && myp >= ry && myp < ry + SETTINGS_ROW_H) {
            settings_hover = i;
        }
    }
    for (int i = 0; i < SETTINGS_COLOR_COUNT; i++) {
        int ry = swatch_y + i * SETTINGS_ROW_H;
        if (mxp >= cx && mxp < cx + cw && myp >= ry && myp < ry + SETTINGS_ROW_H) {
            settings_hover = 10 + i;
        }
    }
    if (mxp >= cx + cw / 2 - 37 && mxp < cx + cw / 2 - 37 + 75 &&
        myp >= btn_y && myp < btn_y + 23) {
        settings_hover = 99;
    }

    w98_fill(cx, cy, cw, w->rect.client_h, W98_BTNFACE);

    w98_text_bold("Settings - Customize", cx + cw / 2 - 60, cy + 6,
                  W98_BTNTEXT, W98_BTNFACE, 1, NULL);
    w98_bevel(cx + 4, cy + 18, cw - 8, 2, W98_BEVEL_ETCHED);

    /* Wallpaper mode radios. */
    w98_text_bold("Wallpaper:", cx + 8, radio_y - 14, W98_BTNTEXT,
                  W98_BTNFACE, 1, NULL);
    const char* modes[3] = { "Teal (dithered)", "Solid colour",
                             "SharkOS image" };
    for (int i = 0; i < 3; i++) {
        bool on = (desktop.wallpaper_mode == i);
        settings_draw_radio(cx + 10, radio_y + i * SETTINGS_ROW_H, on,
                            modes[i], settings_hover == i);
    }

    w98_text_bold("Solid colour:", cx + 8, swatch_y - 14, W98_BTNTEXT,
                  W98_BTNFACE, 1, NULL);
    for (int i = 0; i < SETTINGS_COLOR_COUNT; i++) {
        int ry = swatch_y + i * SETTINGS_ROW_H;
        bool current = (settings_colors[i].color == wallpaper_top);

        if (settings_hover == 10 + i) {
            w98_fill(cx + 6, ry, cw - 12, SETTINGS_ROW_H - 1, W98_HIGHLIGHT);
        }
        /* Swatch: flat colour with a sunken ring. */
        w98_surface(cx + 10, ry, 16, 14, W98_BEVEL_SUNKEN,
                    settings_colors[i].color);

        uint32_t fg = (settings_hover == 10 + i) ? W98_HIGHLIGHTTEXT
                                                 : W98_BTNTEXT;
        uint32_t bg = (settings_hover == 10 + i) ? W98_HIGHLIGHT
                                                 : W98_BTNFACE;
        w98_text(settings_colors[i].label, cx + 32, ry + 3, fg, bg, 1, NULL);
        if (current) {
            w98_text("<- Active", cx + 32 +
                     w98_text_width(settings_colors[i].label, 1) + 6,
                     ry + 3, W98_GRAYTEXT, bg, 1, NULL);
        }
    }

    w98_button(cx + cw / 2 - 37, btn_y, 75, 23, "Shut Down...",
               settings_hover == 99, true, false);
}

void app_window_mouse_settings(window_t* w, int mx, int my, int buttons) {
    (void)w;
    if (!(buttons & 1)) return;

    int radio_y, swatch_y, btn_y;
    settings_layout(w, &radio_y, &swatch_y, &btn_y);
    int cx = w->rect.client_x;
    int cw = w->rect.client_w;

    for (int i = 0; i < 3; i++) {
        int ry = radio_y + i * SETTINGS_ROW_H;
        if (mx >= cx && mx < cx + cw && my >= ry && my < ry + SETTINGS_ROW_H) {
            desktop_set_wallpaper_mode(i);
            return;
        }
    }

    for (int i = 0; i < SETTINGS_COLOR_COUNT; i++) {
        int ry = swatch_y + i * SETTINGS_ROW_H;
        if (mx >= cx && mx < cx + cw && my >= ry && my < ry + SETTINGS_ROW_H) {
            uint32_t color = settings_colors[i].color;
            uint8_t r = (color >> 16) & 0xFF;
            uint8_t g = (color >> 8) & 0xFF;
            uint8_t b = color & 0xFF;
            desktop_set_wallpaper_color(color,
                0xFF000000u | ((r / 2) << 16) | ((g / 2) << 8) | (b / 2));
            desktop_set_wallpaper_mode(W98_WALL_SOLID);
            return;
        }
    }

    if (mx >= cx + cw / 2 - 37 && mx < cx + cw / 2 - 37 + 75 &&
        my >= btn_y && my < btn_y + 23) {
        shutdown();
    }
}

/* --------------------------------------------------------------------- faq */

void app_window_draw_faq(window_t* w) {
    int cx = w->rect.client_x;
    int cy = w->rect.client_y;
    int cw = w->rect.client_w;

    w98_fill(cx, cy, cw, w->rect.client_h, W98_BTNFACE);
    w98_text_bold("SharkOS FAQ", cx + cw / 2 - 40, cy + 8, W98_BTNTEXT,
                  W98_BTNFACE, 1, NULL);
    w98_bevel(cx + 4, cy + 20, cw - 8, 2, W98_BEVEL_ETCHED);
    w98_text_bold("Q: Who made SharkOS?", cx + 8, cy + 30, W98_BTNTEXT,
                  W98_BTNFACE, 1, NULL);
    w98_text("A: Mayshecry", cx + 8, cy + 44, W98_BTNTEXT, W98_BTNFACE, 1,
             NULL);
}

/* --------------------------------------------------------------- fastfetch */

void app_window_draw_fastfetch(window_t* w) {
    int cx = w->rect.client_x;
    int cy = w->rect.client_y;
    int cw = w->rect.client_w;

    w98_fill(cx, cy, cw, w->rect.client_h, W98_BTNFACE);

    w98_text_bold("SharkOS System Information", cx + 8, cy + 6, W98_BTNTEXT,
                  W98_BTNFACE, 1, NULL);
    w98_bevel(cx + 4, cy + 18, cw - 8, 2, W98_BEVEL_ETCHED);

    char buf[64];
    uint32_t total_mb = (uint32_t)(total_system_memory >> 20);
    uint32_t free_kb = (uint32_t)(total_system_memory * 3 / 4);
    uint32_t avail_kb = (uint32_t)(total_system_memory * 7 / 8);
    uint32_t up_secs = uptime_ticks / 100;
    uint32_t hours = up_secs / 3600;
    uint32_t mins = (up_secs % 3600) / 60;
    uint32_t secs = up_secs % 60;
    int task_count = 0;
    task_t* t;
    for (t = task_list; t; t = t->next) task_count++;
    char cpu_model[49];
    get_cpu_model(cpu_model);

    int y = cy + 26;
    int label_x = cx + 8;
    int value_x = cx + 110;
    int line_h = 13;

    label_value(label_x, value_x, y, "OS:", "SharkOS 98 (Sharkslayer)",
                W98_BTNTEXT); y += line_h;
    label_value(label_x, value_x, y, "Host:", "SharkOS PC", W98_BTNTEXT);
    y += line_h;
    label_value(label_x, value_x, y, "Kernel:", "SharkOS V2.2", W98_BTNTEXT);
    y += line_h;
    label_value(label_x, value_x, y, "Shell:", "nemo-shell", W98_BTNTEXT);
    y += line_h;
    label_value(label_x, value_x, y, "Arch:", "x86 (32-bit)", W98_BTNTEXT);
    y += line_h + 4;
    label_value(label_x, value_x, y, "CPU:", cpu_model, W98_BTNTEXT);
    y += line_h;
    label_value(label_x, value_x, y, "Memory:", "", W98_BTNTEXT);
    int_to_string(total_mb, buf);
    kstrcat(buf, " MB");
    w98_text(buf, value_x, y, W98_BTNTEXT, W98_BTNFACE, 1, NULL);
    y += line_h;
    label_value(label_x, value_x, y, "Memory Free:", "", W98_BTNTEXT);
    int_to_string((uint32_t)(free_kb >> 10), buf);
    kstrcat(buf, " MB");
    w98_text(buf, value_x, y, W98_BTNTEXT, W98_BTNFACE, 1, NULL);
    y += line_h;
    label_value(label_x, value_x, y, "Memory Avail:", "", W98_BTNTEXT);
    int_to_string((uint32_t)(avail_kb >> 10), buf);
    kstrcat(buf, " MB");
    w98_text(buf, value_x, y, W98_BTNTEXT, W98_BTNFACE, 1, NULL);
    y += line_h + 4;
    label_value(label_x, value_x, y, "Resolution:", "", W98_BTNTEXT);
    int_to_string((uint32_t)screen_width, buf);
    kstrcat(buf, "x");
    char b2[16];
    int_to_string((uint32_t)screen_height, b2);
    kstrcat(buf, b2);
    w98_text(buf, value_x, y, W98_BTNTEXT, W98_BTNFACE, 1, NULL);
    y += line_h + 4;
    label_value(label_x, value_x, y, "Uptime:", "", W98_BTNTEXT);
    int_to_string(hours, buf);
    kstrcat(buf, "h ");
    int_to_string(mins, b2);
    kstrcat(buf, b2);
    kstrcat(buf, "m ");
    int_to_string(secs, b2);
    kstrcat(buf, b2);
    kstrcat(buf, "s");
    w98_text(buf, value_x, y, W98_BTNTEXT, W98_BTNFACE, 1, NULL);
    y += line_h;
    label_value(label_x, value_x, y, "Tasks:", "", W98_BTNTEXT);
    int_to_string((uint32_t)task_count, buf);
    w98_text(buf, value_x, y, W98_BTNTEXT, W98_BTNFACE, 1, NULL);
    y += line_h;
    label_value(label_x, value_x, y, "Theme:", "Windows 98 (SharkOS 98)",
                W98_BTNTEXT);
}

/* ----------------------------------------------------------------- notepad */

static char notepad_buffer[4096] = "";
static int notepad_len = 0;
static char notepad_path[128] = "";
static int notepad_cursor = 0;
static bool notepad_dirty = false;

#define NOTEPAD_MENU_H 16

static const char* notepad_menus[] = { "File", "Edit", "Search", "Help" };

void app_window_draw_notepad(window_t* w) {
    int cx = w->rect.client_x;
    int cy = w->rect.client_y;
    int cw = w->rect.client_w;
    int ch = w->rect.client_h;

    w98_fill(cx, cy, cw, ch, W98_BTNFACE);

    /* Menu strip. */
    int mx = cx + 2;
    for (int i = 0; i < 4; i++) {
        int tw = w98_text_width(notepad_menus[i], 1) + 10;
        w98_text(notepad_menus[i], mx + 5, cy + 4, W98_BTNTEXT, W98_BTNFACE,
                 1, NULL);
        mx += tw;
    }

    char title[160];
    int p = 0;
    if (notepad_path[0]) { strcpy(title + p, notepad_path); p = (int)strlen(title); }
    else { strcpy(title + p, "Untitled"); p = (int)strlen(title); }
    if (notepad_dirty) { strcpy(title + p, " *"); }
    w98_text(title, cx + cw - w98_text_width(title, 1) - 6, cy + 4,
             W98_GRAYTEXT, W98_BTNFACE, 1, NULL);

    /* White sunken text area. */
    int ta_x = cx + 2, ta_y = cy + NOTEPAD_MENU_H + 2;
    int ta_w = cw - 4, ta_h = ch - NOTEPAD_MENU_H - 6;
    w98_surface(ta_x, ta_y, ta_w, ta_h, W98_BEVEL_SUNKEN, W98_WINDOW);

    w98_rect_t clip;
    clip.x = ta_x + 3; clip.y = ta_y + 3; clip.w = ta_w - 6; clip.h = ta_h - 6;

    int max_chars_per_line = (ta_w - 8) / 6;
    if (max_chars_per_line > 90) max_chars_per_line = 90;
    int visible_lines = (ta_h - 8) / 11;

    int line = 0;
    int col = 0;
    for (int i = 0; i < notepad_len && line < visible_lines; i++) {
        char c = notepad_buffer[i];
        if (c == '\n') { line++; col = 0; }
        else {
            if (col < max_chars_per_line) {
                char str[2] = { c, 0 };
                w98_text(str, ta_x + 4 + col * 6, ta_y + 4 + line * 11,
                         W98_WINDOWTEXT, W98_WINDOW, 1, &clip);
                col++;
            }
        }
    }

    static int blink = 0;
    if (blink < 30 && notepad_cursor <= notepad_len) {
        int cur_line = 0;
        int cur_col = 0;
        for (int i = 0; i < notepad_cursor && i < notepad_len; i++) {
            if (notepad_buffer[i] == '\n') { cur_line++; cur_col = 0; }
            else { cur_col++; }
        }
        if (cur_line < visible_lines && cur_col < max_chars_per_line) {
            int cur_x = ta_x + 4 + cur_col * 6;
            int cur_y = ta_y + 4 + cur_line * 11;
            if (cur_x >= clip.x && cur_y >= clip.y &&
                cur_x < clip.x + clip.w && cur_y < clip.y + clip.h) {
                w98_fill(cur_x, cur_y, 6, 11, W98_HIGHLIGHT);
            }
        }
    }
    if (blink++ >= 60) blink = 0;
}

void app_window_keyboard_notepad(window_t* w, char c) {
    if (c == '\n') {
        if (notepad_len < (int)sizeof(notepad_buffer) - 1) {
            if (notepad_cursor < notepad_len) {
                kmemmove(notepad_buffer + notepad_cursor + 1,
                         notepad_buffer + notepad_cursor,
                         notepad_len - notepad_cursor);
            }
            notepad_buffer[notepad_cursor] = '\n';
            notepad_len++; notepad_cursor++; notepad_dirty = true;
        }
    } else if (c == '\b') {
        if (notepad_cursor > 0) {
            notepad_cursor--;
            kmemmove(notepad_buffer + notepad_cursor,
                     notepad_buffer + notepad_cursor + 1,
                     notepad_len - notepad_cursor - 1);
            notepad_len--; notepad_dirty = true;
        }
    } else if (c == 27) { return; }
    else if (c >= 32 && c < 127) {
        if (notepad_len < (int)sizeof(notepad_buffer) - 1) {
            if (notepad_cursor < notepad_len) {
                kmemmove(notepad_buffer + notepad_cursor + 1,
                         notepad_buffer + notepad_cursor,
                         notepad_len - notepad_cursor);
            }
            notepad_buffer[notepad_cursor] = c;
            notepad_len++; notepad_cursor++; notepad_dirty = true;
        }
    }
    w->needs_redraw = true;
}

/* ------------------------------------------------------------ file manager */

static struct fs_node* fm_current_dir = NULL;
static int fm_hovered_item = -1;
static int fm_items_y[21];

#define FM_STATUS_H 18

void app_window_draw_filemanager(window_t* w) {
    int cx = w->rect.client_x;
    int cy = w->rect.client_y;
    int cw = w->rect.client_w;
    int ch = w->rect.client_h;

    w98_fill(cx, cy, cw, ch, W98_BTNFACE);

    if (!fm_current_dir) fm_current_dir = root;

    /* Address bar. */
    char path[128] = "";
    struct fs_node* tmp = fm_current_dir;
    char parts[8][32];
    int pcount = 0;
    while (tmp && tmp != root && pcount < 8) {
        strcpy(parts[pcount], tmp->name); pcount++; tmp = tmp->parent;
    }
    for (int i = pcount - 1; i >= 0; i--) {
        if (path[0]) kstrcat(path, "\\");
        kstrcat(path, parts[i]);
    }
    if (path[0] == '\0') strcpy(path, "C:\\");

    w98_surface(cx + 2, cy + 2, cw - 4, 16, W98_BEVEL_SUNKEN, W98_WINDOW);
    w98_text(path, cx + 6, cy + 6, W98_WINDOWTEXT, W98_WINDOW, 1, NULL);

    /* Listview. */
    int lv_y = cy + 22;
    int lv_h = ch - 22 - FM_STATUS_H - 4;
    w98_surface(cx + 2, lv_y, cw - 4, lv_h, W98_BEVEL_SUNKEN, W98_WINDOW);

    int y = lv_y + 4;
    int max_items = (lv_h - 8) / 16;
    if (max_items > 20) max_items = 20;
    fm_hovered_item = -1;
    fm_items_y[0] = y;

    /* ".." entry */
    bool hov0 = (desktop_mouse_x >= cx + 6 && desktop_mouse_x < cx + cw - 6 &&
                 desktop_mouse_y >= y && desktop_mouse_y < y + 16);
    if (hov0) {
        fm_hovered_item = 0;
        w98_fill(cx + 4, y, cw - 8, 16, W98_HIGHLIGHT);
    }
    w98_icon_blit_small(desktop_icon_get_folder(), ICON_SIZE, 16, cx + 6,
                        y, NULL);
    w98_text("..", cx + 26, y + 4,
             hov0 ? W98_HIGHLIGHTTEXT : W98_WINDOWTEXT,
             hov0 ? W98_HIGHLIGHT : W98_WINDOW, 1, NULL);
    y += 16;

    for (int i = 0; i < fm_current_dir->num_children && i < max_items - 1;
         i++) {
        struct fs_node* child = fm_current_dir->children[i];
        fm_items_y[i + 1] = y;

        bool hovered = (desktop_mouse_x >= cx + 6 &&
                        desktop_mouse_x < cx + cw - 6 &&
                        desktop_mouse_y >= y && desktop_mouse_y < y + 16);
        if (hovered) {
            fm_hovered_item = i + 1;
            w98_fill(cx + 4, y, cw - 8, 16, W98_HIGHLIGHT);
        }

        const uint32_t* glyph =
            (child->type == FS_DIRECTORY) ? desktop_icon_get_folder()
                                          : desktop_icon_get_file();
        w98_icon_blit_small(glyph, ICON_SIZE, 16, cx + 6, y, NULL);
        w98_text(child->name, cx + 26, y + 4,
                 hovered ? W98_HIGHLIGHTTEXT : W98_WINDOWTEXT,
                 hovered ? W98_HIGHLIGHT : W98_WINDOW, 1, NULL);
        y += 16;
    }

    /* Status bar. */
    int sb_y = cy + ch - FM_STATUS_H - 2;
    w98_bevel(cx + 2, sb_y, cw - 4, FM_STATUS_H, W98_BEVEL_SUNKEN);
    char status[40];
    int_to_string((uint32_t)(fm_current_dir->num_children + 1), status);
    kstrcat(status, " object(s)");
    w98_text(status, cx + 8, sb_y + 5, W98_BTNTEXT, W98_BTNFACE, 1, NULL);
}

void app_window_mouse_filemanager(window_t* w, int mx, int my, int buttons) {
    (void)mx; (void)my;
    if (!(buttons & 1)) return;
    if (!fm_current_dir) fm_current_dir = root;
    if (fm_hovered_item == 0) {
        if (fm_current_dir && fm_current_dir->parent) {
            fm_current_dir = fm_current_dir->parent;
        }
    } else if (fm_hovered_item > 0 &&
               fm_hovered_item <= fm_current_dir->num_children) {
        struct fs_node* child = fm_current_dir->children[fm_hovered_item - 1];
        if (child && child->type == FS_DIRECTORY) {
            fm_current_dir = child;
        }
    }
    w->needs_redraw = true;
}

void app_window_keyboard_filemanager(window_t* w, char c) {
    if (!fm_current_dir) fm_current_dir = root;
    if (c == 27) return;
    else if (c == '\b' || c == 8) {
        if (fm_current_dir && fm_current_dir->parent) {
            fm_current_dir = fm_current_dir->parent;
        }
    }
    w->needs_redraw = true;
}

/* ------------------------------------------------------------------- about */

void app_window_draw_about(window_t* w) {
    int cx = w->rect.client_x;
    int cy = w->rect.client_y;
    int cw = w->rect.client_w;

    w98_fill(cx, cy, cw, w->rect.client_h, W98_BTNFACE);

    const uint32_t* start_pix = desktop_icon_get_start();
    w98_icon_blit(start_pix, 16, 32, cx + cw / 2 - 16, cy + 12, NULL);

    w98_text_bold("About SharkOS 98", cx + cw / 2 - 54, cy + 52, W98_BTNTEXT,
                  W98_BTNFACE, 1, NULL);
    w98_bevel(cx + 8, cy + 66, cw - 16, 2, W98_BEVEL_ETCHED);
    w98_text("Version: 2.2 Desktop (Windows 98 skin)", cx + cw / 2 - 110,
             cy + 76, W98_BTNTEXT, W98_BTNFACE, 1, NULL);
    w98_text("A Win98-styled desktop environment", cx + cw / 2 - 96, cy + 92,
             W98_BTNTEXT, W98_BTNFACE, 1, NULL);
    w98_text("for the SharkOS kernel", cx + cw / 2 - 66, cy + 106,
             W98_BTNTEXT, W98_BTNFACE, 1, NULL);
    w98_text("Created by Mayshecry", cx + cw / 2 - 66, cy + 128,
             W98_GRAYTEXT, W98_BTNFACE, 1, NULL);
}

/* ----------------------------------------------------------------- network */

static void append_u8(char* dst, uint32_t v) {
    char b[16];
    int_to_string(v, b);
    kstrcat(dst, b);
}

static void append_ip(char* dst, const uint8_t* ip) {
    append_u8(dst, ip[0]); kstrcat(dst, ".");
    append_u8(dst, ip[1]); kstrcat(dst, ".");
    append_u8(dst, ip[2]); kstrcat(dst, ".");
    append_u8(dst, ip[3]);
}

void app_window_draw_network(window_t* w) {
    int cx = w->rect.client_x;
    int cy = w->rect.client_y;
    int cw = w->rect.client_w;

    w98_fill(cx, cy, cw, w->rect.client_h, W98_BTNFACE);

    w98_text_bold("Network Status", cx + cw / 2 - 48, cy + 6, W98_BTNTEXT,
                  W98_BTNFACE, 1, NULL);
    w98_bevel(cx + 4, cy + 18, cw - 8, 2, W98_BEVEL_ETCHED);

    int y = cy + 26;
    char b[16];

    label_value(cx + 8, cx + 80, y, "NIC:", net_driver_name, W98_BTNTEXT);
    y += 14;

    char mac_str[20];
    int mp = 0;
    for (int i = 0; i < 6; i++) {
        hex_to_string(net_mac[i], b);
        if (b[0] == '0') { mac_str[mp++] = b[2]; mac_str[mp++] = b[3]; }
        else { mac_str[mp++] = b[0]; mac_str[mp++] = b[1]; }
        if (i < 5) mac_str[mp++] = ':';
    }
    mac_str[mp] = 0;
    label_value(cx + 8, cx + 80, y, "MAC:", mac_str, W98_BTNTEXT);
    y += 14;

    label_value(cx + 8, cx + 80, y, "Link:", net_has_link ? "UP" : "DOWN",
                net_has_link ? 0xFF008000u : 0xFFA00000u);
    y += 14;
    label_value(cx + 8, cx + 80, y, "Configured:",
                net_configured ? "YES" : "NO",
                net_configured ? 0xFF008000u : 0xFFA00000u);
    y += 18;

    char ip_str[20] = "";
    append_ip(ip_str, net_ip);
    label_value(cx + 8, cx + 80, y, "IP:", ip_str, W98_BTNTEXT);
    y += 14;
    char mask_str[20] = "";
    append_ip(mask_str, net_mask);
    label_value(cx + 8, cx + 80, y, "Mask:", mask_str, W98_BTNTEXT);
    y += 14;
    char gw_str[20] = "";
    append_ip(gw_str, net_gw);
    label_value(cx + 8, cx + 80, y, "Gateway:", gw_str, W98_BTNTEXT);
    y += 14;
    char dns_str[20] = "";
    append_ip(dns_str, net_dns);
    label_value(cx + 8, cx + 80, y, "DNS:", dns_str, W98_BTNTEXT);
    y += 20;

    w98_text_bold("Commands:", cx + 8, y, W98_BTNTEXT, W98_BTNFACE, 1, NULL);
    y += 14;
    w98_text("ifconfig, dhcp, ping <ip>, dns <host>,", cx + 8, y,
             W98_GRAYTEXT, W98_BTNFACE, 1, NULL);
    y += 12;
    w98_text("wget <url>, netstat   (use in Terminal)", cx + 8, y,
             W98_GRAYTEXT, W98_BTNFACE, 1, NULL);
}

/* ---------------------------------------------------------- key forwarding */

void app_window_keyboard_terminal(window_t* w, char c) {
    if (c == 27) return; /* ESC - let the desktop handle it */
    if (c == '\n') {
        if (terminal_buf_len > 0) {
            terminal_buffer[terminal_buf_len] = '\0';
            terminal_output_len = 0;
            strcpy(terminal_output, "$ ");
            terminal_output_len = 2;
            strcpy(terminal_output + terminal_output_len, terminal_buffer);
            terminal_output_len += terminal_buf_len;
            terminal_output[terminal_output_len++] = '\n';
            terminal_output[terminal_output_len] = '\0';
            if (!terminal_capture_buffer) {
                terminal_capture_buffer = (char*)kmalloc(4096);
            }
            if (terminal_capture_buffer) {
                terminal_capture_len = 0;
                execute_command(terminal_buffer);
                if (terminal_capture_len > 0) {
                    int copy_len = terminal_capture_len;
                    if (copy_len > 1000) copy_len = 1000;
                    for (int i = 0; i < copy_len; i++) {
                        terminal_output[terminal_output_len++] =
                            terminal_capture_buffer[i];
                    }
                    terminal_output[terminal_output_len] = '\0';
                }
                terminal_capture_buffer = NULL;
                terminal_capture_len = 0;
            }
            terminal_buf_len = 0;
            terminal_output_len = (int)strlen(terminal_output);
        }
        w->needs_redraw = true;
    } else if (c == '\b') {
        if (terminal_buf_len > 0) { terminal_buf_len--; w->needs_redraw = true; }
    } else if (c >= 32 && c < 127) {
        if (terminal_buf_len < 255) {
            terminal_buffer[terminal_buf_len++] = c;
            w->needs_redraw = true;
        }
    }
}

void app_window_keyboard_doom(window_t* w, char c) { (void)w; doom_handle_key((int)c); }
void app_window_keyboard_flappybird(window_t* w, char c) { (void)w; flappybird_handle_key((int)c); }
void app_window_keyboard_smb(window_t* w, char c) { (void)w; smb_handle_key((int)c); }

void app_window_keyboard_pong(window_t* w, char c) {
    if (c == 27) { window_close_by_ptr(w); return; }
    pong_handle_key((int)c);
}

void app_window_keyboard_gdash(window_t* w, char c) {
    if (c == 27) { window_close_by_ptr(w); return; }
    gd_handle_key((int)c);
}
