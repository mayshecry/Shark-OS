/* rndr_column.h - textured wall column drawing (cpp-doom rndr/column).
 * Draws one screen column of a wall face with world-aligned texture v. */
#ifndef RNDR_COLUMN_H
#define RNDR_COLUMN_H

#include "rndr_types.h"
#include "rndr_clip.h"

void rndr_draw_wall_column(rndr_clip_t *clip, uint8_t (*screen)[RNDR_W],
                           int x, int ya, int yb, int texid,
                           int h_top, int h_bot, int shade, int tex_x);

#endif /* RNDR_COLUMN_H */
