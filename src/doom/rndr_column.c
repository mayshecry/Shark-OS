/* rndr_column.c - wall column rendering (rndr/column) */
#include "rndr_column.h"
#include "rndr_light.h"
#include "rndr_gamedata.h"

void rndr_draw_wall_column(rndr_clip_t *clip, uint8_t (*screen)[RNDR_W],
                           int x, int ya, int yb, int texid,
                           int h_top, int h_bot, int shade, int tex_x) {
    int span = h_top - h_bot;
    if (span <= 0 || yb <= ya) return;
    for (int y = ya; y < yb; y++) {
        int h = h_top - (y - ya) * span / (yb - ya);
        int ty = h & (RNDR_TEX_H - 1);
        screen[y][x] = rndr_apply_shade(rndr_wall_tex[texid & 7][ty][tex_x], shade);
        clip->wallmask[y][x] = 1;
    }
}
