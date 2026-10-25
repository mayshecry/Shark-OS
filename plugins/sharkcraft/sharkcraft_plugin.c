#include "sharkapi.h"
#include "desktop.h"
#include <stdint.h>

#define RW 320
#define RH 224
#define WX 32
#define WY 24
#define WZ 32
#define WATER_LEVEL 5

#define FP 1024
#define Q 256
#define NEAR_CLIP 32
#define MAX_RENDER_DIST (28 * Q)

#define PLAYER_RADIUS (Q * 28 / 100)
#define PLAYER_HEIGHT (Q * 18 / 10)
#define EYE_HEIGHT (Q * 162 / 100)
#define WALK_SPEED (Q * 45 / 10)
#define SPRINT_SPEED (Q * 64 / 10)
#define FLY_SPEED (Q * 72 / 10)
#define SWIM_SPEED (Q * 30 / 10)
#define JUMP_SPEED (Q * 78 / 10)
#define GRAVITY (Q * 22)
#define WATER_GRAVITY (Q * 5)
#define MAX_FALL_SPEED (Q * 36)
#define MAX_WATER_FALL (Q * 8)
#define MAX_MOVE_STEP (Q / 8)

#define INPUT_INITIAL_GRACE_MS 540
#define INPUT_REPEAT_GRACE_MS 220
#define COYOTE_TIME_MS 110
#define JUMP_BUFFER_MS 120

#define INPUT_FORWARD   0
#define INPUT_BACK      1
#define INPUT_LEFT      2
#define INPUT_RIGHT     3
#define INPUT_UP        4
#define INPUT_DOWN      5
#define INPUT_LOOK_UP   6
#define INPUT_LOOK_DOWN 7
#define INPUT_LOOK_LEFT 8
#define INPUT_LOOK_RIGHT 9
#define INPUT_SPRINT    10
#define INPUT_COUNT     11

#define AIR     0
#define GRASS   1
#define DIRT    2
#define STONE   3
#define COBBLE  4
#define WOOD    5
#define PLANKS  6
#define BRICK   7
#define SAND    8
#define LEAVES  9
#define GLASS   10
#define WATER   11
#define BEDROCK 12
#define BLOCK_COUNT 13
#define HOTBAR_COUNT 10

plugin_info_t sharkcraft_plugin_info = {
    .version = SHARKAPI_VERSION,
    .name = "sharkcraft",
    .author = "SharkOS community",
    .description = "3D voxel sandbox: procedural terrain, water, trees, physics, and building.",
    .major = 1,
    .minor = 0
};

static const uint8_t hotbar_blocks[HOTBAR_COUNT] = {
    GRASS, DIRT, STONE, COBBLE, WOOD, PLANKS, BRICK, SAND, LEAVES, GLASS
};

static const char *const block_names[BLOCK_COUNT] = {
    "AIR", "GRASS", "DIRT", "STONE", "COBBLE", "OAK LOG",
    "PLANKS", "BRICKS", "SAND", "LEAVES", "GLASS", "WATER", "BEDROCK"
};

static uint8_t world[WY][WZ][WX];
static uint8_t face_mask[WY][WZ][WX];
static uint8_t col_min_y[WZ][WX];
static uint8_t col_max_y[WZ][WX];
static uint8_t surface_light[WZ][WX];

static uint32_t pixels[RW * RH];
static uint16_t zbuf[RW * RH];
static uint32_t tex_lut[BLOCK_COUNT][6][256];
static int textures_ready = 0;

static int camera_x, camera_y, camera_z;
static int yaw, pitch;
static int selected_slot = 0;
static int selected = GRASS;
static int focal_length = 128;
static uint32_t world_seed = 1337u;
static int world_ready = 0;

static int drag_active = 0, drag_moved = 0;
static int drag_x = 0, drag_y = 0, drag_start_x = 0, drag_start_y = 0;
static uint32_t drag_start_tick = 0;
static int yaw_drag_remainder = 0, pitch_drag_remainder = 0;
static int prev_mouse_buttons = 0;

static int velocity_x, velocity_y, velocity_z;
static int on_ground, flying, in_water;
static uint32_t last_grounded_tick = 0;
static uint32_t jump_buffer_deadline = 0;
static uint32_t move_deadline[INPUT_COUNT];
static uint32_t min_hold_deadline[INPUT_COUNT];
static uint8_t raw_key_held[INPUT_COUNT];
static int raw_scancode_active = 0;
static uint32_t last_raw_event_tick = 0;
static uint32_t physics_last_tick = 0;
static int physics_clock_ready = 0;
static int position_remainder_x, position_remainder_y, position_remainder_z;
static int yaw_look_remainder, pitch_look_remainder;

static int has_target = 0;
static int target_bx, target_by, target_bz;
static int target_px, target_py, target_pz;
static int target_face = -1;

typedef struct { int x, y, z, iz, uoz, voz; } vertex_t;
typedef struct { int x, y, z, u, v; } camera_vertex_t;

static const int cs[256] = {
    1024, 1024, 1023, 1021, 1019, 1016, 1013, 1009, 1004, 999, 993, 987, 980, 972, 964, 955,
    946, 936, 926, 915, 903, 891, 878, 865, 851, 837, 822, 807, 792, 775, 759, 742,
    724, 706, 688, 669, 650, 630, 610, 590, 569, 548, 526, 505, 483, 460, 438, 415,
    392, 369, 345, 321, 297, 273, 249, 224, 200, 175, 150, 125, 100, 75, 50, 25,
    0, -25, -50, -75, -100, -125, -150, -175, -200, -224, -249, -273, -297, -321, -345, -369,
    -392, -415, -438, -460, -483, -505, -526, -548, -569, -590, -610, -630, -650, -669, -688, -706,
    -724, -742, -759, -775, -792, -807, -822, -837, -851, -865, -878, -891, -903, -915, -926, -936,
    -946, -955, -964, -972, -980, -987, -993, -999, -1004, -1009, -1013, -1016, -1019, -1021, -1023, -1024,
    -1024, -1024, -1023, -1021, -1019, -1016, -1013, -1009, -1004, -999, -993, -987, -980, -972, -964, -955,
    -946, -936, -926, -915, -903, -891, -878, -865, -851, -837, -822, -807, -792, -775, -759, -742,
    -724, -706, -688, -669, -650, -630, -610, -590, -569, -548, -526, -505, -483, -460, -438, -415,
    -392, -369, -345, -321, -297, -273, -249, -224, -200, -175, -150, -125, -100, -75, -50, -25,
    0, 25, 50, 75, 100, 125, 150, 175, 200, 224, 249, 273, 297, 321, 345, 369,
    392, 415, 438, 460, 483, 505, 526, 548, 569, 590, 610, 630, 650, 669, 688, 706,
    724, 742, 759, 775, 792, 807, 822, 837, 851, 865, 878, 891, 903, 915, 926, 936,
    946, 955, 964, 972, 980, 987, 993, 999, 1004, 1009, 1013, 1016, 1019, 1021, 1023, 1024,
};

static const int sn[256] = {
    0, 25, 50, 75, 100, 125, 150, 175, 200, 224, 249, 273, 297, 321, 345, 369,
    392, 415, 438, 460, 483, 505, 526, 548, 569, 590, 610, 630, 650, 669, 688, 706,
    724, 742, 759, 775, 792, 807, 822, 837, 851, 865, 878, 891, 903, 915, 926, 936,
    946, 955, 964, 972, 980, 987, 993, 999, 1004, 1009, 1013, 1016, 1019, 1021, 1023, 1024,
    1024, 1024, 1023, 1021, 1019, 1016, 1013, 1009, 1004, 999, 993, 987, 980, 972, 964, 955,
    946, 936, 926, 915, 903, 891, 878, 865, 851, 837, 822, 807, 792, 775, 759, 742,
    724, 706, 688, 669, 650, 630, 610, 590, 569, 548, 526, 505, 483, 460, 438, 415,
    392, 369, 345, 321, 297, 273, 249, 224, 200, 175, 150, 125, 100, 75, 50, 25,
    0, -25, -50, -75, -100, -125, -150, -175, -200, -224, -249, -273, -297, -321, -345, -369,
    -392, -415, -438, -460, -483, -505, -526, -548, -569, -590, -610, -630, -650, -669, -688, -706,
    -724, -742, -759, -775, -792, -807, -822, -837, -851, -865, -878, -891, -903, -915, -926, -936,
    -946, -955, -964, -972, -980, -987, -993, -999, -1004, -1009, -1013, -1016, -1019, -1021, -1023, -1024,
    -1024, -1024, -1023, -1021, -1019, -1016, -1013, -1009, -1004, -999, -993, -987, -980, -972, -964, -955,
    -946, -936, -926, -915, -903, -891, -878, -865, -851, -837, -822, -807, -792, -775, -759, -742,
    -724, -706, -688, -669, -650, -630, -610, -590, -569, -548, -526, -505, -483, -460, -438, -415,
    -392, -369, -345, -321, -297, -273, -249, -224, -200, -175, -150, -125, -100, -75, -50, -25,
};

static const int face_neighbor[6][3] = {
    { 1,  0,  0}, {-1,  0,  0},
    { 0,  1,  0}, { 0, -1,  0},
    { 0,  0,  1}, { 0,  0, -1}
};

static const int face_corner[6][4][3] = {
    {{1, 0, 0}, {1, 1, 0}, {1, 1, 1}, {1, 0, 1}},
    {{0, 0, 1}, {0, 1, 1}, {0, 1, 0}, {0, 0, 0}},
    {{0, 1, 1}, {1, 1, 1}, {1, 1, 0}, {0, 1, 0}},
    {{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}},
    {{1, 0, 1}, {1, 1, 1}, {0, 1, 1}, {0, 0, 1}},
    {{0, 0, 0}, {0, 1, 0}, {1, 1, 0}, {1, 0, 0}}
};

static inline int clamp_byte(int v) {
    return v < 0 ? 0 : (v > 255 ? 255 : v);
}

