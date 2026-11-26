#include "kernel.h"
extern int printf(const char *, ...);
#define W 512
#define H 224
static uint32_t guarded[(W + 3) * H + 2];
uint32_t *lfbptr = guarded + 1;
uint64_t screen_width = W, screen_height = H, screen_pitch;
uint32_t font_scale;
void fill32(uint32_t *dst, uint32_t color, size_t n) {
    asm volatile("rep stosl" : "+D"(dst), "+c"(n) : "a"(color) : "memory");
}
int main(void) {
    uint64_t hash = 14695981039346656037ull;
    for (int padded = 0; padded < 2; padded++) {
        int stride = W + 3 * padded;
        screen_pitch = 4 * stride;
        for (unsigned i = 0; i < sizeof guarded / sizeof *guarded; i++)
            guarded[i] = 0xaaccef;
        for (int frame = 0; frame < 100; frame++) {
            font_scale = frame % 8 ? 1 : 2;
            draw_rect(-7, -1, W + 19, H + 2, 0xff115566u);
            for (int y = -3; y < H; y += 16 * font_scale)
                for (int x = -5; x < W; x += 8 * font_scale)
                    draw_char(32 + (frame * 71 + (y + 4) * 9 + x + 6) % 95, x, y, 0xff123456,
                              0xff876543);
        }
        if (guarded[0] != 0xaaccef || lfbptr[stride * H] != 0xaaccef)
            return 1;
        for (int y = 0; y < H; y++) {
            for (int x = 0; x < W; x++)
                hash = (hash ^ lfbptr[y * stride + x]) * 1099511628211ull;
            for (int x = W; x < stride; x++)
                if (lfbptr[y * stride + x] != 0xaaccef)
                    return 2;
        }
    }
    printf("%016llx\n", (unsigned long long)hash);
    return 0;
}
