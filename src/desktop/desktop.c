

#include "kernel.h"
#include "desktop.h"
#include "net.h"
#include "win98_theme.h"
#include "plugin_manager.h"
#include "doom.h"
#include "flappybird.h"
#include "smb.h"
#include "pong.h"
#include "geometrydash.h"
#include "wallpaper_data.h"
#include "kawaii_wallpaper_data.h"
#include "wallpaper_cottage_data.h"
#include "wallpaper_sakura_data.h"
#include "wallpaper_neko_data.h"
#include "wallpaper_thumbs_data.h"
#include "icon_data.h"
#include "font.h"

uint32_t wallpaper_top = DESKTOP_BG_TOP;
uint32_t wallpaper_bot = DESKTOP_BG_BOT;

static uint32_t* wallpaper_cache = NULL;
static int wallpaper_cache_w = 0;
static int wallpaper_cache_h = 0;
static bool wallpaper_dirty = true;

#define DOUBLE_CLICK_TICKS 400

void desktop_set_wallpaper_color(uint32_t top, uint32_t bottom) {
    wallpaper_top = top;
    wallpaper_bot = bottom;
    wallpaper_dirty = true;
}

void desktop_set_wallpaper_mode(int mode) {
    desktop.wallpaper_mode = mode;
    wallpaper_dirty = true;
    desktop.dirty = true;
}

/* ------------------------------------------------------------------ */
/* Bundled wallpaper catalog                                           */
/* ------------------------------------------------------------------ */

typedef struct {
    const char* name;
    const uint32_t* pixels;
    int w;
    int h;
    const uint32_t* thumb;
} wallpaper_entry_t;

static const wallpaper_entry_t wallpaper_catalog[WP_COUNT] = {
    { "SharkOS Classic", wallpaper_pixels,
      WALLPAPER_WIDTH, WALLPAPER_HEIGHT, wallpaper_thumb_classic },
    { "Kawaii", kawaii_wallpaper_pixels,
      KAWAII_WALLPAPER_WIDTH, KAWAII_WALLPAPER_HEIGHT, wallpaper_thumb_kawaii },
    { "Cliffside Cottage", cottage_pixels,
      COTTAGE_WIDTH, COTTAGE_HEIGHT, wallpaper_thumb_cottage },
    { "Sakura Bridge", sakura_pixels,
      SAKURA_WIDTH, SAKURA_HEIGHT, wallpaper_thumb_sakura },
    { "Mono Neko", neko_pixels,
      NEKO_WIDTH, NEKO_HEIGHT, wallpaper_thumb_neko },
};

int desktop_wallpaper_count(void) {
    return WP_COUNT;
}

const char* desktop_wallpaper_name(int id) {
    if (id < 0 || id >= WP_COUNT) return "";
    return wallpaper_catalog[id].name;
}

const uint32_t* desktop_wallpaper_thumb(int id) {
    if (id < 0 || id >= WP_COUNT) return NULL;
    return wallpaper_catalog[id].thumb;
}

void desktop_set_wallpaper(int id) {
    if (id < 0 || id >= WP_COUNT) return;
    desktop.wallpaper_id = id;
    desktop.wallpaper_mode = W98_WALL_IMAGE;
    wallpaper_dirty = true;
    desktop.dirty = true;
}

void desktop_set_wallpaper_fit(int fit) {
    if (fit < 0 || fit > W98_WALLFIT_STRETCH) return;
    desktop.wallpaper_fit = fit;
    wallpaper_dirty = true;
    desktop.dirty = true;
}

void desktop_icons_clear_selection(void) {
    bool changed = false;
    for (int i = 0; i < desktop.icon_count; i++) {
        if (desktop.icons[i].selected) {
            desktop.icons[i].selected = false;
            changed = true;
        }
    }
    if (changed) desktop.dirty = true;
}

void desktop_layout_icons(void) {
    int cell_w = W98_ICON_CELL_W;
    int cell_h = W98_ICON_CELL_H;
    int cols = ((int)screen_width - W98_ICON_ORIGIN_X) / cell_w;
    if (cols < 1) cols = 1;

    int rows = ((int)screen_height - TASKBAR_HEIGHT - W98_ICON_ORIGIN_Y) / cell_h;
    if (rows < 1) rows = 1;

    for (int i = 0; i < desktop.icon_count; i++) {
        int col = i / rows;
        int row = i % rows;
        desktop.icons[i].icon_x = W98_ICON_ORIGIN_X + col * cell_w;
        desktop.icons[i].icon_y = W98_ICON_ORIGIN_Y + row * cell_h;
    }
}

int desktop_icon_add(const char* label, window_type_t type) {
    if (desktop.icon_count >= MAX_DESKTOP_ICONS) return -1;
    int idx = desktop.icon_count;
    strcpy(desktop.icons[idx].label, label);
    desktop.icons[idx].type = type;
    desktop.icons[idx].selected = false;
    desktop.icon_count++;
    desktop_layout_icons();
    desktop.icons_dirty = true;
    return idx;
}