static inline int abs_val(int v) {
    return v < 0 ? -v : v;
}

static inline int floor_block(int value) {
    return value >> 8;
}

static inline uint8_t block_at(int x, int y, int z) {
    if ((unsigned)x >= WX || (unsigned)y >= WY || (unsigned)z >= WZ) return AIR;
    return world[y][z][x];
}

static inline int is_solid_block(uint8_t b) {
    return b != AIR && b != WATER;
}

static inline int is_opaque_block(uint8_t b) {
    return b != AIR && b != WATER && b != GLASS;
}

static inline int is_translucent_block(uint8_t b) {
    return b == WATER || b == GLASS;
}

static inline int solid(int x, int y, int z) {
    if (y < 0) return 1;
    if ((unsigned)x >= WX || (unsigned)y >= WY || (unsigned)z >= WZ) return 0;
    return is_solid_block(world[y][z][x]);
}

static uint32_t compute_texel(int block, int face, int u, int v) {
    u &= 15;
    v &= 15;
    uint32_t h = (uint32_t)(u * 374761393u + v * 668265263u + block * 2147483647u);
    h = (h ^ (h >> 13)) * 1274126177u;
    int n = (int)((h >> 24) & 15) - 7;
    int r = 128, g = 128, b = 128;

    if (block == GRASS) {
        if (face == 2) {
            int blade = ((u * 5 + v * 3) ^ (u >> 1)) & 3;
            r = 74 + n + blade * 3;
            g = 166 + n * 2 + blade * 5;
            b = 52 + n + blade * 2;
        } else if (face == 3) {
            r = 122 + n * 2;
            g = 84 + n * 2;
            b = 52 + n;
        } else {
            int fringe = 2 + (((u * 7 + 3) ^ (u >> 1)) & 3);
            if (v < fringe) {
                r = 72 + n;
                g = 158 + n * 2;
                b = 49 + n;
            } else {
                int pebble = ((u * 3 + v * 5) & 7) == 0 ? 10 : 0;
                r = 121 + n * 2 - pebble;
                g = 83 + n * 2 - pebble;
                b = 51 + n - pebble / 2;
            }
        }
    } else if (block == DIRT) {
        int pebble = (((u * 7 + v * 13) ^ (u * v)) & 15) < 2 ? 12 : 0;
        r = 126 + n * 2 - pebble;
        g = 86 + n * 2 - pebble;
        b = 54 + n - pebble / 2;
    } else if (block == STONE) {
        int strata = ((v * 3 + (u >> 2)) & 3) * 4 - 6;
        int crack = ((u + v * 2) % 11 == 0 && (u ^ v) & 1) ? -16 : 0;
        r = 126 + n + strata + crack;
        g = 130 + n + strata + crack;
        b = 135 + n + strata + crack;
    } else if (block == COBBLE) {
        int cell_x = (u + ((v >> 2) & 1) * 2) & 3;
        int cell_y = v & 3;
        int mortar = (cell_x == 0 || cell_y == 0);
        if (mortar) {
            r = 76 + n;
            g = 79 + n;
            b = 82 + n;
        } else {
            int stone_id = ((u >> 2) * 7 + (v >> 2) * 13) & 3;
            int shade = stone_id * 8 - 10;
            r = 124 + shade + n;
            g = 128 + shade + n;
            b = 132 + shade + n;
        }
    } else if (block == WOOD) {
        if (face == 2 || face == 3) {
            int dx = u - 7, dy = v - 7;
            int dist2 = dx * dx + dy * dy;
            if (u == 0 || u == 15 || v == 0 || v == 15) {
                r = 92 + n; g = 62 + n; b = 34 + n;
            } else {
                int ring = (dist2 >> 2) & 1;
                r = 168 - ring * 18 + n;
                g = 128 - ring * 14 + n;
                b = 74 - ring * 8 + n;
            }
        } else {
            int groove = ((u + (v >> 3)) & 3) == 0 ? -16 : (((u & 1) ? 6 : 0));
            r = 104 + groove + n;
            g = 72 + groove * 2 / 3 + n;
            b = 40 + groove / 2 + n;
        }
    } else if (block == PLANKS) {
        int row = v >> 2;
        int seam_y = (v & 3) == 0;
        int joint_x = ((u + row * 5) & 7) == 0 && (v & 3) != 0;
        if (seam_y || joint_x) {
            r = 128 + n;
            g = 92 + n;
            b = 50 + n;
        } else {
            int grain = ((u + row * 3) & 3) * 3;
            r = 182 + grain + n;
            g = 140 + grain + n;
            b = 82 + grain / 2 + n;
        }
    } else if (block == BRICK) {
        int row = v >> 2;
        int mortar_y = (v & 3) == 0;
        int mortar_x = ((u + (row & 1) * 4) & 7) == 0;
        if (mortar_y || mortar_x) {
            r = 176 + n;
            g = 172 + n;
            b = 164 + n;
        } else {
            int brick_tint = (((u >> 3) + row * 3) & 3) * 5;
            r = 168 + brick_tint + n * 2;
            g = 72 + brick_tint / 2 + n;
            b = 58 + brick_tint / 2 + n;
        }
    } else if (block == SAND) {
        int wave = (((u + (v >> 1)) & 3) == 0) ? 7 : -3;
        r = 226 + wave + n;
        g = 208 + wave + n;
        b = 142 + wave + n;
    } else if (block == LEAVES) {
        int dither = ((u * 3 + v * 5) ^ (u >> 1)) & 3;
        int dark = (dither == 0) ? -18 : (dither == 3 ? 10 : 0);
        r = 46 + dark / 2 + n;
        g = 132 + dark + n * 2;
        b = 44 + dark / 2 + n;
    } else if (block == GLASS) {
        int border = (u == 0 || u == 15 || v == 0 || v == 15);
        int inner_rim = (u == 1 || u == 14 || v == 1 || v == 14) && ((u + v) & 1);
        int streak = ((u + v == 5) || (u + v == 6) || (u + v == 18)) && u > 2 && u < 13 && v > 2 && v < 13;
        if (border) {
            r = 210; g = 238; b = 248;
        } else if (inner_rim) {
            r = 150; g = 205; b = 225;
        } else if (streak) {
            r = 235; g = 250; b = 255;
        } else {
            return 0;
        }
    } else if (block == WATER) {
        int wave = ((u + v * 2) & 3) == 0 ? 14 : (((u ^ v) & 3) == 2 ? -8 : 0);
        r = 38 + wave / 2;
        g = 108 + wave;
        b = 208 + wave;
    } else {
        int blotch = (((u * 5 + v * 9) ^ (u * 3)) & 7) * 7 - 20;
        r = 54 + blotch + n;
        g = 54 + blotch + n;
        b = 58 + blotch + n;
    }

    int shade_num = 256;
    if (face == 2) shade_num = 256;
    else if (face == 4 || face == 5) shade_num = 222;
    else if (face == 0 || face == 1) shade_num = 192;
    else shade_num = 150;

    if (block != GLASS && block != WATER && (u == 0 || u == 15 || v == 0 || v == 15)) {
        shade_num = (shade_num * 234) >> 8;
    }

    r = clamp_byte((r * shade_num) >> 8);
    g = clamp_byte((g * shade_num) >> 8);
    b = clamp_byte((b * shade_num) >> 8);
    return 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

static void init_textures(void) {
    if (textures_ready) return;
    for (int b = 1; b < BLOCK_COUNT; b++) {
        for (int f = 0; f < 6; f++) {
            for (int v = 0; v < 16; v++) {
                for (int u = 0; u < 16; u++) {
                    tex_lut[b][f][(v << 4) | u] = compute_texel(b, f, u, v);
                }
            }
        }
    }
    textures_ready = 1;
}

static int face_is_visible(int x, int y, int z, uint8_t b, int f) {
    int nx = x + face_neighbor[f][0];
    int ny = y + face_neighbor[f][1];
    int nz = z + face_neighbor[f][2];
    if (ny < 0) return 0;
    uint8_t nb = block_at(nx, ny, nz);
    if (b == WATER) {
        return (f == 2) ? (nb == AIR || nb == GLASS) : (nb == AIR && ((unsigned)nx < WX && (unsigned)nz < WZ));
    }
    if (b == GLASS) {
        return nb != GLASS && !is_opaque_block(nb);
    }
    return !is_opaque_block(nb);
}

static void update_column_bounds(int x, int z) {
    if ((unsigned)x >= WX || (unsigned)z >= WZ) return;
    int min_y = WY, max_y = 0, top_light = 0;
    for (int y = 0; y < WY; y++) {
        uint8_t b = world[y][z][x];
        if (b != AIR) {
            uint8_t mask = 0;
            for (int f = 0; f < 6; f++) {
                if (face_is_visible(x, y, z, b, f)) mask |= (uint8_t)(1u << f);
            }
            face_mask[y][z][x] = mask;
            if (mask) {
                if (y < min_y) min_y = y;
                if (y > max_y) max_y = y;
            }
            if (is_opaque_block(b)) top_light = y;
        } else {
            face_mask[y][z][x] = 0;
        }
    }
    if (min_y > max_y) {
        col_min_y[z][x] = 1;
        col_max_y[z][x] = 0;
    } else {
        col_min_y[z][x] = (uint8_t)min_y;
        col_max_y[z][x] = (uint8_t)max_y;
    }
    surface_light[z][x] = (uint8_t)top_light;
}

static void rebuild_all_visibility(void) {
    for (int z = 0; z < WZ; z++) {
        for (int x = 0; x < WX; x++) {
            update_column_bounds(x, z);
        }
    }
}

static void update_neighborhood_visibility(int x, int y, int z) {
    (void)y;
    update_column_bounds(x, z);
    if (x > 0) update_column_bounds(x - 1, z);
    if (x + 1 < WX) update_column_bounds(x + 1, z);
    if (z > 0) update_column_bounds(x, z - 1);
    if (z + 1 < WZ) update_column_bounds(x, z + 1);
}

static int hash2d(int x, int z, uint32_t seed) {
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)z * 668265263u + seed * 1442695041u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return (int)((h >> 20) & 255);
}

