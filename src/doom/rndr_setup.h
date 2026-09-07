/* rndr_setup.h - renderer setup (cpp-doom rndr/system + core/game_data):
 * the game layer describes its map as a source (grid of cells with
 * sector properties) and the renderer builds real level data from it -
 * segs, subsectors and a BSP node tree - instead of walking the grid
 * at draw time. */
#ifndef RNDR_SETUP_H
#define RNDR_SETUP_H

#include "rndr_types.h"

typedef struct {
    int map_w, map_h;
    int32_t cell_size;                  /* world units per cell (fp) */
    const uint8_t *grid;                /* map_w*map_h cell codes */
    const int8_t *cell_sector;          /* map_w*map_h sector ids, -1 = none */
    const int16_t *sector_floorh, *sector_ceilh;
    const int16_t *sector_floorpic, *sector_ceilpic;
    const int16_t *sector_light;
    int num_sectors;
} rndr_map_source_t;

void rndr_build_level(rndr_level_t *l, const rndr_map_source_t *src);

#endif /* RNDR_SETUP_H */