void desktop_icon_remove(int idx) {
    if (idx < 0 || idx >= desktop.icon_count) return;
    for (int i = idx; i < desktop.icon_count - 1; i++) {
        desktop.icons[i] = desktop.icons[i + 1];
    }
    desktop.icon_count--;
    desktop_layout_icons();
    desktop.icons_dirty = true;
}

static int desktop_icon_label_rows(const char* label) {
    return ((int)strlen(label) + 11) / 12;
}

int desktop_icon_hit_test(int mx, int my) {
    for (int i = 0; i < desktop.icon_count; i++) {
        desktop_icon_t* icon = &desktop.icons[i];
        int rows = desktop_icon_label_rows(icon->label);
        int h = W98_DESKTOP_ICON + 4 + rows * 8;
        if (mx >= icon->icon_x && mx < icon->icon_x + W98_DESKTOP_ICON &&
            my >= icon->icon_y && my < icon->icon_y + h) {
            return i;
        }
    }
    return -1;
}

void desktop_icon_launch_offset(window_type_t type, const char* title) {
    int win_w = 400, win_h = 300;
    int win_x = 40 + (desktop.window_count * 30) % 180;
    int win_y = 30 + (desktop.window_count * 30) % 120;
    int is_game = (type == WINDOW_TYPE_DOOM || type == WINDOW_TYPE_FLAPPYBIRD ||
                   type == WINDOW_TYPE_SMB || type == WINDOW_TYPE_PONG ||
                   type == WINDOW_TYPE_GDASH);
    if (is_game) {
        win_w = 640; win_h = 480;
    } else if (type == WINDOW_TYPE_TASKMANAGER) {
        win_w = 420; win_h = 360;
    } else if (type == WINDOW_TYPE_SETTINGS) {
        win_w = 400; win_h = 545;
    } else if (type == WINDOW_TYPE_DISKMGMT) {
        win_w = 540; win_h = 400;
    } else if (type == WINDOW_TYPE_FAQ) {
        win_w = 480; win_h = 420;
    } else if (type == WINDOW_TYPE_BROWSER) {

        for (int i = 0; i < desktop.window_count; i++) {
            if (desktop.windows[i].type == WINDOW_TYPE_BROWSER) {
                if (desktop.windows[i].state == WINDOW_STATE_MINIMIZED) window_restore(i);
                else window_focus(i);
                desktop.dirty = true;
                return;
            }
        }
        win_w = 660; win_h = 500;
    }
    if (win_x + win_w > (int)screen_width) {
        win_x = screen_width - win_w - 20;
    }
    if (win_y + win_h > (int)screen_height - TASKBAR_HEIGHT) {
        win_y = screen_height - TASKBAR_HEIGHT - win_h - 20;
    }
    if (win_x < 0) win_x = 0;
    if (win_y < 0) win_y = 0;

    int win_idx = window_create(type, title, win_x, win_y, win_w, win_h);
    if (win_idx >= 0) {
        window_t* win = &desktop.windows[win_idx];
        if (is_game) {
            switch (type) {
                case WINDOW_TYPE_PONG:
                    pong_init();
                    win->game_tick = pong_tick;
                    win->close_func = pong_cleanup;
                    break;
                case WINDOW_TYPE_DOOM:
                    doom_init();
                    win->game_tick = doom_tick;
                    win->close_func = doom_cleanup;
                    break;
                case WINDOW_TYPE_FLAPPYBIRD:
                    flappybird_init();
                    win->game_tick = flappybird_tick;
                    win->close_func = flappybird_cleanup;
                    break;
                case WINDOW_TYPE_SMB:
                    smb_init();
                    win->game_tick = smb_tick;
                    win->close_func = smb_cleanup;
                    break;
                case WINDOW_TYPE_GDASH:
                    gd_init();
                    win->game_tick = gd_tick;
                    win->close_func = gd_cleanup;
                    break;
                default: break;
            }
        }
        desktop.dirty = true;
    }
}

void desktop_icon_launch(int idx) {
    if (idx < 0 || idx >= desktop.icon_count) return;
    desktop_icon_launch_offset(desktop.icons[idx].type, desktop.icons[idx].label);
}

static uint32_t wp_bilerp(const uint32_t* s, int sw, int sh, int sxf, int syf);

