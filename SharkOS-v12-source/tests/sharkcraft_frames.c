#ifndef SHARKCRAFT_SOURCE
#define SHARKCRAFT_SOURCE "../plugins/sharkcraft/sharkcraft_plugin.c"
#endif
#include SHARKCRAFT_SOURCE
extern int printf(const char *, ...);
uint32_t sharkapi_get_ticks(void) { return 1000; }
void sharkapi_draw_rect(rect_t r, uint32_t c) {
    (void)r;
    (void)c;
}
int main(void) {
    world_generate();
    camera_reset();
    sc_state = SC_PLAY;
    uint64_t hash = 14695981039346656037ull;
    for (int i = 0; i < 96; i++) {
        yaw = i * 5;
        pitch = (i % 17) - 9;
        sc_third_person = i % 3 == 0;
        if (i == 30 || i == 60) {
            world[12][16][17] = i == 30 ? GLASS : WATER;
            update_neighborhood_visibility(17, 12, 16);
        }
        draw_scene(0, 0, 0, 0);
        for (unsigned k = 0; k < RW * RH; k++)
            hash = (hash ^ pixels[k]) * 1099511628211ull;
    }
    printf("%016llx\n", (unsigned long long)hash);
    return 0;
}
