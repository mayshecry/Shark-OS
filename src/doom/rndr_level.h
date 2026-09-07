/* rndr_level.h - level data queries (level data structures from
 * cpp-doom's core/wad_types + game_data). */
#ifndef RNDR_LEVEL_H
#define RNDR_LEVEL_H

#include "rndr_types.h"

const rndr_subsector_t *rndr_point_in_subsector(const rndr_level_t *l,
                                                int32_t x, int32_t y);

#endif /* RNDR_LEVEL_H */