static void desktop_wallpaper_rebuild_cache(int w, int h) {
    if (w <= 0 || h <= 0) return;

    if (wallpaper_cache &&
        (wallpaper_cache_w != w || wallpaper_cache_h != h)) {
        kfree(wallpaper_cache);
        wallpaper_cache = NULL;
    }
    if (!wallpaper_cache) {
        wallpaper_cache = (uint32_t*)kmalloc((size_t)w * (size_t)h *
                                             sizeof(uint32_t));
        if (!wallpaper_cache) {
            wallpaper_cache_w = 0;
            wallpaper_cache_h = 0;
            return;
        }
        wallpaper_cache_w = w;
        wallpaper_cache_h = h;
    }

    int id = desktop.wallpaper_id;
    if (id < 0 || id >= WP_COUNT) id = WP_ID_CLASSIC;
    const wallpaper_entry_t* e = &wallpaper_catalog[id];
    const uint32_t* src = e->pixels;
    int sw = e->w;
    int sh = e->h;
    if (!src || sw < 2 || sh < 2) return;

    int fit = desktop.wallpaper_fit;
    if (fit < W98_WALLFIT_FILL || fit > W98_WALLFIT_STRETCH) {
        fit = W98_WALLFIT_FILL;
    }

    /* 16.16 fixed point: source steps per destination pixel. */
    uint64_t step_x = ((uint64_t)sw << 16) / (uint64_t)w;
    uint64_t step_y = ((uint64_t)sh << 16) / (uint64_t)h;
    uint64_t step = 0, off_x = 0, off_y = 0;
    int bx0 = 0, by0 = 0, bx1 = w, by1 = h;

    if (fit == W98_WALLFIT_FILL) {
        /* cover: keep aspect, crop the overflow, centre the crop */
        step = (step_x < step_y) ? step_x : step_y;
        off_x = (((uint64_t)sw << 16) - step * (uint64_t)w) / 2;
        off_y = (((uint64_t)sh << 16) - step * (uint64_t)h) / 2;
    } else if (fit == W98_WALLFIT_FIT) {
        /* contain: keep aspect, letterbox with the desktop colour */
        step = (step_x > step_y) ? step_x : step_y;
        int dw = (int)(((uint64_t)sw << 16) / step);
        int dh = (int)(((uint64_t)sh << 16) / step);
        if (dw > w) dw = w;
        if (dh > h) dh = h;
        bx0 = (w - dw) / 2;
        by0 = (h - dh) / 2;
        bx1 = bx0 + dw;
        by1 = by0 + dh;
    }

    uint32_t bar = 0xFF000000u | (theme_current->desktop_dark & 0x00FFFFFFu);
    uint64_t max_x = (uint64_t)(sw - 1) << 16;
    uint64_t max_y = (uint64_t)(sh - 1) << 16;

    for (int y = 0; y < h; y++) {
        uint32_t* drow = &wallpaper_cache[y * w];

        if (fit == W98_WALLFIT_FIT && (y < by0 || y >= by1)) {
            for (int x = 0; x < w; x++) drow[x] = bar;
            continue;
        }

        uint64_t syf;
        if (fit == W98_WALLFIT_FILL) syf = off_y + step * (uint64_t)y;
        else if (fit == W98_WALLFIT_FIT) syf = step * (uint64_t)(y - by0);
        else syf = step_y * (uint64_t)y;
        if (syf > max_y) syf = max_y;

        for (int x = 0; x < w; x++) {
            if (fit == W98_WALLFIT_FIT && (x < bx0 || x >= bx1)) {
                drow[x] = bar;
                continue;
            }

            uint64_t sxf;
            if (fit == W98_WALLFIT_FILL) sxf = off_x + step * (uint64_t)x;
            else if (fit == W98_WALLFIT_FIT) sxf = step * (uint64_t)(x - bx0);
            else sxf = step_x * (uint64_t)x;
            if (sxf > max_x) sxf = max_x;

            drow[x] = wp_bilerp(src, sw, sh, (int)sxf, (int)syf);
        }
    }
}

/* Bilinear sample of the source image at a 16.16 fixed-point point. */
static uint32_t wp_bilerp(const uint32_t* s, int sw, int sh, int sxf, int syf) {
    int x0 = sxf >> 16;
    int y0 = syf >> 16;
    int fx = sxf & 0xFFFF;
    int fy = syf & 0xFFFF;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x0 > sw - 1) x0 = sw - 1;
    if (y0 > sh - 1) y0 = sh - 1;
    int x1 = (x0 + 1 < sw) ? x0 + 1 : sw - 1;
    int y1 = (y0 + 1 < sh) ? y0 + 1 : sh - 1;

    uint32_t c00 = s[y0 * sw + x0];
    uint32_t c10 = s[y0 * sw + x1];
    uint32_t c01 = s[y1 * sw + x0];
    uint32_t c11 = s[y1 * sw + x1];

    int r00 = (int)((c00 >> 16) & 0xFF), g00 = (int)((c00 >> 8) & 0xFF), b00 = (int)(c00 & 0xFF);
    int r10 = (int)((c10 >> 16) & 0xFF), g10 = (int)((c10 >> 8) & 0xFF), b10 = (int)(c10 & 0xFF);
    int r01 = (int)((c01 >> 16) & 0xFF), g01 = (int)((c01 >> 8) & 0xFF), b01 = (int)(c01 & 0xFF);
    int r11 = (int)((c11 >> 16) & 0xFF), g11 = (int)((c11 >> 8) & 0xFF), b11 = (int)(c11 & 0xFF);

    int rt = r00 + ((r10 - r00) * fx >> 16);
    int gt = g00 + ((g10 - g00) * fx >> 16);
    int bt = b00 + ((b10 - b00) * fx >> 16);
    int rb = r01 + ((r11 - r01) * fx >> 16);
    int gb = g01 + ((g11 - g01) * fx >> 16);
    int bb = b01 + ((b11 - b01) * fx >> 16);

    int r = rt + ((rb - rt) * fy >> 16);
    int g = gt + ((gb - gt) * fy >> 16);
    int b = bt + ((bb - bt) * fy >> 16);

    return 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