static int value_noise_2d(int x_fp, int z_fp, uint32_t seed) {
    int ix = x_fp >> 8;
    int iz = z_fp >> 8;
    int fx = x_fp & 255;
    int fz = z_fp & 255;
    int ux = (fx * fx * (768 - 2 * fx)) >> 16;
    int uz = (fz * fz * (768 - 2 * fz)) >> 16;

    int v00 = hash2d(ix,     iz,     seed);
    int v10 = hash2d(ix + 1, iz,     seed);
    int v01 = hash2d(ix,     iz + 1, seed);
    int v11 = hash2d(ix + 1, iz + 1, seed);

    int vx0 = v00 + (((v10 - v00) * ux) >> 8);
    int vx1 = v01 + (((v11 - v01) * ux) >> 8);
    return vx0 + (((vx1 - vx0) * uz) >> 8);
}

static void place_oak_tree(int tx, int th_base, int tz, int trunk_h) {
    if (th_base + trunk_h + 2 >= WY) return;
    for (int dy = -1; dy <= 2; dy++) {
        int ly = th_base + trunk_h + dy;
        if (ly <= th_base || ly >= WY) continue;
        int radius = (dy <= 0) ? 2 : 1;
        for (int dz = -radius; dz <= radius; dz++) {
            for (int dx = -radius; dx <= radius; dx++) {
                int lx = tx + dx, lz = tz + dz;
                if ((unsigned)lx >= WX || (unsigned)lz >= WZ) continue;
                if (radius == 2 && abs_val(dx) == 2 && abs_val(dz) == 2) {
                    if (dy == -1 || ((lx + lz) & 1)) continue;
                }
                if (dy == 2 && (abs_val(dx) + abs_val(dz) > 1)) continue;
                if (world[ly][lz][lx] == AIR) {
                    world[ly][lz][lx] = LEAVES;
                }
            }
        }
    }
    for (int y = th_base + 1; y <= th_base + trunk_h && y < WY; y++) {
        world[y][tz][tx] = WOOD;
    }
}

static void world_generate(void) {
    init_textures();
    for (int y = 0; y < WY; y++) {
        for (int z = 0; z < WZ; z++) {
            for (int x = 0; x < WX; x++) {
                world[y][z][x] = AIR;
            }
        }
    }

    uint8_t heightmap[WZ][WX];
    for (int z = 0; z < WZ; z++) {
        for (int x = 0; x < WX; x++) {
            int n1 = value_noise_2d(x * 32, z * 32, world_seed);
            int n2 = value_noise_2d(x * 64 + 97, z * 64 + 53, world_seed ^ 0x5A5A5Au);
            int combined = (n1 * 3 + n2) >> 2;

            int basin_dx = x - 9, basin_dz = z - 9;
            int basin_dist2 = basin_dx * basin_dx + basin_dz * basin_dz;
            int bias = 0;
            if (basin_dist2 < 56) {
                bias = -((56 - basin_dist2) * 9) / 56;
            }
            int ridge_dx = x - 25, ridge_dz = z - 24;
            int ridge_dist2 = ridge_dx * ridge_dx + ridge_dz * ridge_dz;
            if (ridge_dist2 < 64) {
                bias += ((64 - ridge_dist2) * 5) / 64;
            }

            int spawn_dx = x - 16, spawn_dz = z - 16;
            int spawn_dist2 = spawn_dx * spawn_dx + spawn_dz * spawn_dz;

            int h = 3 + ((combined * 9) >> 8) + bias;
            if (spawn_dist2 <= 18) {
                h = (h + 7) >> 1;
                if (h < WATER_LEVEL + 1) h = WATER_LEVEL + 1;
                if (h > WATER_LEVEL + 2) h = WATER_LEVEL + 2;
            }
            if (h < 2) h = 2;
            if (h > WY - 9) h = WY - 9;
            heightmap[z][x] = (uint8_t)h;
        }
    }

    for (int z = 0; z < WZ; z++) {
        for (int x = 0; x < WX; x++) {
            int h = heightmap[z][x];
            int near_water = (h <= WATER_LEVEL);
            for (int dz = -1; dz <= 1 && !near_water; dz++) {
                for (int dx = -1; dx <= 1; dx++) {
                    int nx = x + dx, nz = z + dz;
                    if ((unsigned)nx < WX && (unsigned)nz < WZ && heightmap[nz][nx] < WATER_LEVEL) {
                        near_water = 1;
                        break;
                    }
                }
            }

            world[0][z][x] = BEDROCK;
            for (int y = 1; y <= h; y++) {
                if (y == h) {
                    if (h < WATER_LEVEL || (h == WATER_LEVEL && near_water)) {
                        world[y][z][x] = SAND;
                    } else if (h >= 12) {
                        world[y][z][x] = ((x + z) & 1) ? STONE : COBBLE;
                    } else {
                        world[y][z][x] = GRASS;
                    }
                } else if (y >= h - 2) {
                    world[y][z][x] = (h <= WATER_LEVEL && near_water) ? SAND : DIRT;
                } else {
                    world[y][z][x] = (((x * 7 + y * 13 + z * 5) & 15) == 0) ? COBBLE : STONE;
                }
            }
            for (int y = h + 1; y <= WATER_LEVEL; y++) {
                world[y][z][x] = WATER;
            }
        }
    }

    for (int px = 10; px <= 13; px++) {
        for (int pz = 11; pz <= 12; pz++) {
            if (heightmap[pz][px] <= WATER_LEVEL) {
                world[WATER_LEVEL + 1][pz][px] = PLANKS;
                if ((px == 10 || px == 13)) {
                    for (int py = heightmap[pz][px] + 1; py <= WATER_LEVEL; py++) {
                        world[py][pz][px] = WOOD;
                    }
                }
            }
        }
    }

    uint8_t tree_map[WZ][WX];
    for (int z = 0; z < WZ; z++) for (int x = 0; x < WX; x++) tree_map[z][x] = 0;

    for (int z = 3; z < WZ - 3; z++) {
        for (int x = 3; x < WX - 3; x++) {
            int h = heightmap[z][x];
            if (world[h][z][x] != GRASS || h <= WATER_LEVEL || h >= 11) continue;
            int sdx = x - 16, sdz = z - 16;
            if (sdx * sdx + sdz * sdz < 28) continue;
            if (x >= 10 && x <= 16 && z >= 10 && z <= 16) continue;

            int hval = hash2d(x * 3, z * 5, world_seed ^ 0x9E3779B9u);
            if ((hval & 15) != 0) continue;

            int too_close = 0;
            for (int dz = -3; dz <= 3 && !too_close; dz++) {
                for (int dx = -3; dx <= 3; dx++) {
                    if (tree_map[z + dz][x + dx]) {
                        too_close = 1;
                        break;
                    }
                }
            }
            if (too_close) continue;

            tree_map[z][x] = 1;
            int trunk_h = 4 + (hval >> 7);
            place_oak_tree(x, h, z, trunk_h);
        }
    }

    rebuild_all_visibility();
    world_ready = 1;
}

static int world_surface_height(int x, int z) {
    if ((unsigned)x >= WX || (unsigned)z >= WZ) return WATER_LEVEL * Q;
    for (int y = WY - 1; y >= 0; y--) {
        uint8_t b = world[y][z][x];
        if (is_solid_block(b) && b != LEAVES) return (y + 1) * Q;
    }
    return (WATER_LEVEL + 1) * Q;
}

static int player_collides_at(int px, int py, int pz) {
    int min_x = floor_block(px - PLAYER_RADIUS);
    int max_x = floor_block(px + PLAYER_RADIUS - 1);
    int min_y = floor_block(py - EYE_HEIGHT);
    int max_y = floor_block(py + (PLAYER_HEIGHT - EYE_HEIGHT) - 1);
    int min_z = floor_block(pz - PLAYER_RADIUS);
    int max_z = floor_block(pz + PLAYER_RADIUS - 1);
    for (int y = min_y; y <= max_y; y++) {
        for (int z = min_z; z <= max_z; z++) {
            for (int x = min_x; x <= max_x; x++) {
                if (solid(x, y, z)) return 1;
            }
        }
    }
    return 0;
}

static int player_in_water_at(int px, int py, int pz) {
    int min_x = floor_block(px - PLAYER_RADIUS / 2);
    int max_x = floor_block(px + PLAYER_RADIUS / 2);
    int min_y = floor_block(py - EYE_HEIGHT + Q / 4);
    int max_y = floor_block(py - Q / 4);
    int min_z = floor_block(pz - PLAYER_RADIUS / 2);
    int max_z = floor_block(pz + PLAYER_RADIUS / 2);
    for (int y = min_y; y <= max_y; y++) {
        for (int z = min_z; z <= max_z; z++) {
            for (int x = min_x; x <= max_x; x++) {
                if (block_at(x, y, z) == WATER) return 1;
            }
        }
    }
    return 0;
}

static void clear_inputs(void) {
    for (int i = 0; i < INPUT_COUNT; i++) {
        move_deadline[i] = 0;
        min_hold_deadline[i] = 0;
        raw_key_held[i] = 0;
    }
}

static void camera_reset(void) {
    int sx = WX / 2;
    int sz = WZ / 2;
    camera_x = sx * Q + Q / 2;
    camera_z = sz * Q + Q / 2;
    camera_y = world_surface_height(sx, sz) + EYE_HEIGHT;
    while (camera_y < (WY + 2) * Q && player_collides_at(camera_x, camera_y, camera_z)) {
        camera_y += Q / 2;
    }
    yaw = 164;
    pitch = -8;
    focal_length = 128;
    selected_slot = 0;
    selected = hotbar_blocks[selected_slot];
    drag_active = 0;
    drag_moved = 0;
    yaw_drag_remainder = 0;
    pitch_drag_remainder = 0;
    yaw_look_remainder = 0;
    pitch_look_remainder = 0;
    velocity_x = velocity_y = velocity_z = 0;
    on_ground = 1;
    flying = 0;
    in_water = 0;
    last_grounded_tick = 0;
    jump_buffer_deadline = 0;
    clear_inputs();
    position_remainder_x = position_remainder_y = position_remainder_z = 0;
    physics_clock_ready = 0;
}

