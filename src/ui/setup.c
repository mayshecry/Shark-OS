#include "kernel.h"
#include "desktop.h"
#include "theme.h"

#define SETUP_ROWS 6

#define SETUP_BG       0xFF04121Fu
#define SETUP_PANEL    0xFF0A2138u
#define SETUP_FRAME    0xFF3FA9F5u
#define SETUP_HEADER   0xFF0E4C7Au
#define SETUP_LABEL    0xFF9FD4F5u
#define SETUP_VALUE    0xFFFFFFFFu
#define SETUP_SEL_FG   0xFF001018u
#define SETUP_SEL_BG   0xFF56C8FFu
#define SETUP_DIM      0xFF6F8FA6u
#define SETUP_WARN     0xFFFFD75Eu

static int setup_sel = 0;
bool boot_to_cli = false;
int boot_theme_id = THEME_ID_WIN98;

static const char* setup_wm_value(void) {
    return tiling_enabled ? "Tiling" : "Floating";
}

static const char* setup_theme_value(void) {
    int id = boot_theme_id;
    if (id == THEME_ID_MODERN) return "Modern";
    if (id == THEME_ID_KAWAII) return "Kawaii";
    if (id == THEME_ID_WINDOWMAKER) return "Window Maker";
    return "Win98";
}

static const char* setup_term_value(void) {
    if (selected_theme == THEME_BLUE) return "Blue";
    if (selected_theme == THEME_TEMPLEOS) return "Temple";
    return "SharkOS";
}

static const char* setup_boot_value(void) {
    return boot_to_cli ? "Command line" : "Desktop";
}

static const char* setup_mouse_value(void) {
    return mouse_enabled ? "On" : "Off";
}

static void setup_box(int x, int y, int w, int h, uint32_t fill, uint32_t frame, int thick) {
    draw_rect(x, y, w, h, fill);
    for (int i = 0; i < thick; i++) {
        draw_rect(x + i, y + i, w - i * 2, 1, frame);
        draw_rect(x + i, y + h - 1 - i, w - i * 2, 1, frame);
        draw_rect(x + i, y + i, 1, h - i * 2, frame);
        draw_rect(x + w - 1 - i, y + i, 1, h - i * 2, frame);
    }
}

static void setup_text(const char* s, int x, int y, uint32_t fg, uint32_t bg) {
    draw_string_px(s, x, y, fg, bg);
}

