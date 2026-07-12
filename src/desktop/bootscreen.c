
#include "kernel.h"
#include "desktop.h"

#define BOOT_BAR_W 400
#define BOOT_BAR_H 20
#define BOOT_BAR_X (((uint32_t)screen_width - BOOT_BAR_W) / 2)
#define BOOT_BAR_Y ((uint32_t)screen_height * 3 / 4)

static int boot_phase = 0;
static int boot_progress = 0;
static char boot_message[64] = "Initializing...";
static uint32_t boot_start_tick = 0;
static volatile uint32_t boot_spinner_counter = 0;

static void draw_pixel_safe(uint32_t x, uint32_t y, uint32_t color) {
    if (x >= screen_width || y >= screen_height) return;
    uint32_t stride = (uint32_t)(screen_pitch / 4);
    lfbptr[y * stride + x] = color;
}

static void draw_rect_safe(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
    uint32_t stride = (uint32_t)(screen_pitch / 4);
    for (uint32_t dy = 0; dy < h && (y + dy) < screen_height; dy++) {
        for (uint32_t dx = 0; dx < w && (x + dx) < screen_width; dx++) {
            lfbptr[(y + dy) * stride + (x + dx)] = color;
        }
    }
}

static void draw_gradient_bg(void) {
    uint32_t h = (uint32_t)screen_height;
    uint32_t w = (uint32_t)screen_width;
    uint32_t stride = (uint32_t)(screen_pitch / 4);
    uint32_t y;
    for (y = 0; y < h; y++) {
        uint8_t r = 10 + (5 * y / h);
        uint8_t g = 10 + (5 * y / h);
        uint8_t b = 42 - (20 * y / h);
        uint32_t color = 0xFF000000 | (r << 16) | (g << 8) | b;
        uint32_t x;
        for (x = 0; x < w; x++) {
            lfbptr[y * stride + x] = color;
        }
    }
}

static void draw_shark_logo(int cx, int cy, int size, uint32_t color) {
    
    int half = size / 2;
    uint32_t stride = (uint32_t)(screen_pitch / 4);
    
    
    for (int dy = -half; dy <= half; dy++) {
        int row_width = half - (dy * dy) / (half + 1);
        if (row_width < 0) row_width = 0;
        for (int dx = -row_width; dx <= row_width; dx++) {
            int px = cx + dx;
            int py = cy + dy;
            if (px >= 0 && px < (int)screen_width && py >= 0 && py < (int)screen_height) {
                lfbptr[py * stride + px] = color;
            }
        }
    }
    
    
    int eye_y = cy - half / 3;
    for (int dy = -2; dy <= 2; dy++) {
        for (int dx = -2; dx <= 2; dx++) {
            int px = cx + half / 3 + dx;
            int py = eye_y + dy;
            if (px >= 0 && px < (int)screen_width && py >= 0 && py < (int)screen_height) {
                lfbptr[py * stride + px] = 0xFFFFFFFF;
            }
        }
    }
}

static void draw_progress_bar(int progress) {
    int bar_x = BOOT_BAR_X;
    int bar_y = BOOT_BAR_Y;
    int bar_w = BOOT_BAR_W;
    int bar_h = BOOT_BAR_H;
    
    
    draw_rect_safe(bar_x - 2, bar_y - 2, bar_w + 4, bar_h + 4, 0xFF00FFFF);
    draw_rect_safe(bar_x, bar_y, bar_w, bar_h, 0xFF000000);
    
    
    int fill_w = (bar_w * progress) / 100;
    if (fill_w > 0) {
        uint32_t fill_color = 0xFF00FF00;
        if (progress < 30) fill_color = 0xFF00AAFF;
        else if (progress < 60) fill_color = 0xFF00FFAA;
        else if (progress < 90) fill_color = 0xFFAAFF00;
        else fill_color = 0xFF00FF00;
        draw_rect_safe(bar_x + 2, bar_y + 2, fill_w - 4, bar_h - 4, fill_color);
    }
    
    
    char pct_buf[8];
    int_to_string(progress, pct_buf);
    int pct_len = strlen(pct_buf);
    uint32_t text_x = bar_x + bar_w + 10;
    uint32_t text_y = bar_y + (bar_h - 8) / 2;
    draw_string_px(pct_buf, text_x, text_y, 0xFF00FFFF, 0xFF000000);
    draw_string_px("%", text_x + pct_len * 8, text_y, 0xFF00FFFF, 0xFF000000);
}

static void draw_boot_message(const char* msg) {
    int msg_x = BOOT_BAR_X;
    int msg_y = BOOT_BAR_Y - 24;
    int msg_w = BOOT_BAR_W;
    
    
    draw_rect_safe(msg_x, msg_y, msg_w, 16, 0xFF000000);
    
    
    int msg_len = strlen(msg);
    int text_x = msg_x + (msg_w - msg_len * 8) / 2;
    if (text_x < 0) text_x = 0;
    draw_string_px(msg, text_x, msg_y, 0xFFAAAAAA, 0xFF000000);
}

static void draw_version_string(void) {
    const char* ver = "SharkOS v2.2 Desktop";
    int ver_len = strlen(ver);
    int ver_x = ((int)screen_width - ver_len * 8) / 2;
    int ver_y = BOOT_BAR_Y - 50;
    draw_string_px(ver, ver_x, ver_y, 0xFF00FFFF, 0xFF000000);
}

static void draw_spinner(int frame) {
    const char* spinner = "|/-\\";
    int idx = frame % 4;
    char spin_str[2] = {spinner[idx], '\0'};
    int spin_x = BOOT_BAR_X + BOOT_BAR_W + 40;
    int spin_y = BOOT_BAR_Y + (BOOT_BAR_H - 8) / 2;
    draw_rect_safe(spin_x, spin_y, 8, 8, 0xFF000000);
    draw_string_px(spin_str, spin_x, spin_y, 0xFF00FF00, 0xFF000000);
}

void boot_screen_show(void) {
    boot_phase = 0;
    boot_progress = 0;
    boot_start_tick = uptime_ticks;
    
    
    draw_gradient_bg();
    
    
    int logo_cx = (int)screen_width / 2;
    int logo_cy = (int)screen_height / 3;
    draw_shark_logo(logo_cx, logo_cy, 80, 0xFF00FFFF);
    
    
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
    draw_spinner(boot_spinner_counter++);
    
    
    for (volatile int i = 0; i < 500000; i++);
}

void boot_screen_hide(void) {
    
    uint32_t stride = (uint32_t)(screen_pitch / 4);
    uint32_t sc_h = (uint32_t)screen_height;
    uint32_t sc_w = (uint32_t)screen_width;
    int fade;
    for (fade = 15; fade >= 0; fade--) {
        uint32_t y;
        for (y = 0; y < sc_h; y++) {
            uint32_t x;
            for (x = 0; x < sc_w; x++) {
                uint32_t* pixel = &lfbptr[y * stride + x];
                uint8_t r = (*pixel >> 16) & 0xFF;
                uint8_t g = (*pixel >> 8) & 0xFF;
                uint8_t b = *pixel & 0xFF;
                r = (r * fade) / 15;
                g = (g * fade) / 15;
                b = (b * fade) / 15;
                *pixel = 0xFF000000 | (r << 16) | (g << 8) | b;
            }
        }
        
        for (volatile int i = 0; i < 2000000; i++);
    }
    
    
    draw_rect_safe(0, 0, (uint32_t)screen_width, (uint32_t)screen_height, 0xFF000000);
}
