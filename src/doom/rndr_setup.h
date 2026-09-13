
#ifndef RNDR_SETUP_H
#define RNDR_SETUP_H

#include "rndr_types.h"

typedef struct {
    int map_w, map_h;
    int32_t cell_size;
    const uint8_t *grid;
    const int8_t *cell_sector;
    const int16_t *sector_floorh, *sector_ceilh;
    const int16_t *sector_floorpic, *sector_ceilpic;
    const int16_t *sector_light;
    int num_sectors;
} rndr_map_source_t;

void rndr_build_level(rndr_level_t *l, const rndr_map_source_t *src);

#endif /* RNDR_SETUP_H */
