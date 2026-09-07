/* rndr_plane.h - visplanes (cpp-doom rndr/visplane, original r_plane):
 * horizontal floor/ceiling planes merged by pic+height, spans recorded
 * per column during BSP traversal, mapped at the end of the frame. */
#ifndef RNDR_PLANE_H
#define RNDR_PLANE_H

#include "rndr_types.h"
#include "rndr_view.h"
#include "rndr_clip.h"

#define RNDR_MAX_VISPLANES 96

typedef struct {
    int pic;          /* flat index */
    int height;       /* world height of the plane */
    int light;
    int minx, maxx;
    int16_t top[RNDR_W], bottom[RNDR_W];
} rndr_visplane_t;

typedef struct {
    rndr_visplane_t planes[RNDR_MAX_VISPLANES];
    int num;
    rndr_visplane_t *floorplane, *ceilingplane;
} rndr_planes_t;

void rndr_planes_reset(rndr_planes_t *ps);
rndr_visplane_t *rndr_check_plane(rndr_planes_t *ps, int pic, int height,
                                  int light, int start, int stop);
void rndr_set_span(rndr_visplane_t *pl, int x, int top, int bottom);
void rndr_draw_planes(rndr_planes_t *ps, const rndr_view_t *v,
                      const rndr_clip_t *clip, uint8_t (*screen)[RNDR_W]);

#endif /* RNDR_PLANE_H */
