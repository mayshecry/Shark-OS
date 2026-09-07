/* rndr_light.c - lighting tables (rndr/lighting_tables) */
#include "rndr_light.h"

uint8_t rndr_apply_shade(uint8_t c, int shade) {
    int s = (c * shade) >> 8;
    return (uint8_t)(s > 255 ? 255 : s);
}

/* light falloff like DOOM's light tables: sector light minus distance */
int rndr_light_shade(int sector_light, int32_t dist_units) {
    int cells = dist_units >> 22;          /* /64 world units */
    int sh = sector_light - cells * 10;
    if (sh < 32) sh = 32;
    if (sh > 255) sh = 255;
    return sh;
}
