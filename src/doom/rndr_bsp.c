#include "rndr_context.h"
#include "rndr_column.h"
#include "rndr_light.h"
#include "rndr_gamedata.h"

void rndr_drawsegs_reset(rndr_drawsegs_t *ds) {
    ds->num = 0;
}

static void store_wall_range(rndr_context_t *ctx, int x, int32_t z,
                             const rndr_seg_t *sg,
                             const rndr_sector_t *front,
                             const rndr_sector_t *back,
                             int markfloor, int markceiling,
                             int16_t *ds_top, int16_t *ds_bot,
                             int sx1, int sx2) {
    rndr_view_t *v = &ctx->view;
    rndr_clip_t *clip = &ctx->clip;
    int fceil = front->ceilh, ffloor = front->floorh;

    int topy = rndr_row(v, z, fceil);
    int boty = rndr_row(v, z, ffloor);

    int ctop = clip->ceiling[x] + 1;
    int cbot = topy - 1;
    int ftop = boty + 1;
    int fbot = clip->floor[x] - 1;
    if (ctop < 0) ctop = 0;
    if (cbot > v->view_h - 1) cbot = v->view_h - 1;
    if (ftop < 0) ftop = 0;
    if (fbot > v->view_h - 1) fbot = v->view_h - 1;
    if (markceiling) rndr_set_span(ctx->planes.ceilingplane, x, ctop, cbot);
    if (markfloor) rndr_set_span(ctx->planes.floorplane, x, ftop, fbot);

    int shade = rndr_light_shade(front->light, z);
    int u = 0;
    if (sg->length > 0 && sx2 > sx1) {
        u = (int)(((int64_t)sg->length * (x - sx1) / (sx2 - sx1)) >> 16);
    }
    int tex_x = u & (RNDR_TEX_W - 1);

    if (!back || sg->midtex >= 0) {
        int ya = topy, yb = boty;
        if (ya < clip->ceiling[x] + 1) ya = clip->ceiling[x] + 1;
        if (yb > clip->floor[x]) yb = clip->floor[x];
        rndr_draw_wall_column(clip, ctx->screen, x, ya, yb,
                              sg->midtex >= 0 ? sg->midtex : 0,
                              fceil, ffloor, shade, tex_x);
    } else {
        int bceil = back->ceilh, bfloor = back->floorh;
        if (fceil > bceil && sg->toptex >= 0) {
            int ya = rndr_row(v, z, fceil), yb = rndr_row(v, z, bceil);
            if (ya < clip->ceiling[x] + 1) ya = clip->ceiling[x] + 1;
            if (yb > clip->floor[x]) yb = clip->floor[x];
            rndr_draw_wall_column(clip, ctx->screen, x, ya, yb,
                                  sg->toptex, fceil, bceil, shade, tex_x);
        }
        if (bfloor > ffloor && sg->bottex >= 0) {
            int ya = rndr_row(v, z, bfloor), yb = rndr_row(v, z, ffloor);
            if (ya < clip->ceiling[x] + 1) ya = clip->ceiling[x] + 1;
            if (yb > clip->floor[x]) yb = clip->floor[x];
            rndr_draw_wall_column(clip, ctx->screen, x, ya, yb,
                                  sg->bottex, bfloor, ffloor, shade, tex_x);
        }
    }

    if (!back || sg->midtex >= 0) {
        if (ds_top) {
            if (topy < ds_top[x]) ds_top[x] = (int16_t)topy;
            if (boty > ds_bot[x]) ds_bot[x] = (int16_t)boty;
        }
        if (boty > clip->ceiling[x]) clip->ceiling[x] = boty;
        if (topy < clip->floor[x]) clip->floor[x] = topy;
    } else {
        int fcl = rndr_row(v, z, fceil);
        int ffl = rndr_row(v, z, ffloor);
        if (fceil > back->ceilh) {
            if (ds_top) {
                if (topy < ds_top[x]) ds_top[x] = (int16_t)topy;
                int ub = rndr_row(v, z, back->ceilh);
                if (ub > ds_bot[x]) ds_bot[x] = (int16_t)ub;
            }
        } else if (back->floorh > ffloor) {
            if (ds_top) {
                int lt = rndr_row(v, z, back->floorh);
                if (lt < ds_top[x]) ds_top[x] = (int16_t)lt;
                if (boty > ds_bot[x]) ds_bot[x] = (int16_t)boty;
            }
        }
        if (fcl > clip->ceiling[x]) clip->ceiling[x] = fcl;
        if (ffl < clip->floor[x]) clip->floor[x] = ffl;
    }
}

