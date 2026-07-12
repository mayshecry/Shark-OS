
#include "kernel.h"
#include "desktop.h"

#define TITLEBAR_H 20
#define BORDER_W 2
#define RESIZE_HANDLE 6

desktop_state_t desktop;
int desktop_mouse_x = 0;
int desktop_mouse_y = 0;
bool desktop_mouse_down = false;
int desktop_drag_window = -1;
int desktop_resize_window = -1;
int desktop_resize_edge = 0;

static int get_resize_edge(window_t* w, int mx, int my) {
    int edge = 0;
    int rx = w->rect.x;
    int ry = w->rect.y;
    int rw = w->rect.width;
    int rh = w->rect.height;
    
    if (mx >= rx && mx < rx + BORDER_W) edge |= 1;        
    if (mx >= rx + rw - BORDER_W && mx < rx + rw) edge |= 2; 
    if (my >= ry && my < ry + BORDER_W) edge |= 4;          
    if (my >= ry + rh - BORDER_W && my < ry + rh) edge |= 8; 
    
    return edge;
}

void window_save_background(window_t* w) {
    if (w->fb_backup) {
        if (w->backup_w != w->rect.width || w->backup_h != w->rect.height) {
            kfree(w->fb_backup);
            w->fb_backup = NULL;
        }
    }
    
    if (!w->fb_backup) {
        w->backup_w = w->rect.width;
        w->backup_h = w->rect.height;
        w->fb_backup = (uint32_t*)kmalloc(w->backup_w * w->backup_h * sizeof(uint32_t));
        if (!w->fb_backup) return;
    }
    
    uint32_t stride = (uint32_t)(screen_pitch / 4);
    for (int y = 0; y < w->backup_h && (w->rect.y + y) < (int)screen_height; y++) {
        for (int x = 0; x < w->backup_w && (w->rect.x + x) < (int)screen_width; x++) {
            w->fb_backup[y * w->backup_w + x] = lfbptr[(w->rect.y + y) * stride + (w->rect.x + x)];
        }
    }
}

void window_restore_background(window_t* w) {
    if (!w->fb_backup) return;
    uint32_t stride = (uint32_t)(screen_pitch / 4);
    for (int y = 0; y < w->backup_h && (w->rect.y + y) < (int)screen_height; y++) {
        for (int x = 0; x < w->backup_w && (w->rect.x + x) < (int)screen_width; x++) {
            lfbptr[(w->rect.y + y) * stride + (w->rect.x + x)] = w->fb_backup[y * w->backup_w + x];
        }
    }
}

