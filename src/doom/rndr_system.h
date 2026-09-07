/* rndr_system.h - renderer system (cpp-doom rndr/system): context
 * initialisation and the per-frame entry point. */
#ifndef RNDR_SYSTEM_H
#define RNDR_SYSTEM_H

#include "rndr_context.h"

void rndr_init_context(rndr_context_t *ctx, rndr_level_t *lvl,
                       uint8_t (*screen)[RNDR_W]);
void rndr_render_frame(rndr_context_t *ctx, int32_t x, int32_t y,
                       rndr_angle_t angle, int eye_z, int view_h);

#endif /* RNDR_SYSTEM_H */