static int map_key_to_input(char key, uint8_t base_scancode) {
    if (base_scancode == 0x2A || base_scancode == 0x36) return INPUT_SPRINT;
    if (key >= 'A' && key <= 'Z') key = (char)(key - 'A' + 'a');
    if (key == 'w') return INPUT_FORWARD;
    if (key == 's') return INPUT_BACK;
    if (key == 'a') return INPUT_LEFT;
    if (key == 'd') return INPUT_RIGHT;
    if (key == ' ') return INPUT_UP;
    if (key == 'c' || key == 'z') return INPUT_DOWN;
    if (key == 'x') return INPUT_SPRINT;
    if (key == 'i' || key == 0x10) return INPUT_LOOK_UP;
    if (key == 'k' || key == 0x11) return INPUT_LOOK_DOWN;
    if (key == 'j' || key == 0x12) return INPUT_LOOK_LEFT;
    if (key == 'l' || key == 0x13) return INPUT_LOOK_RIGHT;
    return -1;
}

static void sharkcraft_raw_key_cb(char key, uint8_t scancode) {
    int released = (scancode & 0x80) != 0;
    uint8_t base = scancode & 0x7F;
    int idx = map_key_to_input(key, base);
    raw_scancode_active = 1;
    last_raw_event_tick = sharkapi_get_ticks();
    if (idx >= 0 && idx < INPUT_COUNT) {
        if (!released) {
            if (!raw_key_held[idx]) {
                min_hold_deadline[idx] = last_raw_event_tick + 180;
            }
            raw_key_held[idx] = 1;
            move_deadline[idx] = last_raw_event_tick + INPUT_INITIAL_GRACE_MS;
            if (idx == INPUT_UP && !flying) {
                jump_buffer_deadline = last_raw_event_tick + JUMP_BUFFER_MS;
            }
        } else {
            raw_key_held[idx] = 0;
            move_deadline[idx] = min_hold_deadline[idx];
        }
    }
}

void sharkcraft_tick(void);

static void ensure_desktop_integration(void) {
    sharkapi_register_key_handler(sharkcraft_raw_key_cb);
#ifdef SHARKCRAFT_BUILTIN
    for (int i = 0; i < desktop.window_count; i++) {
        if (desktop.windows[i].type == WINDOW_TYPE_SHARKCRAFT) {
            desktop.windows[i].game_tick = sharkcraft_tick;
        }
    }
#endif
}

void sharkcraft_game_reset(void) {
    world_generate();
    camera_reset();
    ensure_desktop_integration();
}

static int ray_target(int *tx, int *ty, int *tz, int *px, int *py, int *pz, int *out_face) {
    int r = yaw & 255, p = pitch & 255;
    int dx = (sn[r] * cs[p]) >> 10;
    int dy = sn[p];
    int dz = (cs[r] * cs[p]) >> 10;

    int cur_x = camera_x;
    int cur_y = camera_y;
    int cur_z = camera_z;
    int bx = floor_block(cur_x);
    int by = floor_block(cur_y);
    int bz = floor_block(cur_z);

    for (int step = 0; step < 104; step++) {
        int next_x = cur_x + (dx >> 4);
        int next_y = cur_y + (dy >> 4);
        int next_z = cur_z + (dz >> 4);
        int nbx = floor_block(next_x);
        int nby = floor_block(next_y);
        int nbz = floor_block(next_z);

        int axes[3] = { nbx != bx, nby != by, nbz != bz };
        for (int a = 0; a < 3; a++) {
            if (!axes[a]) continue;
            int prev_bx = bx, prev_by = by, prev_bz = bz;
            int face = 0;
            if (a == 0) { face = (nbx > bx) ? 1 : 0; bx = nbx; }
            else if (a == 1) { face = (nby > by) ? 3 : 2; by = nby; }
            else { face = (nbz > bz) ? 5 : 4; bz = nbz; }

            if ((unsigned)bx < WX && (unsigned)by < WY && (unsigned)bz < WZ) {
                if (is_solid_block(world[by][bz][bx])) {
                    *tx = bx; *ty = by; *tz = bz;
                    *px = prev_bx; *py = prev_by; *pz = prev_bz;
                    if (out_face) *out_face = face;
                    return 1;
                }
            }
        }
        cur_x = next_x;
        cur_y = next_y;
        cur_z = next_z;
    }
    return 0;
}

static void settle_blocks_at(int x, int y, int z) {
    if ((unsigned)x >= WX || (unsigned)z >= WZ) return;
    for (int cy = 1; cy < WY; cy++) {
        if (world[cy][z][x] == SAND) {
            int dest_y = cy;
            while (dest_y > 0 && (world[dest_y - 1][z][x] == AIR || world[dest_y - 1][z][x] == WATER)) {
                dest_y--;
            }
            if (dest_y != cy) {
                uint8_t displaced = world[dest_y][z][x];
                world[dest_y][z][x] = SAND;
                world[cy][z][x] = (displaced == WATER && cy <= WATER_LEVEL) ? WATER : AIR;
            }
        }
    }
    if ((unsigned)y <= WATER_LEVEL && world[y][z][x] == AIR) {
        for (int f = 0; f < 6; f++) {
            if (f == 3) continue;
            int nx = x + face_neighbor[f][0];
            int ny = y + face_neighbor[f][1];
            int nz = z + face_neighbor[f][2];
            if (block_at(nx, ny, nz) == WATER) {
                world[y][z][x] = WATER;
                break;
            }
        }
    }
}

static void interact(int mode) {
    int tx, ty, tz, px, py, pz, face;
    if (!ray_target(&tx, &ty, &tz, &px, &py, &pz, &face)) return;

    if (mode == 2) {
        uint8_t b = world[ty][tz][tx];
        for (int i = 0; i < HOTBAR_COUNT; i++) {
            if (hotbar_blocks[i] == b) {
                selected_slot = i;
                selected = b;
                return;
            }
        }
        return;
    }

    if (mode == 1) {
        if ((unsigned)px < WX && (unsigned)py < WY && (unsigned)pz < WZ && !solid(px, py, pz)) {
            uint8_t prev = world[py][pz][px];
            world[py][pz][px] = (uint8_t)selected;
            if (player_collides_at(camera_x, camera_y, camera_z)) {
                world[py][pz][px] = prev;
                return;
            }
            settle_blocks_at(px, py, pz);
            update_neighborhood_visibility(px, py, pz);
        }
    } else {
        if (ty == 0 && world[ty][tz][tx] == BEDROCK) return;
        world[ty][tz][tx] = AIR;
        settle_blocks_at(tx, ty, tz);
        update_neighborhood_visibility(tx, ty, tz);
    }
}

static int plane_dist(camera_vertex_t v, int plane) {
    const int hw = (RW / 2) + 2;
    const int hh = (RH / 2) + 2;
    switch (plane) {
        case 0: return v.z - NEAR_CLIP;
        case 1: return v.x * focal_length + hw * v.z;
        case 2: return hw * v.z - v.x * focal_length;
        case 3: return v.y * focal_length + hh * v.z;
        default: return hh * v.z - v.y * focal_length;
    }
}

static camera_vertex_t intersect_edge(camera_vertex_t a, camera_vertex_t b, int da, int db, int plane) {
    int num = da >= 0 ? da : -da;
    int den = num + (db >= 0 ? db : -db);
    while (den > 32767) {
        num >>= 1;
        den >>= 1;
    }
    int t = den > 0 ? ((num << 14) / den) : 0;
    camera_vertex_t out;
    out.x = a.x + (((b.x - a.x) * t) >> 14);
    out.y = a.y + (((b.y - a.y) * t) >> 14);
    out.z = a.z + (((b.z - a.z) * t) >> 14);
    out.u = a.u + (((b.u - a.u) * t) >> 14);
    out.v = a.v + (((b.v - a.v) * t) >> 14);
    if (plane == 0 && out.z < NEAR_CLIP) out.z = NEAR_CLIP;
    return out;
}

static int clip_plane(const camera_vertex_t *in, int count, camera_vertex_t *out, int plane) {
    if (count < 3) return 0;
    int n = 0;
    int d_prev = plane_dist(in[count - 1], plane);
    camera_vertex_t prev = in[count - 1];

    for (int i = 0; i < count; i++) {
        camera_vertex_t curr = in[i];
        int d_curr = plane_dist(curr, plane);
        if (d_curr >= 0) {
            if (d_prev < 0) {
                out[n++] = intersect_edge(curr, prev, d_curr, d_prev, plane);
            }
            out[n++] = curr;
        } else if (d_prev >= 0) {
            out[n++] = intersect_edge(prev, curr, d_prev, d_curr, plane);
        }
        prev = curr;
        d_prev = d_curr;
    }
    return n;
}

static int clip_frustum(const camera_vertex_t *in, camera_vertex_t *out) {
    camera_vertex_t buf_a[12], buf_b[12];
    int n = clip_plane(in, 4, buf_a, 0);
    if (n < 3) return 0;
    n = clip_plane(buf_a, n, buf_b, 1);
    if (n < 3) return 0;
    n = clip_plane(buf_b, n, buf_a, 2);
    if (n < 3) return 0;
    n = clip_plane(buf_a, n, buf_b, 3);
    if (n < 3) return 0;
    return clip_plane(buf_b, n, out, 4);
}

static void project_camera(vertex_t *p, camera_vertex_t v) {
    int z = v.z < NEAR_CLIP ? NEAR_CLIP : v.z;
    p->x = (RW / 2) + (v.x * focal_length) / z;
    p->y = (RH / 2) - (v.y * focal_length) / z;
    int iz = (16383 * NEAR_CLIP) / z;
    if (iz < 1) iz = 1;
    if (iz > 16383) iz = 16383;
    p->iz = iz;
    p->uoz = (v.u * iz) >> 2;
    p->voz = (v.v * iz) >> 2;
}

