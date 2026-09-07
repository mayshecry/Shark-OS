/* rndr_trig.h - trigonometry for binary angles (cpp-doom rndr/trigonometry).
 * The original engine's approach: a precomputed sine table indexed by
 * bams angle; cpp-doom uses libm floats, but this kernel has no FPU
 * enabled, so the classic table is the faithful choice here. */
#ifndef RNDR_TRIG_H
#define RNDR_TRIG_H

#include "rndr_types.h"

void rndr_trig_init(void);
int32_t rndr_sin(rndr_angle_t a);
int32_t rndr_cos(rndr_angle_t a);

#endif /* RNDR_TRIG_H */
