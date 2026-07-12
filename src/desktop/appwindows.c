
#include "kernel.h"
#include "desktop.h"
#include "doom.h"
#include "flappybird.h"
#include "smb.h"
#include "pong.h"
#include "geometrydash.h"
#include "net.h"


static void kmemmove(void* dst, const void* src, int n) {
    char* d = (char*)dst;
    const char* s = (const char*)src;
    for (int i = 0; i < n; i++) d[i] = s[i];
}
static void kstrcat(char* dst, const char* src) {
    while (*dst) dst++;
    while ((*dst++ = *src++));
}
static int kstrcmp(const char* a, const char* b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}


static char terminal_buffer[256] = "";
static int terminal_buf_len = 0;
static char terminal_output[4096] = "";
static int terminal_output_len = 0;

void app_window_draw_terminal(window_t* w) {
    int cx = w->rect.client_x;
    int cy = w->rect.client_y;
    int cw = w->rect.client_w;
    int ch = w->rect.client_h;
    
    draw_rect(cx, cy, cw, ch, 0xFF000000);
    draw_string_px("SharkOS Terminal", cx + 4, cy + 4, 0xFF00FFFF, 0xFF000000);
    
    int text_y = cy + 20;
    int max_chars_per_line = (cw - 8) / 8;
    if (max_chars_per_line > 80) max_chars_per_line = 80;
    
    int current_line = 0;
    int max_lines = (ch - 30) / 10;
    if (max_lines > 40) max_lines = 40;
    if (terminal_output_len > 0) {
        for (int i = 0; i < terminal_output_len && current_line < max_lines; i++) {
            int start = i;
            int len = 0;
            while (i < terminal_output_len && terminal_output[i] != '\n' && len < max_chars_per_line) {
                i++;
                len++;
            }
            char line_buf[81];
            for (int j = 0; j < len && j < 80; j++) {
                line_buf[j] = terminal_output[start + j];
            }
            line_buf[len] = '\0';
            draw_string_px(line_buf, cx + 4, text_y + current_line * 10, 0xFFAAAAAA, 0xFF000000);
            i++;
            current_line++;
        }
    }
    
    draw_string_px("$ ", cx + 4, text_y + current_line * 10, 0xFF00FF00, 0xFF000000);
    
    char temp[256];
    int i;
    for (i = 0; i < terminal_buf_len && i < max_chars_per_line - 2; i++) {
        temp[i] = terminal_buffer[i];
    }
    temp[i] = '\0';
    draw_string_px(temp, cx + 24, text_y + current_line * 10, 0xFFFFFF00, 0xFF000000);
    
    static int blink = 0;
    if (blink < 30) {
        int cursor_x = cx + 24 + i * 8;
        draw_rect(cursor_x, text_y + current_line * 10, 8, 8, 0xFF00FF00);
    }
    if (blink++ >= 60) blink = 0;
}

void app_window_draw_doom(window_t* w) { doom_draw_frame(); }
void app_window_draw_flappybird(window_t* w) { flappybird_draw_frame(); }
void app_window_draw_smb(window_t* w) { smb_draw_frame(); }
void app_window_draw_pong(window_t* w) { pong_draw_frame(); }
void app_window_draw_gdash(window_t* w) { gd_draw_frame(); }

typedef struct {
    int y;
    uint32_t color;
    char label[32];
    bool selected;
} settings_color_opt_t;

static settings_color_opt_t settings_colors[] = {
    {0, 0xFF0A0A2A, "Dark Blue (Default)", false},
    {0, 0xFF1A0A0A, "Dark Red", false},
    {0, 0xFF0A1A0A, "Dark Green", false},
    {0, 0xFF1A1A0A, "Amber", false},
    {0, 0xFF0A0A0A, "Pure Black", false},
    {0, 0xFF2A1A0A, "Brown", false},
    {0, 0xFF1A0A2A, "Purple", false},
    {0, 0xFF0A1A2A, "Teal", false},
};

#define SETTINGS_COLOR_COUNT 8
#define SETTINGS_BTN_H 18
#define SETTINGS_BTN_GAP 2

static int settings_scroll = 0;
static int settings_sel_theme = 0;
static int settings_hovered_item = -1;