static void raster_triangle(vertex_t a, vertex_t b, vertex_t c,
                            const uint32_t *face_tex, int flags) {
    int area = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
    if (area == 0) return;
    if (area < 0) {
        vertex_t tmp = b;
        b = c;
        c = tmp;
        area = -area;
    }

    int minx = a.x, maxx = a.x;
    int miny = a.y, maxy = a.y;
    if (b.x < minx) minx = b.x;
    if (c.x < minx) minx = c.x;
    if (b.x > maxx) maxx = b.x;
    if (c.x > maxx) maxx = c.x;
    if (b.y < miny) miny = b.y;
    if (c.y < miny) miny = c.y;
    if (b.y > maxy) maxy = b.y;
    if (c.y > maxy) maxy = c.y;

    if (minx < 0) minx = 0;
    if (miny < 0) miny = 0;
    if (maxx >= RW) maxx = RW - 1;
    if (maxy >= RH) maxy = RH - 1;
    if (minx > maxx || miny > maxy) return;

    uint32_t inv_area = (1u << 29) / (uint32_t)area;

    int diz_ac = a.iz - c.iz, diz_bc = b.iz - c.iz;
    int duoz_ac = a.uoz - c.uoz, duoz_bc = b.uoz - c.uoz;
    int dvoz_ac = a.voz - c.voz, dvoz_bc = b.voz - c.voz;

    int dwa_dx = b.y - c.y, dwa_dy = c.x - b.x;
    int dwb_dx = c.y - a.y, dwb_dy = a.x - c.x;
    int dwc_dx = a.y - b.y, dwc_dy = b.x - a.x;

    int wa_row = (minx - b.x) * dwa_dx + (miny - b.y) * dwa_dy;
    int wb_row = (minx - c.x) * dwb_dx + (miny - c.y) * dwb_dy;
    int wc_row = (minx - a.x) * dwc_dx + (miny - a.y) * dwc_dy;

    int is_water = (flags & 1);
    int is_glass = (flags & 2);
    int is_shadowed = (flags & 4);
    int is_highlighted = (flags & 8);

    for (int y = miny; y <= maxy; y++) {
        int wa = wa_row, wb = wb_row, wc = wc_row;
        int row_idx = y * RW;
        int entered = 0;

        for (int x = minx; x <= maxx; x++) {
            if ((wa | wb | wc) >= 0) {
                entered = 1;
                int alpha = (int)(((uint32_t)wa * inv_area) >> 18);
                int beta  = (int)(((uint32_t)wb * inv_area) >> 18);
                int iz = c.iz + ((alpha * diz_ac + beta * diz_bc) >> 11);
                int idx = row_idx + x;

                if (iz > (int)zbuf[idx]) {
                    int uoz = c.uoz + ((alpha * duoz_ac + beta * duoz_bc) >> 11);
                    int voz = c.voz + ((alpha * dvoz_ac + beta * dvoz_bc) >> 11);
                    if (iz < 1) iz = 1;
                    int u = ((uoz / iz) >> 2) & 15;
                    int v = ((voz / iz) >> 2) & 15;
                    uint32_t col = face_tex[(v << 4) | u];

                    if (is_water) {
                        uint32_t bg = pixels[idx];
                        uint32_t blended = 0xFF000000u |
                            ((((bg & 0x00FEFEFEu) >> 1) + ((col & 0x00FEFEFEu) >> 1)));
                        pixels[idx] = blended;
                        zbuf[idx] = (uint16_t)iz;
                    } else if (!is_glass || col != 0) {
                        if (is_shadowed) {
                            col = 0xFF000000u | (((col & 0x00FCFCFCu) >> 2) * 3);
                        }
                        if (is_highlighted) {
                            if (u == 0 || u == 15 || v == 0 || v == 15) {
                                col = 0xFFFFFFCCu;
                            } else {
                                uint32_t boost = ((col & 0x00FEFEFEu) >> 1) + 0x00242420u;
                                col |= (boost & 0x00202020u);
                            }
                        }
                        zbuf[idx] = (uint16_t)iz;
                        pixels[idx] = col;
                    }
                }
            } else if (entered) {
                break;
            }
            wa += dwa_dx;
            wb += dwb_dx;
            wc += dwc_dx;
        }
        wa_row += dwa_dy;
        wb_row += dwb_dy;
        wc_row += dwc_dy;
    }
}

static void render_block(int x, int y, int z, uint8_t b, uint8_t fm,
                         int cs_r, int sn_r, int cs_p, int sn_p) {
    int cdx = (x << 8) + 128 - camera_x;
    int cdy = (y << 8) + 128 - camera_y;
    int cdz = (z << 8) + 128 - camera_z;

    int chz = (cdx * sn_r + cdz * cs_r) >> 10;
    int cz = (cdy * sn_p + chz * cs_p) >> 10;
    if (cz < NEAR_CLIP - Q || cz > MAX_RENDER_DIST) return;

    int cx = (cdx * cs_r - cdz * sn_r) >> 10;
    int cy = (cdy * cs_p - chz * sn_p) >> 10;

    int z_pad = (cz > NEAR_CLIP ? cz : NEAR_CLIP) + Q + 64;
    if (abs_val(cx) * focal_length > (RW / 2 + 20) * z_pad) return;
    if (abs_val(cy) * focal_length > (RH / 2 + 20) * z_pad) return;

    int block_Cx = (x << 8) + 128;
    int block_Cy = (y << 8) + 128;
    int block_Cz = (z << 8) + 128;
    int is_target_block = (has_target && x == target_bx && y == target_by && z == target_bz);

    for (int f = 0; f < 6; f++) {
        if (!(fm & (1u << f))) continue;

        int fcx = block_Cx + face_neighbor[f][0] * 128;
        int fcy = block_Cy + face_neighbor[f][1] * 128;
        int fcz = block_Cz + face_neighbor[f][2] * 128;
        int facing = face_neighbor[f][0] * (camera_x - fcx) +
                     face_neighbor[f][1] * (camera_y - fcy) +
                     face_neighbor[f][2] * (camera_z - fcz);
        if (facing <= 0) continue;

        camera_vertex_t input[4], clipped[12];
        for (int k = 0; k < 4; k++) {
            const int *c = face_corner[f][k];
            int vdx = ((x + c[0]) << 8) - camera_x;
            int vdy = ((y + c[1]) << 8) - camera_y;
            int vdz = ((z + c[2]) << 8) - camera_z;
            int vhz = (vdx * sn_r + vdz * cs_r) >> 10;
            input[k].x = (vdx * cs_r - vdz * sn_r) >> 10;
            input[k].y = (vdy * cs_p - vhz * sn_p) >> 10;
            input[k].z = (vdy * sn_p + vhz * cs_p) >> 10;
            input[k].u = (k >= 2) ? 255 : 0;
            input[k].v = (k == 1 || k == 2) ? 0 : 255;
        }

        int count = clip_frustum(input, clipped);
        if (count >= 3) {
            int flags = 0;
            if (b == WATER) flags |= 1;
            else if (b == GLASS) flags |= 2;
            if (f == 2 && y < (int)surface_light[z][x]) flags |= 4;
            if (is_target_block) flags |= (f == target_face) ? 8 : 0;

            const uint32_t *face_tex = tex_lut[b][f];
            vertex_t first, previous, current;
            project_camera(&first, clipped[0]);
            project_camera(&previous, clipped[1]);
            for (int k = 2; k < count; k++) {
                project_camera(&current, clipped[k]);
                raster_triangle(first, previous, current, face_tex, flags);
                previous = current;
            }
        }
    }
}

#define G5(r0, r1, r2, r3, r4) \
    (((uint32_t)(r0) << 20) | ((uint32_t)(r1) << 15) | ((uint32_t)(r2) << 10) | ((uint32_t)(r3) << 5) | (uint32_t)(r4))

static uint32_t glyph_5x7(char c) {
    if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
    switch (c) {
        case '0': return G5(14, 17, 17, 17, 14);
        case '1': return G5( 4, 12,  4,  4, 14);
        case '2': return G5(14, 17,  6,  8, 31);
        case '3': return G5(30,  1, 14,  1, 30);
        case '4': return G5(18, 18, 31,  2,  2);
        case '5': return G5(31, 16, 30,  1, 30);
        case '6': return G5(14, 16, 30, 17, 14);
        case '7': return G5(31,  1,  2,  4,  4);
        case '8': return G5(14, 17, 14, 17, 14);
        case '9': return G5(14, 17, 15,  1, 14);
        case 'A': return G5(14, 17, 31, 17, 17);
        case 'B': return G5(30, 17, 30, 17, 30);
        case 'C': return G5(14, 17, 16, 17, 14);
        case 'D': return G5(30, 17, 17, 17, 30);
        case 'E': return G5(31, 16, 30, 16, 31);
        case 'F': return G5(31, 16, 30, 16, 16);
        case 'G': return G5(14, 16, 19, 17, 14);
        case 'H': return G5(17, 17, 31, 17, 17);
        case 'I': return G5(14,  4,  4,  4, 14);
        case 'J': return G5( 7,  2,  2, 18, 12);
        case 'K': return G5(17, 18, 28, 18, 17);
        case 'L': return G5(16, 16, 16, 16, 31);
        case 'M': return G5(17, 27, 21, 17, 17);
        case 'N': return G5(17, 25, 21, 19, 17);
        case 'O': return G5(14, 17, 17, 17, 14);
        case 'P': return G5(30, 17, 30, 16, 16);
        case 'Q': return G5(14, 17, 17, 19, 15);
        case 'R': return G5(30, 17, 30, 18, 17);
        case 'S': return G5(15, 16, 14,  1, 30);
        case 'T': return G5(31,  4,  4,  4,  4);
        case 'U': return G5(17, 17, 17, 17, 14);
        case 'V': return G5(17, 17, 17, 10,  4);
        case 'W': return G5(17, 17, 21, 27, 17);
        case 'X': return G5(17, 10,  4, 10, 17);
        case 'Y': return G5(17, 10,  4,  4,  4);
        case 'Z': return G5(31,  2,  4,  8, 31);
        case ':': return G5( 0,  4,  0,  4,  0);
        case ',': return G5( 0,  0,  0,  4,  8);
        case '-': return G5( 0,  0, 14,  0,  0);
        case '/': return G5( 1,  2,  4,  8, 16);
        case '[': return G5(12,  8,  8,  8, 12);
        case ']': return G5( 6,  2,  2,  2,  6);
        default:  return 0;
    }
}