void desktop_draw_wallpaper(void) {
    int h = (int)screen_height - TASKBAR_HEIGHT;
    int w = (int)screen_width;
    uint32_t stride = (uint32_t)(screen_pitch / 4);

    if (desktop.wallpaper_mode == W98_WALL_DITHER) {
        w98_fill_dither(0, 0, w, h);
        return;
    }

    if (desktop.wallpaper_mode == W98_WALL_SOLID) {
        w98_fill(0, 0, w, h, wallpaper_top);
        return;
    }

    if (wallpaper_dirty || !wallpaper_cache ||
        wallpaper_cache_w != w || wallpaper_cache_h != h) {
        desktop_wallpaper_rebuild_cache(w, h);
        wallpaper_dirty = false;
    }

    if (wallpaper_cache && wallpaper_cache_w == w && wallpaper_cache_h == h) {
        for (int y = 0; y < h; y++) {
            memcpy(&lfbptr[y * stride],
                   &wallpaper_cache[y * w],
                   (size_t)w * sizeof(uint32_t));
        }
        return;
    }

    w98_fill_dither(0, 0, w, h);
}

void desktop_draw_icons(void) {
    for (int i = 0; i < desktop.icon_count; i++) {
        desktop_icon_t* icon = &desktop.icons[i];
        int ix = icon->icon_x;
        int iy = icon->icon_y;

        if (icon->selected) {

            int rows = desktop_icon_label_rows(icon->label);
            int lx = ix - 6;
            int lw = W98_DESKTOP_ICON + 12;
            int ly = iy + W98_DESKTOP_ICON + 2;
            int lh = rows * 8 + 2;
            w98_fill(lx, ly, lw, lh, W98_HIGHLIGHT);
            for (int dx = 0; dx < lw; dx += 2) {
                draw_pixel(lx + dx, ly, W98_BTNHILITE);
                draw_pixel(lx + dx, ly + lh - 1, W98_BTNHILITE);
            }
            for (int dy = 0; dy < lh; dy += 2) {
                draw_pixel(lx, ly + dy, W98_BTNHILITE);
                draw_pixel(lx + lw - 1, ly + dy, W98_BTNHILITE);
            }
        }

        uint32_t* icon_pixels = desktop_icon_get_pixels(i);
        if (icon_pixels) {
            w98_icon_blit(icon_pixels, ICON_SIZE, W98_DESKTOP_ICON, ix, iy,
                          NULL);
        }

        char line1[16], line2[16];
        int len = (int)strlen(icon->label);
        if (len > 12) {
            int cut = 12;
            for (int i = 0; i < cut; i++) line1[i] = icon->label[i];
            line1[cut] = '\0';
            int j = 0;
            for (int i = cut; i < len && j < 15; i++) line2[j++] = icon->label[i];
            line2[j] = '\0';
        } else {
            strcpy(line1, icon->label);
            line2[0] = '\0';
        }

        int label_y = iy + W98_DESKTOP_ICON + 3;
        for (int row = 0; row < 2; row++) {
            const char* ln = row == 0 ? line1 : line2;
            if (!ln[0]) break;
            int label_x = ix + (W98_DESKTOP_ICON - w98_text_width(ln, 1)) / 2;
            if (label_x < 0) label_x = 0;
            if (icon->selected) {
                w98_text(ln, label_x, label_y + row * 8, W98_HIGHLIGHTTEXT,
                         W98_HIGHLIGHT, 1, NULL);
            } else {
                w98_text_outline(ln, label_x, label_y + row * 8,
                                 W98_ICON_LABEL, 1, NULL);
            }
        }
    }
}

static int taskbar_button_x(int slot) {
    return W98_STARTBTN_W + 6 + slot * (W98_TASKBTN_W + 2);
}

