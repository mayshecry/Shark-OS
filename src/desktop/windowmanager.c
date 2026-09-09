/* windowmanager.c - Win98-style window manager for SharkOS.
 *
 * Painting is non-destructive: every frame, desktop_render() composites the
 * wallpaper, then the windows in ascending z-order, then the start menu and
 * the taskbar, and flushes once. There is therefore no per-window framebuffer
 * backup to save or restore - that model leaked memory (kfree() is a no-op in
 * this bump allocator) and smeared the desktop whenever windows overlapped.
 */

#include "kernel.h"
#include "desktop.h"
#include "win98_theme.h"

#define RESIZE_EDGE_LEFT   1
#define RESIZE_EDGE_RIGHT  2
#define RESIZE_EDGE_TOP    4
#define RESIZE_EDGE_BOTTOM 8

/* How close to an edge counts as grabbing it. */
#define RESIZE_GRAB        4

desktop_state_t desktop;
int desktop_mouse_x = 0;
int desktop_mouse_y = 0;
bool desktop_mouse_down = false;
int desktop_drag_window = -1;
int desktop_resize_window = -1;
int desktop_resize_edge = 0;

/* Caption-button press is deferred to mouse-up, like Win98. */
static int caption_press_window = -1;
static int caption_press_kind = -1;

/* ------------------------------------------------------------------ geometry */

/* Single source of truth for the client rectangle. It used to be recomputed
 * inline in window_create(), window_drag_update() and window_resize_update(),
 * and window_maximize() did not recompute it at all - so maximized windows
 * kept drawing at their restored size. */
void window_update_client_rect(window_t* w) {
    w->rect.client_x = w->rect.x + W98_CLIENT_INSET;
    w->rect.client_y = w->rect.y + W98_CLIENT_TOP;
    w->rect.client_w = w->rect.width - W98_CLIENT_INSET * 2;
    w->rect.client_h = w->rect.height - W98_CLIENT_TOP - W98_CLIENT_BOTTOM;
    if (w->rect.client_w < 0) w->rect.client_w = 0;
    if (w->rect.client_h < 0) w->rect.client_h = 0;
}

void window_caption_button_rect(window_t* w, int which, int* rx, int* ry,
                                int* rw, int* rh) {
    /* Order left to right: minimise, maximise/restore, close. */
    int slot = 0;
    if (which == W98_GLYPHKIND_MIN) slot = 0;
    else if (which == W98_GLYPHKIND_MAX) slot = 1;
    else if (which == W98_GLYPHKIND_CLOSE) slot = 2;

    int right = w->rect.x + w->rect.width - W98_BORDER_W - W98_CAPBTN_MARGIN;
    int bx = right - (slot + 1) * W98_CAPBTN_W - slot * W98_CAPBTN_GAP;
    int by = w->rect.y + W98_BORDER_W + W98_CAPBTN_MARGIN;

    if (rx) *rx = bx;
    if (ry) *ry = by;
    if (rw) *rw = W98_CAPBTN_W;
    if (rh) *rh = W98_CAPBTN_H;
}

/* Returns the resize-edge bitmask for a point, or 0. Corners combine flags,
 * which the old code could never produce: window_begin_drag() intercepted the
 * top and bottom bands first and hardcoded the edge to 1, 2 or 8. */
static int get_resize_edge(window_t* w, int mx, int my) {
    int rx = w->rect.x;
    int ry = w->rect.y;
    int rw = w->rect.width;
    int rh = w->rect.height;
    int edge = 0;

    if (mx >= rx && mx < rx + RESIZE_GRAB) edge |= RESIZE_EDGE_LEFT;
    else if (mx >= rx + rw - RESIZE_GRAB && mx < rx + rw) edge |= RESIZE_EDGE_RIGHT;

    if (my >= ry && my < ry + RESIZE_GRAB) edge |= RESIZE_EDGE_TOP;
    else if (my >= ry + rh - RESIZE_GRAB && my < ry + rh) edge |= RESIZE_EDGE_BOTTOM;

    return edge;
}