int window_create(window_type_t type, const char* title, int x, int y, int w, int h) {
    if (desktop.window_count >= MAX_WINDOWS) return -1;
    
    int idx = desktop.window_count;
    window_t* win = &desktop.windows[idx];
    
    memset(win, 0, sizeof(window_t));
    
    strcpy(win->title, title);
    win->type = type;
    win->state = WINDOW_STATE_NORMAL;
    win->visible = true;
    win->has_focus = true;
    win->needs_redraw = true;
    win->z_order = desktop.next_z_order++;
    
    win->rect.x = x;
    win->rect.y = y;
    win->rect.width = w < WINDOW_MIN_W ? WINDOW_MIN_W : w;
    win->rect.height = h < WINDOW_MIN_H ? WINDOW_MIN_H : h;
    win->rect.min_w = WINDOW_MIN_W;
    win->rect.min_h = WINDOW_MIN_H;
    
    
    win->rect.client_x = x + BORDER_W;
    win->rect.client_y = y + TITLEBAR_H;
    win->rect.client_w = win->rect.width - BORDER_W * 2;
    win->rect.client_h = win->rect.height - TITLEBAR_H - BORDER_W;
    
    
    switch (type) {
        case WINDOW_TYPE_TERMINAL:
            win->draw_func = app_window_draw_terminal;
            win->keyboard_func = app_window_keyboard_terminal;
            break;
        case WINDOW_TYPE_DOOM:
            win->draw_func = app_window_draw_doom;
            win->keyboard_func = app_window_keyboard_doom;
            break;
        case WINDOW_TYPE_FLAPPYBIRD:
            win->draw_func = app_window_draw_flappybird;
            win->keyboard_func = app_window_keyboard_flappybird;
            break;
        case WINDOW_TYPE_SMB:
            win->draw_func = app_window_draw_smb;
            win->keyboard_func = app_window_keyboard_smb;
            break;
        case WINDOW_TYPE_PONG:
            win->draw_func = app_window_draw_pong;
            break;
        case WINDOW_TYPE_GDASH:
            win->draw_func = app_window_draw_gdash;
            break;
        case WINDOW_TYPE_SETTINGS:
            win->draw_func = app_window_draw_settings;
            win->mouse_func = app_window_mouse_settings;
            break;
        case WINDOW_TYPE_FAQ:
            win->draw_func = app_window_draw_faq;
            break;
        case WINDOW_TYPE_FASTFETCH:
            win->draw_func = app_window_draw_fastfetch;
            break;
        case WINDOW_TYPE_ABOUT:
            win->draw_func = app_window_draw_about;
            break;
        case WINDOW_TYPE_NOTEPAD:
            win->draw_func = app_window_draw_notepad;
            break;
        case WINDOW_TYPE_FILEMANAGER:
            win->draw_func = app_window_draw_filemanager;
            win->mouse_func = app_window_mouse_filemanager;
            break;
        case WINDOW_TYPE_NETWORK:
            win->draw_func = app_window_draw_network;
            break;
        default:
            break;
    }
    
    
    for (int i = 0; i < desktop.window_count; i++) {
        if (i != idx) desktop.windows[i].has_focus = false;
    }
    
    desktop.window_count++;
    window_save_background(win);
    return idx;
}

void window_close(int idx) {
    if (idx < 0 || idx >= desktop.window_count) return;
    window_t* win = &desktop.windows[idx];
    
    window_restore_background(win);
    
    if (win->close_func) {
        win->close_func(win);
    }
    
    if (win->fb_backup) {
        kfree(win->fb_backup);
        win->fb_backup = NULL;
    }
    
    win->visible = false;
    
    
    for (int i = idx; i < desktop.window_count - 1; i++) {
        desktop.windows[i] = desktop.windows[i + 1];
    }
    desktop.window_count--;
    
    
    if (desktop.window_count > 0) {
        int top = 0;
        for (int i = 0; i < desktop.window_count; i++) {
            if (desktop.windows[i].z_order > desktop.windows[top].z_order) {
                top = i;
            }
        }
        desktop.windows[top].has_focus = true;
    }
    
    
    desktop.dirty = true;
}

void window_minimize(int idx) {
    if (idx < 0 || idx >= desktop.window_count) return;
    window_t* win = &desktop.windows[idx];
    if (win->state == WINDOW_STATE_NORMAL) {
        win->state = WINDOW_STATE_MINIMIZED;
        win->visible = false;
        window_restore_background(win);
        if (win->fb_backup) {
            kfree(win->fb_backup);
            win->fb_backup = NULL;
        }
    }
}

void window_maximize(int idx) {
    if (idx < 0 || idx >= desktop.window_count) return;
    window_t* win = &desktop.windows[idx];
    
    if (win->state == WINDOW_STATE_MAXIMIZED) {
        window_restore(idx);
        return;
    }
    
    win->rect.prev_x = win->rect.x;
    win->rect.prev_y = win->rect.y;
    win->rect.prev_w = win->rect.width;
    win->rect.prev_h = win->rect.height;
    
    window_restore_background(win);
    
    win->rect.x = 0;
    win->rect.y = 0;
    win->rect.width = screen_width;
    win->rect.height = screen_height - TASKBAR_HEIGHT;
    win->state = WINDOW_STATE_MAXIMIZED;
    win->needs_redraw = true;
}