void app_window_draw_settings(window_t* w) {
    int cx = w->rect.client_x;
    int cy = w->rect.client_y;
    int cw = w->rect.client_w;
    int ch = w->rect.client_h;
    
    draw_rect(cx, cy, cw, ch, 0xFF000000);
    
    int y = cy + 6;
    
    /* Title */
    draw_string_px("Settings - Customize", cx + cw/2 - 56, y, 0xFF00FFFF, 0xFF000000);
    y += 18;
    
    /* Separator */
    for (int x = cx + 4; x < cx + cw - 4; x++) {
        if (y < (int)screen_height && x < (int)screen_width) {
            uint32_t stride = (uint32_t)(screen_pitch / 4);
            lfbptr[y * stride + x] = 0xFF333355;
        }
    }
    y += 4;
    
    /* Theme Section */
    draw_string_px("Wallpaper Color:", cx + 6, y, 0xFF00FFFF, 0xFF000000);
    y += 16;
    
    int vis_y = y - settings_scroll;
    settings_hovered_item = -1;
    
    for (int i = 0; i < SETTINGS_COLOR_COUNT; i++) {
        int by = y + i * (SETTINGS_BTN_H + SETTINGS_BTN_GAP);
        int draw_y = cy + (by - cy) - settings_scroll + 6;
        
        if (draw_y < cy - SETTINGS_BTN_H || draw_y > cy + ch) continue;
        
        settings_colors[i].y = draw_y;
        
        int btn_x = cx + 8;
        int btn_w = 16;
        
        /* Color swatch */
        draw_rect(btn_x, draw_y, btn_w, SETTINGS_BTN_H, settings_colors[i].color);
        /* Border */
        for (int b = 0; b < 1; b++) {
            if (btn_x < (int)screen_width && draw_y < (int)screen_height) {
                uint32_t stride = (uint32_t)(screen_pitch / 4);
                lfbptr[draw_y * stride + btn_x + b] = 0xFF555555;
                lfbptr[(draw_y + SETTINGS_BTN_H - 1) * stride + btn_x + b] = 0xFF555555;
            }
        }
        for (int b = 0; b < SETTINGS_BTN_H; b++) {
            if (btn_x < (int)screen_width && draw_y + b < (int)screen_height) {
                uint32_t stride = (uint32_t)(screen_pitch / 4);
                lfbptr[(draw_y + b) * stride + btn_x] = 0xFF555555;
                lfbptr[(draw_y + b) * stride + btn_x + btn_w - 1] = 0xFF555555;
            }
        }
        
        /* Label */
        uint32_t text_color = 0xFFAAAAAA;
        bool is_current = (settings_colors[i].color == wallpaper_top);
        if (is_current) text_color = 0xFF00FF00;
        
        draw_string_px(settings_colors[i].label, cx + 30, draw_y + (SETTINGS_BTN_H - 8) / 2, text_color, 0xFF000000);
        
        if (is_current) {
            draw_string_px(" <- Active", cx + 30 + strlen(settings_colors[i].label) * 8 + 4, draw_y + (SETTINGS_BTN_H - 8) / 2, 0xFF00FF00, 0xFF000000);
        }
        
        /* Check if mouse is hovering */
        if (desktop_mouse_y >= draw_y && desktop_mouse_y < draw_y + SETTINGS_BTN_H &&
            desktop_mouse_x >= cx + 8 && desktop_mouse_x < cx + cw - 8) {
            settings_hovered_item = i;
            /* Highlight */
            draw_rect(cx + 6, draw_y, cw - 12, SETTINGS_BTN_H, 0xFF1A1A3A);
            /* Redraw swatch on top */
            draw_rect(btn_x, draw_y, btn_w, SETTINGS_BTN_H, settings_colors[i].color);
        }
    }
    
    /* Mouse toggle section */
    y += SETTINGS_COLOR_COUNT * (SETTINGS_BTN_H + SETTINGS_BTN_GAP) + 8;
    int scroll_y = cy + 6;
    int section_y = scroll_y + (y - cy) - settings_scroll;
    
    if (section_y > cy && section_y < cy + ch - 20) {
        for (int x = cx + 4; x < cx + cw - 4; x++) {
            if (section_y < (int)screen_height && x < (int)screen_width) {
                uint32_t stride = (uint32_t)(screen_pitch / 4);
                lfbptr[section_y * stride + x] = 0xFF333355;
            }
        }
    }
}

