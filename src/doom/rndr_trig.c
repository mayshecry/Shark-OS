/* rndr_trig.c - sine table for binary angular measure (rndr/trigonometry) */
#include "rndr_trig.h"

#define SIN_TAB_SIZE 8192
static int32_t sin_table[SIN_TAB_SIZE];

void rndr_trig_init(void) {
    int quarter = SIN_TAB_SIZE / 4;

    for (int i = 0; i <= quarter; i++) {
        int32_t x = (i * 102944) / quarter;

        int32_t x2 = fp_mul(x, x);
        int32_t x3 = fp_mul(x2, x);
        int32_t x5 = fp_mul(x3, x2);
        int32_t x7 = fp_mul(x5, x2);

        int32_t val = x
                    - fp_div(x3, int_to_fp(6))
                    + fp_div(x5, int_to_fp(120))
                    - fp_div(x7, int_to_fp(5040));

        if (val > FP_ONE) val = FP_ONE;
        if (val < 0) val = 0;
        sin_table[i] = val;
    }

    for (int i = 1; i < quarter; i++)
        sin_table[quarter + i] = sin_table[quarter - i];

    for (int i = 1; i < SIN_TAB_SIZE / 2; i++)
        sin_table[SIN_TAB_SIZE / 2 + i] = -sin_table[i];

    sin_table[0] = 0;
    sin_table[quarter] = FP_ONE;
    sin_table[SIN_TAB_SIZE / 2] = 0;
    sin_table[SIN_TAB_SIZE / 2 + quarter] = -FP_ONE;
}

int32_t rndr_sin(rndr_angle_t angle) {
    return sin_table[(angle >> (32 - 13)) & (SIN_TAB_SIZE - 1)];
}

int32_t rndr_cos(rndr_angle_t angle) {
    return sin_table[((angle >> (32 - 13)) + SIN_TAB_SIZE / 4) & (SIN_TAB_SIZE - 1)];
}
