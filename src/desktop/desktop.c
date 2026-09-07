/* desktop.c - Win98-style desktop environment for SharkOS.
 *
 * Rendering model: desktop_render() composes wallpaper -> icons -> windows ->
 * start menu -> taskbar -> mouse cursor into the lfbptr back buffer and then
 * flushes once. Nothing in this file writes to hw_lfbptr directly.
 *
 * The wallpaper image used to be rescaled (736x460 -> fullscreen) on every
 * frame with per-pixel fixed point math. It is now scaled once into a cached
 * buffer and blitted with memcpy per row.
 */

#include "kernel.h"
#include "desktop.h"
#include "win98_theme.h"
#include "plugin_manager.h"
#include "doom.h"
#include "flappybird.h"
#include "smb.h"
#include "pong.h"
#include "geometrydash.h"
#include "wallpaper_data.h"
#include "icon_data.h"

uint32_t wallpaper_top = DESKTOP_BG_TOP;
uint32_t wallpaper_bot = DESKTOP_BG_BOT;

static uint32_t* wallpaper_cache = NULL;
static int wallpaper_cache_w = 0;
static int wallpaper_cache_h = 0;
static bool wallpaper_dirty = true;

/* Double-click: 400ms. uptime_ticks runs at 100 Hz. */
#define DOUBLE_CLICK_TICKS 40

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

/* ------------------------------------------------------------ icon layout */

/* Explorer layout: top-to-bottom, then next column. The old grid ran left to
 * right across the screen, which wastes the tall desktop and looks nothing
 * like Win98. */
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
    return ((int)strlen(label) + 11) / 12;      /* 12 chars per row at 6px */
}

/* Hit area = the 32x32 glyph plus its label band, like Explorer. */
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

/* ---------------------------------------------------------------- wallpaper */

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

    uint32_t ww = (uint32_t)WALLPAPER_WIDTH;
    uint32_t wh = (uint32_t)WALLPAPER_HEIGHT;

    for (int y = 0; y < h; y++) {
        uint32_t sy = 0;
        if (wh) sy = ((uint32_t)y * wh) / (uint32_t)h;
        if (sy >= wh) sy = wh - 1;
        const uint32_t* srow = &wallpaper_pixels[sy * ww];
        uint32_t* drow = &wallpaper_cache[y * w];
        for (int x = 0; x < w; x++) {
            uint32_t sx = ((uint32_t)x * ww) / (uint32_t)w;
            if (sx >= ww) sx = ww - 1;
            drow[x] = 0xFF000000u | (srow[sx] & 0x00FFFFFFu);
        }
    }
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
        w98_fill(0, 0, w, h, W98_DESKTOP);
        return;
    }

    /* Image mode. Rescale once, then blit one row at a time. */
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

    /* The cache allocation failed; fall back to the dither so the desktop is
     * never unpainted. */
    w98_fill_dither(0, 0, w, h);
}

/* -------------------------------------------------------------------- icons */

void desktop_draw_icons(void) {
    for (int i = 0; i < desktop.icon_count; i++) {
        desktop_icon_t* icon = &desktop.icons[i];
        int ix = icon->icon_x;
        int iy = icon->icon_y;

        if (icon->selected) {
            /* Explorer selection: highlight behind the label only, plus a
             * dotted outline around the glyph. */
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

        /* Labels wider than the cell wrap onto a second row, like
         * Explorer, instead of spilling into the neighbour. */
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
                                 W98_BTNHILITE, 1, NULL);
            }
        }
    }
}

/* ------------------------------------------------------------------ taskbar */

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

