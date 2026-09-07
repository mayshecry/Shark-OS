/* bootscreen.c - Windows 98 style boot splash.
 *
 * Teal dithered background, product name, and the classic block progress
 * bar. Same public API as before.
 */

#include "kernel.h"
#include "desktop.h"
#include "win98_theme.h"

#define BOOT_BAR_W 320
#define BOOT_BAR_H 20
#define BOOT_BAR_X (((int)screen_width - BOOT_BAR_W) / 2)
#define BOOT_BAR_Y ((int)screen_height * 3 / 4)

static int boot_progress = 0;
static char boot_message[64] = "Initializing...";
static volatile uint32_t boot_spinner_counter = 0;

static void draw_pixel_safe(int x, int y, uint32_t color) {
    if (x < 0 || y < 0) return;
    if ((uint32_t)x >= screen_width || (uint32_t)y >= screen_height) return;
    uint32_t stride = screen_pitch / 4;
    lfbptr[(uint32_t)y * stride + (uint32_t)x] = color;
}

static void draw_rect_safe(int x, int y, int w, int h, uint32_t color) {
    draw_rect(x, y, w, h, color);
}

/* Teal checkerboard, exactly what the desktop shows after login. */
static void draw_dither_bg(void) {
    int w = (int)screen_width;
    int h = (int)screen_height;
    uint32_t stride = screen_pitch / 4;
    for (int y = 0; y < h; y++) {
        uint32_t first = (y & 1) ? W98_DESKTOP_DARK : W98_DESKTOP;
        uint32_t second = (y & 1) ? W98_DESKTOP : W98_DESKTOP_DARK;
        uint32_t* row = &lfbptr[(uint32_t)y * stride];
        for (int x = 0; x < w; x++) {
            row[x] = (x & 1) ? second : first;
        }
    }
}

static void draw_shark_logo(int cx, int cy, int size, uint32_t color) {
    int half = size / 2;

    for (int dy = -half; dy <= half; dy++) {
        int row_width = half - (dy * dy) / (half + 1);
        if (row_width < 0) row_width = 0;
        for (int dx = -row_width; dx <= row_width; dx++) {
            draw_pixel_safe(cx + dx, cy + dy, color);
        }
    }

    int eye_y = cy - half / 3;
    for (int dy = -2; dy <= 2; dy++) {
        for (int dx = -2; dx <= 2; dx++) {
            draw_pixel_safe(cx + half / 3 + dx, eye_y + dy, W98_DESKTOP);
        }
    }

    /* Dorsal fin, so it reads as a shark and not a blob. */
    for (int dy = 0; dy < half / 2; dy++) {
        int fw = half / 2 - dy;
        for (int dx = 0; dx < fw; dx++) {
            draw_pixel_safe(cx - half / 4 + dx, cy - half - dy, color);
        }
    }
}

/* 10 Win98-style blocks sliding along a sunken track. */
static void draw_progress_bar(int progress) {
    int bar_x = BOOT_BAR_X;
    int bar_y = BOOT_BAR_Y;

    w98_surface(bar_x - 4, bar_y - 4, BOOT_BAR_W + 8, BOOT_BAR_H + 8,
                W98_BEVEL_SUNKEN, W98_BTNFACE);

    int blocks = (progress * 10) / 100;
    for (int i = 0; i < blocks; i++) {
        int bx = bar_x + i * (BOOT_BAR_W / 10);
        w98_fill(bx + 2, bar_y, BOOT_BAR_W / 10 - 4, BOOT_BAR_H,
                 W98_ACTIVE_TITLE);
    }

    char pct_buf[8];
    int_to_string((uint32_t)progress, pct_buf);
    int pct_len = (int)strlen(pct_buf);
    w98_text(pct_buf, bar_x + BOOT_BAR_W + 12, bar_y + 6, W98_BTNTEXT,
             W98_BTNFACE, 1, NULL);
    w98_text("%", bar_x + BOOT_BAR_W + 12 + pct_len * 6, bar_y + 6,
             W98_BTNTEXT, W98_BTNFACE, 1, NULL);
}

static void draw_boot_message(const char* msg) {
    int msg_w = BOOT_BAR_W + 60;
    int msg_x = ((int)screen_width - msg_w) / 2;
    int msg_y = BOOT_BAR_Y - 26;

    w98_text(msg, msg_x, msg_y, W98_BTNTEXT, W98_BTNFACE, 1, NULL);
    (void)msg_w;
}

static void draw_version_string(void) {
    w98_text_bold("SharkOS 98", BOOT_BAR_X, BOOT_BAR_Y - 60, W98_BTNTEXT,
                  W98_BTNFACE, 2, NULL);
    w98_text("Starting...", BOOT_BAR_X + w98_text_width("SharkOS 98", 2) + 12,
             BOOT_BAR_Y - 52, W98_GRAYTEXT, W98_BTNFACE, 1, NULL);
}

static void draw_spinner(int frame) {
    const char* spinner = "|/-\\";
    int idx = frame % 4;
    char spin_str[2] = { spinner[idx], '\0' };
    w98_text(spin_str, BOOT_BAR_X + BOOT_BAR_W + 44, BOOT_BAR_Y + 6,
             W98_GRAYTEXT, W98_BTNFACE, 1, NULL);
}

void boot_screen_show(void) {
    boot_progress = 0;

    draw_dither_bg();

    int logo_cx = (int)screen_width / 2;
    int logo_cy = (int)screen_height / 3;
    draw_shark_logo(logo_cx, logo_cy, 80, W98_BTNHILITE);

    draw_version_string();
    draw_progress_bar(0);
    draw_boot_message("Initializing...");
}

void boot_screen_update(const char* message, int progress) {
    if (progress > 100) progress = 100;
    if (progress < 0) progress = 0;

    boot_progress = progress;
    if (message) {
        strcpy(boot_message, message);
    }

    draw_progress_bar(progress);
    draw_boot_message(boot_message);
    draw_spinner((int)(boot_spinner_counter++));

    for (volatile int i = 0; i < 500000; i++);
}

void boot_screen_hide(void) {
    uint32_t stride = (uint32_t)(screen_pitch / 4);
    uint32_t sc_h = (uint32_t)screen_height;
    uint32_t sc_w = (uint32_t)screen_width;

    for (int fade = 15; fade >= 0; fade--) {
        for (uint32_t y = 0; y < sc_h; y++) {
            uint32_t* pixel = &lfbptr[y * stride];
            for (uint32_t x = 0; x < sc_w; x++) {
                uint8_t r = (pixel[x] >> 16) & 0xFF;
                uint8_t g = (pixel[x] >> 8) & 0xFF;
                uint8_t b = pixel[x] & 0xFF;
                r = (uint8_t)((r * (uint32_t)fade) / 15);
                g = (uint8_t)((g * (uint32_t)fade) / 15);
                b = (uint8_t)((b * (uint32_t)fade) / 15);
                pixel[x] = 0xFF000000u | ((uint32_t)r << 16) |
                           ((uint32_t)g << 8) | b;
            }
        }
        for (volatile int i = 0; i < 2000000; i++);
    }

    draw_rect_safe(0, 0, (int)screen_width, (int)screen_height,
                   W98_BTNDKSHADOW);
}