int desktop_get_taskbar_hover(int mx, int my) {
    int bar_y = (int)screen_height - TASKBAR_HEIGHT;
    if (my < bar_y || my >= bar_y + TASKBAR_HEIGHT) return -1;

    if (mx >= 2 && mx < 2 + W98_STARTBTN_W) return -2;

    int slot = 0;
    for (int i = 0; i < desktop.window_count; i++) {
        window_t* w = &desktop.windows[i];
        if (!w->visible && w->state != WINDOW_STATE_MINIMIZED) continue;
        int bx = taskbar_button_x(slot);
        if (mx >= bx && mx < bx + W98_TASKBTN_W) return i;
        slot++;
    }
    return -1;
}

void desktop_update_taskbar(void) {
    desktop.taskbar_dirty = true;
    desktop.dirty = true;
}

static void taskbar_time_string(char* time_str) {
    int hh = (int)rtc_hours;
    const char* suffix = "";
    if (hh >= 12) suffix = " PM";
    else suffix = " AM";
    int disp = hh % 12;
    if (disp == 0) disp = 12;

    int p = 0;
    if (disp < 10) time_str[p++] = ' ';
    int_to_string((uint32_t)disp, time_str + p);
    p = (int)strlen(time_str);
    time_str[p++] = ':';
    if (rtc_minutes < 10) time_str[p++] = '0';
    int_to_string(rtc_minutes, time_str + p);
    p = (int)strlen(time_str);
    for (int k = 0; suffix[k]; k++) time_str[p++] = suffix[k];
    time_str[p] = '\0';
}

