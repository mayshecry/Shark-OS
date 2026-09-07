/* rndr_bsp.h - BSP traversal renderer (cpp-doom rndr/bsp_renderer,
 * original r_bsp/r_segs): walks the tree front-to-back, projects segs,
 * draws wall columns and records visplane spans and drawsegs. */
#ifndef RNDR_BSP_H
#define RNDR_BSP_H

#include "rndr_types.h"
#include "rndr_view.h"
#include "rndr_clip.h"
#include "rndr_plane.h"

#define RNDR_MAX_DRAWSEGS 64

typedef struct {
    int x1, x2;
    int16_t *sprtopclip, *sprbottomclip;
} rndr_drawseg_t;

typedef struct {
    rndr_drawseg_t segs[RNDR_MAX_DRAWSEGS];
    int num;
    int16_t clip_pool[RNDR_MAX_DRAWSEGS * 2][RNDR_W];
} rndr_drawsegs_t;

typedef struct rndr_context_s rndr_context_t;

void rndr_drawsegs_reset(rndr_drawsegs_t *ds);
void rndr_render_bsp(rndr_context_t *ctx);

#endif /* RNDR_BSP_H */
