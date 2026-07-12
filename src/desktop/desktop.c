/* SharkOS Desktop Environment */
#include "kernel.h"
#include "desktop.h"
#include "plugin_manager.h"
#include "doom.h"
#include "flappybird.h"
#include "smb.h"
#include "pong.h"
#include "geometrydash.h"
#include "wallpaper_data.h"

uint32_t wallpaper_top = DESKTOP_BG_TOP;
uint32_t wallpaper_bot = DESKTOP_BG_BOT;
static uint32_t* wallpaper_cache = NULL;
static int wallpaper_cache_w = 0;
static int wallpaper_cache_h = 0;
static bool wallpaper_dirty = true;

void desktop_set_wallpaper_color(uint32_t top, uint32_t bottom) { wallpaper_top = top; wallpaper_bot = bottom; wallpaper_dirty = true; }

void desktop_draw_wallpaper(void) {
    int h = screen_height - TASKBAR_HEIGHT;
    int w = screen_width;
    
    if (WALLPAPER_WIDTH > 0 && WALLPAPER_HEIGHT > 0) {
        uint32_t stride = (uint32_t)(screen_pitch / 4);
        uint32_t h_u = (uint32_t)h, w_u = (uint32_t)w;
        uint32_t wh = (uint32_t)WALLPAPER_HEIGHT, ww = (uint32_t)WALLPAPER_WIDTH;
        uint32_t src_y = 0, src_y_frac = 0;
        uint32_t y_step = (wh << 8) / h_u;
        for (uint32_t y = 0; y < h_u; y++) {
            uint32_t sx = 0, sx_frac = 0;
            uint32_t x_step = (ww << 8) / w_u;
            for (uint32_t x = 0; x < w_u; x++) {
                uint32_t color = wallpaper_pixels[(src_y >> 8) * ww + (sx >> 8)];
                lfbptr[y * stride + x] = color;
                sx += x_step;
            }
            src_y += y_step;
        }
        return;
    }
    
    if (!wallpaper_dirty && wallpaper_cache && wallpaper_cache_w == w && wallpaper_cache_h == h) {
        uint32_t stride = (uint32_t)(screen_pitch / 4);
        for (uint32_t y = 0; y < (uint32_t)h; y++) for (uint32_t x = 0; x < (uint32_t)w; x++) lfbptr[y * stride + x] = wallpaper_cache[y * w + x];
        return;
    }
    if (wallpaper_cache && (wallpaper_cache_w != w || wallpaper_cache_h != h)) { kfree(wallpaper_cache); wallpaper_cache = NULL; }
    if (!wallpaper_cache) { wallpaper_cache = (uint32_t*)kmalloc(w * h * sizeof(uint32_t)); if (!wallpaper_cache) return; wallpaper_cache_w = w; wallpaper_cache_h = h; }
    for (uint32_t y = 0; y < (uint32_t)h; y++) {
        uint32_t t = wallpaper_top, b = wallpaper_bot;
        uint8_t tr = (t >> 16) & 0xFF, tg = (t >> 8) & 0xFF, tb = t & 0xFF;
        uint8_t br = (b >> 16) & 0xFF, bg = (b >> 8) & 0xFF, bb = b & 0xFF;
        uint8_t rr = (uint8_t)((tr * ((uint32_t)h - y) + br * y) / (uint32_t)h);
        uint8_t rg = (uint8_t)((tg * ((uint32_t)h - y) + bg * y) / (uint32_t)h);
        uint8_t rb = (uint8_t)((tb * ((uint32_t)h - y) + bb * y) / (uint32_t)h);
        uint32_t color = (0xFFu << 24) | (rr << 16) | (rg << 8) | rb;
        for (uint32_t x = 0; x < (uint32_t)w; x++) wallpaper_cache[y * w + x] = color;
    }
    uint32_t stride = (uint32_t)(screen_pitch / 4);
    for (uint32_t y = 0; y < (uint32_t)h; y++) for (uint32_t x = 0; x < (uint32_t)w; x++) lfbptr[y * stride + x] = wallpaper_cache[y * w + x];
    wallpaper_dirty = false;
}