void desktop_draw_taskbar(void) {
    int bar_y = (int)screen_height - TASKBAR_HEIGHT;
    int bar_w = (int)screen_width;

    w98_fill(0, bar_y, bar_w, TASKBAR_HEIGHT, W98_BTNFACE);

    if (theme_current->flat_taskbar) {
        draw_rect(0, bar_y, bar_w, 1, W98_BTNSHADOW);
    } else {
        draw_rect(0, bar_y, bar_w, 1, W98_BTNHILITE);
        draw_rect(0, bar_y + 1, bar_w, 1, W98_BTNLIGHT);
    }

    int hover = desktop_get_taskbar_hover(desktop_mouse_x, desktop_mouse_y);

    bool start_pressed = desktop.start_menu.active;
    int sx = 2, sy = bar_y + 2;
    if (theme_current->flat_taskbar) {
        uint32_t fill = W98_BTNFACE;
        if (start_pressed) fill = W98_BTNSHADOW;
        else if (hover == -2) fill = W98_BTNLIGHT;
        w98_fill(sx, sy, W98_STARTBTN_W, W98_STARTBTN_H, fill);
    } else {
        w98_fill(sx, sy, W98_STARTBTN_W, W98_STARTBTN_H, W98_BTNFACE);
        w98_bevel(sx, sy, W98_STARTBTN_W, W98_STARTBTN_H,
                  start_pressed ? W98_BEVEL_RAISED_PRESSED : W98_BEVEL_RAISED);
    }

    int ox = (start_pressed && !theme_current->flat_taskbar) ? 1 : 0;
    const uint32_t* start_pix = desktop_icon_get_start();
    w98_icon_blit(start_pix, 16, 16, sx + 3 + ox, sy + 4 + ox, NULL);
    w98_text_bold("Start", sx + 22 + ox, sy + 8 + ox, W98_BTNTEXT,
                  W98_BTNFACE, 1, NULL);

    int gx = W98_STARTBTN_W + 3;
    if (theme_current->flat_taskbar) {
        draw_rect(gx, bar_y + 3, 1, TASKBAR_HEIGHT - 6, W98_BTNSHADOW);
    } else {
        draw_rect(gx, bar_y + 3, 1, TASKBAR_HEIGHT - 6, W98_BTNSHADOW);
        draw_rect(gx + 1, bar_y + 3, 1, TASKBAR_HEIGHT - 6, W98_BTNHILITE);
    }

    int slot = 0;
    for (int i = 0; i < desktop.window_count; i++) {
        window_t* w = &desktop.windows[i];
        if (!w->visible && w->state != WINDOW_STATE_MINIMIZED) continue;

        int bx = taskbar_button_x(slot);
        int bw = W98_TASKBTN_W;
        if (bx + bw > bar_w - 80) {
            bw = bar_w - 80 - bx;
            if (bw < 24) break;
        }

        bool active = w->has_focus && w->visible;
        if (theme_current->flat_taskbar) {
            uint32_t fill = W98_BTNFACE;
            if (active) fill = W98_BTNLIGHT;
            else if (hover == i) fill = W98_BTNLIGHT;
            w98_fill(bx, bar_y + 2, bw, W98_TASKBTN_H, fill);
            if (active) {
                w98_fill(bx, bar_y + TASKBAR_HEIGHT - 4, bw, 2, W98_HIGHLIGHT);
            }
        } else {
            w98_fill(bx, bar_y + 2, bw, W98_TASKBTN_H, W98_BTNFACE);
            w98_bevel(bx, bar_y + 2, bw, W98_TASKBTN_H,
                      active ? W98_BEVEL_RAISED_PRESSED : W98_BEVEL_RAISED);
        }

        int cx = bx + 3;
        uint32_t* pix = desktop_icon_pixels_for_type(w->type);
        if (pix) {
            w98_icon_blit_small(pix, ICON_SIZE, 16, cx, bar_y + 6, NULL);
            cx += 18;
        }

        char fit[WINDOW_TITLE_MAX];
        w98_text_fit(w->title, fit, sizeof(fit),
                     bx + bw - cx - (active ? 16 : 5), 1);

        if (active) {
            if (!theme_current->flat_taskbar) {
                for (int dx = bx + 3; dx < bx + bw - 3; dx += 2) {
                    for (int dy = bar_y + 5; dy < bar_y + TASKBAR_HEIGHT - 5; dy += 2) {
                        draw_pixel(dx, dy, W98_BTNSHADOW);
                    }
                }
            }
            w98_text_bold(fit, cx + 1, bar_y + 10, W98_BTNTEXT, W98_BTNFACE,
                          1, NULL);
        } else {
            w98_text(fit, cx, bar_y + 9, W98_BTNTEXT, W98_BTNFACE, 1, NULL);
        }
        slot++;
    }

    char time_str[16];
    taskbar_time_string(time_str);

    int tw = w98_text_width(time_str, 1);
    int tray_w = tw + 14;
    int tray_x = bar_w - tray_w - 3;
    int tray_y = bar_y + 3;

    int net_icon_w = 0;
    int has_nic = net_driver_name[0] && net_driver_name[0] != 'n';
    if (has_nic) net_icon_w = 20;
    tray_w += net_icon_w;
    tray_x -= net_icon_w;

    w98_fill(tray_x, tray_y, tray_w, W98_TRAY_H, W98_BTNFACE);
    if (theme_current->flat_taskbar) {
        draw_rect(tray_x, bar_y + 3, 1, TASKBAR_HEIGHT - 6, W98_BTNSHADOW);
    } else {
        w98_bevel(tray_x, tray_y, tray_w, W98_TRAY_H, W98_BEVEL_SUNKEN);
    }

    if (has_nic) {

        int ix = tray_x + 5, iy = tray_y + 5;
        int st = net_dhcp_state();
        uint32_t screen = net_has_link ? (st == 3 ? 0xFF1084D0u : 0xFF808080u) : 0xFF404040u;
        for (int k = 0; k < 2; k++) {
            int ox = ix + k * 7, oy = iy + (k ? 3 : 0);
            w98_fill(ox, oy, 8, 7, W98_BTNDKSHADOW);
            w98_fill(ox + 1, oy + 1, 6, 5, screen);
            w98_fill(ox + 3, oy + 7, 2, 1, W98_BTNDKSHADOW);
            w98_fill(ox + 2, oy + 8, 4, 1, W98_BTNDKSHADOW);
        }
        if (!net_has_link) {
            for (int k = 0; k < 6; k++) {
                draw_pixel(ix + 5 + k, iy + 3 + k, 0xFFFF0000u);
                draw_pixel(ix + 10 - k, iy + 3 + k, 0xFFFF0000u);
            }
        }
    }

    w98_text(time_str, tray_x + net_icon_w + 7, tray_y + 6, W98_BTNTEXT,
             W98_BTNFACE, 1, NULL);
}

extern void mouse_draw_cursor(void);

extern void mouse_restore_under_cursor(void);

void desktop_invalidate_rect(int x0, int y0, int x1, int y1) {
    int sw = (int)screen_width;
    int sh = (int)screen_height;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > sw) x1 = sw;
    if (y1 > sh) y1 = sh;
    if (x0 >= x1 || y0 >= y1) return;

    if (!desktop.flush_partial) {
        desktop.flush_x0 = x0;
        desktop.flush_y0 = y0;
        desktop.flush_x1 = x1;
        desktop.flush_y1 = y1;
        desktop.flush_partial = true;
    } else {
        if (x0 < desktop.flush_x0) desktop.flush_x0 = x0;
        if (y0 < desktop.flush_y0) desktop.flush_y0 = y0;
        if (x1 > desktop.flush_x1) desktop.flush_x1 = x1;
        if (y1 > desktop.flush_y1) desktop.flush_y1 = y1;
    }
    desktop.dirty = true;
}

