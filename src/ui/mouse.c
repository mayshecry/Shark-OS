#include "kernel.h"

static uint32_t cursor_backup[16][16];
static int cursor_backup_valid = 0;
static int cursor_backup_x = -1;
static int cursor_backup_y = -1;

void mouse_draw_cursor(void) {
    if (!mouse_enabled) return;

    int cx = mouse_cursor_x;
    int cy = mouse_cursor_y;

    if (cx < 0 || cy < 0 || (uint32_t)cx >= screen_width || (uint32_t)cy >= screen_height) return;

    uint32_t stride = screen_pitch / 4;
    for (int dy = 0; dy < 16; dy++) {
        for (int dx = 0; dx < 16; dx++) {
            int px = cx + dx;
            int py = cy + dy;
            if (px < 0 || py < 0 || (uint32_t)px >= screen_width || (uint32_t)py >= screen_height) {
                cursor_backup[dy][dx] = 0xFF000000;
                continue;
            }
            cursor_backup[dy][dx] = lfbptr[py * stride + px];
        }
    }
    cursor_backup_x = cx;
    cursor_backup_y = cy;
    cursor_backup_valid = 1;

    uint32_t color = 0xFFFFFFFF;
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
    if (!mouse_enabled || !cursor_backup_valid) return;

    int cx = cursor_backup_x;
    int cy = cursor_backup_y;

    uint32_t stride = screen_pitch / 4;

    for (int dy = 0; dy < 16; dy++) {
        for (int dx = 0; dx < 16; dx++) {
            int px = cx + dx;
            int py = cy + dy;
            if (px < 0 || py < 0 || (uint32_t)px >= screen_width || (uint32_t)py >= screen_height) continue;

            lfbptr[py * stride + px] = cursor_backup[dy][dx];
        }
    }

    cursor_backup_valid = 0;
}

void mouse_update_cursor(void) {
    if (!mouse_enabled) return;
    mouse_restore_under_cursor();
    mouse_draw_cursor();
}