void window_restore(int idx) {
    if (idx < 0 || idx >= desktop.window_count) return;
    window_t* win = &desktop.windows[idx];
    
    if (win->state == WINDOW_STATE_MINIMIZED) {
        win->visible = true;
        win->state = WINDOW_STATE_NORMAL;
        window_save_background(win);
        win->needs_redraw = true;
        return;
    }
    
    if (win->state == WINDOW_STATE_MAXIMIZED) {
        window_restore_background(win);
        win->rect.x = win->rect.prev_x;
        win->rect.y = win->rect.prev_y;
        win->rect.width = win->rect.prev_w;
        win->rect.height = win->rect.prev_h;
        win->state = WINDOW_STATE_NORMAL;
        window_save_background(win);
        win->needs_redraw = true;
    }
}

void window_focus(int idx) {
    if (idx < 0 || idx >= desktop.window_count) return;
    
    
    for (int i = 0; i < desktop.window_count; i++) {
        desktop.windows[i].has_focus = false;
    }
    
    window_t* win = &desktop.windows[idx];
    win->has_focus = true;
    win->z_order = desktop.next_z_order++;
    win->needs_redraw = true;
}

window_t* window_get_focused(void) {
    for (int i = 0; i < desktop.window_count; i++) {
        if (desktop.windows[i].has_focus && desktop.windows[i].visible) {
            return &desktop.windows[i];
        }
    }
    return NULL;
}

void window_draw_frame(window_t* w) {
    int x = w->rect.x;
    int y = w->rect.y;
    int ww = w->rect.width;
    int wh = w->rect.height;
    
    
    for (int dy = 0; dy < 4; dy++) {
        for (int dx = 0; dx < 4; dx++) {
            int sx = x + ww + dx;
            int sy = y + wh + dy;
            if (sx >= (int)screen_width || sy >= (int)screen_height) continue;
            uint32_t stride = (uint32_t)(screen_pitch / 4);
            uint32_t alpha = 0x40 - dy * 0x10 - dx * 0x10;
            uint32_t* pixel = &lfbptr[sy * stride + sx];
            uint8_t r = ((*pixel >> 16) & 0xFF) * (255 - alpha) / 255;
            uint8_t g = ((*pixel >> 8) & 0xFF) * (255 - alpha) / 255;
            uint8_t b = (*pixel & 0xFF) * (255 - alpha) / 255;
            *pixel = 0xFF000000 | (r << 16) | (g << 8) | b;
        }
    }
    
    
    uint32_t border_color = w->has_focus ? 0xFF00FFFF : 0xFF555555;
    for (int dy = 0; dy < BORDER_W; dy++) {
        for (int dx = 0; dx < ww; dx++) {
            int px = x + dx;
            int py = y + dy;
            if (px < (int)screen_width && py < (int)screen_height) {
                uint32_t stride = (uint32_t)(screen_pitch / 4);
                lfbptr[py * stride + px] = border_color;
            }
            py = y + wh - 1 - dy;
            if (px < (int)screen_width && py < (int)screen_height) {
                uint32_t stride = (uint32_t)(screen_pitch / 4);
                lfbptr[py * stride + px] = border_color;
            }
        }
        for (int py = y; py < y + wh; py++) {
            int px = x + dy;
            if (px < (int)screen_width && py < (int)screen_height) {
                uint32_t stride = (uint32_t)(screen_pitch / 4);
                lfbptr[py * stride + px] = border_color;
            }
            px = x + ww - 1 - dy;
            if (px < (int)screen_width && py < (int)screen_height) {
                uint32_t stride = (uint32_t)(screen_pitch / 4);
                lfbptr[py * stride + px] = border_color;
            }
        }
    }
    
    
    uint32_t title_bg = w->has_focus ? 0xFF0F0F3A : 0xFF0A0A2A;
    for (int dy = 0; dy < TITLEBAR_H; dy++) {
        for (int dx = 0; dx < ww; dx++) {
            int px = x + dx;
            int py = y + BORDER_W + dy;
            if (px < (int)screen_width && py < (int)screen_height) {
                uint32_t stride = (uint32_t)(screen_pitch / 4);
                lfbptr[py * stride + px] = title_bg;
            }
        }
    }
    
    
    uint32_t title_color = w->has_focus ? 0xFFFF0000 : 0xFFAAAAAA;
    draw_string_px(w->title, x + 4, y + BORDER_W + (TITLEBAR_H - 8) / 2, title_color, title_bg);
    
    
    int close_x = x + ww - 16;
    int close_y = y + BORDER_W + 2;
    draw_rect(close_x, close_y, 12, TITLEBAR_H - 4, border_color);
    draw_string_px("X", close_x + 2, close_y + 2, 0xFFFF0000, border_color);
    
    
    int max_x = close_x - 14;
    draw_rect(max_x, close_y, 12, TITLEBAR_H - 4, border_color);
    draw_string_px("O", max_x + 2, close_y + 2, 0xFF00FF00, border_color);
    
    
    int min_x = max_x - 14;
    draw_rect(min_x, close_y, 12, TITLEBAR_H - 4, border_color);
    draw_string_px("_", min_x + 2, close_y + 2, 0xFFFFFF00, border_color);
}