void desktop_render(void) {
    desktop.dirty = false;
    desktop.taskbar_dirty = false;
    desktop.icons_dirty = false;

    desktop_draw_wallpaper();
    desktop_draw_icons();
    window_draw_all();
    if (desktop.start_menu.visible) start_menu_draw();
    desktop_draw_taskbar();

    if (mouse_enabled) {
        mouse_draw_cursor();
    }

    if (desktop.flush_partial) {
        int y0 = desktop.flush_y0;
        int y1 = desktop.flush_y1;
        desktop.flush_partial = false;

        if (y1 - y0 > (int)screen_height * 2 / 3) {
            flush_screen_to_hw();
        } else {
            flush_rows_to_hw(y0, y1);
        }
    } else {
        flush_screen_to_hw();
    }
}

void desktop_render_cursor_only(int old_x, int old_y) {
    (void)old_x;
    if (!mouse_enabled) return;

    mouse_restore_under_cursor();
    mouse_draw_cursor();

    int y0 = old_y < mouse_cursor_y ? old_y : mouse_cursor_y;
    int y1 = (old_y > mouse_cursor_y ? old_y : mouse_cursor_y) + 16;
    flush_rows_to_hw(y0, y1);
}

bool desktop_pointer_needs_repaint(int mx, int my) {
    if (desktop.start_menu.visible) return true;
    if (desktop_mouse_down) return true;
    for (int i = 0; i < desktop.window_count; i++) {
        window_t* w = &desktop.windows[i];
        if (!w->visible || w->state == WINDOW_STATE_MINIMIZED) continue;
        if (w->type != WINDOW_TYPE_SETTINGS && w->type != WINDOW_TYPE_FILEMANAGER &&
            w->type != WINDOW_TYPE_DISKMGMT) continue;
        if (mx >= w->rect.x && mx < w->rect.x + w->rect.width &&
            my >= w->rect.y && my < w->rect.y + w->rect.height) {
            return true;
        }
    }
    return false;
}

void desktop_handle_mouse(int mx, int my, int buttons) {
    desktop_mouse_x = mx;
    desktop_mouse_y = my;

    if (buttons & 1) {
        if (!desktop_mouse_down) {
            desktop_mouse_down = true;

            int taskbar_hit = desktop_get_taskbar_hover(mx, my);
            if (taskbar_hit == -2) {
                start_menu_toggle();
                desktop.dirty = true;
                return;
            } else if (taskbar_hit >= 0) {

                if (desktop.start_menu.visible) {
                    start_menu_close();
                    desktop.dirty = true;
                    return;
                }
                window_t* w = &desktop.windows[taskbar_hit];
                if (w->state == WINDOW_STATE_MINIMIZED) {
                    window_restore(taskbar_hit);
                } else if (w->has_focus) {
                    window_minimize(taskbar_hit);
                } else {
                    window_focus(taskbar_hit);
                }
                desktop.dirty = true;
                return;
            }

            if (mx < (int)screen_width &&
                my < (int)screen_height - TASKBAR_HEIGHT) {

                if (desktop.start_menu.visible) {
                    start_menu_handle_click(mx, my);
                    desktop.dirty = true;
                    return;
                }

                window_t* focused_click = NULL;
                int focus_idx = -1;
                for (int i = desktop.window_count - 1; i >= 0; i--) {
                    window_t* wc = &desktop.windows[i];
                    if (!wc->visible || wc->state == WINDOW_STATE_MINIMIZED) {
                        continue;
                    }
                    if (mx >= wc->rect.x && mx < wc->rect.x + wc->rect.width &&
                        my >= wc->rect.y && my < wc->rect.y + wc->rect.height) {
                        focused_click = wc;
                        focus_idx = i;
                        break;
                    }
                }

                if (focused_click && focused_click->mouse_func &&
                    mx >= focused_click->rect.client_x &&
                    mx < focused_click->rect.client_x + focused_click->rect.client_w &&
                    my >= focused_click->rect.client_y &&
                    my < focused_click->rect.client_y + focused_click->rect.client_h) {
                    window_focus(focus_idx);
                    focused_click->mouse_func(focused_click, mx, my, buttons);
                    desktop.dirty = true;
                    return;
                }

                window_begin_drag(mx, my);

                if (desktop_drag_window < 0 && desktop_resize_window < 0 &&
                    window_caption_pressed() < 0) {
                    int icon_idx = desktop_icon_hit_test(mx, my);
                    if (icon_idx >= 0) {

                        if (desktop.selected_icon != icon_idx) {
                            desktop_icons_clear_selection();
                            desktop.icons[icon_idx].selected = true;
                            desktop.selected_icon = icon_idx;
                        }
                        if ((int)(uptime_ticks - desktop.last_click_tick) <=
                                DOUBLE_CLICK_TICKS &&
                            desktop.last_click_icon == icon_idx) {
                            desktop_icon_launch(icon_idx);
                            desktop.last_click_tick = 0;
                        } else {
                            desktop.last_click_tick = uptime_ticks;
                            desktop.last_click_icon = icon_idx;
                        }
                        desktop.dirty = true;
                    } else {
                        desktop_icons_clear_selection();
                        desktop.selected_icon = -1;
                        start_menu_close();
                        desktop.dirty = true;
                    }
                }
            }
        } else {
            if (desktop_drag_window >= 0) window_drag_update(mx, my);
            else if (desktop_resize_window >= 0) window_resize_update(mx, my);
        }
    } else {
        if (desktop_mouse_down) {
            desktop_mouse_down = false;
            window_end_drag();
        }
        if (desktop.start_menu.visible) {
            start_menu_handle_hover(mx, my);
        }
    }
}