static void hud_draw_char(int x, int y, char c, uint32_t col) {
    uint32_t bits = glyph_5x7(c);
    if (!bits) return;
    for (int row = 0; row < 5; row++) {
        int py = y + row;
        if ((unsigned)py >= RH) continue;
        uint32_t row_bits = (bits >> ((4 - row) * 5)) & 0x1F;
        for (int col_i = 0; col_i < 5; col_i++) {
            if (row_bits & (16u >> col_i)) {
                int px = x + col_i;
                if ((unsigned)px < RW) pixels[py * RW + px] = col;
            }
        }
    }
}

static int hud_draw_text(int x, int y, const char *s, uint32_t col) {
    while (*s) {
        if (*s != ' ') hud_draw_char(x, y, *s, col);
        x += (s[0] == ':' || s[0] == ',' || s[0] == '[' || s[0] == ']') ? 4 : 6;
        s++;
    }
    return x;
}

static int hud_draw_int(int x, int y, int val, uint32_t col) {
    char buf[12];
    int n = 0;
    if (val < 0) {
        hud_draw_char(x, y, '-', col);
        x += 6;
        val = -val;
    }
    if (val == 0) {
        buf[n++] = '0';
    } else {
        while (val > 0 && n < 10) {
            buf[n++] = (char)('0' + (val % 10));
            val /= 10;
        }
    }
    while (n > 0) {
        hud_draw_char(x, y, buf[--n], col);
        x += 6;
    }
    return x;
}

static void draw_hud(void) {
    for (int y = 0; y < 11; y++) {
        uint32_t *row = &pixels[y * RW];
        for (int x = 0; x < RW; x++) {
            row[x] = 0xFF000000u | ((row[x] & 0x00FEFEFEu) >> 1);
        }
    }
    int tx = 4;
    if (flying) tx = hud_draw_text(tx, 3, "[FLY] ", 0xFFFFDD55u);
    else if (in_water) tx = hud_draw_text(tx, 3, "[SWIM] ", 0xFF55CCFFu);
    else tx = hud_draw_text(tx, 3, "[WALK] ", 0xFF88FF88u);

    tx = hud_draw_text(tx, 3, block_names[selected], 0xFFFFFFFFu);
    tx += 6;
    tx = hud_draw_text(tx, 3, "XYZ:", 0xFFBBBBBBu);
    tx = hud_draw_int(tx, 3, camera_x >> 8, 0xFF66EEFFu);
    tx = hud_draw_text(tx, 3, ",", 0xFFBBBBBBu);
    tx = hud_draw_int(tx, 3, (camera_y - EYE_HEIGHT) >> 8, 0xFF66EEFFu);
    tx = hud_draw_text(tx, 3, ",", 0xFFBBBBBBu);
    tx = hud_draw_int(tx, 3, camera_z >> 8, 0xFF66EEFFu);

    hud_draw_text(RW - 110, 3, "Q/E:BLK F:FLY G:NEW", 0xFFCCCCCCu);

    int cx = RW / 2, cy = RH / 2;
    for (int d = -4; d <= 4; d++) {
        pixels[cy * RW + cx + d] = (d == 0) ? 0xFF202020u : 0xFFFFFFFFu;
        pixels[(cy + d) * RW + cx] = (d == 0) ? 0xFF202020u : 0xFFFFFFFFu;
    }

    int slot_w = 18;
    int bar_w = HOTBAR_COUNT * slot_w + 2;
    int bar_x = (RW - bar_w) / 2;
    int bar_y = RH - 21;

    for (int yy = bar_y - 1; yy < bar_y + 19; yy++) {
        for (int xx = bar_x - 1; xx < bar_x + bar_w + 1; xx++) {
            if ((unsigned)xx < RW && (unsigned)yy < RH) {
                pixels[yy * RW + xx] = 0xFF1A1C20u;
            }
        }
    }

    for (int s = 0; s < HOTBAR_COUNT; s++) {
        int x0 = bar_x + 1 + s * slot_w;
        int y0 = bar_y;
        int is_sel = (s == selected_slot);
        uint32_t border_col = is_sel ? 0xFFFFFFFFu : 0xFF444850u;
        uint32_t bg_col = is_sel ? 0xFF3A4048u : 0xFF25282Eu;

        for (int yy = 0; yy < 18; yy++) {
            for (int xx = 0; xx < 17; xx++) {
                int edge = (xx == 0 || xx == 16 || yy == 0 || yy == 17) ||
                           (is_sel && (xx == 1 || xx == 15 || yy == 1 || yy == 16));
                pixels[(y0 + yy) * RW + (x0 + xx)] = edge ? border_col : bg_col;
            }
        }

        uint8_t blk = hotbar_blocks[s];
        const uint32_t *icon_tex = tex_lut[blk][(blk == GRASS || blk == WOOD) ? 0 : 2];
        for (int iy = 0; iy < 10; iy++) {
            int v = (iy * 16) / 10;
            for (int ix = 0; ix < 10; ix++) {
                int u = (ix * 16) / 10;
                uint32_t c = icon_tex[(v << 4) | u];
                if (c != 0) {
                    pixels[(y0 + 4 + iy) * RW + (x0 + 3 + ix)] = c;
                }
            }
        }
    }
}

static void blit_scene(int ox, int oy, int w, int h) {
    if (w <= 0 || h <= 0) return;

#ifdef SHARKCRAFT_BUILTIN
    if (lfbptr && screen_width > 0 && screen_height > 0 && screen_pitch > 0) {
        int sw = (int)screen_width;
        int sh = (int)screen_height;
        uint32_t stride = (uint32_t)(screen_pitch / 4);

        int x0 = ox < 0 ? 0 : ox;
        int y0 = oy < 0 ? 0 : oy;
        int x1 = (ox + w > sw) ? sw : (ox + w);
        int y1 = (oy + h > sh) ? sh : (oy + h);
        if (x0 >= x1 || y0 >= y1) return;

        int span_w = x1 - x0;
        uint16_t src_x_lut[1920];
        if (span_w > 1920) span_w = 1920;
        uint32_t step_x = ((uint32_t)RW << 16) / (uint32_t)w;
        uint32_t cur_sx = (uint32_t)(x0 - ox) * step_x;
        for (int i = 0; i < span_w; i++) {
            uint32_t sx = cur_sx >> 16;
            if (sx >= RW) sx = RW - 1;
            src_x_lut[i] = (uint16_t)sx;
            cur_sx += step_x;
        }

        uint32_t step_y = ((uint32_t)RH << 16) / (uint32_t)h;
        uint32_t cur_sy = (uint32_t)(y0 - oy) * step_y;
        int prev_sy = -1;

        for (int py = y0; py < y1; py++) {
            int sy = (int)(cur_sy >> 16);
            if (sy >= RH) sy = RH - 1;
            cur_sy += step_y;

            uint32_t *dst_row = &lfbptr[(uint32_t)py * stride + (uint32_t)x0];
            if (sy == prev_sy && py > y0) {
                uint32_t *prev_row = &lfbptr[(uint32_t)(py - 1) * stride + (uint32_t)x0];
                memcpy(dst_row, prev_row, (size_t)span_w * sizeof(uint32_t));
            } else {
                const uint32_t *src_row = &pixels[sy * RW];
                for (int i = 0; i < span_w; i++) {
                    dst_row[i] = src_row[src_x_lut[i]];
                }
                prev_sy = sy;
            }
        }
        return;
    }
#endif

    for (int y = 0; y < RH; y++) {
        int x = 0, sy = oy + y * h / RH, ey = oy + (y + 1) * h / RH;
        while (x < RW) {
            uint32_t c = pixels[y * RW + x];
            int e = x + 1;
            while (e < RW && pixels[y * RW + e] == c) e++;
            int sx = ox + x * w / RW, ex = ox + e * w / RW;
            if (ex > sx && ey > sy) {
                sharkapi_draw_rect((rect_t){c, sx, sy, ex - sx, ey - sy}, c);
            }
            x = e;
        }
    }
}

