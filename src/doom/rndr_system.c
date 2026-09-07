/* rndr_system.c - renderer system (rndr/system) */
#include "rndr_system.h"

void rndr_init_context(rndr_context_t *ctx, rndr_level_t *lvl,
                       uint8_t (*screen)[RNDR_W]) {
    ctx->level = lvl;
    ctx->screen = screen;
    rndr_drawsegs_reset(&ctx->segs);
    rndr_planes_reset(&ctx->planes);
}

void rndr_render_frame(rndr_context_t *ctx, int32_t x, int32_t y,
                       rndr_angle_t angle, int eye_z, int view_h) {
    rndr_view_init(&ctx->view, x, y, eye_z, angle, view_h);
    rndr_clip_reset(&ctx->clip, view_h);
    rndr_planes_reset(&ctx->planes);
    rndr_drawsegs_reset(&ctx->segs);

    /* clear the view area (HUD keeps its pixels) */
    volatile uint8_t (*vs)[RNDR_W] = ctx->screen;
    for (int yy = 0; yy < view_h; yy++)
        for (int xx = 0; xx < RNDR_W; xx++)
            vs[yy][xx] = 0;

    rndr_render_bsp(ctx);
    rndr_draw_planes(&ctx->planes, &ctx->view, &ctx->clip, ctx->screen);
}
