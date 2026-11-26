
#ifndef RNDR_CLIP_H
#define RNDR_CLIP_H

#include "rndr_types.h"

typedef struct {
    int ceiling[RNDR_W];
    int floor[RNDR_W];
    uint8_t wallmask[RNDR_H][RNDR_W];
} rndr_clip_t;

void rndr_clip_reset(rndr_clip_t *c, int view_h);

#endif