static void draw_scene(int ox, int oy, int w, int h) {
    init_textures();
    has_target = ray_target(&target_bx, &target_by, &target_bz,
                            &target_px, &target_py, &target_pz, &target_face);

    int r = yaw & 255, p = pitch & 255;
    int cs_r = cs[r], sn_r = sn[r];
    int cs_p = cs[p], sn_p = sn[p];

    int horizon_y = (RH / 2) + ((sn_p * focal_length) / (cs_p != 0 ? cs_p : 1));
    if (horizon_y < 0) horizon_y = 0;
    if (horizon_y > RH) horizon_y = RH;

    for (int y = 0; y < RH; y++) {
        uint32_t row_col;
        if (in_water && camera_y < (WATER_LEVEL + 1) * Q) {
            row_col = 0xFF184888u;
        } else if (y < horizon_y) {
            int t = ((horizon_y - y) << 7) / (RH + 1);
            if (t > 127) t = 127;
            int sr = 168 - ((t * 68) >> 7);
            int sg = 212 - ((t * 50) >> 7);
            int sb = 248 - ((t * 18) >> 7);
            row_col = 0xFF000000u | ((uint32_t)sr << 16) | ((uint32_t)sg << 8) | (uint32_t)sb;
        } else {
            int t = ((y - horizon_y) << 7) / (RH + 1);
            if (t > 127) t = 127;
            int gr = 94 - ((t * 36) >> 7);
            int gg = 142 - ((t * 46) >> 7);
            int gb = 178 - ((t * 64) >> 7);
            row_col = 0xFF000000u | ((uint32_t)gr << 16) | ((uint32_t)gg << 8) | (uint32_t)gb;
        }
        uint32_t *pr = &pixels[y * RW];
        uint16_t *zr = &zbuf[y * RW];
        for (int x = 0; x < RW; x++) {
            pr[x] = row_col;
            zr[x] = 0;
        }
    }

    int cam_bx = floor_block(camera_x);
    int cam_by = floor_block(camera_y);
    int cam_bz = floor_block(camera_z);
    if (cam_bx < 0) cam_bx = 0;
    if (cam_bx >= WX) cam_bx = WX - 1;
    if (cam_by < 0) cam_by = 0;
    if (cam_by >= WY) cam_by = WY - 1;
    if (cam_bz < 0) cam_bz = 0;
    if (cam_bz >= WZ) cam_bz = WZ - 1;

    int max_ring = WX + WZ;
    for (int ring = 0; ring <= max_ring; ring++) {
        int z_min = cam_bz - ring;
        int z_max = cam_bz + ring;
        if (z_min < 0) z_min = 0;
        if (z_max >= WZ) z_max = WZ - 1;

        for (int z = z_min; z <= z_max; z++) {
            int rem = ring - abs_val(z - cam_bz);
            for (int side = 0; side < 2; side++) {
                if (side == 1 && rem == 0) break;
                int x = (side == 0) ? (cam_bx - rem) : (cam_bx + rem);
                if ((unsigned)x >= WX) continue;

                int ymin = col_min_y[z][x];
                int ymax = col_max_y[z][x];
                if (ymin > ymax) continue;

                int y_split = cam_by;
                if (y_split < ymin) y_split = ymin;
                if (y_split > ymax) y_split = ymax;

                for (int y = y_split; y >= ymin; y--) {
                    uint8_t fm = face_mask[y][z][x];
                    if (!fm) continue;
                    uint8_t b = world[y][z][x];
                    if (is_translucent_block(b)) continue;
                    render_block(x, y, z, b, fm, cs_r, sn_r, cs_p, sn_p);
                }
                for (int y = y_split + 1; y <= ymax; y++) {
                    uint8_t fm = face_mask[y][z][x];
                    if (!fm) continue;
                    uint8_t b = world[y][z][x];
                    if (is_translucent_block(b)) continue;
                    render_block(x, y, z, b, fm, cs_r, sn_r, cs_p, sn_p);
                }
            }
        }
    }

    for (int ring = max_ring; ring >= 0; ring--) {
        int z_min = cam_bz - ring;
        int z_max = cam_bz + ring;
        if (z_min < 0) z_min = 0;
        if (z_max >= WZ) z_max = WZ - 1;

        for (int z = z_min; z <= z_max; z++) {
            int rem = ring - abs_val(z - cam_bz);
            for (int side = 0; side < 2; side++) {
                if (side == 1 && rem == 0) break;
                int x = (side == 0) ? (cam_bx - rem) : (cam_bx + rem);
                if ((unsigned)x >= WX) continue;

                int ymin = col_min_y[z][x];
                int ymax = col_max_y[z][x];
                if (ymin > ymax) continue;

                for (int y = ymin; y <= ymax; y++) {
                    uint8_t fm = face_mask[y][z][x];
                    if (!fm) continue;
                    uint8_t b = world[y][z][x];
                    if (!is_translucent_block(b)) continue;
                    render_block(x, y, z, b, fm, cs_r, sn_r, cs_p, sn_p);
                }
            }
        }
    }

    draw_hud();
    blit_scene(ox, oy, w, h);
}

static int input_active(int index, uint32_t now) {
    if (raw_scancode_active && (int32_t)(now - last_raw_event_tick) < 15000) {
        if (raw_key_held[index]) return 1;
        return (int32_t)(min_hold_deadline[index] - now) > 0;
    }
    return (int32_t)(move_deadline[index] - now) > 0;
}

static int approach_value(int value, int target, int amount) {
    if (value < target) return value + ((target - value < amount) ? target - value : amount);
    if (value > target) return value - ((value - target < amount) ? value - target : amount);
    return value;
}

static int integrate_velocity(int velocity, int dt, int *remainder) {
    int total = velocity * dt + *remainder;
    int delta = total / 1000;
    *remainder = total - delta * 1000;
    return delta;
}

static void set_axis_position(int axis, int value) {
    if (axis == 0) camera_x = value;
    else if (axis == 1) camera_y = value;
    else camera_z = value;
}

static void move_axis(int axis, int delta) {
    if (!delta) return;
    int steps = (abs_val(delta) + MAX_MOVE_STEP - 1) / MAX_MOVE_STEP;
    int base = delta / steps, remainder = delta % steps;
    int extra = abs_val(remainder), direction = delta < 0 ? -1 : 1;

    for (int i = 0; i < steps; i++) {
        int step = base + (i < extra ? direction : 0);
        if (!step) continue;
        int nx = camera_x, ny = camera_y, nz = camera_z;
        if (axis == 0) nx += step;
        else if (axis == 1) ny += step;
        else nz += step;

        if (player_collides_at(nx, ny, nz)) {
            int safe = axis == 0 ? camera_x : (axis == 1 ? camera_y : camera_z);
            int blocked = axis == 0 ? nx : (axis == 1 ? ny : nz);
            while (abs_val(blocked - safe) > 1) {
                int mid = safe + (blocked - safe) / 2;
                int tx = camera_x, ty = camera_y, tz = camera_z;
                if (axis == 0) tx = mid;
                else if (axis == 1) ty = mid;
                else tz = mid;
                if (player_collides_at(tx, ty, tz)) blocked = mid;
                else safe = mid;
            }
            set_axis_position(axis, safe);
            if (axis == 0) {
                velocity_x = 0;
                position_remainder_x = 0;
            } else if (axis == 1) {
                if (delta < 0) on_ground = 1;
                velocity_y = 0;
                position_remainder_y = 0;
            } else {
                velocity_z = 0;
                position_remainder_z = 0;
            }
            return;
        }
        camera_x = nx;
        camera_y = ny;
        camera_z = nz;
    }
}

static void clamp_camera(void) {
    int min_pos = PLAYER_RADIUS;
    int max_x = WX * Q - PLAYER_RADIUS;
    int max_z = WZ * Q - PLAYER_RADIUS;
    if (flying) {
        min_pos = -2 * Q;
        max_x = (WX + 2) * Q;
        max_z = (WZ + 2) * Q;
    }
    if (camera_x < min_pos) { camera_x = min_pos; velocity_x = 0; }
    if (camera_x > max_x)   { camera_x = max_x;   velocity_x = 0; }
    if (camera_z < min_pos) { camera_z = min_pos; velocity_z = 0; }
    if (camera_z > max_z)   { camera_z = max_z;   velocity_z = 0; }
}

static void physics_update(void) {
    uint32_t now = sharkapi_get_ticks();
    if (!physics_clock_ready) {
        physics_last_tick = now;
        physics_clock_ready = 1;
        return;
    }
    uint32_t elapsed = now - physics_last_tick;
    physics_last_tick = now;
    if (elapsed > 50) elapsed = 50;
    if (!elapsed) return;
    int dt = (int)elapsed;

    int look_yaw = input_active(INPUT_LOOK_RIGHT, now) - input_active(INPUT_LOOK_LEFT, now);
    int look_pitch = input_active(INPUT_LOOK_UP, now) - input_active(INPUT_LOOK_DOWN, now);
    if (look_yaw) {
        int dyaw = integrate_velocity(look_yaw * 96, dt, &yaw_look_remainder);
        yaw = (yaw + dyaw) & 255;
    }
    if (look_pitch) {
        int dpitch = integrate_velocity(look_pitch * 72, dt, &pitch_look_remainder);
        pitch += dpitch;
        if (pitch > 56) pitch = 56;
        if (pitch < -56) pitch = -56;
    }

    int grounded_at_start = on_ground;
    if (grounded_at_start) last_grounded_tick = now;
    in_water = player_in_water_at(camera_x, camera_y, camera_z);

    int forward = input_active(INPUT_FORWARD, now) - input_active(INPUT_BACK, now);
    int strafe  = input_active(INPUT_RIGHT, now) - input_active(INPUT_LEFT, now);

    int r = yaw & 255;
    int direction_x = sn[r] * forward + cs[r] * strafe;
    int direction_z = cs[r] * forward - sn[r] * strafe;
    if (forward && strafe) {
        direction_x = (direction_x * 724) >> 10;
        direction_z = (direction_z * 724) >> 10;
    }

    int sprinting = input_active(INPUT_SPRINT, now);
#ifdef SHARKCRAFT_BUILTIN
    if (shift_pressed) sprinting = 1;
#endif
    int max_speed = flying ? FLY_SPEED : (in_water ? SWIM_SPEED : (sprinting ? SPRINT_SPEED : WALK_SPEED));
    int target_x = (direction_x * max_speed) >> 10;
    int target_z = (direction_z * max_speed) >> 10;

    int accel = direction_x || direction_z
        ? (flying ? 48 * Q : (grounded_at_start ? 42 * Q : 18 * Q))
        : (flying ? 34 * Q : (grounded_at_start ? 28 * Q : 6 * Q));
    int change = (accel * dt) / 1000;
    if (change < 1) change = 1;
    velocity_x = approach_value(velocity_x, target_x, change);
    velocity_z = approach_value(velocity_z, target_z, change);

    if (flying) {
        int up = input_active(INPUT_UP, now);
        int down = input_active(INPUT_DOWN, now);
        int target_y = (up - down) * FLY_SPEED;
        int change_y = (42 * Q * dt) / 1000;
        if (change_y < 1) change_y = 1;
        velocity_y = approach_value(velocity_y, target_y, change_y);
        move_axis(0, integrate_velocity(velocity_x, dt, &position_remainder_x));
        move_axis(2, integrate_velocity(velocity_z, dt, &position_remainder_z));
        move_axis(1, integrate_velocity(velocity_y, dt, &position_remainder_y));
        on_ground = 0;
        clamp_camera();
        if (camera_y < EYE_HEIGHT + Q) camera_y = EYE_HEIGHT + Q;
        if (camera_y > (WY + 8) * Q) camera_y = (WY + 8) * Q;
        return;
    }

    int want_up = input_active(INPUT_UP, now) || ((int32_t)(jump_buffer_deadline - now) > 0);
    if (in_water) {
        if (want_up) {
            velocity_y = approach_value(velocity_y, Q * 42 / 10, (24 * Q * dt) / 1000 + 1);
        } else {
            velocity_y -= (WATER_GRAVITY * dt) / 1000;
            if (velocity_y < -MAX_WATER_FALL) velocity_y = -MAX_WATER_FALL;
        }
        on_ground = 0;
    } else {
        int can_jump = grounded_at_start || ((int32_t)(now - last_grounded_tick) <= COYOTE_TIME_MS);
        if (want_up && can_jump && velocity_y <= 0) {
            velocity_y = JUMP_SPEED;
            position_remainder_y = 0;
            on_ground = 0;
            grounded_at_start = 0;
            last_grounded_tick = now - COYOTE_TIME_MS - 1;
            jump_buffer_deadline = 0;
        }
    }

    on_ground = 0;
    move_axis(0, integrate_velocity(velocity_x, dt, &position_remainder_x));
    move_axis(2, integrate_velocity(velocity_z, dt, &position_remainder_z));

    if (!in_water) {
        if (grounded_at_start && velocity_y <= 0 && player_collides_at(camera_x, camera_y - 2, camera_z)) {
            on_ground = 1;
            velocity_y = 0;
            position_remainder_y = 0;
        } else {
            velocity_y -= (GRAVITY * dt) / 1000;
            if (velocity_y < -MAX_FALL_SPEED) velocity_y = -MAX_FALL_SPEED;
        }
    }

    move_axis(1, integrate_velocity(velocity_y, dt, &position_remainder_y));
    if (velocity_y <= 0 && player_collides_at(camera_x, camera_y - 2, camera_z)) {
        on_ground = 1;
        velocity_y = 0;
    }

    clamp_camera();
    if (camera_y < -4 * Q) camera_reset();
}