void app_window_mouse_settings(window_t* w, int mx, int my, int buttons) {
    (void)w;
    if (!(buttons & 1)) return;
    
    if (settings_hovered_item >= 0 && settings_hovered_item < SETTINGS_COLOR_COUNT) {
        uint32_t color = settings_colors[settings_hovered_item].color;
        /* Apply as wallpaper top/bottom gradient */
        wallpaper_top = color;
        /* Slightly darker for bottom */
        uint8_t r = (color >> 16) & 0xFF;
        uint8_t g = (color >> 8) & 0xFF;
        uint8_t b = color & 0xFF;
        wallpaper_bot = (0xFF << 24) | ((r/2) << 16) | ((g/2) << 8) | (b/2);
        desktop.dirty = true;
        w->needs_redraw = true;
    }
}

void app_window_draw_faq(window_t* w) {
    int cx = w->rect.client_x;
    int cy = w->rect.client_y;
    int cw = w->rect.client_w;
    draw_rect(cx, cy, w->rect.client_w, w->rect.client_h, 0xFF000000);
    draw_string_px("SharkOS FAQ", cx + cw/2 - 36, cy + 8, 0xFF00FFFF, 0xFF000000);
    draw_string_px("Q: Who made SharkOS?", cx + 8, cy + 28, 0xFFAAAAAA, 0xFF000000);
    draw_string_px("A: Mayshecry", cx + 8, cy + 42, 0xFF00FF00, 0xFF000000);
}