void desktop_handle_keyboard(char c) {
    if (c == 27 && ctrl_pressed) {

        for (int i = 0; i < desktop.window_count; i++) {
            if (desktop.windows[i].type == WINDOW_TYPE_TASKMANAGER) {
                if (desktop.windows[i].state == WINDOW_STATE_MINIMIZED) {
                    window_restore(i);
                } else {
                    window_focus(i);
                }
                desktop.dirty = true;
                return;
            }
        }
        desktop_icon_launch_offset(WINDOW_TYPE_TASKMANAGER, "Task Manager");
        return;
    }
    if (c == '\t' && ctrl_pressed) {
        if (!desktop.alt_tab_active) {
            desktop.alt_tab_active = true;
            desktop.alt_tab_index = 0;
        } else {
            desktop.alt_tab_index =
                (desktop.alt_tab_index + 1) % desktop.window_count;
        }
        return;
    }
    if (desktop.alt_tab_active) {
        if (desktop.window_count > 0) {
            window_focus(desktop.alt_tab_index % desktop.window_count);
        }
        desktop.alt_tab_active = false;
        return;
    }

    window_t* focused = window_get_focused();
    if (focused && focused->keyboard_func) {
        focused->keyboard_func(focused, c);
    }

    if (c == 27) {
        if (desktop.start_menu.visible) {
            start_menu_close();
            desktop.dirty = true;
            return;
        }

        if (focused && focused->visible &&
            focused->state == WINDOW_STATE_NORMAL &&
            !focused->keyboard_func) {
            window_close_by_ptr(focused);
            desktop.dirty = true;
        }
    }
}

void boot_screen_show(void);
void boot_screen_hide(void);

void desktop_init(void) {
    br_font_init();
    memset(&desktop, 0, sizeof(desktop_state_t));
    desktop.desktop_mode = true;
    desktop.initialized = true;
    desktop.window_count = 0;
    desktop.next_z_order = 1;
    desktop.icon_count = 0;
    desktop.start_menu.active = false;
    desktop.start_menu.visible = false;
    desktop.wallpaper_mode = W98_WALL_DITHER;
    desktop.wallpaper_id = WP_ID_CLASSIC;
    desktop.wallpaper_fit = W98_WALLFIT_FILL;
    desktop.selected_icon = -1;
    desktop.last_click_icon = -1;
    desktop_drag_window = -1;
    desktop_resize_window = -1;

    desktop_icon_add("My Computer", WINDOW_TYPE_FASTFETCH);
    desktop_icon_add("Terminal", WINDOW_TYPE_TERMINAL);
    desktop_icon_add("Notepad", WINDOW_TYPE_NOTEPAD);
    desktop_icon_add("File Manager", WINDOW_TYPE_FILEMANAGER);
    desktop_icon_add("Settings", WINDOW_TYPE_SETTINGS);
    desktop_icon_add("Disk Mgmt", WINDOW_TYPE_DISKMGMT);
    desktop_icon_add("Network", WINDOW_TYPE_NETWORK);
    desktop_icon_add("Browser", WINDOW_TYPE_BROWSER);
    desktop_icon_add("Task Manager", WINDOW_TYPE_TASKMANAGER);
    desktop_icon_add("About", WINDOW_TYPE_ABOUT);
    desktop_icon_add("FAQ", WINDOW_TYPE_FAQ);

    int plugin_count = 0;
    plugin_t* plugins = plugin_get_list(&plugin_count);
    for (int i = 0; i < plugin_count; i++) {
        if (!plugins[i].loaded) continue;
        if (strcmp(plugins[i].name, "doom") == 0) {
            desktop_icon_add("Doom", WINDOW_TYPE_DOOM);
        } else if (strcmp(plugins[i].name, "flappybird") == 0) {
            desktop_icon_add("Flappy Bird", WINDOW_TYPE_FLAPPYBIRD);
        } else if (strcmp(plugins[i].name, "smb") == 0) {
            desktop_icon_add("Super Mario Bros", WINDOW_TYPE_SMB);
        } else if (strcmp(plugins[i].name, "pong") == 0) {
            desktop_icon_add("Pong", WINDOW_TYPE_PONG);
        } else if (strcmp(plugins[i].name, "gdash") == 0) {
            desktop_icon_add("Geometry Dash", WINDOW_TYPE_GDASH);
        }
    }
    desktop_icons_load();
}
