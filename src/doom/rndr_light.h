
#ifndef RNDR_LIGHT_H
#define RNDR_LIGHT_H

#include <stdint.h>

uint8_t rndr_apply_shade(uint8_t c, int shade);
int rndr_light_shade(int sector_light, int32_t dist_units);

#endif