void app_window_draw_fastfetch(window_t* w) {
    int cx = w->rect.client_x;
    int cy = w->rect.client_y;
    int cw = w->rect.client_w;
    int ch = w->rect.client_h;
    draw_rect(cx, cy, cw, ch, 0xFF000000);
    draw_string_px("SharkOS System Information", cx + 4, cy + 4, 0xFF00FFFF, 0xFF000000);
    draw_string_px("==========================", cx + 4, cy + 12, 0xFF00AAAA, 0xFF000000);
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
    int y = cy + 28;
    int label_x = cx + 8;
    int value_x = cx + 140;
    int line_h = 14;
    draw_string_px("OS:", label_x, y, 0xFF00FFFF, 0xFF000000);
    draw_string_px("SharkOS V2 - Sharkslayer", value_x, y, 0xFFAAAAAA, 0xFF000000);
    y += line_h;
    draw_string_px("Host:", label_x, y, 0xFF00FFFF, 0xFF000000);
    draw_string_px("SharkOS PC", value_x, y, 0xFFAAAAAA, 0xFF000000);
    y += line_h;
    draw_string_px("Kernel:", label_x, y, 0xFF00FFFF, 0xFF000000);
    draw_string_px("SharkOS V2.2", value_x, y, 0xFFAAAAAA, 0xFF000000);
    y += line_h;
    draw_string_px("Shell:", label_x, y, 0xFF00FFFF, 0xFF000000);
    draw_string_px("nemo-shell", value_x, y, 0xFFAAAAAA, 0xFF000000);
    y += line_h;
    draw_string_px("Arch:", label_x, y, 0xFF00FFFF, 0xFF000000);
    draw_string_px("x86 (32-bit)", value_x, y, 0xFFAAAAAA, 0xFF000000);
    y += line_h + 4;
    draw_string_px("CPU:", label_x, y, 0xFF00FFFF, 0xFF000000);
    draw_string_px(cpu_model, value_x, y, 0xFFAAAAAA, 0xFF000000);
    y += line_h;
    draw_string_px("CPU Speed:", label_x, y, 0xFF00FFFF, 0xFF000000);
    draw_string_px("2394.466 MHz", value_x, y, 0xFFAAAAAA, 0xFF000000);
    y += line_h;
    draw_string_px("Cache:", label_x, y, 0xFF00FFFF, 0xFF000000);
    draw_string_px("6144 KB", value_x, y, 0xFFAAAAAA, 0xFF000000);
    y += line_h + 4;
    draw_string_px("Memory:", label_x, y, 0xFF00FFFF, 0xFF000000);
    int_to_string(total_mb, buf);
    draw_string_px(buf, value_x, y, 0xFFAAAAAA, 0xFF000000);
    draw_string_px(" MB", value_x + strlen(buf)*8, y, 0xFFAAAAAA, 0xFF000000);
    y += line_h;
    draw_string_px("Memory Free:", label_x, y, 0xFF00FFFF, 0xFF000000);
    int_to_string((uint32_t)(free_kb >> 10), buf);
    draw_string_px(buf, value_x, y, 0xFFAAAAAA, 0xFF000000);
    draw_string_px(" MB", value_x + strlen(buf)*8, y, 0xFFAAAAAA, 0xFF000000);
    y += line_h;
    draw_string_px("Memory Avail:", label_x, y, 0xFF00FFFF, 0xFF000000);
    int_to_string((uint32_t)(avail_kb >> 10), buf);
    draw_string_px(buf, value_x, y, 0xFFAAAAAA, 0xFF000000);
    draw_string_px(" MB", value_x + strlen(buf)*8, y, 0xFFAAAAAA, 0xFF000000);
    y += line_h + 4;
    draw_string_px("Resolution:", label_x, y, 0xFF00FFFF, 0xFF000000);
    int_to_string(screen_width, buf);
    draw_string_px(buf, value_x, y, 0xFFAAAAAA, 0xFF000000);
    draw_string_px("x", value_x + strlen(buf)*8, y, 0xFFAAAAAA, 0xFF000000);
    int_to_string(screen_height, buf);
    draw_string_px(buf, value_x + strlen(buf)*8 + 8, y, 0xFFAAAAAA, 0xFF000000);
    y += line_h + 4;
    draw_string_px("Uptime:", label_x, y, 0xFF00FFFF, 0xFF000000);
    int_to_string(hours, buf);
    draw_string_px(buf, value_x, y, 0xFFAAAAAA, 0xFF000000);
    draw_string_px("h ", value_x + strlen(buf)*8, y, 0xFFAAAAAA, 0xFF000000);
    int_to_string(mins, buf);
    draw_string_px(buf, value_x + strlen(buf)*8 + 16, y, 0xFFAAAAAA, 0xFF000000);
    draw_string_px("m ", value_x + strlen(buf)*8 + 24, y, 0xFFAAAAAA, 0xFF000000);
    int_to_string(secs, buf);
    draw_string_px(buf, value_x + strlen(buf)*8 + 32, y, 0xFFAAAAAA, 0xFF000000);
    draw_string_px("s", value_x + strlen(buf)*8 + 40, y, 0xFFAAAAAA, 0xFF000000);
    y += line_h;
    draw_string_px("Tasks:", label_x, y, 0xFF00FFFF, 0xFF000000);
    int_to_string(task_count, buf);
    draw_string_px(buf, value_x, y, 0xFFAAAAAA, 0xFF000000);
    y += line_h;
    draw_string_px("Theme:", label_x, y, 0xFF00FFFF, 0xFF000000);
    draw_string_px("SharkOS Blue", value_x, y, 0xFFAAAAAA, 0xFF000000);
}

static char notepad_buffer[4096] = "";
static int notepad_len = 0;
static char notepad_path[128] = "";
static int notepad_cursor = 0;
static bool notepad_dirty = false;

