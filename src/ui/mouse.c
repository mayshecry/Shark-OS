#include "kernel.h"


void mouse_draw_cursor(void) {
    if (!mouse_enabled) return;

    int cx = mouse_cursor_x;
    int cy = mouse_cursor_y;

    if (cx < 0 || cy < 0 || (uint32_t)cx >= screen_width || (uint32_t)cy >= screen_height) return;

    uint32_t color = 0xFFFFFFFF;
    uint32_t stride = screen_pitch / 4;

    for (int dy = 0; dy < 16; dy++) {
        for (int dx = 0; dx < 16; dx++) {
            int px = cx + dx;
            int py = cy + dy;
            if (px < 0 || py < 0 || (uint32_t)px >= screen_width || (uint32_t)py >= screen_height) continue;

            uint8_t row = font8x8[0x5F][dy];
            int bit = 15 - dx;
            if (bit < 0 || bit > 7) continue;

            if ((row >> bit) & 1) {
                lfbptr[py * stride + px] = color;
            }
        }
    }
}

void mouse_restore_under_cursor(void) {
    if (!mouse_enabled) return;

    int cx = mouse_cursor_x;
    int cy = mouse_cursor_y;

    if (cx < 0 || cy < 0 || (uint32_t)cx >= screen_width || (uint32_t)cy >= screen_height) return;

    uint32_t stride = screen_pitch / 4;

    for (int dy = 0; dy < 16; dy++) {
        for (int dx = 0; dx < 16; dx++) {
            int px = cx + dx;
            int py = cy + dy;
            if (px < 0 || py < 0 || (uint32_t)px >= screen_width || (uint32_t)py >= screen_height) continue;

            lfbptr[py * stride + px] = 0xFF000000;
        }
    }
}

void mouse_update_cursor(void) {
    if (!mouse_enabled) return;
    mouse_restore_under_cursor();
    mouse_draw_cursor();
}