window_hit_t window_hit_test(window_t* w, int mx, int my, int* resize_edge) {
    if (resize_edge) *resize_edge = 0;
    if (!w || !w->visible || w->state == WINDOW_STATE_MINIMIZED) {
        return WINDOW_HIT_NONE;
    }

    int rx = w->rect.x, ry = w->rect.y;
    int rw = w->rect.width, rh = w->rect.height;

    if (mx < rx || mx >= rx + rw || my < ry || my >= ry + rh) {
        return WINDOW_HIT_NONE;
    }

    int bx, by, bw, bh;

    window_caption_button_rect(w, W98_GLYPHKIND_MIN, &bx, &by, &bw, &bh);
    if (mx >= bx && mx < bx + bw && my >= by && my < by + bh) {
        return WINDOW_HIT_BTN_MIN;
    }
    window_caption_button_rect(w, W98_GLYPHKIND_MAX, &bx, &by, &bw, &bh);
    if (mx >= bx && mx < bx + bw && my >= by && my < by + bh) {
        return WINDOW_HIT_BTN_MAX;
    }
    window_caption_button_rect(w, W98_GLYPHKIND_CLOSE, &bx, &by, &bw, &bh);
    if (mx >= bx && mx < bx + bw && my >= by && my < by + bh) {
        return WINDOW_HIT_BTN_CLOSE;
    }

    int title_bottom = ry + W98_BORDER_W + W98_TITLEBAR_H;
    if (my >= ry && my < title_bottom) {
        return WINDOW_HIT_TITLEBAR;
    }

    if (mx >= w->rect.client_x && mx < w->rect.client_x + w->rect.client_w &&
        my >= w->rect.client_y && my < w->rect.client_y + w->rect.client_h) {
        return WINDOW_HIT_CLIENT;
    }

    if (w->state != WINDOW_STATE_MAXIMIZED) {
        int edge = get_resize_edge(w, mx, my);
        if (edge) {
            if (resize_edge) *resize_edge = edge;
            return WINDOW_HIT_RESIZE;
        }
    }

    return WINDOW_HIT_TITLEBAR;   /* the frame padding - still a grip */
}

/* ------------------------------------------------------------ z-order access */

static window_t* window_topmost_from(int start, int* out_idx) {
    int best = -1;
    for (int i = 0; i < desktop.window_count; i++) {
        if (i <= start) continue;
        if (best < 0) { best = i; continue; }
        if (desktop.windows[i].z_order > desktop.windows[best].z_order) {
            best = i;
        }
    }
    if (best < 0) return NULL;
    if (out_idx) *out_idx = best;
    return &desktop.windows[best];
}

void window_focus_top_visible(void) {
    for (int i = 0; i < desktop.window_count; i++) {
        desktop.windows[i].has_focus = false;
    }
    int best = -1;
    for (int i = 0; i < desktop.window_count; i++) {
        window_t* w = &desktop.windows[i];
        if (!w->visible || w->state == WINDOW_STATE_MINIMIZED) continue;
        if (best < 0 || w->z_order > desktop.windows[best].z_order) best = i;
    }
    if (best >= 0) desktop.windows[best].has_focus = true;
}

/* ------------------------------------------------------------------ lifecycle */

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
    win->pressed_button = 0;

    win->rect.x = x;
    win->rect.y = y;
    win->rect.width = w < WINDOW_MIN_W ? WINDOW_MIN_W : w;
    win->rect.height = h < WINDOW_MIN_H ? WINDOW_MIN_H : h;
    win->rect.min_w = WINDOW_MIN_W;
    win->rect.min_h = WINDOW_MIN_H;

    window_update_client_rect(win);

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
            win->keyboard_func = app_window_keyboard_pong;
            break;
        case WINDOW_TYPE_GDASH:
            win->draw_func = app_window_draw_gdash;
            win->keyboard_func = app_window_keyboard_gdash;
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
            win->keyboard_func = app_window_keyboard_notepad;
            break;
        case WINDOW_TYPE_FILEMANAGER:
            win->draw_func = app_window_draw_filemanager;
            win->mouse_func = app_window_mouse_filemanager;
            win->keyboard_func = app_window_keyboard_filemanager;
            break;
        case WINDOW_TYPE_NETWORK:
            win->draw_func = app_window_draw_network;
            win->mouse_func = app_window_mouse_network;
            break;
        case WINDOW_TYPE_TASKMANAGER:
            win->draw_func = app_window_draw_taskmanager;
            win->mouse_func = app_window_mouse_taskmanager;
            win->keyboard_func = app_window_keyboard_taskmanager;
            break;
        case WINDOW_TYPE_BROWSER:
            win->draw_func = app_window_draw_browser;
            win->mouse_func = app_window_mouse_browser;
            win->keyboard_func = app_window_keyboard_browser;
            win->close_func = browser_close;
            break;
        default:
            break;
    }

    desktop.window_count++;

    for (int i = 0; i < desktop.window_count; i++) {
        if (i != idx) desktop.windows[i].has_focus = false;
    }
    desktop.dirty = true;
    return idx;
}