void boot_setup_draw(void) {
    if (!lfbptr || !screen_width || !screen_height) return;

    int sw = (int)screen_width;
    int sh = (int)screen_height;
    draw_rect(0, 0, sw, sh, SETUP_BG);

    int bw = sw - 96;
    if (bw > 760) bw = 760;
    if (bw < 420) bw = sw - 24;
    int bh = sh - 96;
    if (bh > 404) bh = 404;
    if (bh < 300) bh = sh - 24;
    int bx = (sw - bw) / 2;
    int by = (sh - bh) / 2;

    setup_box(bx, by, bw, bh, SETUP_PANEL, SETUP_FRAME, 2);

    draw_rect(bx + 2, by + 2, bw - 4, 26, SETUP_HEADER);
    setup_text("SharkOS Setup Utility", bx + 12, by + 8, 0xFFFFFFFFu, SETUP_HEADER);
    setup_text("NEMO-BIOS v1.0", bx + bw - 12 - 14 * 8, by + 8, 0xFFBEE3FFu, SETUP_HEADER);

    int logo = bh - 150;
    if (logo > 176) logo = 176;
    if (logo > bw / 3) logo = bw / 3;
    if (logo < 96) logo = 96;
    int lx = bx + 22;
    int ly = by + 44 + ((bh - 190) - logo) / 2;
    if (ly < by + 40) ly = by + 40;
    imgfetch_blit(lx, ly, logo, lx, ly, lx + logo, ly + logo);
    setup_text("SharkOS", lx + 4, ly + logo + 10, SETUP_WARN, SETUP_PANEL);

    int col = lx + logo + 28;
    int row = by + 52;
    int step = 30;
    if (row + step * SETUP_ROWS > by + bh - 60) step = 26;
    int value_col = col + 20 * 8;

    for (int i = 0; i < SETUP_ROWS - 1; i++) {
        const char* label = "";
        const char* value = "";
        switch (i) {
            case 0: label = "Window manager"; value = setup_wm_value(); break;
            case 1: label = "Desktop theme "; value = setup_theme_value(); break;
            case 2: label = "Terminal theme"; value = setup_term_value(); break;
            case 3: label = "Boot to       "; value = setup_boot_value(); break;
            case 4: label = "Mouse         "; value = setup_mouse_value(); break;
        }
        int y = row + i * step;
        int sel = (setup_sel == i);
        if (sel) draw_rect(col - 6, y - 4, bw - (col - bx) - 24, step - 4, SETUP_SEL_BG);
        setup_text(sel ? ">" : " ", col - 4, y, SETUP_SEL_FG, sel ? SETUP_SEL_BG : SETUP_PANEL);
        setup_text(label, col + 12, y, sel ? SETUP_SEL_FG : SETUP_LABEL,
                   sel ? SETUP_SEL_BG : SETUP_PANEL);
        setup_text(value, value_col, y, sel ? SETUP_SEL_FG : SETUP_VALUE,
                   sel ? SETUP_SEL_BG : SETUP_PANEL);
    }

    int start_y = row + (SETUP_ROWS - 1) * step + 10;
    if (setup_sel == SETUP_ROWS - 1) {
        draw_rect(col - 6, start_y - 4, bw - (col - bx) - 24, step - 2, SETUP_SEL_BG);
        setup_text(">", col - 4, start_y, SETUP_SEL_FG, SETUP_SEL_BG);
        setup_text("[ Start SharkOS ]", col + 12, start_y, SETUP_SEL_FG, SETUP_SEL_BG);
    } else {
        setup_text("  [ Start SharkOS ]", col + 12, start_y, SETUP_WARN, SETUP_PANEL);
    }

    int foot = by + bh - 26;
    draw_rect(bx + 2, foot - 6, bw - 4, 1, SETUP_FRAME);
    setup_text("UP/DOWN move   LEFT/RIGHT or ENTER change   S or ESC boots now",
               bx + 12, foot, SETUP_DIM, SETUP_PANEL);
}

static void setup_apply(void) {
    tiling_enabled = tiling_enabled ? true : false;
    mouse_enabled = mouse_enabled ? 1 : 0;
}

static void setup_change(int dir) {
    switch (setup_sel) {
        case 0:
            tiling_enabled = !tiling_enabled;
            break;
        case 1: {
            int id = boot_theme_id + dir;
            if (id < 0) id = THEME_COUNT - 1;
            if (id >= THEME_COUNT) id = 0;
            boot_theme_id = id;
            break;
        }
        case 2: {
            int t = selected_theme + dir;
            if (t < 0) t = 2;
            if (t > 2) t = 0;
            apply_theme(t);
            break;
        }
        case 3:
            boot_to_cli = boot_to_cli ? false : true;
            break;
        case 4:
            mouse_enabled = mouse_enabled ? 0 : 1;
            break;
    }
}

void boot_setup_run(void) {
    if (!lfbptr || !screen_width || !screen_height) return;
    if (lite_mode || legacy_mode) return;

    setup_sel = 0;
    boot_setup_draw();

    for (;;) {
        char c = keyboard_getchar();
        if (!c) {
            yield();
            continue;
        }
        if (c == 0x10) {
            if (setup_sel > 0) setup_sel--;
        } else if (c == 0x11) {
            if (setup_sel < SETUP_ROWS - 1) setup_sel++;
        } else if (c == 0x12) {
            setup_change(-1);
        } else if (c == 0x13) {
            setup_change(1);
        } else if (c == ' ') {
            if (setup_sel == SETUP_ROWS - 1) break;
            setup_change(1);
        } else if (c == '\n' || c == '\r') {
            if (setup_sel == SETUP_ROWS - 1) break;
            setup_change(1);
        } else if (c == 27 || c == 's' || c == 'S') {
            break;
        }
        boot_setup_draw();
    }

    setup_apply();
    draw_rect(0, 0, (int)screen_width, (int)screen_height, 0xFF000000u);
}
