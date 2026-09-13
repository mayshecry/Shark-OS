
#ifndef RNDR_THINGS_H
#define RNDR_THINGS_H

#include "rndr_context.h"

typedef struct {
    int x0, width;
    int ytop, ybot;
    int32_t depth;
} rndr_sprite_proj_t;

int rndr_project_sprite(const rndr_context_t *ctx, int32_t wx, int32_t wy,
                        int h_floor, int h_top, rndr_sprite_proj_t *out);
void rndr_clip_sprite_column(const rndr_context_t *ctx, int x,
                             const rndr_sprite_proj_t *sp,
                             int *top, int *bot);

#endif