void window_close(int idx) {
    if (idx < 0 || idx >= desktop.window_count) return;
    window_t* win = &desktop.windows[idx];

    if (win->close_func) {
        win->close_func();
    }

    if (desktop_drag_window == idx) desktop_drag_window = -1;
    if (desktop_resize_window == idx) desktop_resize_window = -1;
    if (caption_press_window == idx) {
        caption_press_window = -1;
        caption_press_kind = -1;
    }

    win->visible = false;

    for (int i = idx; i < desktop.window_count - 1; i++) {
        desktop.windows[i] = desktop.windows[i + 1];
    }
    desktop.window_count--;

    if (desktop_drag_window > idx) desktop_drag_window--;
    if (desktop_resize_window > idx) desktop_resize_window--;
    if (caption_press_window > idx) caption_press_window--;

    /* The old code gave focus to the highest z_order window whether or not it
     * was visible, which left keyboard input going nowhere. */
    window_focus_top_visible();

    desktop.dirty = true;
}

void window_minimize(int idx) {
    if (idx < 0 || idx >= desktop.window_count) return;
    window_t* win = &desktop.windows[idx];
    if (win->state == WINDOW_STATE_MINIMIZED) return;

    win->state = WINDOW_STATE_MINIMIZED;
    win->visible = false;
    win->needs_redraw = true;

    /* Minimizing used to leave has_focus set on a hidden window, so
     * window_get_focused() (which requires visible) returned NULL and the
     * keyboard went dead until the user clicked something. */
    if (win->has_focus) {
        win->has_focus = false;
        window_focus_top_visible();
    }

    desktop.dirty = true;
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

    /* Win98 lets a maximized window hang 4px off each edge, so the outer
     * bevel is off-screen and only the 1px inner frame shows. */
    win->rect.x = -W98_BORDER_W - W98_FRAME_PAD;
    win->rect.y = -W98_BORDER_W - W98_FRAME_PAD;
    win->rect.width = (int)screen_width + (W98_BORDER_W + W98_FRAME_PAD) * 2;
    win->rect.height = (int)(screen_height - TASKBAR_HEIGHT)
                       + (W98_BORDER_W + W98_FRAME_PAD) * 2;
    win->state = WINDOW_STATE_MAXIMIZED;
    win->needs_redraw = true;

    window_update_client_rect(win);
    desktop.dirty = true;
}