static void add_line(rndr_context_t *ctx, const rndr_seg_t *sg) {
    rndr_view_t *v = &ctx->view;
    rndr_level_t *level = ctx->level;
    int32_t lat1, dep1, lat2, dep2;
    rndr_project(v, sg->x1, sg->y1, &lat1, &dep1);
    rndr_project(v, sg->x2, sg->y2, &lat2, &dep2);

    if (dep1 <= RNDR_NEAR_DIST && dep2 <= RNDR_NEAR_DIST) return;

    if (dep1 <= RNDR_NEAR_DIST) {
        int64_t t = ((int64_t)(RNDR_NEAR_DIST - dep1) << 16) / (dep2 - dep1);
        lat1 = lat1 + (int32_t)((t * (lat2 - lat1)) >> 16);
        dep1 = RNDR_NEAR_DIST;
    } else if (dep2 <= RNDR_NEAR_DIST) {
        int64_t t = ((int64_t)(RNDR_NEAR_DIST - dep2) << 16) / (dep1 - dep2);
        lat2 = lat2 + (int32_t)((t * (lat1 - lat2)) >> 16);
        dep2 = RNDR_NEAR_DIST;
    }

    int sx1 = rndr_screen_x(lat1, dep1);
    int sx2 = rndr_screen_x(lat2, dep2);
    if (sx1 > sx2) {
        int t; int32_t q;
        t = sx1; sx1 = sx2; sx2 = t;
        q = dep1; dep1 = dep2; dep2 = q;
        q = lat1; lat1 = lat2; lat2 = q;
    }
    if (sx1 < 0) sx1 = 0;
    if (sx2 > RNDR_W) sx2 = RNDR_W;
    if (sx2 - sx1 < 1) return;

    int start = -1, stop = -1;
    for (int x = sx1; x < sx2; x++) {
        if (ctx->clip.ceiling[x] + 1 < ctx->clip.floor[x]) {
            if (start < 0) start = x;
            stop = x + 1;
        }
    }
    if (start < 0) return;

    const rndr_sector_t *front, *back;
    if (sg->back >= 0) {
        int64_t vside = rndr_cross64(sg->x2 - sg->x1, sg->y2 - sg->y1,
                                     v->x - sg->x1, v->y - sg->y1);
        if (vside <= 0) {
            front = &level->sectors[sg->front];
            back = &level->sectors[sg->back];
        } else {
            front = &level->sectors[sg->back];
            back = &level->sectors[sg->front];
        }
    } else {
        front = &level->sectors[sg->front];
        back = NULL;
    }

    int markfloor, markceiling;
    if (!back) {
        markfloor = (front->floorh < v->z);
        markceiling = (front->ceilh > v->z);
    } else {
        markfloor = (front->floorh != back->floorh ||
                     front->floorpic != back->floorpic) &&
                    (front->floorh < v->z);
        markceiling = (front->ceilh != back->ceilh ||
                       front->ceilpic != back->ceilpic) &&
                      (front->ceilh > v->z);
    }

    if (markfloor)
        ctx->planes.floorplane = rndr_check_plane(&ctx->planes,
                                                  front->floorpic, front->floorh,
                                                  front->light, start, stop - 1);
    else
        ctx->planes.floorplane = NULL;
    if (markceiling)
        ctx->planes.ceilingplane = rndr_check_plane(&ctx->planes,
                                                    front->ceilpic, front->ceilh,
                                                    front->light, start, stop - 1);
    else
        ctx->planes.ceilingplane = NULL;

    int dsi = -1;
    rndr_drawsegs_t *ds = &ctx->segs;
    if (ds->num < RNDR_MAX_DRAWSEGS) {
        dsi = ds->num;
        ds->segs[dsi].x1 = start;
        ds->segs[dsi].x2 = stop;
        ds->segs[dsi].sprtopclip = ds->clip_pool[dsi * 2];
        ds->segs[dsi].sprbottomclip = ds->clip_pool[dsi * 2 + 1];
        for (int x = start; x < stop; x++) {
            ds->clip_pool[dsi * 2][x] = 0x7FFF;
            ds->clip_pool[dsi * 2 + 1][x] = -0x7FFF;
        }
        ds->num++;
    }

    int64_t inv1 = ((int64_t)1 << 40) / dep1;
    int64_t inv2 = ((int64_t)1 << 40) / dep2;

    for (int x = start; x < stop; x++) {
        if (ctx->clip.ceiling[x] + 1 >= ctx->clip.floor[x]) continue;
        int64_t invx = inv1 + (inv2 - inv1) * (x - sx1) / (sx2 > sx1 ? sx2 - sx1 : 1);
        int32_t z = (int32_t)(((int64_t)1 << 40) / invx);
        store_wall_range(ctx, x, z, sg, front, back, markfloor, markceiling,
                         dsi >= 0 ? ds->segs[dsi].sprtopclip : NULL,
                         dsi >= 0 ? ds->segs[dsi].sprbottomclip : NULL,
                         sx1, sx2);
    }
}

static void add_lines(rndr_context_t *ctx, const rndr_subsector_t *sub) {
    for (int i = 0; i < sub->numsegs; i++)
        add_line(ctx, &ctx->level->segs[sub->firstseg + i]);
}

static void render_node(rndr_context_t *ctx, int ni) {
    if (ni < 0) {
        add_lines(ctx, &ctx->level->subs[~ni]);
        return;
    }
    rndr_node_t *n = &ctx->level->nodes[ni];
    int64_t c = rndr_cross64(n->dx, n->dy,
                             ctx->view.x - n->x, ctx->view.y - n->y);
    int side = (c > 0) ? 1 : 0;
    render_node(ctx, n->children[side]);
    render_node(ctx, n->children[side ^ 1]);
}

void rndr_render_bsp(rndr_context_t *ctx) {
    if (!ctx->level || !ctx->level->built) return;
    render_node(ctx, ctx->level->root);
}