void desktop_layout_icons(void) {
    int start_x = 20, start_y = 20;
    for (int i = 0; i < desktop.icon_count; i++) {
        int col = i % DESKTOP_ICONS_PER_ROW, row = i / DESKTOP_ICONS_PER_ROW;
        desktop.icons[i].icon_x = start_x + col * (DESKTOP_ICON_SIZE + DESKTOP_ICON_GAP);
        desktop.icons[i].icon_y = start_y + row * (DESKTOP_ICON_SIZE + DESKTOP_ICON_GAP + 16);
    }
}

int desktop_icon_add(const char* label, window_type_t type) {
    if (desktop.icon_count >= MAX_DESKTOP_ICONS) return -1;
    int idx = desktop.icon_count;
    strcpy(desktop.icons[idx].label, label);
    desktop.icons[idx].type = type;
    desktop.icons[idx].hovered = false;
    desktop.icon_count++;
    desktop_layout_icons();
    desktop.icons_dirty = true;
    return idx;
}

void desktop_icon_remove(int idx) {
    if (idx < 0 || idx >= desktop.icon_count) return;
    for (int i = idx; i < desktop.icon_count - 1; i++) desktop.icons[i] = desktop.icons[i + 1];
    desktop.icon_count--;
    desktop_layout_icons();
    desktop.icons_dirty = true;
}

int desktop_icon_hit_test(int mx, int my) {
    for (int i = 0; i < desktop.icon_count; i++) {
        int ix = desktop.icons[i].icon_x, iy = desktop.icons[i].icon_y;
        if (mx >= ix && mx < ix + DESKTOP_ICON_SIZE && my >= iy && my < iy + DESKTOP_ICON_SIZE + 16) return i;
    }
    return -1;
}

void desktop_icon_launch_offset(window_type_t type, const char* title) {
    int win_w = 400, win_h = 300;
    int win_x = 40 + (desktop.window_count * 30) % 180;
    int win_y = 30 + (desktop.window_count * 30) % 120;
    int is_game = (type == WINDOW_TYPE_DOOM || type == WINDOW_TYPE_FLAPPYBIRD || type == WINDOW_TYPE_SMB || type == WINDOW_TYPE_PONG || type == WINDOW_TYPE_GDASH);
    if (is_game) {
        switch (type) {
            case WINDOW_TYPE_PONG: pong_init(); pong_run(); break;
            case WINDOW_TYPE_DOOM: doom_init(); doom_run(); break;
            case WINDOW_TYPE_FLAPPYBIRD: flappybird_init(); flappybird_run(); break;
            case WINDOW_TYPE_SMB: smb_init(); smb_run(); break;
            case WINDOW_TYPE_GDASH: gd_init(); gd_run(); break;
            default: break;
        }
        return;
    }
    if (win_x + win_w > (int)screen_width) win_x = screen_width - win_w - 20;
    if (win_y + win_h > (int)screen_height - TASKBAR_HEIGHT) win_y = screen_height - TASKBAR_HEIGHT - win_h - 20;
    window_create(type, title, win_x, win_y, win_w, win_h);
}

void desktop_icon_launch(int idx) {
    if (idx < 0 || idx >= desktop.icon_count) return;
    desktop_icon_launch_offset(desktop.icons[idx].type, desktop.icons[idx].label);
}

