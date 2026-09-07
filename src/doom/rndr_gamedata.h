/* rndr_gamedata.h - renderer-owned texture data (cpp-doom core/game_data).
 * The game layer generates the procedural textures into these arrays. */
#ifndef RNDR_GAMEDATA_H
#define RNDR_GAMEDATA_H

#include <stdint.h>
#include "rndr_types.h"

extern uint8_t rndr_wall_tex[8][RNDR_TEX_H][RNDR_TEX_W];
extern uint8_t rndr_floor_tex[8][RNDR_TEX_H][RNDR_TEX_W];
extern uint8_t rndr_ceil_tex[8][RNDR_TEX_H][RNDR_TEX_W];

#endif /* RNDR_GAMEDATA_H */
