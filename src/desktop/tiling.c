#include "kernel.h"
#include "desktop.h"

#define TILING_GAP 6
#define TILING_ANIM_NONE 0

int tiling_compute(int n, int ax, int ay, int aw, int ah, int gap, tile_rect_t* out) {
    if (!out || n <= 0) return 0;
    if (n > MAX_WINDOWS) n = MAX_WINDOWS;
    if (aw < 96) aw = 96;
    if (ah < 96) ah = 96;

    if (n == 1) {
        out[0].x = ax; out[0].y = ay; out[0].w = aw; out[0].h = ah;
        return 1;
    }
    if (n == 2) {
        int w = (aw - gap) / 2;
        out[0].x = ax;            out[0].y = ay; out[0].w = w;             out[0].h = ah;
        out[1].x = ax + w + gap;  out[1].y = ay; out[1].w = aw - w - gap;  out[1].h = ah;
        return 2;
    }
    if (n == 3) {
        int mw = (aw - gap) * 3 / 5;
        int sw = aw - mw - gap;
        int h2 = (ah - gap) / 2;
        out[0].x = ax;            out[0].y = ay;                out[0].w = mw; out[0].h = ah;
        out[1].x = ax + mw + gap; out[1].y = ay;                out[1].w = sw; out[1].h = h2;
        out[2].x = ax + mw + gap; out[2].y = ay + h2 + gap;     out[2].w = sw; out[2].h = ah - h2 - gap;
        return 3;
    }

    int cols = 1;
    while (cols * cols < n) cols++;
    int rows = (n + cols - 1) / cols;
    int cw = (aw - gap * (cols - 1)) / cols;
    int chh = (ah - gap * (rows - 1)) / rows;
    for (int i = 0; i < n; i++) {
        int c = i % cols;
        int r = i / cols;
        out[i].x = ax + c * (cw + gap);
        out[i].y = ay + r * (chh + gap);
        out[i].w = cw;
        out[i].h = chh;
        if (c == cols - 1) out[i].w = aw - c * (cw + gap);
        if (r == rows - 1) out[i].h = ah - r * (chh + gap);
    }
    return n;
}

void tiling_arrange(void) {
    if (!tiling_enabled) return;
    if (desktop.window_count <= 0) return;

    int idx[MAX_WINDOWS];
    int n = 0;
    for (int i = 0; i < desktop.window_count && n < MAX_WINDOWS; i++) {
        window_t* w = &desktop.windows[i];
        if (!w->visible) continue;
        if (w->state == WINDOW_STATE_MINIMIZED) continue;
        if (w->anim_kind != TILING_ANIM_NONE) continue;
        idx[n++] = i;
    }
    if (n == 0) return;

    tile_rect_t r[MAX_WINDOWS];
    int aw = (int)screen_width - TILING_GAP * 2 - (wm_jaws()?64:0);
    int ah = (int)screen_height - (wm_jaws()?0:(int)TASKBAR_HEIGHT) - TILING_GAP * 2;
    int count = tiling_compute(n, TILING_GAP, TILING_GAP, aw, ah, TILING_GAP, r);

    for (int k = 0; k < count; k++) {
        window_t* w = &desktop.windows[idx[k]];
        int x = r[k].x, y = r[k].y, ww = r[k].w, hh = r[k].h;
        if (ww < (int)WINDOW_MIN_W) ww = (int)WINDOW_MIN_W;
        if (hh < (int)WINDOW_MIN_H) hh = (int)WINDOW_MIN_H;
        if (x < 0) x = 0;
        if (y < 0) y = 0;
        if (x != w->rect.x || y != w->rect.y ||
            ww != w->rect.width || hh != w->rect.height) {
            w->rect.x = x;
            w->rect.y = y;
            w->rect.width = ww;
            w->rect.height = hh;
            window_update_client_rect(w);
            w->needs_redraw = true;
        }
    }
}

void tiling_set(int on) {
    tiling_enabled = on ? true : false;
    if (tiling_enabled) tiling_arrange();
}

int tiling_is_enabled(void) {
    return tiling_enabled ? 1 : 0;
}
