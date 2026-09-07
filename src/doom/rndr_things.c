/* rndr_things.c - sprite projection and drawseg clipping (r_things) */
#include "rndr_things.h"

int rndr_project_sprite(const rndr_context_t *ctx, int32_t wx, int32_t wy,
                        int h_floor, int h_top, rndr_sprite_proj_t *out) {
    const rndr_view_t *v = &ctx->view;
    int32_t lat, dep;
    rndr_project(v, wx, wy, &lat, &dep);
    if (dep <= RNDR_NEAR_DIST) return 0;

    int sx = rndr_screen_x(lat, dep);
    int y_bot = rndr_row(v, dep, h_floor);
    int y_top = rndr_row(v, dep, h_top);
    int h = y_bot - y_top;
    if (h < 2) return 0;

    out->x0 = sx - h / 2;
    out->width = h;
    out->ytop = y_top;
    out->ybot = y_bot;
    out->depth = dep;
    return 1;
}

void rndr_clip_sprite_column(const rndr_context_t *ctx, int x,
                             const rndr_sprite_proj_t *sp,
                             int *top, int *bot) {
    *top = sp->ytop;
    *bot = sp->ybot;
    const rndr_drawsegs_t *ds = &ctx->segs;
    for (int d = 0; d < ds->num; d++) {
        if (x < ds->segs[d].x1 || x >= ds->segs[d].x2) continue;
        int wtop = ds->segs[d].sprtopclip[x];
        int wbot = ds->segs[d].sprbottomclip[x];
        if (wtop == 0x7FFF) continue;
        if (wtop <= *top && wbot >= *bot) { *top = 1; *bot = 0; return; }
        if (wtop <= *top && wbot > *top && wbot < *bot) *top = wbot + 1;
        if (wbot >= *bot && wtop < *bot && wtop > *top) *bot = wtop - 1;
    }
}