void desktop_draw_icons(void) {
    uint32_t stride = (uint32_t)(screen_pitch / 4);
    int sw = (int)screen_width, sh = (int)screen_height;
    for (int i = 0; i < desktop.icon_count; i++) {
        desktop_icon_t* icon = &desktop.icons[i];
        int ix = icon->icon_x, iy = icon->icon_y, label_y = iy + DESKTOP_ICON_SIZE + 2;
        uint32_t icon_color = icon->hovered ? 0xFF2A2A4E : 0xFF0F0F2A;
        uint32_t border_color = icon->hovered ? 0xFF00FFFF : 0xFF555555;
        draw_rect(ix, iy, DESKTOP_ICON_SIZE, DESKTOP_ICON_SIZE, icon_color);
        int y_end = iy + DESKTOP_ICON_SIZE;
        if (y_end > sh) y_end = sh;
        for (int dy = iy; dy < y_end; dy++) {
            if (ix >= 0 && ix < sw) lfbptr[dy * stride + ix] = border_color;
            int rx = ix + DESKTOP_ICON_SIZE - 1;
            if (rx < sw) lfbptr[dy * stride + rx] = border_color;
        }
        int x_end = ix + DESKTOP_ICON_SIZE;
        if (x_end > sw) x_end = sw;
        for (int dx = ix; dx < x_end; dx++) {
            if (iy >= 0 && iy < sh) lfbptr[iy * stride + dx] = border_color;
            int by = iy + DESKTOP_ICON_SIZE - 1;
            if (by < sh) lfbptr[by * stride + dx] = border_color;
        }
        uint32_t* icon_pixels = desktop_icon_get_pixels(i);
        if (icon_pixels) {
            for (int py = 0; py < DESKTOP_ICON_SIZE && iy + py < sh; py++) {
                int sy = iy + py;
                for (int px = 0; px < DESKTOP_ICON_SIZE && ix + px < sw; px++) {
                    uint32_t pixel = icon_pixels[py * DESKTOP_ICON_SIZE + px];
                    if ((uint8_t)(pixel >> 24) > 128) lfbptr[sy * stride + (ix + px)] = pixel;
                }
            }
        }
        int label_x = ix + (DESKTOP_ICON_SIZE - (int)strlen(icon->label) * 8) / 2;
        if (label_x < 0) label_x = 0;
        draw_string_px(icon->label, label_x, label_y, icon->hovered ? 0xFF00FFFF : 0xFFAAAAAA, 0xFF000000);
    }
}

void desktop_draw_taskbar(void) {
    int bar_y = screen_height - TASKBAR_HEIGHT;
    int bar_w = screen_width;
    draw_rect(0, bar_y, bar_w, TASKBAR_HEIGHT, TASKBAR_BG);
    uint32_t stride = (uint32_t)(screen_pitch / 4);
    for (int dx = 0; dx < bar_w; dx++) lfbptr[bar_y * stride + dx] = TASKBAR_BORDER;
    draw_rect(2, bar_y + 2, START_BUTTON_W - 4, TASKBAR_HEIGHT - 4, desktop.start_menu.active ? 0xFF2A2A4E : 0xFF0A0A2A);
    draw_string_px("Start", 10, bar_y + (TASKBAR_HEIGHT - 8) / 2, 0xFF00FFFF, desktop.start_menu.active ? 0xFF2A2A4E : 0xFF0A0A2A);
    int btn_x = START_BUTTON_W + 4;
    for (int i = 0; i < desktop.window_count; i++) {
        window_t* w = &desktop.windows[i];
        if (!w->visible && w->state != WINDOW_STATE_MINIMIZED) continue;
        int btn_w = 120;
        if (btn_x + btn_w > bar_w) btn_w = bar_w - btn_x - 2;
        if (btn_w < 20) break;
        draw_rect(btn_x, bar_y + 2, btn_w, TASKBAR_HEIGHT - 4, w->has_focus ? 0xFF2A2A4E : 0xFF0A0A2A);
        draw_string_px(w->title, btn_x + 4, bar_y + (TASKBAR_HEIGHT - 8) / 2, w->has_focus ? 0xFF00FFFF : 0xFFAAAAAA, w->has_focus ? 0xFF2A2A4E : 0xFF0A0A2A);
        for (int dy = 2; dy < TASKBAR_HEIGHT - 2; dy++) lfbptr[(bar_y + dy) * stride + btn_x] = 0xFF333355;
        btn_x += btn_w + 2;
    }
    char time_str[32];
    int p = 0;
    if (rtc_hours < 10) time_str[p++] = '0';
    int_to_string(rtc_hours, time_str + p); p = strlen(time_str);
    time_str[p++] = ':';
    if (rtc_minutes < 10) time_str[p++] = '0';
    int_to_string(rtc_minutes, time_str + p); p = strlen(time_str);
    time_str[p++] = ':';
    if (rtc_seconds < 10) time_str[p++] = '0';
    int_to_string(rtc_seconds, time_str + p); p = strlen(time_str);
    time_str[p] = '\0';
    draw_string_px(time_str, bar_w - strlen(time_str) * 8 - 8, bar_y + (TASKBAR_HEIGHT - 8) / 2, 0xFFAAAAAA, TASKBAR_BG);
}

