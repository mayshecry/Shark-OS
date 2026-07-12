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
    int max_dy = 16;
    int max_dx = 16;
    if (cy + max_dy > (int)screen_height) max_dy = screen_height - cy;
    if (cx + max_dx > (int)screen_width) max_dx = screen_width - cx;

    for (int dy = 0; dy < max_dy; dy++) {
        int py = cy + dy;
        uint32_t* row_ptr = &lfbptr[py * stride + cx];
        for (int dx = 0; dx < max_dx; dx++) {
            cursor_backup[dy][dx] = row_ptr[dx];
        }
    }
    cursor_backup_x = cx;
    cursor_backup_y = cy;
    cursor_backup_valid = 1;

    uint32_t color = 0xFFFFFFFF;
    uint32_t border_color = 0xFF000000;
    
    static const uint16_t cursor_shape[16] = {
        0b1000000000000000,
        0b1100000000000000,
        0b1110000000000000,
        0b1111000000000000,
        0b1111100000000000,
        0b1111110000000000,
        0b1111111000000000,
        0b1111111100000000,
        0b1111111110000000,
        0b1111111111000000,
        0b1111111000000000,
        0b1110111100000000,
        0b1100111100000000,
        0b1000011110000000,
        0b0000011110000000,
        0b0000001100000000
    };
    static const uint16_t cursor_mask[16] = {
        0b1100000000000000,
        0b1110000000000000,
        0b1111000000000000,
        0b1111100000000000,
        0b1111110000000000,
        0b1111111000000000,
        0b1111111100000000,
        0b1111111110000000,
        0b1111111111000000,
        0b1111111111100000,
        0b1111111111110000,
        0b1111111100000000,
        0b1111111110000000,
        0b1110011111000000,
        0b1000011111000000,
        0b0000011110000000
    };
    for (int dy = 0; dy < max_dy; dy++) {
        int py = cy + dy;
        uint32_t* row_ptr = &lfbptr[py * stride + cx];
        uint16_t shape_row = cursor_shape[dy];
        uint16_t mask_row = cursor_mask[dy];
        for (int dx = 0; dx < max_dx; dx++) {
            int bit = 15 - dx;
            int is_shape = (shape_row >> bit) & 1;
            int is_mask = (mask_row >> bit) & 1;
            if (is_shape) {
                row_ptr[dx] = color;
            } else if (is_mask) {
                row_ptr[dx] = border_color;
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

void mouse_draw_cursor_only(void) {
    if (!mouse_enabled) return;
    int cx = mouse_cursor_x;
    int cy = mouse_cursor_y;
    if (cx < 0 || cy < 0 || (uint32_t)cx >= screen_width || (uint32_t)cy >= screen_height) return;

    uint32_t stride = screen_pitch / 4;
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
