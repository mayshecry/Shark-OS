/* rndr_context.h - render context (cpp-doom rndr/context): everything a
 * frame needs, owned by the game layer and handed to the renderer. */
#ifndef RNDR_CONTEXT_H
#define RNDR_CONTEXT_H

#include "rndr_types.h"
#include "rndr_view.h"
#include "rndr_clip.h"
#include "rndr_plane.h"
#include "rndr_bsp.h"

typedef struct rndr_context_s {
    rndr_view_t view;
    rndr_clip_t clip;
    rndr_planes_t planes;
    rndr_drawsegs_t segs;
    rndr_level_t *level;
    uint8_t (*screen)[RNDR_W];
} rndr_context_t;

#endif /* RNDR_CONTEXT_H */
