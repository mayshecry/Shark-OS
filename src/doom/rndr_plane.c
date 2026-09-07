/* rndr_plane.c - visplanes (rndr/visplane) */
#include "rndr_plane.h"
#include "rndr_light.h"
#include "rndr_gamedata.h"

void rndr_planes_reset(rndr_planes_t *ps) {
    ps->num = 0;
    ps->floorplane = NULL;
    ps->ceilingplane = NULL;
}

rndr_visplane_t *rndr_check_plane(rndr_planes_t *ps, int pic, int height,
                                  int light, int start, int stop) {
    rndr_visplane_t *pl = NULL;
    for (int i = 0; i < ps->num; i++) {
        rndr_visplane_t *p = &ps->planes[i];
        if (p->pic != pic || p->height != height) continue;
        if (start > p->maxx) {
            int r = p->maxx + 1;
            while (--r >= start)
                if (p->top[r] != 0x7FFF) break;
            if (r < start) { pl = p; break; }
        }
        pl = p;
        break;
    }
    if (!pl) {
        if (ps->num >= RNDR_MAX_VISPLANES) return NULL;
        pl = &ps->planes[ps->num++];
        for (int i = 0; i < RNDR_W; i++) {
            pl->top[i] = 0x7FFF;
            pl->bottom[i] = 0x7FFF;
        }
        pl->minx = RNDR_W;
        pl->maxx = -1;
    }
    pl->pic = pic;
    pl->height = height;
    pl->light = light;
    if (start < pl->minx) pl->minx = start;
    if (stop > pl->maxx) pl->maxx = stop;
    return pl;
}

/* BSP traversal is front-to-back, so the first span recorded for a
 * column is the nearest one and wins. */
void rndr_set_span(rndr_visplane_t *pl, int x, int top, int bottom) {
    if (!pl || top > bottom) return;
    if (pl->top[x] != 0x7FFF) return;
    pl->top[x] = (int16_t)top;
    pl->bottom[x] = (int16_t)bottom;
}

/* Textured flat row like DOOM's R_MapPlane: one distance division per
 * scanline, then per-pixel texture stepping along the column rays. */
static void rndr_map_plane(const rndr_view_t *v, const rndr_clip_t *clip,
                           uint8_t (*screen)[RNDR_W],
                           int y, int x1, int x2, int pic,
                           int planeheight, int light) {
    int p = y - v->centery;
    if (p < 0) p = (int)(0u - (uint32_t)p);
    if (p < 1) p = 1;
    const uint8_t (*flat)[8][RNDR_TEX_H][RNDR_TEX_W] = &rndr_floor_tex;
    int height = planeheight;
    if (height < 0) {
        flat = &rndr_ceil_tex;
        height = (int)(0u - (uint32_t)height);
    }
    if (height < 1) height = 1;
    int32_t dist = fp_div(int_to_fp(height * (RNDR_W / 2)), int_to_fp(p));
    int shade = rndr_light_shade(light, dist);
    for (int x = x1; x <= x2; x++) {
        int32_t wx = v->x + fp_mul(dist, v->dircos[x]);
        int32_t wy = v->y + fp_mul(dist, v->dirsin[x]);
        if (clip->wallmask[y][x]) continue;
        int tx = (fp_to_int(wx) % RNDR_TEX_W + RNDR_TEX_W) % RNDR_TEX_W;
        int ty = (fp_to_int(wy) % RNDR_TEX_H + RNDR_TEX_H) % RNDR_TEX_H;
        screen[y][x] = rndr_apply_shade((*flat)[pic & 7][ty][tx], shade);
    }
}

void rndr_draw_planes(rndr_planes_t *ps, const rndr_view_t *v,
                      const rndr_clip_t *clip, uint8_t (*screen)[RNDR_W]) {
    for (int i = 0; i < ps->num; i++) {
        rndr_visplane_t *pl = &ps->planes[i];
        for (int x = pl->minx; x <= pl->maxx; x++) {
            int t = pl->top[x];
            if (t == 0x7FFF) continue;
            int b = pl->bottom[x];
            if (t < 0) t = 0;
            if (b > v->view_h - 1) b = v->view_h - 1;
            if (b < t || t >= v->view_h) continue;
            /* group runs of equal spans, then map every scanline */
            int x2 = x;
            while (x2 + 1 <= pl->maxx && pl->top[x2 + 1] == t &&
                   pl->bottom[x2 + 1] == b)
                x2++;
            int planeheight = pl->height >= v->z ? -(pl->height - v->z)
                                                 : (v->z - pl->height);
            for (int y = t; y <= b; y++)
                rndr_map_plane(v, clip, screen, y, x, x2, pl->pic,
                               planeheight, pl->light);
            x = x2;
        }
    }
}