void window_begin_drag(int mx, int my) {
    for (int i = desktop.window_count - 1; i >= 0; i--) {
        window_t* w = &desktop.windows[i];
        if (!w->visible || w->state == WINDOW_STATE_MINIMIZED) continue;
        
        int rx = w->rect.x;
        int ry = w->rect.y;
        int rw = w->rect.width;
        int rh = w->rect.height;
        
        
        int close_x = rx + rw - 16;
        int close_y = ry + BORDER_W + 2;
        int btn_w = 12;
        int btn_h = TITLEBAR_H - 4;
        if (mx >= close_x && mx < close_x + btn_w && my >= close_y && my < close_y + btn_h) {
            window_close(i);
            return;
        }
        
        int max_x = close_x - 14;
        if (mx >= max_x && mx < max_x + btn_w && my >= close_y && my < close_y + btn_h) {
            window_maximize(i);
            return;
        }
        
        int min_x = max_x - 14;
        if (mx >= min_x && mx < min_x + btn_w && my >= close_y && my < close_y + btn_h) {
            window_minimize(i);
            return;
        }
        
        
        if (mx >= rx + BORDER_W && mx < min_x - 2 &&
            my >= ry && my < ry + BORDER_W + TITLEBAR_H) {
            window_focus(i);
            desktop_drag_window = i;
            w->is_dragging = true;
            w->drag_off_x = mx - rx;
            w->drag_off_y = my - ry;
            window_restore_background(w);
            return;
        }
        
        
        if (my >= ry + BORDER_W && my < ry + BORDER_W + TITLEBAR_H) {
            
            if (mx >= rx && mx < rx + BORDER_W + 4) {
                window_focus(i);
                desktop_resize_window = i;
                desktop_resize_edge = 1; 
                w->is_resizing = true;
                w->drag_off_x = mx;
                w->drag_off_y = my;
                window_restore_background(w);
                return;
            }
            if (mx >= rx + rw - BORDER_W - 4 && mx < rx + rw) {
                window_focus(i);
                desktop_resize_window = i;
                desktop_resize_edge = 2; 
                w->is_resizing = true;
                w->drag_off_x = mx;
                w->drag_off_y = my;
                window_restore_background(w);
                return;
            }
        }
        
        
        if (my >= ry + rh - BORDER_W - 4 && my < ry + rh &&
            mx >= rx && mx < rx + rw) {
            window_focus(i);
            desktop_resize_window = i;
            desktop_resize_edge = 8; 
            w->is_resizing = true;
            w->drag_off_x = mx;
            w->drag_off_y = my;
            window_restore_background(w);
            return;
        }
    }
}

void window_begin_resize(int mx, int my) {
    for (int i = desktop.window_count - 1; i >= 0; i--) {
        window_t* w = &desktop.windows[i];
        if (!w->visible || w->state != WINDOW_STATE_NORMAL) continue;
        
        int edge = get_resize_edge(w, mx, my);
        if (edge) {
            window_focus(i);
            desktop_resize_window = i;
            desktop_resize_edge = edge;
            w->is_resizing = true;
            w->drag_off_x = mx;
            w->drag_off_y = my;
            return;
        }
    }
}

