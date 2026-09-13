
#ifndef RNDR_VIEW_H
#define RNDR_VIEW_H

#include "rndr_types.h"

#define RNDR_NEAR_DIST (8 * FP_ONE)

typedef struct {
    int32_t x, y;
    int z;
    rndr_angle_t angle;
    int32_t viewsin, viewcos;
    int32_t dircos[RNDR_W], dirsin[RNDR_W];
    int centery, view_h;
} rndr_view_t;

void rndr_view_init(rndr_view_t *v, int32_t x, int32_t y, int z,
                    rndr_angle_t angle, int view_h);

void rndr_project(const rndr_view_t *v, int32_t wx, int32_t wy,
                  int32_t *lat, int32_t *dep);
int rndr_screen_x(int32_t lat, int32_t dep);
int rndr_row(const rndr_view_t *v, int32_t z, int h);

#endif
