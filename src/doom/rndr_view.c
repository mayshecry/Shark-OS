/* rndr_view.c - viewpoint and projection (rndr/view) */
#include "rndr_view.h"
#include "rndr_trig.h"

void rndr_view_init(rndr_view_t *v, int32_t x, int32_t y, int z,
                    rndr_angle_t angle, int view_h) {
    v->x = x;
    v->y = y;
    v->z = z;
    v->angle = angle;
    v->viewsin = rndr_sin(angle);
    v->viewcos = rndr_cos(angle);
    v->view_h = view_h;
    v->centery = view_h / 2;

    /* column ray directions for flat casting (90 deg FOV) */
    rndr_angle_t a = angle - RNDR_ANG45;
    rndr_angle_t step = RNDR_ANG90 / RNDR_W;
    for (int i = 0; i < RNDR_W; i++) {
        v->dircos[i] = rndr_cos(a);
        v->dirsin[i] = rndr_sin(a);
        a += step;
    }
}

void rndr_project(const rndr_view_t *v, int32_t wx, int32_t wy,
                  int32_t *lat, int32_t *dep) {
    int32_t dx = wx - v->x, dy = wy - v->y;
    *lat = fp_mul(v->viewcos, dx) - fp_mul(v->viewsin, dy);
    *dep = fp_mul(v->viewsin, dx) + fp_mul(v->viewcos, dy);
}

int rndr_screen_x(int32_t lat, int32_t dep) {
    return RNDR_W / 2 + fp_to_int(fp_div(fp_mul(lat, int_to_fp(RNDR_W / 2)), dep));
}

/* screen row of world height h at perpendicular distance z (fp units) */
int rndr_row(const rndr_view_t *v, int32_t z, int h) {
    if (z < RNDR_NEAR_DIST) z = RNDR_NEAR_DIST;
    return v->centery - fp_to_int(fp_div(int_to_fp((h - v->z) * (RNDR_W / 2)), z));
}