int desktop_get_taskbar_hover(int mx, int my) {
    int bar_y = screen_height - TASKBAR_HEIGHT;
    if (my < bar_y || my >= bar_y + TASKBAR_HEIGHT) return -1;
    if (mx >= 2 && mx < START_BUTTON_W) return -2;
    int btn_x = START_BUTTON_W + 4;
    for (int i = 0; i < desktop.window_count; i++) {
        int btn_w = 120;
        if (btn_x + btn_w > (int)screen_width) btn_w = screen_width - btn_x - 2;
        if (mx >= btn_x && mx < btn_x + btn_w) return i;
        btn_x += btn_w + 2;
    }
    return -1;
}

void desktop_render(void) {
    desktop.dirty = false;
    desktop_draw_wallpaper();
    desktop_draw_icons();
    window_draw_all();
    if (desktop.start_menu.visible) start_menu_draw();
    desktop_draw_taskbar();
}

void desktop_handle_mouse(int mx, int my, int buttons) {
    desktop_mouse_x = mx;
    desktop_mouse_y = my;
    
    /* Only process mouse clicks and drags - movement is handled by cursor update */
    if (buttons & 1) {
        if (!desktop_mouse_down) {
            desktop_mouse_down = true;
            int taskbar_hit = desktop_get_taskbar_hover(mx, my);
            if (taskbar_hit == -2) {
                start_menu_toggle();
                desktop.dirty = true;
                return;
            } else if (taskbar_hit >= 0) {
                window_t* w = &desktop.windows[taskbar_hit];
                if (w->state == WINDOW_STATE_MINIMIZED) window_restore(taskbar_hit);
                else window_minimize(taskbar_hit);
                start_menu_close();
                desktop.dirty = true;
                return;
            } else if (mx < (int)screen_width && my < (int)screen_height - TASKBAR_HEIGHT) {
                if (desktop.start_menu.visible) {
                    start_menu_handle_click(mx, my);
                    desktop.dirty = true;
                    return;
                }
                
                /* If clicking inside a window's client area, try its mouse_func first */
                window_t* focused_click = NULL;
                int focus_idx = -1;
                for (int i = desktop.window_count - 1; i >= 0; i--) {
                    window_t* wc = &desktop.windows[i];
                    if (!wc->visible || wc->state == WINDOW_STATE_MINIMIZED) continue;
                    if (mx >= wc->rect.x && mx < wc->rect.x + wc->rect.width &&
                        my >= wc->rect.y && my < wc->rect.y + wc->rect.height) {
                        focused_click = wc;
                        focus_idx = i;
                        break;
                    }
                }
                
                if (focused_click && focused_click->mouse_func &&
                    mx >= focused_click->rect.client_x && mx < focused_click->rect.client_x + focused_click->rect.client_w &&
                    my >= focused_click->rect.client_y && my < focused_click->rect.client_y + focused_click->rect.client_h) {
                    window_focus(focus_idx);
                    focused_click->mouse_func(focused_click, mx, my, buttons);
                    desktop.dirty = true;
                    return;
                }
                
                window_begin_drag(mx, my);
                if (desktop_drag_window < 0) window_begin_resize(mx, my);
                if (desktop_drag_window < 0 && desktop_resize_window < 0) {
                    int icon_idx = desktop_icon_hit_test(mx, my);
                    if (icon_idx >= 0) desktop_icon_launch(icon_idx);
                    else { start_menu_close(); desktop.dirty = true; }
                }
            }
        } else {
            if (desktop_drag_window >= 0) window_drag_update(mx, my);
            else if (desktop_resize_window >= 0) window_resize_update(mx, my);
        }
    } else {
        if (desktop_mouse_down) { desktop_mouse_down = false; window_end_drag(); }
        if (desktop.start_menu.visible) start_menu_handle_hover(mx, my);
        bool icons_changed = false;
        for (int i = 0; i < desktop.icon_count; i++) {
            bool new_hover = (mx >= desktop.icons[i].icon_x && mx < desktop.icons[i].icon_x + DESKTOP_ICON_SIZE && my >= desktop.icons[i].icon_y && my < desktop.icons[i].icon_y + DESKTOP_ICON_SIZE + 16);
            if (new_hover != desktop.icons[i].hovered) { desktop.icons[i].hovered = new_hover; icons_changed = true; }
        }
        if (icons_changed) desktop.icons_dirty = true;
    }
}