void window_drag_update(int mx, int my) {
    if (desktop_drag_window < 0 || desktop_drag_window >= desktop.window_count) return;
    window_t* w = &desktop.windows[desktop_drag_window];
    
    int new_x = mx - w->drag_off_x;
    int new_y = my - w->drag_off_y;
    
    if (new_x < 0) new_x = 0;
    if (new_y < 0) new_y = 0;
    if (new_x > (int)screen_width - w->rect.width) new_x = screen_width - w->rect.width;
    if (new_y > (int)screen_height - TASKBAR_HEIGHT - w->rect.height) new_y = screen_height - TASKBAR_HEIGHT - w->rect.height;
    
    window_restore_background(w);
    w->rect.x = new_x;
    w->rect.y = new_y;
    w->rect.client_x = new_x + BORDER_W;
    w->rect.client_y = new_y + TITLEBAR_H;
    window_save_background(w);
    desktop.dirty = true;
}

void window_resize_update(int mx, int my) {
    if (desktop_resize_window < 0 || desktop_resize_window >= desktop.window_count) return;
    window_t* w = &desktop.windows[desktop_resize_window];
    
    int new_w = w->rect.width;
    int new_h = w->rect.height;
    int new_x = w->rect.x;
    int new_y = w->rect.y;
    
    int dx = mx - w->drag_off_x;
    int dy = my - w->drag_off_y;
    
    window_restore_background(w);
    
    if (desktop_resize_edge & 1) {  
        new_x += dx;
        new_w -= dx;
    }
    if (desktop_resize_edge & 2) {  
        new_w += dx;
    }
    if (desktop_resize_edge & 4) {  
        new_y += dy;
        new_h -= dy;
    }
    if (desktop_resize_edge & 8) {  
        new_h += dy;
    }
    
    if (new_w < w->rect.min_w) {
        if (desktop_resize_edge & 1) new_x -= w->rect.min_w - new_w;
        new_w = w->rect.min_w;
    }
    if (new_h < w->rect.min_h) {
        if (desktop_resize_edge & 4) new_y -= w->rect.min_h - new_h;
        new_h = w->rect.min_h;
    }
    
    w->rect.x = new_x;
    w->rect.y = new_y;
    w->rect.width = new_w;
    w->rect.height = new_h;
    w->rect.client_x = new_x + BORDER_W;
    w->rect.client_y = new_y + TITLEBAR_H;
    w->rect.client_w = new_w - BORDER_W * 2;
    w->rect.client_h = new_h - TITLEBAR_H - BORDER_W;
    
    w->drag_off_x = mx;
    w->drag_off_y = my;
    
    window_save_background(w);
    desktop.dirty = true;
}

void window_end_drag(void) {
    if (desktop_drag_window >= 0) {
        window_t* w = &desktop.windows[desktop_drag_window];
        w->is_dragging = false;
        desktop_drag_window = -1;
    }
    if (desktop_resize_window >= 0) {
        window_t* w = &desktop.windows[desktop_resize_window];
        w->is_resizing = false;
        desktop_resize_window = -1;
    }
}

static void draw_window_content(window_t* w) {
    if (w->draw_func) {
        w->draw_func(w);
    }
}

void window_draw_all(void) {
    
    int draw_order[MAX_WINDOWS];
    int draw_count = 0;
    
    for (int i = 0; i < desktop.window_count; i++) {
        if (desktop.windows[i].visible && desktop.windows[i].state != WINDOW_STATE_MINIMIZED) {
            draw_order[draw_count++] = i;
        }
    }
    
    
    for (int i = 1; i < draw_count; i++) {
        int j = i;
        while (j > 0 && desktop.windows[draw_order[j]].z_order < desktop.windows[draw_order[j-1]].z_order) {
            int tmp = draw_order[j];
            draw_order[j] = draw_order[j-1];
            draw_order[j-1] = tmp;
            j--;
        }
    }
    
    
    for (int i = 0; i < draw_count; i++) {
        window_t* w = &desktop.windows[draw_order[i]];
        window_draw_frame(w);
        draw_window_content(w);
        w->needs_redraw = false;
    }
}