void app_window_draw_notepad(window_t* w) {
    int cx = w->rect.client_x;
    int cy = w->rect.client_y;
    int cw = w->rect.client_w;
    int ch = w->rect.client_h;
    draw_rect(cx, cy, cw, ch, 0xFF000000);
    draw_string_px("Notepad", cx + 4, cy + 4, 0xFF00FFFF, 0xFF000000);
    char title[160];
    int p = 0;
    if (notepad_path[0]) { strcpy(title + p, notepad_path); p = strlen(title); }
    else { strcpy(title + p, "Untitled"); p = strlen(title); }
    if (notepad_dirty) { strcpy(title + p, " *"); p += 2; }
    draw_string_px(title, cx + 4, cy + 14, 0xFFAAAAAA, 0xFF000000);
    int ta_x = cx + 4;
    int ta_y = cy + 24;
    int ta_w = cw - 12;
    int ta_h = ch - 30;
    draw_rect(ta_x, ta_y, ta_w, ta_h, 0xFF0A0A2A);
    int max_chars_per_line = (ta_w - 4) / 8;
    if (max_chars_per_line > 80) max_chars_per_line = 80;
    int visible_lines = ta_h / 12;
    int line = 0;
    int col = 0;
    for (int i = 0; i < notepad_len && line < visible_lines; i++) {
        char c = notepad_buffer[i];
        if (c == '\n') { line++; col = 0; }
        else {
            if (col < max_chars_per_line) {
                int px = ta_x + 4 + col * 8;
                int py = ta_y + 4 + line * 12;
                char str[2] = {c, 0};
                draw_string_px(str, px, py, 0xFFAAAAAA, 0xFF0A0A2A);
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
            int cur_x = ta_x + 4 + cur_col * 8;
            int cur_y = ta_y + 4 + cur_line * 12;
            draw_rect(cur_x, cur_y, 8, 12, 0xFF00FFFF);
        }
    }
    if (blink++ >= 60) blink = 0;
}

void app_window_keyboard_notepad(window_t* w, char c) {
    if (c == '\n') {
        if (notepad_len < sizeof(notepad_buffer) - 1) {
            if (notepad_cursor < notepad_len) kmemmove(notepad_buffer + notepad_cursor + 1, notepad_buffer + notepad_cursor, notepad_len - notepad_cursor);
            notepad_buffer[notepad_cursor] = '\n';
            notepad_len++; notepad_cursor++; notepad_dirty = true;
        }
    } else if (c == '\b') {
        if (notepad_cursor > 0) {
            notepad_cursor--;
            kmemmove(notepad_buffer + notepad_cursor, notepad_buffer + notepad_cursor + 1, notepad_len - notepad_cursor - 1);
            notepad_len--; notepad_dirty = true;
        }
    } else if (c == 27) { return; }
    else if (c >= 32 && c < 127) {
        if (notepad_len < sizeof(notepad_buffer) - 1) {
            if (notepad_cursor < notepad_len) kmemmove(notepad_buffer + notepad_cursor + 1, notepad_buffer + notepad_cursor, notepad_len - notepad_cursor);
            notepad_buffer[notepad_cursor] = c;
            notepad_len++; notepad_cursor++; notepad_dirty = true;
        }
    }
    w->needs_redraw = true;
}

static struct fs_node* fm_current_dir = NULL;
static int fm_hovered_item = -1;
static int fm_items_y[21];

void app_window_draw_filemanager(window_t* w) {
    int cx = w->rect.client_x;
    int cy = w->rect.client_y;
    int cw = w->rect.client_w;
    int ch = w->rect.client_h;
    draw_rect(cx, cy, cw, ch, 0xFF000000);
    draw_string_px("File Manager", cx + cw/2 - 40, cy + 4, 0xFF00FFFF, 0xFF000000);
    if (!fm_current_dir) fm_current_dir = root;
    char path[128] = "";
    struct fs_node* tmp = fm_current_dir;
    char parts[8][32];
    int pcount = 0;
    while (tmp && tmp != root && pcount < 8) { strcpy(parts[pcount], tmp->name); pcount++; tmp = tmp->parent; }
    for (int i = pcount-1; i >= 0; i--) { if (path[0]) kstrcat(path, "/"); kstrcat(path, parts[i]); }
    if (path[0] == '\0') strcpy(path, "/");
    draw_rect(cx + 4, cy + 16, cw - 12, 14, 0xFF0A0A2A);
    draw_string_px(path, cx + 6, cy + 18, 0xFF00FF00, 0xFF0A0A2A);
    int y = cy + 34;
    int max_items = (ch - 40) / 14;
    if (max_items > 20) max_items = 20;
    fm_hovered_item = -1;
    fm_items_y[0] = y;
    draw_string_px("..", cx + 8, y, 0xFFFFFF00, 0xFF000000);
    y += 14;
    for (int i = 0; i < fm_current_dir->num_children && i < max_items-1; i++) {
        struct fs_node* child = fm_current_dir->children[i];
        fm_items_y[i+1] = y;
        uint32_t color = (child->type == FS_DIRECTORY) ? 0xFF00FFFF : 0xFFAAAAAA;
        bool hovered = (desktop_mouse_x >= cx + 8 && desktop_mouse_x < cx + cw - 8 &&
                       desktop_mouse_y >= y && desktop_mouse_y < y + 14);
        if (hovered) {
            fm_hovered_item = i + 1;
            draw_rect(cx + 4, y, cw - 8, 14, 0xFF1A1A3A);
        }
        draw_string_px(child->name, cx + 8, y, hovered ? 0xFF00FFFF : color, 0xFF000000);
        y += 14;
    }
}

void app_window_mouse_filemanager(window_t* w, int mx, int my, int buttons) {
    if (!(buttons & 1)) return;
    if (!fm_current_dir) fm_current_dir = root;
    if (fm_hovered_item == 0) {
        if (fm_current_dir && fm_current_dir->parent) fm_current_dir = fm_current_dir->parent;
    } else if (fm_hovered_item > 0 && fm_hovered_item <= fm_current_dir->num_children) {
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
    else if (c == '\b' || c == 8) { if (fm_current_dir && fm_current_dir->parent) fm_current_dir = fm_current_dir->parent; }
    w->needs_redraw = true;
}

void app_window_draw_about(window_t* w) {
    int cx = w->rect.client_x;
    int cy = w->rect.client_y;
    int cw = w->rect.client_w;
    draw_rect(cx, cy, w->rect.client_w, w->rect.client_h, 0xFF000000);
    draw_string_px("About SharkOS Desktop", cx + cw/2 - 64, cy + 20, 0xFF00FFFF, 0xFF000000);
    draw_string_px("Version: 2.1 Desktop", cx + cw/2 - 56, cy + 40, 0xFFAAAAAA, 0xFF000000);
    draw_string_px("A modern desktop environment", cx + cw/2 - 88, cy + 60, 0xFFAAAAAA, 0xFF000000);
    draw_string_px("for SharkOS kernel", cx + cw/2 - 64, cy + 74, 0xFFAAAAAA, 0xFF000000);
    draw_string_px("Created by Mayshecry", cx + cw/2 - 64, cy + 100, 0xFF00FF00, 0xFF000000);
}

void app_window_draw_network(window_t* w) {
    int cx = w->rect.client_x;
    int cy = w->rect.client_y;
    int cw = w->rect.client_w;
    int ch = w->rect.client_h;
    draw_rect(cx, cy, cw, ch, 0xFF000000);
    int y = cy + 8;
    draw_string_px("Network Status", cx + cw/2 - 48, y, 0xFF00FFFF, 0xFF000000);
    y += 20;
    char b[16];
    draw_string_px("NIC:", cx + 8, y, 0xFF00FFFF, 0xFF000000);
    draw_string_px(net_driver_name, cx + 80, y, 0xFFAAAAAA, 0xFF000000);
    y += 14;
    draw_string_px("MAC:", cx + 8, y, 0xFF00FFFF, 0xFF000000);
    char mac_str[20]; int mp = 0;
    for(int i=0;i<6;i++){ hex_to_string(net_mac[i],b); if(b[0]=='0'){ mac_str[mp++]=b[2]; mac_str[mp++]=b[3]; } else { mac_str[mp++]=b[0]; mac_str[mp++]=b[1]; } if(i<5) mac_str[mp++]=':'; }
    mac_str[mp]=0;
    draw_string_px(mac_str, cx + 80, y, 0xFFAAAAAA, 0xFF000000);
    y += 14;
    draw_string_px("Link:", cx + 8, y, 0xFF00FFFF, 0xFF000000);
    draw_string_px(net_has_link ? "UP" : "DOWN", cx + 80, y, net_has_link ? 0xFF00FF00 : 0xFFFF0000, 0xFF000000);
    y += 14;
    draw_string_px("Configured:", cx + 8, y, 0xFF00FFFF, 0xFF000000);
    draw_string_px(net_configured ? "YES" : "NO", cx + 80, y, net_configured ? 0xFF00FF00 : 0xFFFF0000, 0xFF000000);
    y += 18;
    draw_string_px("IP:", cx + 8, y, 0xFF00FFFF, 0xFF000000);
    char ip_str[20];
    int_to_string(net_ip[0],b); strcpy(ip_str,b); kstrcat(ip_str,".");
    int_to_string(net_ip[1],b); kstrcat(ip_str,b); kstrcat(ip_str,".");
    int_to_string(net_ip[2],b); kstrcat(ip_str,b); kstrcat(ip_str,".");
    int_to_string(net_ip[3],b); kstrcat(ip_str,b);
    draw_string_px(ip_str, cx + 80, y, 0xFFAAAAAA, 0xFF000000);
    y += 14;
    draw_string_px("Mask:", cx + 8, y, 0xFF00FFFF, 0xFF000000);
    char mask_str[20];
    int_to_string(net_mask[0],b); strcpy(mask_str,b); kstrcat(mask_str,".");
    int_to_string(net_mask[1],b); kstrcat(mask_str,b); kstrcat(mask_str,".");
    int_to_string(net_mask[2],b); kstrcat(mask_str,b); kstrcat(mask_str,".");
    int_to_string(net_mask[3],b); kstrcat(mask_str,b);
    draw_string_px(mask_str, cx + 80, y, 0xFFAAAAAA, 0xFF000000);
    y += 14;
    draw_string_px("Gateway:", cx + 8, y, 0xFF00FFFF, 0xFF000000);
    char gw_str[20];
    int_to_string(net_gw[0],b); strcpy(gw_str,b); kstrcat(gw_str,".");
    int_to_string(net_gw[1],b); kstrcat(gw_str,b); kstrcat(gw_str,".");
    int_to_string(net_gw[2],b); kstrcat(gw_str,b); kstrcat(gw_str,".");
    int_to_string(net_gw[3],b); kstrcat(gw_str,b);
    draw_string_px(gw_str, cx + 80, y, 0xFFAAAAAA, 0xFF000000);
    y += 14;
    draw_string_px("DNS:", cx + 8, y, 0xFF00FFFF, 0xFF000000);
    char dns_str[20];
    int_to_string(net_dns[0],b); strcpy(dns_str,b); kstrcat(dns_str,".");
    int_to_string(net_dns[1],b); kstrcat(dns_str,b); kstrcat(dns_str,".");
    int_to_string(net_dns[2],b); kstrcat(dns_str,b); kstrcat(dns_str,".");
    int_to_string(net_dns[3],b); kstrcat(dns_str,b);
    draw_string_px(dns_str, cx + 80, y, 0xFFAAAAAA, 0xFF000000);
    y += 20;
    draw_string_px("Commands:", cx + 8, y, 0xFF00FF00, 0xFF000000);
    y += 14;
    draw_string_px("  ifconfig  - Show network config", cx + 8, y, 0xFF888888, 0xFF000000);
    y += 12;
    draw_string_px("  dhcp      - Run DHCP client", cx + 8, y, 0xFF888888, 0xFF000000);
    y += 12;
    draw_string_px("  ping <ip> - Ping an address", cx + 8, y, 0xFF888888, 0xFF000000);
    y += 12;
    draw_string_px("  dns <host>- DNS lookup", cx + 8, y, 0xFF888888, 0xFF000000);
    y += 12;
    draw_string_px("  wget <url>- HTTP GET", cx + 8, y, 0xFF888888, 0xFF000000);
    y += 12;
    draw_string_px("  netstat   - Network statistics", cx + 8, y, 0xFF888888, 0xFF000000);
}

void app_window_keyboard_terminal(window_t* w, char c) {
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
            if (!terminal_capture_buffer) terminal_capture_buffer = (char*)kmalloc(4096);
            if (terminal_capture_buffer) {
                terminal_capture_len = 0;
                execute_command(terminal_buffer);
                if (terminal_capture_len > 0) {
                    int copy_len = terminal_capture_len;
                    if (copy_len > 1000) copy_len = 1000;
                    for (int i = 0; i < copy_len; i++) terminal_output[terminal_output_len++] = terminal_capture_buffer[i];
                    terminal_output[terminal_output_len] = '\0';
                }
                terminal_capture_buffer = NULL;
                terminal_capture_len = 0;
            }
            terminal_buf_len = 0;
            terminal_output_len = strlen(terminal_output);
        }
        w->needs_redraw = true;
    } else if (c == '\b') {
        if (terminal_buf_len > 0) { terminal_buf_len--; w->needs_redraw = true; }
    } else if (c >= 32 && c < 127) {
        if (terminal_buf_len < 255) { terminal_buffer[terminal_buf_len++] = c; w->needs_redraw = true; }
    }
}

void app_window_keyboard_doom(window_t* w, char c) { (void)w; doom_handle_key((int)c); }
void app_window_keyboard_flappybird(window_t* w, char c) { (void)w; flappybird_handle_key((int)c); }
void app_window_keyboard_smb(window_t* w, char c) { (void)w; smb_handle_key((int)c); }