void window_restore(int idx) {
    if (idx < 0 || idx >= desktop.window_count) return;
    window_t* win = &desktop.windows[idx];

    if (win->state == WINDOW_STATE_MINIMIZED) {
        win->visible = true;
        win->state = WINDOW_STATE_NORMAL;
        win->needs_redraw = true;
        window_focus(idx);
        return;
    }

    if (win->state == WINDOW_STATE_MAXIMIZED) {
        win->rect.x = win->rect.prev_x;
        win->rect.y = win->rect.prev_y;
        win->rect.width = win->rect.prev_w;
        win->rect.height = win->rect.prev_h;
        win->state = WINDOW_STATE_NORMAL;
        win->needs_redraw = true;
        window_update_client_rect(win);
        desktop.dirty = true;
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
    desktop.dirty = true;
}

window_t* window_get_focused(void) {
    for (int i = 0; i < desktop.window_count; i++) {
        if (desktop.windows[i].has_focus && desktop.windows[i].visible) {
            return &desktop.windows[i];
        }
    }
    return NULL;
}

void window_close_by_ptr(window_t* w) {
    if (!w) return;
    for (int i = 0; i < desktop.window_count; i++) {
        if (&desktop.windows[i] == w) {
            window_close(i);
            break;
        }
    }
}

/* ------------------------------------------------------------------ painting */

/* Nearest-neighbour 32x32 -> 16x16 for the titlebar icon, cached per window
 * type so dragging does not resample every frame. */
static uint32_t title_icon_cache[WINDOW_TYPE_MAX][16 * 16];
static bool title_icon_ready[WINDOW_TYPE_MAX];

static void window_build_title_icon(window_t* w) {
    if (w->type < 0 || w->type >= WINDOW_TYPE_MAX) return;
    if (title_icon_ready[w->type]) return;

    const uint32_t* src = desktop_icon_pixels_for_type(w->type);
    uint32_t* dst = title_icon_cache[w->type];

    if (!src) {
        for (int i = 0; i < 16 * 16; i++) dst[i] = 0;
        title_icon_ready[w->type] = true;
        return;
    }

    for (int y = 0; y < 16; y++) {
        int sy = y * DESKTOP_ICON_SIZE / 16;
        for (int x = 0; x < 16; x++) {
            int sx = x * DESKTOP_ICON_SIZE / 16;
            uint32_t p = src[sy * DESKTOP_ICON_SIZE + sx];
            if (((p >> 24) & 0xFF) > 128) {
                dst[y * 16 + x] = 0xFF000000u | (p & 0x00FFFFFFu);
            } else {
                /* Win98 title icons are transparent where the icon is. */
                dst[y * 16 + x] = 0;
            }
        }
    }
    title_icon_ready[w->type] = true;
}

static void window_draw_caption_button(window_t* w, int kind) {
    int bx, by, bw, bh;
    window_caption_button_rect(w, kind, &bx, &by, &bw, &bh);

    bool pressed = (w->pressed_button == kind + 1);
    w98_fill(bx, by, bw, bh, W98_BTNFACE);
    w98_bevel(bx, by, bw, bh,
              pressed ? W98_BEVEL_RAISED_PRESSED : W98_BEVEL_RAISED);

    uint32_t fg = w->has_focus ? W98_BTNTEXT : W98_GRAYTEXT;
    int ox = pressed ? 1 : 0;

    int glyph_kind = kind;
    if (kind == W98_GLYPHKIND_MAX && w->state == WINDOW_STATE_MAXIMIZED) {
        glyph_kind = W98_GLYPHKIND_RESTORE;
    }
    w98_glyph(glyph_kind,
              bx + (bw - W98_GLYPH_W) / 2 + ox,
              by + (bh - W98_GLYPH_H) / 2 + ox,
              fg);
}

void window_draw_frame(window_t* w) {
    int x = w->rect.x;
    int y = w->rect.y;
    int ww = w->rect.width;
    int wh = w->rect.height;

    /* Opaque body. Windows are painted back-to-front into the back buffer, so
     * each one has to cover what is behind it. */
    w98_fill(x, y, ww, wh, W98_BTNFACE);

    /* Outer bevel: highlight/dark-shadow on the outside, light/shadow inside. */
    w98_bevel(x, y, ww, wh, W98_BEVEL_RAISED);

    /* Titlebar. */
    int tb_x = x + W98_BORDER_W + W98_FRAME_PAD;
    int tb_y = y + W98_BORDER_W + W98_FRAME_PAD;
    int tb_w = ww - (W98_BORDER_W + W98_FRAME_PAD) * 2;
    int tb_h = W98_TITLEBAR_H;

    if (w->has_focus) {
#if W98_TITLE_GRADIENT
        w98_hgradient(tb_x, tb_y, tb_w, tb_h, W98_ACTIVE_TITLE, W98_ACTIVE_TITLE2);
#else
        w98_fill(tb_x, tb_y, tb_w, tb_h, W98_ACTIVE_TITLE);
#endif
    } else {
#if W98_TITLE_GRADIENT
        w98_hgradient(tb_x, tb_y, tb_w, tb_h,
                      W98_INACTIVE_TITLE, W98_INACTIVE_TITLE2);
#else
        w98_fill(tb_x, tb_y, tb_w, tb_h, W98_INACTIVE_TITLE);
#endif
    }

    /* Title icon and caption. */
    int text_x = tb_x + 3;
    window_build_title_icon(w);
    if (w->type >= 0 && w->type < WINDOW_TYPE_MAX) {
        w98_icon_blit(title_icon_cache[w->type], 16, 16,
                      tb_x + 2, tb_y + (tb_h - 16) / 2, NULL);
        text_x = tb_x + 20;
    }

    uint32_t title_fg = w->has_focus ? W98_TITLETEXT : W98_TITLETEXT_INACT;
    int text_max = tb_w - (text_x - tb_x) - 3 * (W98_CAPBTN_W + W98_CAPBTN_GAP);
    char fit[WINDOW_TITLE_MAX];
    w98_text_fit(w->title, fit, sizeof(fit), text_max, 1);

    if (w->has_focus) {
        w98_text_bold(fit, text_x, tb_y + (tb_h - 8) / 2, title_fg,
                      W98_ACTIVE_TITLE, 1, NULL);
    } else {
        w98_text(fit, text_x, tb_y + (tb_h - 8) / 2, title_fg,
                 W98_INACTIVE_TITLE, 1, NULL);
    }

    window_draw_caption_button(w, W98_GLYPHKIND_MIN);
    window_draw_caption_button(w, W98_GLYPHKIND_MAX);
    window_draw_caption_button(w, W98_GLYPHKIND_CLOSE);

    /* Client area. Apps paint over this; the fill just guarantees that a
     * partially-drawn client never shows the wallpaper through it. */
    w98_fill(w->rect.client_x, w->rect.client_y,
             w->rect.client_w, w->rect.client_h, W98_BTNFACE);
}

/* ------------------------------------------------------------------- input */

static void window_caption_activate(window_t* w, int kind) {
    if (!w) return;
    for (int i = 0; i < desktop.window_count; i++) {
        if (&desktop.windows[i] != w) continue;
        if (kind == W98_GLYPHKIND_MIN) window_minimize(i);
        else if (kind == W98_GLYPHKIND_MAX) window_maximize(i);
        else if (kind == W98_GLYPHKIND_CLOSE) window_close(i);
        return;
    }
}

void window_begin_drag(int mx, int my) {
    /* Front-to-back, stopping at the first window that contains the point.
     * The old version kept scanning after a hit, so a click inside a lower
     * window's resize border could steal it from the window on top. */
    int idx = -1;
    window_t* w = window_topmost_from(-1, &idx);

    while (w) {
        int edge = 0;
        window_hit_t hit = window_hit_test(w, mx, my, &edge);

        if (hit != WINDOW_HIT_NONE) {
            window_focus(idx);

            if (hit == WINDOW_HIT_BTN_MIN || hit == WINDOW_HIT_BTN_MAX ||
                hit == WINDOW_HIT_BTN_CLOSE) {
                int kind = (hit == WINDOW_HIT_BTN_MIN) ? W98_GLYPHKIND_MIN
                         : (hit == WINDOW_HIT_BTN_MAX) ? W98_GLYPHKIND_MAX
                         : W98_GLYPHKIND_CLOSE;
                caption_press_window = idx;
                caption_press_kind = kind;
                w->pressed_button = kind + 1;
            } else if (hit == WINDOW_HIT_TITLEBAR &&
                       w->state != WINDOW_STATE_MAXIMIZED) {
                desktop_drag_window = idx;
                w->is_dragging = true;
                w->drag_off_x = mx - w->rect.x;
                w->drag_off_y = my - w->rect.y;
            } else if (hit == WINDOW_HIT_RESIZE) {
                desktop_resize_window = idx;
                desktop_resize_edge = edge;
                w->is_resizing = true;
                w->drag_off_x = mx;
                w->drag_off_y = my;
            }
            desktop.dirty = true;
            return;
        }

        w = window_topmost_from(idx, &idx);
    }
}

void window_begin_resize(int mx, int my) {
    int idx = -1;
    window_t* w = window_topmost_from(-1, &idx);
    while (w) {
        if (w->visible && w->state == WINDOW_STATE_NORMAL) {
            int edge = get_resize_edge(w, mx, my);
            if (edge) {
                window_focus(idx);
                desktop_resize_window = idx;
                desktop_resize_edge = edge;
                w->is_resizing = true;
                w->drag_off_x = mx;
                w->drag_off_y = my;
                desktop.dirty = true;
                return;
            }
        }
        w = window_topmost_from(idx, &idx);
    }
}

void window_drag_update(int mx, int my) {
    if (desktop_drag_window < 0 || desktop_drag_window >= desktop.window_count) {
        return;
    }
    window_t* w = &desktop.windows[desktop_drag_window];

    int new_x = mx - w->drag_off_x;
    int new_y = my - w->drag_off_y;

    /* Win98 lets you drag a window partly off the top and sides, but never so
     * far that the titlebar leaves the screen. */
    int work_h = (int)screen_height - TASKBAR_HEIGHT;
    if (new_y < 0) new_y = 0;
    if (new_y > work_h - W98_BORDER_W - W98_TITLEBAR_H) {
        new_y = work_h - W98_BORDER_W - W98_TITLEBAR_H;
    }
    if (new_x > (int)screen_width - W98_CAPBTN_W) {
        new_x = (int)screen_width - W98_CAPBTN_W;
    }
    if (new_x < W98_CAPBTN_W - w->rect.width) {
        new_x = W98_CAPBTN_W - w->rect.width;
    }

    w->rect.x = new_x;
    w->rect.y = new_y;
    window_update_client_rect(w);
    w->needs_redraw = true;
    desktop.dirty = true;
}

void window_resize_update(int mx, int my) {
    if (desktop_resize_window < 0 ||
        desktop_resize_window >= desktop.window_count) {
        return;
    }
    window_t* w = &desktop.windows[desktop_resize_window];

    int dx = mx - w->drag_off_x;
    int dy = my - w->drag_off_y;

    int new_x = w->rect.x;
    int new_y = w->rect.y;
    int new_w = w->rect.width;
    int new_h = w->rect.height;

    int min_w = w->rect.min_w > 0 ? w->rect.min_w : WINDOW_MIN_W;
    int min_h = w->rect.min_h > 0 ? w->rect.min_h : WINDOW_MIN_H;

    if (desktop_resize_edge & RESIZE_EDGE_RIGHT) {
        new_w += dx;
    }
    if (desktop_resize_edge & RESIZE_EDGE_LEFT) {
        new_w -= dx;
        new_x += dx;
    }
    if (desktop_resize_edge & RESIZE_EDGE_BOTTOM) {
        new_h += dy;
    }
    if (desktop_resize_edge & RESIZE_EDGE_TOP) {
        new_h -= dy;
        new_y += dy;
    }

    /* Clamp without letting the opposite edge drift, which is what made the
     * old left/top resizing feel like the window was running away. */
    if (new_w < min_w) {
        if (desktop_resize_edge & RESIZE_EDGE_LEFT) {
            new_x -= min_w - new_w;
        }
        new_w = min_w;
    }
    if (new_h < min_h) {
        if (desktop_resize_edge & RESIZE_EDGE_TOP) {
            new_y -= min_h - new_h;
        }
        new_h = min_h;
    }

    int work_h = (int)screen_height - TASKBAR_HEIGHT;
    if (new_y < 0) {
        new_h += new_y;
        new_y = 0;
        if (new_h < min_h) new_h = min_h;
    }
    if (new_y + new_h > work_h) {
        new_h = work_h - new_y;
        if (new_h < min_h) new_h = min_h;
    }

    w->rect.x = new_x;
    w->rect.y = new_y;
    w->rect.width = new_w;
    w->rect.height = new_h;
    window_update_client_rect(w);

    w->drag_off_x = mx;
    w->drag_off_y = my;

    w->needs_redraw = true;
    desktop.dirty = true;
}

int window_caption_pressed(void) {
    return caption_press_window;
}

void window_end_drag(void) {
    if (desktop_drag_window >= 0 &&
        desktop_drag_window < desktop.window_count) {
        desktop.windows[desktop_drag_window].is_dragging = false;
    }
    if (desktop_resize_window >= 0 &&
        desktop_resize_window < desktop.window_count) {
        desktop.windows[desktop_resize_window].is_resizing = false;
    }
    desktop_drag_window = -1;
    desktop_resize_window = -1;
    desktop_resize_edge = 0;

    /* Caption buttons fire on release, so a press-and-drag-away cancels. */
    if (caption_press_window >= 0 &&
        caption_press_window < desktop.window_count) {
        window_t* w = &desktop.windows[caption_press_window];
        int kind = caption_press_kind;
        w->pressed_button = 0;
        caption_press_window = -1;
        caption_press_kind = -1;
        if (kind >= 0) {
            window_caption_activate(w, kind);
        }
    }

    desktop.dirty = true;
}

/* ------------------------------------------------------------------- paint */

static void draw_window_content(window_t* w) {
    if (w->draw_func) {
        w->draw_func(w);
    }
}

void window_draw_all(void) {
    int draw_order[MAX_WINDOWS];
    int draw_count = 0;

    for (int i = 0; i < desktop.window_count; i++) {
        if (desktop.windows[i].visible &&
            desktop.windows[i].state != WINDOW_STATE_MINIMIZED) {
            draw_order[draw_count++] = i;
        }
    }

    /* Ascending z_order: lowest window first, so higher ones paint over it. */
    for (int i = 1; i < draw_count; i++) {
        int j = i;
        while (j > 0 &&
               desktop.windows[draw_order[j]].z_order <
               desktop.windows[draw_order[j - 1]].z_order) {
            int tmp = draw_order[j];
            draw_order[j] = draw_order[j - 1];
            draw_order[j - 1] = tmp;
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

void window_redraw_clients(void) {
    for (int i = 0; i < desktop.window_count; i++) {
        desktop.windows[i].needs_redraw = true;
    }
    desktop.dirty = true;
}