void desktop_draw_taskbar(void) {
    int bar_y = (int)screen_height - TASKBAR_HEIGHT;
    int bar_w = (int)screen_width;

    /* Raised bar, highlight along the top edge only. */
    w98_fill(0, bar_y, bar_w, TASKBAR_HEIGHT, W98_BTNFACE);
    draw_rect(0, bar_y, bar_w, 1, W98_BTNHILITE);
    draw_rect(0, bar_y + 1, bar_w, 1, W98_BTNLIGHT);

    /* Start button. */
    bool start_pressed = desktop.start_menu.active;
    int sx = 2, sy = bar_y + 2;
    w98_fill(sx, sy, W98_STARTBTN_W, W98_STARTBTN_H, W98_BTNFACE);
    w98_bevel(sx, sy, W98_STARTBTN_W, W98_STARTBTN_H,
              start_pressed ? W98_BEVEL_RAISED_PRESSED : W98_BEVEL_RAISED);

    int ox = start_pressed ? 1 : 0;
    const uint32_t* start_pix = desktop_icon_get_start();
    w98_icon_blit(start_pix, 16, 16, sx + 3 + ox, sy + 4 + ox, NULL);
    w98_text_bold("Start", sx + 22 + ox, sy + 8 + ox, W98_BTNTEXT,
                  W98_BTNFACE, 1, NULL);

    /* Separator groove after the start button. */
    int gx = W98_STARTBTN_W + 3;
    draw_rect(gx, bar_y + 3, 1, TASKBAR_HEIGHT - 6, W98_BTNSHADOW);
    draw_rect(gx + 1, bar_y + 3, 1, TASKBAR_HEIGHT - 6, W98_BTNHILITE);

    /* Task buttons, one per open or minimized window. */
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
        w98_fill(bx, bar_y + 2, bw, W98_TASKBTN_H, W98_BTNFACE);
        w98_bevel(bx, bar_y + 2, bw, W98_TASKBTN_H,
                  active ? W98_BEVEL_RAISED_PRESSED : W98_BEVEL_RAISED);

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
            /* Dithered interior marks the focused window, like Win98. */
            for (int dx = bx + 3; dx < bx + bw - 3; dx += 2) {
                for (int dy = bar_y + 5; dy < bar_y + TASKBAR_HEIGHT - 5; dy += 2) {
                    draw_pixel(dx, dy, W98_BTNSHADOW);
                }
            }
            w98_text_bold(fit, cx + 1, bar_y + 10, W98_BTNTEXT, W98_BTNFACE,
                          1, NULL);
        } else {
            w98_text(fit, cx, bar_y + 9, W98_BTNTEXT, W98_BTNFACE, 1, NULL);
        }
        slot++;
    }

    /* Clock tray, sunk on the right. */
    char time_str[16];
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

    int tw = w98_text_width(time_str, 1);
    int tray_w = tw + 14;
    int tray_x = bar_w - tray_w - 3;
    int tray_y = bar_y + 3;

    w98_fill(tray_x, tray_y, tray_w, W98_TRAY_H, W98_BTNFACE);
    w98_bevel(tray_x, tray_y, tray_w, W98_TRAY_H, W98_BEVEL_SUNKEN);
    w98_text(time_str, tray_x + 7, tray_y + 6, W98_BTNTEXT, W98_BTNFACE, 1,
             NULL);
}

/* ----------------------------------------------------------------- render */

extern void mouse_draw_cursor(void);

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

    flush_screen_to_hw();
}

/* ------------------------------------------------------------------- input */

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
                /* Clicking a task button while the menu is open closes the
                 * menu and swallows the click, like Win98. */
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

                /* Click inside a focused window's client area first so its
                 * mouse_func sees the event before drag logic runs. */
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
                        /* Select immediately; launch on double-click. */
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
        /* ESC used to close the focused window unconditionally, which made
         * the escape key useless inside Notepad et al. Only windows without
         * a keyboard handler (pure info dialogs) close on ESC. */
        if (focused && focused->visible &&
            focused->state == WINDOW_STATE_NORMAL &&
            !focused->keyboard_func) {
            window_close_by_ptr(focused);
            desktop.dirty = true;
        }
    }
}

/* -------------------------------------------------------------------- init */

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
    desktop.wallpaper_mode = W98_WALL_DITHER;
    desktop.selected_icon = -1;
    desktop.last_click_icon = -1;
    desktop_drag_window = -1;
    desktop_resize_window = -1;

    desktop_icon_add("My Computer", WINDOW_TYPE_FASTFETCH);
    desktop_icon_add("Terminal", WINDOW_TYPE_TERMINAL);
    desktop_icon_add("Notepad", WINDOW_TYPE_NOTEPAD);
    desktop_icon_add("File Manager", WINDOW_TYPE_FILEMANAGER);
    desktop_icon_add("Settings", WINDOW_TYPE_SETTINGS);
    desktop_icon_add("Network", WINDOW_TYPE_NETWORK);
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