static void register_key_press(int idx, uint32_t now) {
    uint32_t grace = ((int32_t)(move_deadline[idx] - now) > 0)
        ? INPUT_REPEAT_GRACE_MS
        : INPUT_INITIAL_GRACE_MS;
    move_deadline[idx] = now + grace;
    if ((int32_t)(min_hold_deadline[idx] - now) <= 0) {
        min_hold_deadline[idx] = now + 180;
    }
}

static int move_key(char key) {
    if (key >= 'A' && key <= 'Z') key = (char)(key - 'A' + 'a');

    if (key >= '1' && key <= '9') {
        selected_slot = key - '1';
        selected = hotbar_blocks[selected_slot];
        return 1;
    }
    if (key == '0') {
        selected_slot = 9;
        selected = hotbar_blocks[selected_slot];
        return 1;
    }
    if (key == '[') {
        selected_slot = (selected_slot + HOTBAR_COUNT - 1) % HOTBAR_COUNT;
        selected = hotbar_blocks[selected_slot];
        return 1;
    }
    if (key == ']') {
        selected_slot = (selected_slot + 1) % HOTBAR_COUNT;
        selected = hotbar_blocks[selected_slot];
        return 1;
    }
    if (key == '-') {
        if (focal_length > 80) focal_length -= 8;
        return 1;
    }
    if (key == '+' || key == '=') {
        if (focal_length < 200) focal_length += 8;
        return 1;
    }
    if (key == 'f') {
        flying = !flying;
        velocity_x = velocity_y = velocity_z = 0;
        position_remainder_x = position_remainder_y = position_remainder_z = 0;
        clear_inputs();
        if (!flying) {
            for (int i = 0; i < 8 * Q && player_collides_at(camera_x, camera_y, camera_z); i += Q / 8) {
                camera_y += Q / 8;
            }
            on_ground = player_collides_at(camera_x, camera_y - 2, camera_z);
        } else {
            on_ground = 0;
        }
        return 1;
    }
    if (key == 'r') {
        camera_reset();
        return 1;
    }
    if (key == 'g') {
        world_seed = world_seed * 1664525u + 1013904223u;
        world_generate();
        camera_reset();
        return 1;
    }

    uint32_t now = sharkapi_get_ticks();
    if (key == 'w') { register_key_press(INPUT_FORWARD, now); return 1; }
    if (key == 's') { register_key_press(INPUT_BACK, now); return 1; }
    if (key == 'a') { register_key_press(INPUT_LEFT, now); return 1; }
    if (key == 'd') { register_key_press(INPUT_RIGHT, now); return 1; }
    if (key == 'x') { register_key_press(INPUT_SPRINT, now); return 1; }
    if (key == 'i' || key == 0x10) {
        pitch += 4;
        if (pitch > 56) pitch = 56;
        register_key_press(INPUT_LOOK_UP, now);
        return 1;
    }
    if (key == 'k' || key == 0x11) {
        pitch -= 4;
        if (pitch < -56) pitch = -56;
        register_key_press(INPUT_LOOK_DOWN, now);
        return 1;
    }
    if (key == 'j' || key == 0x12) {
        yaw = (yaw - 6) & 255;
        register_key_press(INPUT_LOOK_LEFT, now);
        return 1;
    }
    if (key == 'l' || key == 0x13) {
        yaw = (yaw + 6) & 255;
        register_key_press(INPUT_LOOK_RIGHT, now);
        return 1;
    }
    if (key == ' ') {
        register_key_press(INPUT_UP, now);
        if (!flying) {
            jump_buffer_deadline = now + JUMP_BUFFER_MS;
        }
        return 1;
    }
    if (key == 'c' || key == 'z') {
        if (flying) register_key_press(INPUT_DOWN, now);
        return 1;
    }
    if (key == 'q') { interact(0); return 1; }
    if (key == 'e') { interact(1); return 1; }
    if (key == 'p') { interact(2); return 1; }
    return 0;
}

void sharkcraft_tick(void) {
    if (!world_ready) sharkcraft_game_reset();
    physics_update();
}

void sharkcraft_draw_window(int x, int y, int w, int h) {
    if (!world_ready) sharkcraft_game_reset();
    ensure_desktop_integration();
    physics_update();
    if (w > 0 && h > 0) draw_scene(x, y, w, h);
}

int sharkcraft_handle_key(char key) {
    return move_key(key);
}

int sharkcraft_handle_mouse(int x, int y, int buttons) {
    int changed = 0;

    if ((buttons & 2) && !(prev_mouse_buttons & 2)) {
        interact(1);
        changed = 1;
    }
    if ((buttons & 4) && !(prev_mouse_buttons & 4)) {
        interact(2);
        changed = 1;
    }

    if (!(buttons & 1)) {
        if (drag_active && !drag_moved &&
            (sharkapi_get_ticks() - drag_start_tick) < 240) {
            interact(0);
            changed = 1;
        }
        drag_active = 0;
        drag_moved = 0;
        prev_mouse_buttons = buttons;
        return changed;
    }

    prev_mouse_buttons = buttons;
    if (!drag_active) {
        drag_active = 1;
        drag_moved = 0;
        drag_x = drag_start_x = x;
        drag_y = drag_start_y = y;
        drag_start_tick = sharkapi_get_ticks();
        return changed;
    }

    int dx = x - drag_x;
    int dy = y - drag_y;
    drag_x = x;
    drag_y = y;
    if (abs_val(x - drag_start_x) + abs_val(y - drag_start_y) > 4) {
        drag_moved = 1;
    }

    yaw_drag_remainder += dx;
    pitch_drag_remainder += dy;
    int yaw_step = yaw_drag_remainder / 3;
    int pitch_step = pitch_drag_remainder / 3;
    yaw_drag_remainder -= yaw_step * 3;
    pitch_drag_remainder -= pitch_step * 3;

    yaw = (yaw + yaw_step) & 255;
    pitch -= pitch_step;
    if (pitch > 56)  { pitch = 56;  pitch_drag_remainder = 0; }
    if (pitch < -56) { pitch = -56; pitch_drag_remainder = 0; }
    return changed || dx || dy;
}

int sharkcraft_plugin_init(void) {
    if (!world_ready) sharkcraft_game_reset();
    return 0;
}

void sharkcraft_plugin_cleanup(void) {
    sharkapi_unregister_key_handler();
}

int sharkcraft_plugin_command(int argc, char **argv) {
    (void)argc;
    (void)argv;
    sharkcraft_game_reset();
    int w = 640, h = 480;
    sharkapi_get_screen_size(&w, &h);
    sharkapi_clear_screen();
    draw_scene(0, 0, w, h);
    uint32_t last = sharkapi_get_ticks();
    for (;;) {
        int changed = 0;
        char c;
        int n = 0;
        while (n++ < 12 && (c = sharkapi_getchar()) != 0) {
            if (c == 27) {
                sharkapi_unregister_key_handler();
                sharkapi_clear_screen();
                return 0;
            }
            changed |= move_key(c);
        }
        physics_update();
        uint32_t now = sharkapi_get_ticks();
        if (changed || now - last >= 16) {
            draw_scene(0, 0, w, h);
            last = now;
        }
        sharkapi_yield();
    }
}

plugin_info_t *sharkcraft_plugin_get_info(void) {
    return &sharkcraft_plugin_info;
}

#ifndef SHARKCRAFT_BUILTIN
int plugin_init_entry(void) { return sharkcraft_plugin_init(); }
void plugin_cleanup_entry(void) { sharkcraft_plugin_cleanup(); }
int plugin_command_entry(int argc, char **argv) { return sharkcraft_plugin_command(argc, argv); }
plugin_info_t *plugin_get_info(void) { return sharkcraft_plugin_get_info(); }
#endif