void desktop_handle_keyboard(char c) {
    if (c == '\t' && ctrl_pressed) {
        if (!desktop.alt_tab_active) { desktop.alt_tab_active = true; desktop.alt_tab_index = 0; }
        else desktop.alt_tab_index = (desktop.alt_tab_index + 1) % desktop.window_count;
        return;
    }
    if (desktop.alt_tab_active) {
        if (desktop.window_count > 0) window_focus(desktop.alt_tab_index % desktop.window_count);
        desktop.alt_tab_active = false;
        return;
    }
    window_t* focused = window_get_focused();
    if (focused && focused->keyboard_func) focused->keyboard_func(focused, c);
    if (c == 27) {
        if (desktop.start_menu.visible) { start_menu_close(); desktop.dirty = true; }
        else if (focused && focused->visible && focused->state == WINDOW_STATE_NORMAL) {
            for (int i = 0; i < desktop.window_count; i++) { if (&desktop.windows[i] == focused) { window_close(i); desktop.dirty = true; break; } }
        }
    }
}

void boot_screen_show(void);
void boot_screen_hide(void);

void desktop_init(void) {
    memset(&desktop, 0, sizeof(desktop_state_t));
    desktop.desktop_mode = true;
    desktop.initialized = true;
    desktop.window_count = 0;
    desktop.next_z_order = 1;
    desktop.icon_count = 0;
    desktop.start_menu.active = false;
    desktop.start_menu.visible = false;
    desktop_drag_window = -1;
    desktop_resize_window = -1;
    desktop_icon_add("Terminal", WINDOW_TYPE_TERMINAL);
    desktop_icon_add("Settings", WINDOW_TYPE_SETTINGS);
    desktop_icon_add("FAQ", WINDOW_TYPE_FAQ);
    desktop_icon_add("System Info", WINDOW_TYPE_FASTFETCH);
    desktop_icon_add("About", WINDOW_TYPE_ABOUT);
    desktop_icon_add("Notepad", WINDOW_TYPE_NOTEPAD);
    desktop_icon_add("File Manager", WINDOW_TYPE_FILEMANAGER);
    desktop_icon_add("Network", WINDOW_TYPE_NETWORK);
    int plugin_count = 0;
    plugin_t* plugins = plugin_get_list(&plugin_count);
    for (int i = 0; i < plugin_count; i++) {
        if (plugins[i].loaded) {
            if (strcmp(plugins[i].name, "doom") == 0) desktop_icon_add("Doom", WINDOW_TYPE_DOOM);
            else if (strcmp(plugins[i].name, "flappybird") == 0) desktop_icon_add("Flappy Bird", WINDOW_TYPE_FLAPPYBIRD);
            else if (strcmp(plugins[i].name, "smb") == 0) desktop_icon_add("SMB", WINDOW_TYPE_SMB);
            else if (strcmp(plugins[i].name, "pong") == 0) desktop_icon_add("Pong", WINDOW_TYPE_PONG);
            else if (strcmp(plugins[i].name, "gdash") == 0) desktop_icon_add("GDash", WINDOW_TYPE_GDASH);
        }
    }
    desktop_icons_load();
}
