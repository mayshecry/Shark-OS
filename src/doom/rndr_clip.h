/* rndr_clip.h - per-column occlusion (cpp-doom rndr/clip_range_array):
 * ceilingclip/floorclip from the original engine plus a wall mask so
 * planes can never overdraw wall pixels. */
#ifndef RNDR_CLIP_H
#define RNDR_CLIP_H

#include "rndr_types.h"

typedef struct {
    int ceiling[RNDR_W];        /* lowest row occluded from above */
    int floor[RNDR_W];          /* highest row occluded from below */
    uint8_t wallmask[RNDR_H][RNDR_W];
} rndr_clip_t;

void rndr_clip_reset(rndr_clip_t *c, int view_h);

#endif /* RNDR_CLIP_H */
