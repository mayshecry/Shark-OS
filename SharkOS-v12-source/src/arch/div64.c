#include "kernel.h"

uint64_t __udivdi3(uint64_t numerator, uint64_t denominator) {
    if (!denominator) return 0;
    uint64_t quotient = 0;
    uint64_t remainder = 0;
    for (int bit = 63; bit >= 0; bit--) {
        uint32_t overflow = (uint32_t)(remainder >> 63);
        remainder = (remainder << 1) | ((numerator >> bit) & 1);
        if (overflow || remainder >= denominator) {
            remainder -= denominator;
            quotient |= (uint64_t)1 << bit;
        }
    }
    return quotient;
}

uint64_t __udivmoddi4(uint64_t value, uint64_t divisor, uint64_t *rest) {
    uint64_t quotient = __udivdi3(value, divisor);
    if (rest) *rest = value - quotient * divisor;
    return quotient;
}
