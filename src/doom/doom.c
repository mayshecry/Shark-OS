#include "kernel.h"
#include "doom.h"
#include "desktop.h"

uint8_t doom_screen[DOOM_SCREEN_H][DOOM_SCREEN_W];
uint32_t doom_palette[DOOM_PALETTE_SIZE];
int doom_scale_factor = 1;
static doom_state_t game_state = DOOM_MENU;
static bool doom_running = false;
static player_t player;

static int window_x = 0, window_y = 0, window_w = 640, window_h = 480;

#define MAX_ENEMIES 32
#define ENEMY_RADIUS 10

typedef enum {
    ENEMY_IMP,
    ENEMY_DEMON,
    ENEMY_ZOMBIE
} enemy_type_t;

typedef struct {
    bool active;
    int32_t x, y;
    enemy_type_t type;
    int health;
    int state;
    int attack_timer;
} enemy_t;

static enemy_t enemies[MAX_ENEMIES];
static int num_enemies = 0;
static int enemy_kill_count = 0;
static int muzzle_flash = 0;

#define FP_ONE 65536
static inline int32_t int_to_fp(int x) { return (int32_t)((uint32_t)x << 16); }
static inline int fp_to_int(int32_t x) { return (int)(x >> 16); }
static inline int32_t fp_mul(int32_t a, int32_t b) {
    return (int32_t)(((int64_t)a * (int64_t)b) >> 16);
}
static inline int32_t fp_div(int32_t a, int32_t b) {
    if (b == 0) return (a >= 0) ? 0x7FFFFFFF : (int32_t)0x80000001;
    if (b < 0) { a = (int32_t)(0u - (uint32_t)a); b = (int32_t)(0u - (uint32_t)b); }
    if (a < 0) {
        uint32_t ua = 0u - (uint32_t)a;
        uint32_t ub = (uint32_t)b;
        uint32_t result = (ua / ub) << 16;
        result |= (((ua % ub) << 16) / ub);
        return -(int32_t)result;
    } else {
        uint32_t ua = (uint32_t)a;
        uint32_t ub = (uint32_t)b;
        uint32_t result = (ua / ub) << 16;
        result |= (((ua % ub) << 16) / ub);
        return (int32_t)result;
    }
}
static inline int32_t fp_abs(int32_t x) { return (x < 0) ? (int32_t)(0u - (uint32_t)x) : x; }

#define SIN_TAB_SIZE 8192
static int32_t sin_table[SIN_TAB_SIZE];

static void build_trig_tables(void) {

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

    for (int i = 1; i < quarter; i++) {
        sin_table[quarter + i] = sin_table[quarter - i];
    }

    for (int i = 1; i < SIN_TAB_SIZE / 2; i++) {
        sin_table[SIN_TAB_SIZE / 2 + i] = -sin_table[i];
    }

    sin_table[0] = 0;
    sin_table[quarter] = FP_ONE;
    sin_table[SIN_TAB_SIZE / 2] = 0;
    sin_table[SIN_TAB_SIZE / 2 + quarter] = -FP_ONE;
}

static inline int32_t fp_sin(uint32_t angle) {
    return sin_table[(angle >> (32 - 13)) & (SIN_TAB_SIZE - 1)];
}

static inline int32_t fp_cos(uint32_t angle) {
    return sin_table[((angle >> (32 - 13)) + SIN_TAB_SIZE / 4) & (SIN_TAB_SIZE - 1)];
}

static void init_doom_palette(void) {

    for (int i = 0; i < 16; i++) {
        uint8_t v = (uint8_t)((i * 255) / 15);
        doom_palette[i] = (v << 16) | (v << 8) | v;
    }

    for (int i = 0; i < 32; i++) {
        uint8_t r = (uint8_t)(64 + (i * 191) / 31);
        uint8_t g = (uint8_t)((i * 40) / 31);
        uint8_t b = (uint8_t)((i * 20) / 31);
        doom_palette[16 + i] = (r << 16) | (g << 8) | b;
    }

    for (int i = 0; i < 32; i++) {
        uint8_t r = (uint8_t)(80 + (i * 140) / 31);
        uint8_t g = (uint8_t)(50 + (i * 110) / 31);
        uint8_t b = (uint8_t)(20 + (i * 50) / 31);
        doom_palette[48 + i] = (r << 16) | (g << 8) | b;
    }

    for (int i = 0; i < 32; i++) {
        uint8_t r = (uint8_t)((i * 60) / 31);
        uint8_t g = (uint8_t)(60 + (i * 195) / 31);
        uint8_t b = (uint8_t)((i * 40) / 31);
        doom_palette[80 + i] = (r << 16) | (g << 8) | b;
    }

    for (int i = 0; i < 32; i++) {
        uint8_t r = (uint8_t)(50 + (i * 100) / 31);
        uint8_t g = (uint8_t)(55 + (i * 105) / 31);
        uint8_t b = (uint8_t)(70 + (i * 140) / 31);
        doom_palette[112 + i] = (r << 16) | (g << 8) | b;
    }

    for (int i = 0; i < 32; i++) {
        uint8_t v = (uint8_t)(40 + (i * 180) / 31);
        uint8_t r = v;
        uint8_t g = (uint8_t)(v > 10 ? v - 10 : 0);
        uint8_t b = (uint8_t)(v > 20 ? v - 20 : 0);
        doom_palette[144 + i] = (r << 16) | (g << 8) | b;
    }

    for (int i = 0; i < 32; i++) {
        uint8_t r = (uint8_t)(30 + (i * 80) / 31);
        uint8_t g = (uint8_t)(10 + (i * 30) / 31);
        uint8_t b = (uint8_t)(40 + (i * 90) / 31);
        doom_palette[176 + i] = (r << 16) | (g << 8) | b;
    }

    for (int i = 0; i < 32; i++) {
        uint8_t r = (uint8_t)(120 + (i * 135) / 31);
        uint8_t g = (uint8_t)(80 + (i * 175) / 31);
        uint8_t b = (uint8_t)((i * 40) / 31);
        doom_palette[208 + i] = (r << 16) | (g << 8) | b;
    }

    doom_palette[240] = 0x404040;
    doom_palette[241] = 0x606060;
    doom_palette[242] = 0x808080;
    doom_palette[243] = 0x200000;
    doom_palette[244] = 0x8B0000;
    doom_palette[245] = 0xFF0000;
    doom_palette[246] = 0x00AA00;
    doom_palette[247] = 0x0000CC;
    doom_palette[248] = 0xFFFF00;
    doom_palette[249] = 0xFF8800;
    doom_palette[250] = 0x00FF00;
    doom_palette[251] = 0x4488FF;
    doom_palette[252] = 0xFFFF44;
    doom_palette[253] = 0x00FFFF;
    doom_palette[254] = 0xFF44FF;
    doom_palette[255] = 0xFFFFFF;
    doom_palette[0] = 0x000000;
}

#define MAP_W 64
#define MAP_H 64
#define MAX_SECTORS 16
static uint8_t game_map[MAP_H][MAP_W];
static int8_t cell_sector[MAP_H][MAP_W];
static int16_t sector_floor[MAX_SECTORS];
static int16_t sector_ceil[MAX_SECTORS];
static int num_sectors;

static void carve_room(int x1, int y1, int x2, int y2, int sector_id, int wall_tex_id) {

    for (int x = x1; x <= x2; x++) {
        if (y1 >= 0 && y1 < MAP_H && x >= 0 && x < MAP_W)
            if (game_map[y1][x] == 0) game_map[y1][x] = wall_tex_id;
        if (y2 >= 0 && y2 < MAP_H && x >= 0 && x < MAP_W)
            if (game_map[y2][x] == 0) game_map[y2][x] = wall_tex_id;
    }
    for (int y = y1; y <= y2; y++) {
        if (y >= 0 && y < MAP_H && x1 >= 0 && x1 < MAP_W)
            if (game_map[y][x1] == 0) game_map[y][x1] = wall_tex_id;
        if (y >= 0 && y < MAP_H && x2 >= 0 && x2 < MAP_W)
            if (game_map[y][x2] == 0) game_map[y][x2] = wall_tex_id;
    }

    for (int y = y1 + 1; y < y2; y++)
        for (int x = x1 + 1; x < x2; x++)
            if (y >= 0 && y < MAP_H && x >= 0 && x < MAP_W)
                cell_sector[y][x] = (int8_t)sector_id;
}

static void carve_door(int x1, int y1, int x2, int y2) {
    for (int y = y1; y <= y2; y++)
        for (int x = x1; x <= x2; x++)
            if (y >= 0 && y < MAP_H && x >= 0 && x < MAP_W)
                game_map[y][x] = 0;
}

static void build_e1m1_map(void) {
    memset(game_map, 0, sizeof(game_map));
    memset(cell_sector, -1, sizeof(cell_sector));
    num_sectors = 10;

    sector_floor[0] = 0;   sector_ceil[0] = 128;
    sector_floor[1] = 0;   sector_ceil[1] = 128;
    sector_floor[2] = 0;   sector_ceil[2] = 160;
    sector_floor[3] = 24;  sector_ceil[3] = 112;
    sector_floor[4] = -16; sector_ceil[4] = 96;
    sector_floor[5] = 0;   sector_ceil[5] = 128;
    sector_floor[6] = 0;   sector_ceil[6] = 128;
    sector_floor[7] = 0;   sector_ceil[7] = 144;
    sector_floor[8] = 0;   sector_ceil[8] = 128;
    sector_floor[9] = 16;  sector_ceil[9] = 120;

    carve_room(4, 4, 14, 12, 0, 1);

    carve_room(7, 12, 11, 20, 1, 2);
    carve_door(8, 12, 10, 12);

    carve_room(2, 20, 22, 32, 2, 3);
    carve_door(8, 20, 10, 20);

    carve_room(9, 23, 15, 29, 3, 4);
    carve_door(10, 23, 14, 23);
    carve_door(10, 29, 14, 29);

    carve_room(22, 22, 30, 30, 4, 5);
    carve_door(22, 25, 22, 28);

    carve_room(2, 34, 10, 42, 5, 6);
    carve_door(4, 32, 8, 32);

    for (int y = 32; y <= 34; y++)
        for (int x = 4; x <= 8; x++)
            if (y >= 0 && y < MAP_H && x >= 0 && x < MAP_W) {
                game_map[y][x] = 0;
                if (cell_sector[y][x] < 0) cell_sector[y][x] = 5;
            }

    carve_room(14, 34, 22, 42, 6, 2);
    for (int y = 32; y <= 34; y++)
        for (int x = 16; x <= 20; x++)
            if (y >= 0 && y < MAP_H && x >= 0 && x < MAP_W) {
                game_map[y][x] = 0;
                if (cell_sector[y][x] < 0) cell_sector[y][x] = 6;
            }

    carve_room(22, 20, 34, 24, 7, 1);
    carve_door(22, 21, 22, 23);

    carve_room(34, 18, 42, 26, 8, 3);
    carve_door(34, 21, 34, 23);

    carve_room(14, 6, 20, 10, 9, 4);
    carve_door(14, 7, 14, 9);

    for (int y = 0; y < MAP_H; y++) {
        for (int x = 0; x < MAP_W; x++) {
            if (game_map[y][x] > 0 && cell_sector[y][x] < 0) {

                for (int r = 1; r < 5; r++) {
                    int found = 0;
                    for (int dy = -r; dy <= r && !found; dy++) {
                        for (int dx = -r; dx <= r && !found; dx++) {
                            int nx = x + dx, ny = y + dy;
                            if (nx >= 0 && nx < MAP_W && ny >= 0 && ny < MAP_H) {
                                if (game_map[ny][nx] == 0 && cell_sector[ny][nx] >= 0) {
                                    cell_sector[y][x] = cell_sector[ny][nx];
                                    found = 1;
                                }
                            }
                        }
                    }
                    if (found) break;
                }
            }
        }
    }

    player.x = int_to_fp(9 * 64 + 32);
    player.y = int_to_fp(8 * 64 + 32);
    player.angle = ANG90;
    player.sector = 0;
}

#define TEX_W 64
#define TEX_H 64
static uint8_t wall_tex[8][TEX_H][TEX_W];
static uint8_t floor_tex[8][TEX_H][TEX_W];
static uint8_t ceil_tex[8][TEX_H][TEX_W];

static void generate_textures(void) {
    for (int y = 0; y < TEX_H; y++) {
        for (int x = 0; x < TEX_W; x++) {

            {
                int bx = x % 32, by = y % 16;
                int offset = (y / 16) % 2 ? 16 : 0;
                bx = (x + offset) % 32;
                int is_mortar = (bx < 1 || by < 1);
                wall_tex[0][y][x] = is_mortar ? (uint8_t)(144 + 4) : (uint8_t)(48 + 8 + ((x * 7 + y * 3) % 12));
            }

            {
                int noise = ((x * 17 + y * 31 + x * y) % 19);
                wall_tex[1][y][x] = (uint8_t)(144 + 6 + noise % 16);
            }

            {
                int panel_x = x % 32, panel_y = y % 32;
                int is_border = (panel_x < 2 || panel_x >= 30 || panel_y < 2 || panel_y >= 30);
                int is_light = (panel_x >= 12 && panel_x <= 20 && panel_y >= 4 && panel_y <= 8);
                if (is_light) wall_tex[2][y][x] = (uint8_t)(208 + 16);
                else if (is_border) wall_tex[2][y][x] = (uint8_t)(112 + 20);
                else wall_tex[2][y][x] = (uint8_t)(112 + 8 + ((x + y) % 6));
            }

            {
                int v = 144 + 16 + ((x * 3 + y * 7) % 10) - ((x * 11 + y * 5) % 8);
                if (v < 144) v = 144;
                if (v > 175) v = 175;
                wall_tex[3][y][x] = (uint8_t)v;
            }

            {
                int rivet = 0;
                if ((x % 16 == 2 || x % 16 == 13) && (y % 16 == 2 || y % 16 == 13)) rivet = 1;
                int seam = (x % 32 == 0 || y % 32 == 0);
                if (rivet) wall_tex[4][y][x] = (uint8_t)(8);
                else if (seam) wall_tex[4][y][x] = (uint8_t)(3);
                else wall_tex[4][y][x] = (uint8_t)(112 + 4 + ((x * 5 + y * 3) % 6));
            }

            {
                int grain = (x + ((y * 3 + x * 7) % 6)) % 8;
                wall_tex[5][y][x] = (uint8_t)(48 + 4 + grain + ((y % 4 == 0) ? 3 : 0));
            }

            {
                int bx = x % 16, by = y % 8;
                int offset = (y / 8) % 2 ? 8 : 0;
                bx = (x + offset) % 16;
                int is_mortar = (bx < 1 || by < 1);
                wall_tex[6][y][x] = is_mortar ? (uint8_t)(144 + 2) : (uint8_t)(16 + 6 + ((x * 3 + y * 7) % 8));
            }

            {
                int stripe = ((x + y) / 8) % 2;
                if (stripe) wall_tex[7][y][x] = (uint8_t)(80 + 16 + ((x + y) % 4));
                else wall_tex[7][y][x] = (uint8_t)(208 + 10 + ((x + y) % 4));
            }

            {
                int tile = ((x / 16) + (y / 16)) % 2;
                floor_tex[0][y][x] = (uint8_t)(144 + (tile ? 8 : 3) + ((x * 3 + y * 5) % 4));
            }

            floor_tex[1][y][x] = (uint8_t)(144 + 4 + ((x * 7 + y * 13 + x * y) % 8));

            {
                int grate = ((x % 8 < 2) || (y % 8 < 2));
                floor_tex[2][y][x] = grate ? (uint8_t)(112 + 12) : (uint8_t)(2);
            }

            floor_tex[3][y][x] = (uint8_t)(144 + 16 + ((x * 5 + y * 3) % 10));

            {
                int wave = ((x * 3 + y * 7) % 12);
                floor_tex[4][y][x] = (uint8_t)(80 + 8 + wave);
            }

            {
                int tile = ((x / 8) + (y / 8)) % 2;
                floor_tex[5][y][x] = (uint8_t)(tile ? 6 : 3);
            }

            floor_tex[6][y][x] = (uint8_t)(48 + 2 + ((x * 3 + y * 7) % 6));

            {
                int line = (x % 16 == 0 || y % 16 == 0);
                floor_tex[7][y][x] = line ? (uint8_t)(112 + 16) : (uint8_t)(112 + 4 + ((x + y) % 4));
            }

            ceil_tex[0][y][x] = (uint8_t)(144 + 2 + ((x * 3 + y * 5) % 4));

            ceil_tex[1][y][x] = (uint8_t)(3 + ((x + y) % 2));

            {
                int panel = (x >= 16 && x < 48 && y >= 16 && y < 48);
                ceil_tex[2][y][x] = panel ? (uint8_t)(12) : (uint8_t)(5 + ((x + y) % 2));
            }

            ceil_tex[3][y][x] = (uint8_t)(2 + ((x * 7 + y * 3) % 3));

            ceil_tex[4][y][x] = (uint8_t)(48 + 2 + ((x + y) % 4));

            ceil_tex[5][y][x] = (uint8_t)(4 + ((x + y) % 2));

            ceil_tex[6][y][x] = (uint8_t)(112 + 2 + ((x + y) % 3));

            ceil_tex[7][y][x] = (uint8_t)(144 + 3 + ((x * 5 + y * 3) % 5));
        }
    }
}

#define SCREEN_W 320
#define SCREEN_H 200
#define FOV_DEG 66
#define CELL_SIZE 64
#define PLAYER_HEIGHT 41
#define HUD_HEIGHT 32

/* =====================================================================
 * Level geometry in the style of the original DOOM engine (the same
 * architecture cpp-doom ports to C++: r_defs / r_bsp / r_segs / r_plane).
 * The grid map is converted into vertexes, sectors and segs, and a real
 * BSP tree is built once at init. Rendering then walks the BSP instead
 * of raycasting, exactly like DOOM does.
 * ===================================================================== */

#define MAX_LEVEL_SEGS  1024
#define MAX_LEVEL_SUBS  512
#define MAX_LEVEL_NODES 512

typedef struct {
    int16_t floorh, ceilh;
    int16_t floorpic, ceilpic;
    int16_t light;
} lv_sector_t;

typedef struct {
    int32_t x1, y1, x2, y2;      /* fixed world coords */
    int32_t length;
    int16_t front, back;         /* sector idx; back = -1 one-sided */
    int16_t midtex, toptex, bottex;
} lv_seg_t;

typedef struct { int16_t firstseg, numsegs; } lv_subsector_t;

typedef struct {
    int32_t x, y, dx, dy;        /* split plane */
    int32_t children[2];         /* >=0 node idx, <0 = ~(subsector idx) */
} lv_node_t;

static lv_sector_t level_sectors[MAX_SECTORS];
static lv_seg_t level_segs[MAX_LEVEL_SEGS];
static int num_segs;
static lv_subsector_t level_subs[MAX_LEVEL_SUBS];
static int num_subs;
static lv_node_t level_nodes[MAX_LEVEL_NODES];
static int num_nodes;
static int level_built = 0;

/* front sector must be on the left of (v1 -> v2); renderer treats
 * cross(dir, p - v1) < 0 as the front side. */
static void add_line(int32_t ax, int32_t ay, int32_t bx, int32_t by,
                     int front, int back, int mid, int top, int bot) {
    if (num_segs >= MAX_LEVEL_SEGS) return;
    lv_seg_t *sg = &level_segs[num_segs++];
    sg->x1 = ax; sg->y1 = ay; sg->x2 = bx; sg->y2 = by;
    int32_t dx = bx - ax, dy = by - ay;
    sg->length = fp_abs(dx) + fp_abs(dy); /* axis aligned */
    sg->front = (int16_t)front;
    sg->back = (int16_t)back;
    sg->midtex = (int16_t)mid;
    sg->toptex = (int16_t)top;
    sg->bottex = (int16_t)bot;
}

static int cell_open(int x, int y) {
    return x >= 0 && x < MAP_W && y >= 0 && y < MAP_H &&
           game_map[y][x] == 0 && cell_sector[y][x] >= 0;
}

/* canonical front side for two-sided lines: higher ceiling, then higher
 * floor, then lower index (stable, generated exactly once) */
static int is_front_side(int a, int b) {
    if (sector_ceil[a] != sector_ceil[b]) return sector_ceil[a] > sector_ceil[b];
    if (sector_floor[a] != sector_floor[b]) return sector_floor[a] > sector_floor[b];
    return a < b;
}

/* Merge a unit-length edge into the seg list: extend an existing
 * collinear seg when sectors/textures match (grid -> DOOM linedefs). */
static void add_unit_edge(int32_t ax, int32_t ay, int32_t bx, int32_t by,
                          int front, int back, int mid, int top, int bot) {
    for (int i = 0; i < num_segs; i++) {
        lv_seg_t *sg = &level_segs[i];
        if (sg->front != front || sg->back != back || sg->midtex != mid ||
            sg->toptex != top || sg->bottex != bot)
            continue;
        /* only merge collinear, same-orientation edges */
        int e_vert = (ax == bx), s_vert = (sg->x1 == sg->x2);
        if (e_vert != s_vert) continue;
        if (e_vert) {
            if (sg->x1 != ax) continue;
            int ed = (by > ay) ? 1 : -1;
            int sd = (sg->y2 > sg->y1) ? 1 : -1;
            if (ed != sd) continue;
        } else {
            if (sg->y1 != ay) continue;
            int ed = (bx > ax) ? 1 : -1;
            int sd = (sg->x2 > sg->x1) ? 1 : -1;
            if (ed != sd) continue;
        }
        if (sg->x2 == ax && sg->y2 == ay) { sg->x2 = bx; sg->y2 = by; return; }
        if (sg->x1 == bx && sg->y1 == by) { sg->x1 = ax; sg->y1 = ay; return; }
    }
    add_line(ax, ay, bx, by, front, back, mid, top, bot);
}

static void extract_level(void) {
    num_segs = 0; num_subs = 0; num_nodes = 0;

    for (int i = 0; i < num_sectors; i++) {
        level_sectors[i].floorh = sector_floor[i];
        level_sectors[i].ceilh = sector_ceil[i];
        level_sectors[i].floorpic = (int16_t)(i & 7);
        level_sectors[i].ceilpic = (int16_t)(i & 7);
    }
    static const int16_t sector_lights[MAX_SECTORS] =
        { 255, 200, 230, 255, 180, 215, 215, 200, 230, 255 };
    for (int i = 0; i < num_sectors; i++)
        level_sectors[i].light = sector_lights[i];

    /* doorway cells carved out of wall rings have no sector yet: give
     * them the sector of an adjacent open cell so openings stay open */
    for (int y = 0; y < MAP_H; y++)
        for (int x = 0; x < MAP_W; x++)
            if (game_map[y][x] == 0 && cell_sector[y][x] < 0) {
                static const int dxo[4] = { 0, 0, -1, 1 };
                static const int dyo[4] = { -1, 1, 0, 0 };
                for (int d = 0; d < 4; d++) {
                    int nx = x + dxo[d], ny = y + dyo[d];
                    if (nx >= 0 && nx < MAP_W && ny >= 0 && ny < MAP_H &&
                        game_map[ny][nx] == 0 && cell_sector[ny][nx] >= 0) {
                        cell_sector[y][x] = cell_sector[ny][nx];
                        break;
                    }
                }
            }

    for (int y = 0; y < MAP_H; y++) {
        for (int x = 0; x < MAP_W; x++) {
            if (!cell_open(x, y)) continue;
            int s = cell_sector[y][x];
            int32_t gx = int_to_fp(x * CELL_SIZE);
            int32_t gy = int_to_fp(y * CELL_SIZE);
            int32_t ex = int_to_fp((x + 1) * CELL_SIZE);
            int32_t ey = int_to_fp((y + 1) * CELL_SIZE);

            /* one-sided walls against solid cells */
            if (x + 1 >= MAP_W || game_map[y][x + 1] > 0 || cell_sector[y][x + 1] < 0) {
                int wt = 0;
                if (x + 1 < MAP_W && game_map[y][x + 1] > 0) wt = game_map[y][x + 1] - 1;
                add_unit_edge(ex, ey, ex, gy, s, -1, wt, -1, -1);
            }
            if (x - 1 < 0 || game_map[y][x - 1] > 0 || cell_sector[y][x - 1] < 0) {
                int wt = 0;
                if (x - 1 >= 0 && game_map[y][x - 1] > 0) wt = game_map[y][x - 1] - 1;
                add_unit_edge(gx, gy, gx, ey, s, -1, wt, -1, -1);
            }
            if (y + 1 >= MAP_H || game_map[y + 1][x] > 0 || cell_sector[y + 1][x] < 0) {
                int wt = 0;
                if (y + 1 < MAP_H && game_map[y + 1][x] > 0) wt = game_map[y + 1][x] - 1;
                add_unit_edge(gx, ey, ex, ey, s, -1, wt, -1, -1);
            }
            if (y - 1 < 0 || game_map[y - 1][x] > 0 || cell_sector[y - 1][x] < 0) {
                int wt = 0;
                if (y - 1 >= 0 && game_map[y - 1][x] > 0) wt = game_map[y - 1][x] - 1;
                add_unit_edge(ex, gy, gx, gy, s, -1, wt, -1, -1);
            }

            /* two-sided lines between different sectors (steps/doorways) */
            if (cell_open(x + 1, y)) {
                int s2 = cell_sector[y][x + 1];
                if (s2 != s && is_front_side(s, s2))
                    add_unit_edge(ex, ey, ex, gy, s, s2, -1, 4, 4);
            }
            if (cell_open(x, y + 1)) {
                int s2 = cell_sector[y + 1][x];
                if (s2 != s && is_front_side(s, s2))
                    add_unit_edge(gx, ey, ex, ey, s, s2, -1, 4, 4);
            }
        }
    }

    for (int i = 0; i < num_segs; i++) {
        lv_seg_t *sg = &level_segs[i];
        sg->length = fp_abs(sg->x2 - sg->x1) + fp_abs(sg->y2 - sg->y1);
    }
}

/* ------------------------- BSP builder ------------------------- */

static int64_t cross64(int32_t ax, int32_t ay, int32_t bx, int32_t by) {
    return (int64_t)ax * by - (int64_t)ay * bx;
}

static int bsp_arena[MAX_LEVEL_SEGS * 4];
static int bsp_arena_used;

static int bsp_alloc(int n) {
    if (bsp_arena_used + n > MAX_LEVEL_SEGS * 4) return -1;
    int p = bsp_arena_used;
    bsp_arena_used += n;
    return p;
}

static int bsp_make_leaf(const int *list, int count) {
    if (num_subs >= MAX_LEVEL_SUBS) return 0;
    int first = num_segs;
    for (int i = 0; i < count && first + i < MAX_LEVEL_SEGS; i++) {
        /* segs already live in level_segs; leaf records their indices
         * compactly by copying to the tail region */
        level_segs[first + i] = level_segs[list[i]];
    }
    num_segs = first + count;
    level_subs[num_subs].firstseg = (int16_t)first;
    level_subs[num_subs].numsegs = (int16_t)count;
    return ~(num_subs++);
}

/* Classify a seg against a split line. Returns 1 when the seg truly
 * straddles the line and must be split. Endpoints exactly on the line
 * never trigger a split - they are classified to the other endpoint's
 * side (collinear segs go front). Scoring and partitioning must use
 * this identical logic or the builder loops on slivers. */
static int bsp_classify(const lv_seg_t *l, const lv_seg_t *sg, int *s1, int *s2) {
    int64_t c1 = cross64(l->x2 - l->x1, l->y2 - l->y1, sg->x1 - l->x1, sg->y1 - l->y1);
    int64_t c2 = cross64(l->x2 - l->x1, l->y2 - l->y1, sg->x2 - l->x1, sg->y2 - l->y1);
    *s1 = (c1 > 0) ? 1 : 0;
    *s2 = (c2 > 0) ? 1 : 0;
    if (c1 == 0 && c2 == 0) { *s1 = 0; *s2 = 0; return 0; }
    if (c1 == 0) { *s1 = *s2; return 0; }
    if (c2 == 0) { *s2 = *s1; return 0; }
    return (*s1 != *s2) ? 1 : 0;
}

static int bsp_build(const int *list, int count, int depth) {
    if (count <= 1 || depth > 24 || num_nodes >= MAX_LEVEL_NODES - 1)
        return bsp_make_leaf(list, count);

    /* choose split line minimizing splits; candidates that would leave
     * one side empty are useless partitions and get rejected */
    int best = -1, bestscore = 0x7FFFFFFF;
    for (int i = 0; i < count; i++) {
        const lv_seg_t *sp = &level_segs[list[i]];
        int splits = 0;
        int f = 0, b = 0;
        for (int j = 0; j < count; j++) {
            if (i == j) continue;
            int s1, s2;
            if (bsp_classify(sp, &level_segs[list[j]], &s1, &s2)) { splits++; f++; b++; }
            else if (s1 == 0) f++;
            else b++;
        }
        if (f == 0 || b == 0) continue;
        int diff = f - b; if (diff < 0) diff = -diff;
        int score = splits * 8 + diff;
        if (score < bestscore) { bestscore = score; best = i; }
    }
    if (best < 0) return bsp_make_leaf(list, count);

    const lv_seg_t split = level_segs[list[best]];

    int fl = bsp_alloc(count + 1);
    int bl = bsp_alloc(count + 1);
    if (fl < 0 || bl < 0) return bsp_make_leaf(list, count);
    int nf = 0, nb = 0;

    for (int i = 0; i < count; i++) {
        lv_seg_t sg = level_segs[list[i]];
        int s1, s2;
        if (!bsp_classify(&split, &sg, &s1, &s2)) {
            if (s1 == 0) bsp_arena[fl + nf++] = list[i];
            else bsp_arena[bl + nb++] = list[i];
        } else {
            int64_t dxs = split.x2 - split.x1, dys = split.y2 - split.y1;
            int64_t den = cross64(sg.x2 - sg.x1, sg.y2 - sg.y1,
                                  (int32_t)dxs, (int32_t)dys);
            if (den == 0) { bsp_arena[fl + nf++] = list[i]; continue; }
            int64_t num = cross64(split.x1 - sg.x1, split.y1 - sg.y1,
                                  (int32_t)dxs, (int32_t)dys);
            /* t = num/den in 16.16, computed without shifting negatives */
            int neg = (num < 0) != (den < 0);
            uint64_t un = (uint64_t)(num < 0 ? 0 - (uint64_t)num : (uint64_t)num);
            uint64_t ud = (uint64_t)(den < 0 ? 0 - (uint64_t)den : (uint64_t)den);
            uint64_t r = (un << 16) / ud;
            if (r > 0x7FFFFFFFu) r = 0x7FFFFFFFu;
            int32_t t = neg ? (int32_t)(0u - (uint32_t)r) : (int32_t)r;
            if (t <= 0) {          /* v1 on the line: body lies on v2's side */
                if (s2 == 0) bsp_arena[fl + nf++] = list[i];
                else bsp_arena[bl + nb++] = list[i];
                continue;
            }
            if (t >= FP_ONE) {     /* v2 on the line: body lies on v1's side */
                if (s1 == 0) bsp_arena[fl + nf++] = list[i];
                else bsp_arena[bl + nb++] = list[i];
                continue;
            }
            int32_t ix = sg.x1 + fp_mul(t, sg.x2 - sg.x1);
            int32_t iy = sg.y1 + fp_mul(t, sg.y2 - sg.y1);
            if (num_segs + 2 > MAX_LEVEL_SEGS) { bsp_arena[fl + nf++] = list[i]; continue; }
            int ia = num_segs, ib = num_segs + 1;
            num_segs += 2;
            level_segs[ia] = sg; level_segs[ia].x2 = ix; level_segs[ia].y2 = iy;
            level_segs[ib] = sg; level_segs[ib].x1 = ix; level_segs[ib].y1 = iy;
            if (s1 == 0) { bsp_arena[fl + nf++] = ia; bsp_arena[bl + nb++] = ib; }
            else { bsp_arena[bl + nb++] = ia; bsp_arena[fl + nf++] = ib; }
        }
    }

    if (nf == 0) return bsp_build(bsp_arena + bl, nb, depth + 1);
    if (nb == 0) return bsp_build(bsp_arena + fl, nf, depth + 1);

    int ni = num_nodes++;
    level_nodes[ni].x = split.x1;
    level_nodes[ni].y = split.y1;
    level_nodes[ni].dx = split.x2 - split.x1;
    level_nodes[ni].dy = split.y2 - split.y1;
    level_nodes[ni].children[0] = bsp_build(bsp_arena + fl, nf, depth + 1);
    level_nodes[ni].children[1] = bsp_build(bsp_arena + bl, nb, depth + 1);
    return ni;
}

static int level_root;

static void build_level(void) {
    extract_level();
    bsp_arena_used = 0;
    int root_list = bsp_alloc(num_segs);
    if (root_list < 0 || num_segs == 0) { level_built = 1; return; }
    for (int i = 0; i < num_segs; i++) bsp_arena[root_list + i] = i;
    int n0 = num_segs;
    level_root = bsp_build(bsp_arena + root_list, n0, 0);
    level_built = 1;
}

static lv_subsector_t* R_PointInSubsector(int32_t x, int32_t y) {
    if (!level_built || num_subs == 0) return &level_subs[0];
    int ni = level_root;
    while (ni >= 0) {
        lv_node_t *n = &level_nodes[ni];
        int64_t c = cross64(n->dx, n->dy, x - n->x, y - n->y);
        ni = (c > 0) ? n->children[1] : n->children[0];
    }
    return &level_subs[~ni];
}


/* Column renderer in the style of the real DOOM: each screen column walks
 * the map near-to-far, emitting floor/ceiling spans per sector, two-sided
 * "riser" walls where sector heights change (steps, doorways), and finally
 * the solid wall. Heights come from the sector table, so rooms with raised
 * floors or low ceilings actually look different - the old renderer drew
 * every wall full-height like Wolfenstein 3D. */

/* =====================================================================
 * The DOOM rendering pipeline (r_main / r_bsp / r_segs / r_plane style):
 * BSP traversal -> R_AddLine per seg -> visplanes for floors/ceilings
 * -> R_DrawPlanes. Walls use one-sided midtexture or two-sided
 * upper/lower risers between front/back sector heights, with
 * ceilingclip/floorclip occlusion and drawsegs for sprite clipping.
 * ===================================================================== */

#define MAXVISPLANES 96
#define MAXDRAWSEGS  64
#define NEAR_DIST    (8 * FP_ONE)

typedef struct {
    int pic;          /* flat index */
    int height;       /* world height of the plane */
    int light;
    int minx, maxx;
    int16_t top[SCREEN_W], bottom[SCREEN_W];
} visplane_t;

static visplane_t visplanes[MAXVISPLANES];
static int num_visplanes;
static visplane_t *floorplane, *ceilingplane;

static int ceilingclip[SCREEN_W], floorclip[SCREEN_W];
static uint8_t wall_drawn[SCREEN_H][SCREEN_W];

static struct {
    int x1, x2;
    int16_t *sprtopclip, *sprbottomclip;
} drawsegs[MAXDRAWSEGS];
static int num_drawsegs;
static int16_t ds_clip_pool[MAXDRAWSEGS * 2][SCREEN_W];

/* per-frame view state */
static int32_t viewx, viewy;
static int viewz;               /* eye height, world units */
static int32_t viewcos, viewsin;
static int32_t dircos[SCREEN_W], dirsin[SCREEN_W];
static int centery, view_h_r;

static inline int fp_to_int_mul(int32_t f, int m) {
    return (int)(((int64_t)f * m) >> 16);
}

static inline uint8_t apply_shade(uint8_t c, int shade) {
    int s = (c * shade) >> 8;
    return (uint8_t)(s > 255 ? 255 : s);
}

/* light falloff like DOOM's light tables: sector light minus distance */
static inline int light_shade(int sector_light, int32_t dist_units) {
    int cells = dist_units >> 22;          /* /64 world units */
    int sh = sector_light - cells * 10;
    if (sh < 32) sh = 32;
    if (sh > 255) sh = 255;
    return sh;
}

static inline void R_ProjectPoint(int32_t wx, int32_t wy,
                                  int32_t *lat, int32_t *dep) {
    int32_t dx = wx - viewx, dy = wy - viewy;
    *lat = fp_mul(viewcos, dx) - fp_mul(viewsin, dy);
    *dep = fp_mul(viewsin, dx) + fp_mul(viewcos, dy);
}

static inline int proj_screen_x(int32_t lat, int32_t dep) {
    return SCREEN_W / 2 + fp_to_int(fp_div(fp_mul(lat, int_to_fp(SCREEN_W / 2)), dep));
}

/* screen row of world height h at perpendicular distance z (fp units) */
static inline int proj_row(int32_t z, int h) {
    if (z < NEAR_DIST) z = NEAR_DIST;
    return centery - fp_to_int(fp_div(int_to_fp((h - viewz) * (SCREEN_W / 2)), z));
}

/* ------------------------- visplanes (r_plane) ------------------------- */

static visplane_t* R_CheckPlane(int pic, int height, int light, int start, int stop) {
    visplane_t *pl = NULL;
    for (int i = 0; i < num_visplanes; i++) {
        visplane_t *p = &visplanes[i];
        if (p->pic != pic || p->height != height) continue;
        if (start > p->maxx) {
            int r = p->maxx + 1;
            while (--r >= start)
                if (p->top[r] != 0x7FFF) break;
            if (r < start) { pl = p; break; }
        }
        pl = p;
        break;
    }
    if (!pl) {
        if (num_visplanes >= MAXVISPLANES) return NULL;
        pl = &visplanes[num_visplanes++];
        for (int i = 0; i < SCREEN_W; i++) {
            pl->top[i] = 0x7FFF;
            pl->bottom[i] = 0x7FFF;
        }
        pl->minx = SCREEN_W;
        pl->maxx = -1;
    }
    pl->pic = pic;
    pl->height = height;
    pl->light = light;
    if (start < pl->minx) pl->minx = start;
    if (stop > pl->maxx) pl->maxx = stop;
    return pl;
}

/* BSP traversal is front-to-back, so the first span recorded for a
 * column is the nearest one and wins. */
static void R_SetSpan(visplane_t *pl, int x, int top, int bottom) {
    if (!pl || top > bottom) return;
    if (pl->top[x] != 0x7FFF) return;
    pl->top[x] = (int16_t)top;
    pl->bottom[x] = (int16_t)bottom;
}

/* Textured flat row like DOOM's R_MapPlane: one distance division per
 * scanline, then per-pixel texture stepping along the column rays. */
static void R_MapPlane(int y, int x1, int x2, int pic, int planeheight, int light) {
    int p = y - centery;
    if (p < 0) p = (int)(0u - (uint32_t)p);
    if (p < 1) p = 1;
    const uint8_t (*flat)[8][TEX_H][TEX_W] = &floor_tex;
    int height = planeheight;
    if (height < 0) {
        flat = &ceil_tex;
        height = (int)(0u - (uint32_t)height);
    }
    if (height < 1) height = 1;
    int32_t dist = fp_div(int_to_fp(height * (SCREEN_W / 2)), int_to_fp(p));
    int shade = light_shade(light, dist);
    for (int x = x1; x <= x2; x++) {
        int32_t wx = viewx + fp_mul(dist, dircos[x]);
        int32_t wy = viewy + fp_mul(dist, dirsin[x]);
        if (wall_drawn[y][x]) continue;
        int tx = (fp_to_int(wx) % TEX_W + TEX_W) % TEX_W;
        int ty = (fp_to_int(wy) % TEX_H + TEX_H) % TEX_H;
        doom_screen[y][x] = apply_shade((*flat)[pic & 7][ty][tx], shade);
    }
}

static void R_DrawPlanes(void) {
    for (int i = 0; i < num_visplanes; i++) {
        visplane_t *pl = &visplanes[i];
        for (int x = pl->minx; x <= pl->maxx; x++) {
            int t = pl->top[x];
            if (t == 0x7FFF) continue;
            int b = pl->bottom[x];
            if (t < 0) t = 0;
            if (b > view_h_r - 1) b = view_h_r - 1;
            if (b < t || t >= view_h_r) continue;
            /* group runs of equal spans, then map every scanline */
            int x2 = x;
            while (x2 + 1 <= pl->maxx && pl->top[x2 + 1] == t && pl->bottom[x2 + 1] == b)
                x2++;
            int planeheight = pl->height >= viewz ? -(pl->height - viewz)
                                                  : (viewz - pl->height);
            for (int y = t; y <= b; y++)
                R_MapPlane(y, x, x2, pl->pic, planeheight, pl->light);
            x = x2;
        }
    }
}

/* ------------------------- walls (r_segs) ------------------------- */

static void R_StoreWallRange(int x, int32_t z, lv_seg_t *sg,
                             const lv_sector_t *front, const lv_sector_t *back,
                             int markfloor, int markceiling,
                             int16_t *ds_top, int16_t *ds_bot,
                             int sx1, int sx2) {
    int fceil = front->ceilh, ffloor = front->floorh;

    /* full wall extent */
    int topy = proj_row(z, fceil);
    int boty = proj_row(z, ffloor);

    /* ceiling / floor planes visible past this seg */
    int ctop = ceilingclip[x] + 1;
    int cbot = topy - 1;
    int ftop = boty + 1;
    int fbot = floorclip[x] - 1;
    if (ctop < 0) ctop = 0;
    if (cbot > view_h_r - 1) cbot = view_h_r - 1;
    if (ftop < 0) ftop = 0;
    if (fbot > view_h_r - 1) fbot = view_h_r - 1;
    if (markceiling) R_SetSpan(ceilingplane, x, ctop, cbot);
    if (markfloor) R_SetSpan(floorplane, x, ftop, fbot);

    /* wall faces, clipped against nearer geometry */
    int shade = light_shade(front->light, z);
    int u = 0;
    if (sg->length > 0 && sx2 > sx1) {
        /* screen-linear texture column like the original engine */
        u = (int)(((int64_t)sg->length * (x - sx1) / (sx2 - sx1)) >> 16);
    }
    int tex_x = u & (TEX_W - 1);

    int ya, yb, h_top, h_bot, texid;

    if (!back || sg->midtex >= 0) {
        ya = topy; yb = boty;
        h_top = fceil; h_bot = ffloor;
        texid = sg->midtex >= 0 ? sg->midtex : 0;
        if (ya < ceilingclip[x] + 1) ya = ceilingclip[x] + 1;
        if (yb > floorclip[x]) yb = floorclip[x];
        if (yb > ya && texid >= 0) {
            int span = h_top - h_bot;
            if (span > 0 && yb > ya) {
                for (int y = ya; y < yb; y++) {
                    int h = h_top - (y - ya) * span / (yb - ya);
                    int ty = h & (TEX_H - 1);
                    doom_screen[y][x] = apply_shade(wall_tex[texid & 7][ty][tex_x], shade);
                    wall_drawn[y][x] = 1;
                }
            }
        }
    } else {
        int bceil = back->ceilh, bfloor = back->floorh;
        /* upper riser between the two ceilings */
        if (fceil > bceil && sg->toptex >= 0) {
            ya = proj_row(z, fceil); yb = proj_row(z, bceil);
            h_top = fceil; h_bot = bceil; texid = sg->toptex;
            if (ya < ceilingclip[x] + 1) ya = ceilingclip[x] + 1;
            if (yb > floorclip[x]) yb = floorclip[x];
            int span = h_top - h_bot;
            for (int y = ya; y < yb; y++) {
                int h = h_top - (y - ya) * span / (yb - ya);
                doom_screen[y][x] = apply_shade(wall_tex[texid & 7][h & (TEX_H - 1)][tex_x], shade);
                wall_drawn[y][x] = 1;
            }
        }
        /* lower riser between the two floors */
        if (bfloor > ffloor && sg->bottex >= 0) {
            ya = proj_row(z, bfloor); yb = proj_row(z, ffloor);
            h_top = bfloor; h_bot = ffloor; texid = sg->bottex;
            if (ya < ceilingclip[x] + 1) ya = ceilingclip[x] + 1;
            if (yb > floorclip[x]) yb = floorclip[x];
            int span = h_top - h_bot;
            for (int y = ya; y < yb; y++) {
                int h = h_top - (y - ya) * span / (yb - ya);
                doom_screen[y][x] = apply_shade(wall_tex[texid & 7][h & (TEX_H - 1)][tex_x], shade);
                wall_drawn[y][x] = 1;
            }
        }
    }

    /* Occlusion: one-sided walls block the whole column, two-sided
     * lines only close what their upper/lower risers actually cover -
     * the opening between them must stay visible, like in DOOM. */
    if (!back || sg->midtex >= 0) {
        if (ds_top) {
            if (topy < ds_top[x]) ds_top[x] = (int16_t)topy;
            if (boty > ds_bot[x]) ds_bot[x] = (int16_t)boty;
        }
        if (boty > ceilingclip[x]) ceilingclip[x] = boty;
        if (topy < floorclip[x]) floorclip[x] = topy;
    } else {
        /* Two-sided: the opening is bounded by the front sector's edge
         * lines - geometry beyond can only show between them. */
        int fcl = proj_row(z, fceil);
        int ffl = proj_row(z, ffloor);
        if (fceil > back->ceilh) {
            if (ds_top) {
                if (topy < ds_top[x]) ds_top[x] = (int16_t)topy;
                int ub = proj_row(z, back->ceilh);
                if (ub > ds_bot[x]) ds_bot[x] = (int16_t)ub;
            }
        } else if (back->floorh > ffloor) {
            if (ds_top) {
                int lt = proj_row(z, back->floorh);
                if (lt < ds_top[x]) ds_top[x] = (int16_t)lt;
                if (boty > ds_bot[x]) ds_bot[x] = (int16_t)boty;
            }
        }
        if (fcl > ceilingclip[x]) ceilingclip[x] = fcl;
        if (ffl < floorclip[x]) floorclip[x] = ffl;
    }
}

static void R_AddLine(lv_seg_t *sg) {
    int32_t lat1, dep1, lat2, dep2;
    R_ProjectPoint(sg->x1, sg->y1, &lat1, &dep1);
    R_ProjectPoint(sg->x2, sg->y2, &lat2, &dep2);

    if (dep1 <= NEAR_DIST && dep2 <= NEAR_DIST) return;

    /* clip against the near plane */
    if (dep1 <= NEAR_DIST) {
        int64_t t = ((int64_t)(NEAR_DIST - dep1) << 16) / (dep2 - dep1);
        lat1 = lat1 + (int32_t)((t * (lat2 - lat1)) >> 16);
        dep1 = NEAR_DIST;
    } else if (dep2 <= NEAR_DIST) {
        int64_t t = ((int64_t)(NEAR_DIST - dep2) << 16) / (dep1 - dep2);
        lat2 = lat2 + (int32_t)((t * (lat1 - lat2)) >> 16);
        dep2 = NEAR_DIST;
    }

    int sx1 = proj_screen_x(lat1, dep1);
    int sx2 = proj_screen_x(lat2, dep2);
    if (sx1 > sx2) {
        int t; int32_t q;
        t = sx1; sx1 = sx2; sx2 = t;
        q = dep1; dep1 = dep2; dep2 = q;
        q = lat1; lat1 = lat2; lat2 = q;
    }
    if (sx1 < 0) sx1 = 0;
    if (sx2 > SCREEN_W) sx2 = SCREEN_W;
    if (sx2 - sx1 < 1) return;

    /* find the visible column range (not yet covered by nearer walls) */
    int start = -1, stop = -1;
    for (int x = sx1; x < sx2; x++) {
        if (ceilingclip[x] + 1 < floorclip[x]) {
            if (start < 0) start = x;
            stop = x + 1;
        }
    }
    if (start < 0) return;

    /* Orient the seg toward the viewer: front = the sector on the
     * viewpoint side. Two-sided lines then render the correct risers
     * and planes from either side, like DOOM's frontsector/backsector
     * semantics but view-aware. */
    const lv_sector_t *front, *back;
    if (sg->back >= 0) {
        int64_t vside = cross64(sg->x2 - sg->x1, sg->y2 - sg->y1,
                                viewx - sg->x1, viewy - sg->y1);
        if (vside <= 0) {
            front = &level_sectors[sg->front];
            back = &level_sectors[sg->back];
        } else {
            front = &level_sectors[sg->back];
            back = &level_sectors[sg->front];
        }
    } else {
        front = &level_sectors[sg->front];
        back = NULL;
    }

    int markfloor, markceiling;
    if (!back) {
        markfloor = (front->floorh < viewz);
        markceiling = (front->ceilh > viewz);
    } else {
        markfloor = (front->floorh != back->floorh ||
                     front->floorpic != back->floorpic) &&
                    (front->floorh < viewz);
        markceiling = (front->ceilh != back->ceilh ||
                       front->ceilpic != back->ceilpic) &&
                      (front->ceilh > viewz);
    }

    /* Floor below a seg / ceiling above it belong to the front sector;
     * surfaces past the opening are marked by the far sector's own segs,
     * limited by the clip lines this seg closes. */
    if (markfloor)
        floorplane = R_CheckPlane(front->floorpic, front->floorh, front->light,
                                  start, stop - 1);
    else
        floorplane = NULL;
    if (markceiling)
        ceilingplane = R_CheckPlane(front->ceilpic, front->ceilh, front->light,
                                    start, stop - 1);
    else
        ceilingplane = NULL;

    /* drawseg for sprite clipping */
    int dsi = -1;
    if (num_drawsegs < MAXDRAWSEGS) {
        dsi = num_drawsegs;
        drawsegs[dsi].x1 = start;
        drawsegs[dsi].x2 = stop;
        drawsegs[dsi].sprtopclip = ds_clip_pool[dsi * 2];
        drawsegs[dsi].sprbottomclip = ds_clip_pool[dsi * 2 + 1];
        for (int x = start; x < stop; x++) {
            ds_clip_pool[dsi * 2][x] = 0x7FFF;
            ds_clip_pool[dsi * 2 + 1][x] = -0x7FFF;
        }
        num_drawsegs++;
    }

    /* inverse-depth interpolation for perspective-correct columns */
    int64_t inv1 = ((int64_t)1 << 40) / dep1;
    int64_t inv2 = ((int64_t)1 << 40) / dep2;

    for (int x = start; x < stop; x++) {
        if (ceilingclip[x] + 1 >= floorclip[x]) continue;
        int64_t invx = inv1 + (inv2 - inv1) * (x - sx1) / (sx2 > sx1 ? sx2 - sx1 : 1);
        int32_t z = (int32_t)(((int64_t)1 << 40) / invx);
        R_StoreWallRange(x, z, sg, front, back, markfloor, markceiling,
                         dsi >= 0 ? drawsegs[dsi].sprtopclip : NULL,
                         dsi >= 0 ? drawsegs[dsi].sprbottomclip : NULL,
                         sx1, sx2);
    }
}

static void R_AddLines(lv_subsector_t *sub) {
    for (int i = 0; i < sub->numsegs; i++)
        R_AddLine(&level_segs[sub->firstseg + i]);
}

/* ------------------------- BSP traversal (r_bsp) ------------------------- */

static void R_RenderBSPNode(int ni) {
    if (ni < 0) {
        R_AddLines(&level_subs[~ni]);
        return;
    }
    lv_node_t *n = &level_nodes[ni];
    int64_t c = cross64(n->dx, n->dy, viewx - n->x, viewy - n->y);
    int side = (c > 0) ? 1 : 0;
    R_RenderBSPNode(n->children[side]);
    R_RenderBSPNode(n->children[side ^ 1]);
}

static void render_view(void) {
    view_h_r = SCREEN_H - HUD_HEIGHT;
    centery = view_h_r / 2;
    viewx = player.x;
    viewy = player.y;
    viewz = sector_floor[player.sector] + PLAYER_HEIGHT;
    viewsin = fp_sin(player.angle);
    viewcos = fp_cos(player.angle);

    /* column ray directions for flat casting (90 deg FOV) */
    uint32_t a = player.angle - 0x20000000u;   /* ANG45 */
    uint32_t step = 0x40000000u / SCREEN_W;    /* ANG90 / width */
    for (int x = 0; x < SCREEN_W; x++) {
        dircos[x] = fp_cos(a);
        dirsin[x] = fp_sin(a);
        a += step;
    }

    for (int x = 0; x < SCREEN_W; x++) {
        ceilingclip[x] = -1;
        floorclip[x] = view_h_r;
    }
    memset(wall_drawn, 0, sizeof(wall_drawn));
    num_visplanes = 0;
    num_drawsegs = 0;

    R_RenderBSPNode(level_root);
    R_DrawPlanes();
}

static void draw_weapon(void) {
    int weapon = player.weapon;
    int cx = SCREEN_W / 2;
    int bot = SCREEN_H - HUD_HEIGHT;

    if (weapon == 0) {

        for (int y = bot - 30; y < bot; y++) {
            for (int x = cx - 12; x < cx + 12; x++) {
                if (x >= 0 && x < SCREEN_W && y >= 0 && y < SCREEN_H - HUD_HEIGHT) {
                    int dx = x - cx, dy = y - (bot - 15);
                    if (dx * dx + dy * dy < 144) {
                        doom_screen[y][x] = (uint8_t)(48 + 16);
                    }
                }
            }
        }
    } else if (weapon == 1) {

        for (int y = bot - 45; y < bot - 20; y++)
            for (int x = cx - 3; x < cx + 3; x++)
                if (x >= 0 && x < SCREEN_W && y >= 0 && y < bot)
                    doom_screen[y][x] = (uint8_t)(5);

        for (int y = bot - 24; y < bot - 4; y++)
            for (int x = cx - 6; x < cx + 6; x++)
                if (x >= 0 && x < SCREEN_W && y >= 0 && y < bot)
                    doom_screen[y][x] = (uint8_t)(48 + 10);

        for (int y = bot - 20; y < bot - 6; y++)
            for (int x = cx - 10; x < cx - 4; x++)
                if (x >= 0 && x < SCREEN_W && y >= 0 && y < bot)
                    doom_screen[y][x] = (uint8_t)(48 + 18);
    } else if (weapon == 2) {

        for (int y = bot - 50; y < bot - 20; y++) {
            for (int x = cx - 5; x < cx + 5; x++)
                if (x >= 0 && x < SCREEN_W && y >= 0 && y < bot)
                    doom_screen[y][x] = (uint8_t)(4);
        }

        for (int y = bot - 24; y < bot - 2; y++)
            for (int x = cx - 8; x < cx + 8; x++)
                if (x >= 0 && x < SCREEN_W && y >= 0 && y < bot)
                    doom_screen[y][x] = (uint8_t)(48 + 6);

        for (int y = bot - 28; y < bot - 14; y++) {
            for (int x = cx - 14; x < cx - 6; x++)
                if (x >= 0 && x < SCREEN_W && y >= 0 && y < bot)
                    doom_screen[y][x] = (uint8_t)(48 + 20);
            for (int x = cx + 6; x < cx + 14; x++)
                if (x >= 0 && x < SCREEN_W && y >= 0 && y < bot)
                    doom_screen[y][x] = (uint8_t)(48 + 20);
        }
    } else if (weapon == 3) {

        for (int y = bot - 55; y < bot - 18; y++) {
            for (int off = -4; off <= 4; off += 4) {
                for (int x = cx + off - 2; x < cx + off + 2; x++)
                    if (x >= 0 && x < SCREEN_W && y >= 0 && y < bot)
                        doom_screen[y][x] = (uint8_t)(5);
            }
        }

        for (int y = bot - 22; y < bot - 2; y++)
            for (int x = cx - 10; x < cx + 10; x++)
                if (x >= 0 && x < SCREEN_W && y >= 0 && y < bot)
                    doom_screen[y][x] = (uint8_t)(112 + 6);
    } else {

        for (int y = bot - 50; y < bot - 16; y++)
            for (int x = cx - 8; x < cx + 8; x++)
                if (x >= 0 && x < SCREEN_W && y >= 0 && y < bot)
                    doom_screen[y][x] = (uint8_t)(80 + 8);

        for (int y = bot - 20; y < bot - 2; y++)
            for (int x = cx - 6; x < cx + 6; x++)
                if (x >= 0 && x < SCREEN_W && y >= 0 && y < bot)
                    doom_screen[y][x] = (uint8_t)(48 + 12);
    }
}

static void hud_char(int x, int y, char c, uint8_t color, uint8_t bg) {
    extern uint8_t font8x8[96][8];
    unsigned char uc = (unsigned char)c;
    if (uc < 32 || uc >= 128) return;
    int idx = uc - 32;
    for (int row = 0; row < 8; row++) {
        uint8_t bits = font8x8[idx][row];
        for (int bcol = 0; bcol < 8; bcol++) {
            int px = x + bcol, py = y + row;
            if (px >= 0 && px < SCREEN_W && py >= 0 && py < SCREEN_H)
                doom_screen[py][px] = (bits & (0x80 >> bcol)) ? color : bg;
        }
    }
}

static void hud_str(int x, int y, const char* str, uint8_t color, uint8_t bg) {
    int ox = x;
    while (*str) {
        if (*str == '\n') { x = ox; y += 10; }
        else { hud_char(x, y, *str, color, bg); x += 8; }
        str++;
    }
}

/* Brief muzzle flash after firing, like DOOM's weapon light. */
static void draw_muzzle_flash(void) {
    if (muzzle_flash <= 0) return;
    int cx = SCREEN_W / 2;
    int bot = SCREEN_H - HUD_HEIGHT;
    int my = bot - 50;
    int r = (muzzle_flash >= 2) ? 9 : 5;
    for (int y = my - r; y <= my + r; y++) {
        for (int x = cx - r; x <= cx + r; x++) {
            if (x < 0 || x >= SCREEN_W || y < 0 || y >= bot) continue;
            int dx = x - cx, dy = y - my;
            int d2 = dx * dx + dy * dy;
            if (d2 <= r * r / 4) {
                doom_screen[y][x] = 255;            /* white core */
            } else if (d2 <= r * r) {
                doom_screen[y][x] = (muzzle_flash >= 2) ? 248 : 249; /* yellow/orange */
            }
        }
    }
    muzzle_flash--;
}

static void draw_hud(void) {
    int hud_y = SCREEN_H - HUD_HEIGHT;

    /* DOOM status bar: warm gray plate with red text */
    for (int y = hud_y; y < SCREEN_H; y++) {
        for (int x = 0; x < SCREEN_W; x++)
            doom_screen[y][x] = 152;
    }

    for (int x = 0; x < SCREEN_W; x++) {
        doom_screen[hud_y][x] = 146;
        doom_screen[SCREEN_H - 1][x] = 146;
    }

    hud_str(5, hud_y + 4, "HEALTH", 244, 0);
    char buf[8];
    buf[0] = '0' + (player.health / 100) % 10;
    buf[1] = '0' + (player.health / 10) % 10;
    buf[2] = '0' + (player.health) % 10;
    buf[3] = '%'; buf[4] = '\0';

    uint8_t hcolor = 245;
    hud_str(5, hud_y + 16, buf, hcolor, 0);

    hud_str(75, hud_y + 4, "ARMOR", 244, 0);
    buf[0] = '0' + (player.armor / 100) % 10;
    buf[1] = '0' + (player.armor / 10) % 10;
    buf[2] = '0' + (player.armor) % 10;
    buf[3] = '%'; buf[4] = '\0';
    hud_str(75, hud_y + 16, buf, 245, 0);

    hud_str(150, hud_y + 4, "AMMO", 244, 0);
    buf[0] = '0' + (player.ammo / 100) % 10;
    buf[1] = '0' + (player.ammo / 10) % 10;
    buf[2] = '0' + (player.ammo) % 10;
    buf[3] = '\0';
    hud_str(150, hud_y + 16, buf, 245, 0);

    const char* weapons[] = {"FIST", "PISTOL", "SHOTGUN", "CHAINGUN", "ROCKET"};
    int w_idx = player.weapon;
    if (w_idx < 0) w_idx = 0;
    if (w_idx > 4) w_idx = 4;
    hud_str(SCREEN_W - 72, hud_y + 4, "ARMS", 244, 0);
    hud_str(SCREEN_W - 72, hud_y + 16, weapons[w_idx], 255, 0);

    int hx = SCREEN_W / 2, hy = (SCREEN_H - HUD_HEIGHT) / 2;
    if (hy > 0 && hy < SCREEN_H - HUD_HEIGHT) {
        for (int i = -3; i <= 3; i++) {
            if (hx + i >= 0 && hx + i < SCREEN_W)
                doom_screen[hy][hx + i] = 245;
            if (hy + i >= 0 && hy + i < SCREEN_H - HUD_HEIGHT)
                doom_screen[hy + i][hx] = 245;
        }
    }
}

static void draw_menu(void) {

    for (int y = 0; y < SCREEN_H; y++) {
        uint8_t bg = (uint8_t)(1 + y / 80);
        for (int x = 0; x < SCREEN_W; x++)
            doom_screen[y][x] = bg;
    }

    const char* title[] = {
        " ####   ####   ####  #   #",
        " #   # #    # #    # ## ##",
        " #   # #    # #    # # # #",
        " #   # #    # #    # #   #",
        " ####   ####   ####  #   #",
    };
    int tx = (SCREEN_W - 26 * 8) / 2, ty = 20;
    for (int i = 0; i < 5; i++)
        hud_str(tx, ty + i * 10, title[i], 245, 0);

    hud_str((SCREEN_W - 15 * 8) / 2, 85, "SharkOS Edition", 252, 0);

    for (int x = 40; x < SCREEN_W - 40; x++)
        doom_screen[100][x] = 245;

    hud_str((SCREEN_W - 12 * 8) / 2, 110, "RIP AND TEAR", 245, 0);

    hud_str((SCREEN_W - 20 * 8) / 2, 135, "Press ENTER to start", 250, 0);
    hud_str((SCREEN_W - 22 * 8) / 2, 150, "ESC to return to shell", 7, 0);

    hud_str((SCREEN_W - 24 * 8) / 2, 172, "WASD - Move   Z/X - Turn", 253, 0);
    hud_str((SCREEN_W - 19 * 8) / 2, 185, "SPACE - Shoot  1-5 Wep", 253, 0);
}

#define MOVE_SPEED (FP_ONE * 2 / 3)

void doom_handle_key(int key) {

    if (game_state == DOOM_MENU) {
        if (key == '\n') {
            game_state = DOOM_PLAYING;
            player.x = int_to_fp(9 * 64 + 32);
            player.y = int_to_fp(8 * 64 + 32);
            player.angle = ANG90;
            player.health = 100; player.armor = 50;
            player.ammo = 50; player.weapon = 1;
            player.velocity_x = 0; player.velocity_y = 0;
        } else if (key == 27) {
            game_state = DOOM_QUIT;
        }
        return;
    }
    if (game_state == DOOM_PLAYING) {
        switch (key) {
            case 27:
                game_state = DOOM_MENU;
                break;
            case 'w': case 'W':
                player.velocity_x += fp_mul(fp_sin(player.angle), MOVE_SPEED);
                player.velocity_y += fp_mul(fp_cos(player.angle), MOVE_SPEED);
                break;
            case 's': case 'S':
                player.velocity_x -= fp_mul(fp_sin(player.angle), MOVE_SPEED * 6 / 10);
                player.velocity_y -= fp_mul(fp_cos(player.angle), MOVE_SPEED * 6 / 10);
                break;
            case 'a': case 'A':
                player.velocity_x += fp_mul(fp_sin(player.angle - ANG90), MOVE_SPEED * 7 / 10);
                player.velocity_y += fp_mul(fp_cos(player.angle - ANG90), MOVE_SPEED * 7 / 10);
                break;
            case 'd': case 'D':
                player.velocity_x += fp_mul(fp_sin(player.angle + ANG90), MOVE_SPEED * 7 / 10);
                player.velocity_y += fp_mul(fp_cos(player.angle + ANG90), MOVE_SPEED * 7 / 10);
                break;
            case 'z': case 'Z':
                player.angle -= ANG1 * 5;
                break;
            case 'x': case 'X':
                player.angle += ANG1 * 5;
                break;
            case ' ':
                if (player.ammo > 0) {
                    player.ammo--;
                    muzzle_flash = 3;

                    for (int i = 0; i < num_enemies; i++) {
                        enemy_t *e = &enemies[i];
                        if (!e->active) continue;

                        int32_t dx = e->x - player.x;
                        int32_t dy = e->y - player.y;

                        int32_t det = fp_mul(fp_cos(player.angle), fp_sin(player.angle + ANG90)) - fp_mul(fp_sin(player.angle), fp_cos(player.angle + ANG90));
        int32_t inv_det = (det == 0) ? FP_ONE : fp_div(FP_ONE, det);
                        int32_t t1 = fp_mul(fp_cos(player.angle), dx) - fp_mul(fp_sin(player.angle), dy);
                        int32_t t2 = fp_mul(fp_sin(player.angle + ANG90), dx) + fp_mul(fp_cos(player.angle + ANG90), dy);
                        int32_t transform_x = fp_mul(t1, inv_det);
                        int32_t transform_y = fp_mul(t2, inv_det);

                        if (transform_y <= 0) continue;

                        int screen_x = SCREEN_W / 2 + fp_to_int(fp_mul(transform_x, int_to_fp(SCREEN_W / 2)) / (transform_y > FP_ONE/4 ? transform_y : FP_ONE/4));
                        int sprite_h = fp_to_int(fp_div(int_to_fp(SCREEN_H), transform_y));
                        if (sprite_h > SCREEN_H) sprite_h = SCREEN_H;

                        if (screen_x >= 0 && screen_x < SCREEN_W) {
                            int damage = 10;
                            if (player.weapon == 1) damage = 10;
                            else if (player.weapon == 2) damage = 30;
                            else if (player.weapon == 3) damage = 15;
                            else if (player.weapon == 4) damage = 100;
                            else if (player.weapon == 0) damage = 15;

                            e->health -= damage;
                            if (e->health <= 0) {
                                e->active = false;
                                enemy_kill_count++;
                                player.ammo += 5;
                            }
                        }
                    }
                }
                break;
            case '1': player.weapon = 0; break;
            case '2': player.weapon = 1; break;
            case '3': player.weapon = 2; break;
            case '4': player.weapon = 3; break;
            case '5': player.weapon = 4; break;
        }
    }
}

static void blit_to_fb(void) {
    uint32_t stride = (uint32_t)(screen_pitch / 4);
    /* Always use scale 2 for 320x200 to 640x400 (aspect ratio preserved) */
    int scale = doom_scale_factor;
    if (scale < 2) scale = 2;  /* Minimum scale 2 for desktop windows */
    int sw = SCREEN_W * scale;
    int sh = SCREEN_H * scale;

    for (int y = 0; y < SCREEN_H && y * scale < sh; y++) {
        for (int sy = 0; sy < scale && y * scale + sy < sh; sy++) {
            int fy = window_y + y * scale + sy;
            for (int x = 0; x < SCREEN_W && x * scale < sw; x++) {
                uint32_t color = doom_palette[doom_screen[y][x]];
                for (int sx = 0; sx < scale && x * scale + sx < sw; sx++) {
                    int fx = window_x + x * scale + sx;
                    lfbptr[fy * stride + fx] = color;
                }
            }
        }
    }
}

void doom_set_window_rect(int x, int y, int w, int h) {
    window_x = x;
    window_y = y;
    window_w = w;
    window_h = h;
    
    /* Calculate scale based on window client area */
    doom_scale_factor = 1;
    if (w >= 640 && h >= 400) doom_scale_factor = 2;
    if (w >= 1280 && h >= 800) doom_scale_factor = 4;
    if (doom_scale_factor < 1) doom_scale_factor = 1;
}

static int enemy_dist_fp(int32_t ex, int32_t ey) {
    int32_t dx = ex - player.x;
    int32_t dy = ey - player.y;
    return fp_to_int(fp_mul(dx, dx) + fp_mul(dy, dy));
}

static int spawn_enemy(int x, int y, enemy_type_t type) {
    if (num_enemies >= MAX_ENEMIES) return -1;
    enemies[num_enemies].active = true;
    enemies[num_enemies].x = int_to_fp(x * CELL_SIZE + CELL_SIZE / 2);
    enemies[num_enemies].y = int_to_fp(y * CELL_SIZE + CELL_SIZE / 2);
    enemies[num_enemies].type = type;
    enemies[num_enemies].health = (type == ENEMY_DEMON) ? 60 : 20;
    enemies[num_enemies].state = 0;
    enemies[num_enemies].attack_timer = 0;
    return num_enemies++;
}

static void init_enemies(void) {
    num_enemies = 0;
    enemy_kill_count = 0;

    spawn_enemy(10, 24, ENEMY_IMP);
    spawn_enemy(14, 24, ENEMY_IMP);
    spawn_enemy(17, 27, ENEMY_IMP);

    spawn_enemy(26, 26, ENEMY_DEMON);

    spawn_enemy(8, 16, ENEMY_ZOMBIE);
    spawn_enemy(26, 22, ENEMY_ZOMBIE);

    spawn_enemy(5, 38, ENEMY_IMP);
    spawn_enemy(19, 38, ENEMY_ZOMBIE);

    spawn_enemy(38, 22, ENEMY_IMP);
    spawn_enemy(40, 20, ENEMY_DEMON);
}

static bool enemy_check_collision(int32_t nx, int32_t ny) {
    int margin = ENEMY_RADIUS;
    for (int oy = -margin; oy <= margin; oy += margin) {
        for (int ox = -margin; ox <= margin; ox += margin) {
            int ix = (fp_to_int(nx) + ox) / CELL_SIZE;
            int iy = (fp_to_int(ny) + oy) / CELL_SIZE;
            if (ix < 0 || ix >= MAP_W || iy < 0 || iy >= MAP_H) return true;
            if (game_map[iy][ix] > 0 || cell_sector[iy][ix] < 0) return true;
        }
    }
    return false;
}

static void update_enemies(void) {
    int player_cell_x = fp_to_int(player.x) / CELL_SIZE;
    int player_cell_y = fp_to_int(player.y) / CELL_SIZE;
    int los_dist_sq = (40 * 40);

    for (int i = 0; i < num_enemies; i++) {
        enemy_t *e = &enemies[i];
        if (!e->active) continue;

        int dist_sq = enemy_dist_fp(e->x, e->y);

        int mx = fp_to_int(e->x) / CELL_SIZE;
        int my = fp_to_int(e->y) / CELL_SIZE;

        bool los = true;
        if (mx != player_cell_x || my != player_cell_y) {
            int dx = player_cell_x - mx;
            int dy = player_cell_y - my;
            int steps = (dx > dy) ? dx : dy;
            if (steps > 0) {
                for (int s = 0; s <= steps; s++) {
                    int cx = mx + (s * dx) / steps;
                    int cy = my + (s * dy) / steps;
                    if (cx < 0 || cx >= MAP_W || cy < 0 || cy >= MAP_H) {
                        los = false; break;
                    }
                    if (game_map[cy][cx] > 0 || cell_sector[cy][cx] < 0) { los = false; break; }
                }
            }
        }

        if (los && dist_sq < los_dist_sq) {
            e->state = 1;

            int32_t speed = int_to_fp(e->type == ENEMY_DEMON ? 2 : 1);

            int32_t edx = player.x - e->x;
            int32_t edy = player.y - e->y;
            int32_t len = fp_mul(edx, edx) + fp_mul(edy, edy);
            if (len > 0) {
                int32_t inv_len = fp_div(FP_ONE, len > FP_ONE ? len : FP_ONE);
                int32_t nx = fp_mul(edx, inv_len);
                int32_t ny = fp_mul(edy, inv_len);
                int32_t mvx = fp_mul(nx, speed);
                int32_t mvy = fp_mul(ny, speed);

                if (!enemy_check_collision(e->x + mvx, e->y))
                    e->x += mvx;
                if (!enemy_check_collision(e->x, e->y + mvy))
                    e->y += mvy;
            }

            if (dist_sq < ((ENEMY_RADIUS + 8) * (ENEMY_RADIUS + 8))) {
                if (e->attack_timer <= 0) {
                    int dmg = (e->type == ENEMY_DEMON) ? 15 : 5;
                    if (player.armor > 0) {
                        int armor_dmg = (dmg * 2) / 3;
                        player.armor -= armor_dmg;
                        if (player.armor < 0) player.armor = 0;
                        player.health -= dmg - armor_dmg;
                    } else {
                        player.health -= dmg;
                    }
                    if (player.health < 0) player.health = 0;
                    e->attack_timer = 30;
                }
            }
        } else {
            e->state = 0;
        }

        if (e->attack_timer > 0) e->attack_timer--;
    }
}

static bool check_collision(int32_t nx_fp, int32_t ny_fp) {

    int margin = 8;
    for (int oy = -margin; oy <= margin; oy += margin) {
        for (int ox = -margin; ox <= margin; ox += margin) {
            int ix = (fp_to_int(nx_fp) + ox) / CELL_SIZE;
            int iy = (fp_to_int(ny_fp) + oy) / CELL_SIZE;
            if (ix < 0 || ix >= MAP_W || iy < 0 || iy >= MAP_H) return true;
            if (game_map[iy][ix] > 0 || cell_sector[iy][ix] < 0) return true;
        }
    }
    return false;
}

static void update_player(void) {
    int32_t px = player.x, py = player.y;
    int32_t nx = px + player.velocity_x;
    int32_t ny = py + player.velocity_y;

    if (!check_collision(nx, ny)) {
        player.x = nx;
        player.y = ny;
    } else if (!check_collision(nx, py)) {
        player.x = nx;
    } else if (!check_collision(px, ny)) {
        player.y = ny;
    }

    player.velocity_x = player.velocity_x * 70 / 100;
    player.velocity_y = player.velocity_y * 70 / 100;

    if (fp_abs(player.velocity_x) < 64) player.velocity_x = 0;
    if (fp_abs(player.velocity_y) < 64) player.velocity_y = 0;

    if (level_built) {
        lv_subsector_t *sub = R_PointInSubsector(player.x, player.y);
        if (sub->numsegs > 0) {
            int s = level_segs[sub->firstseg].front;
            if (s >= 0 && s < num_sectors) { player.sector = s; return; }
        }
    }
    int mx = fp_to_int(player.x) / CELL_SIZE;
    int my = fp_to_int(player.y) / CELL_SIZE;
    if (mx >= 0 && mx < MAP_W && my >= 0 && my < MAP_H) {
        int s = cell_sector[my][mx];
        if (s >= 0 && s < num_sectors) player.sector = s;
    }

}

/* Sprites the DOOM way (r_things): projected from their sector floor,
 * sorted far-to-near, and clipped column-wise against the drawsegs
 * stored during wall rendering. */

#define MAX_VISIBLE_SPRITES MAX_ENEMIES

typedef struct { int idx; int32_t dep; } vis_sprite_t;
static vis_sprite_t vis_sprites[MAX_VISIBLE_SPRITES];
static int num_vis_sprites;

static void draw_enemies(void) {
    /* view state (viewx/viewz/centery) is left over from render_view */
    num_vis_sprites = 0;

    for (int i = 0; i < num_enemies; i++) {
        enemy_t *e = &enemies[i];
        if (!e->active) continue;
        int32_t lat, dep;
        R_ProjectPoint(e->x, e->y, &lat, &dep);
        if (dep <= NEAR_DIST) continue;
        int sx = proj_screen_x(lat, dep);
        if (sx < -128 || sx > SCREEN_W + 128) continue;
        vis_sprites[num_vis_sprites].idx = i;
        vis_sprites[num_vis_sprites].dep = dep;
        num_vis_sprites++;
    }

    /* insertion sort, far first (painter's order like R_DrawMasked) */
    for (int i = 1; i < num_vis_sprites; i++) {
        vis_sprite_t key = vis_sprites[i];
        int j = i - 1;
        while (j >= 0 && vis_sprites[j].dep < key.dep) {
            vis_sprites[j + 1] = vis_sprites[j];
            j--;
        }
        vis_sprites[j + 1] = key;
    }

    for (int si = 0; si < num_vis_sprites; si++) {
        enemy_t *e = &enemies[vis_sprites[si].idx];
        int32_t lat, dep2;
        R_ProjectPoint(e->x, e->y, &lat, &dep2);
        int sx = proj_screen_x(lat, dep2);

        int emx = fp_to_int(e->x) / CELL_SIZE;
        int emy = fp_to_int(e->y) / CELL_SIZE;
        int es = (emx >= 0 && emx < MAP_W && emy >= 0 && emy < MAP_H)
                     ? cell_sector[emy][emx] : -1;
        int h_floor = (es >= 0 && es < num_sectors) ? sector_floor[es] : 0;
        int elight = (es >= 0 && es < num_sectors) ? level_sectors[es].light : 255;

        int y_bot = proj_row(dep2, h_floor);
        int y_top = proj_row(dep2, h_floor + 56);
        int sprite_h = y_bot - y_top;
        if (sprite_h < 2) continue;
        int sprite_w = sprite_h;
        int x0 = sx - sprite_w / 2;
        int shade = light_shade(elight, dep2);

        for (int x = x0; x < x0 + sprite_w; x++) {
            if (x < 0 || x >= SCREEN_W) continue;
            int top = y_top, bot = y_bot;

            /* clip against walls recorded this frame */
            for (int d = 0; d < num_drawsegs; d++) {
                if (x < drawsegs[d].x1 || x >= drawsegs[d].x2) continue;
                int wtop = drawsegs[d].sprtopclip[x];
                int wbot = drawsegs[d].sprbottomclip[x];
                if (wtop == 0x7FFF) continue;
                if (wtop <= top && wbot >= bot) { top = 1; bot = 0; break; }
                if (wtop <= top && wbot > top && wbot < bot) top = wbot + 1;
                if (wbot >= bot && wtop < bot && wtop > top) bot = wtop - 1;
            }
            if (bot <= top) continue;
            if (top < 0) top = 0;
            if (bot > view_h_r) bot = view_h_r;

            int tex_x = (x - x0) * 64 / sprite_h;

            for (int y = top; y < bot; y++) {
                int tex_y = (y - y_top) * 64 / sprite_h;
                if (tex_y < 0 || tex_y > 63) continue;

                bool draw_pixel = false;
                uint8_t pixel_color = 0;

                if (e->type == ENEMY_ZOMBIE) {
                    if (tex_y >= 10 && tex_y <= 22 && tex_x >= 24 && tex_x <= 40) {
                        draw_pixel = true;
                        bool ey = ((tex_x >= 27 && tex_x <= 29 && tex_y >= 15 && tex_y <= 17) ||
                                    (tex_x >= 35 && tex_x <= 37 && tex_y >= 15 && tex_y <= 17));
                        bool mouth = (tex_x >= 30 && tex_x <= 34 && tex_y >= 20 && tex_y <= 21);
                        pixel_color = ey ? 245 : (mouth ? 0 : (uint8_t)(80 + 16));
                    } else if (tex_y >= 23 && tex_y <= 50 && tex_x >= 16 && tex_x <= 48) {
                        draw_pixel = true; pixel_color = (uint8_t)(48 + 8);
                    } else if (tex_y >= 26 && tex_y <= 32 && ((tex_x >= 8 && tex_x <= 15) || (tex_x >= 49 && tex_x <= 56))) {
                        draw_pixel = true; pixel_color = (uint8_t)(80 + 12);
                    } else if (tex_y >= 51 && tex_y <= 63 && ((tex_x >= 20 && tex_x <= 28) || (tex_x >= 36 && tex_x <= 44))) {
                        draw_pixel = true; pixel_color = (uint8_t)(176 + 12);
                    }
                } else if (e->type == ENEMY_IMP) {
                    if (tex_y >= 10 && tex_y <= 22 && tex_x >= 24 && tex_x <= 40) {
                        draw_pixel = true;
                        bool ey = ((tex_x >= 27 && tex_x <= 29 && tex_y >= 15 && tex_y <= 17) ||
                                    (tex_x >= 35 && tex_x <= 37 && tex_y >= 15 && tex_y <= 17));
                        pixel_color = ey ? 245 : (uint8_t)(144 + 4);
                    } else if (tex_y >= 23 && tex_y <= 50 && tex_x >= 16 && tex_x <= 48) {
                        draw_pixel = true; pixel_color = (uint8_t)(144 + 2);
                    } else if (tex_y >= 26 && tex_y <= 32 && ((tex_x >= 8 && tex_x <= 15) || (tex_x >= 49 && tex_x <= 56))) {
                        draw_pixel = true; pixel_color = (uint8_t)(144 + 2);
                    } else if (tex_y >= 51 && tex_y <= 63 && ((tex_x >= 20 && tex_x <= 28) || (tex_x >= 36 && tex_x <= 44))) {
                        draw_pixel = true; pixel_color = (uint8_t)(144 + 1);
                    }
                } else if (e->type == ENEMY_DEMON) {
                    if (tex_y >= 10 && tex_y <= 22 && tex_x >= 22 && tex_x <= 42) {
                        draw_pixel = true;
                        bool ey = ((tex_x >= 26 && tex_x <= 28 && tex_y >= 14 && tex_y <= 16) ||
                                    (tex_x >= 36 && tex_x <= 38 && tex_y >= 14 && tex_y <= 16));
                        pixel_color = ey ? 255 : (uint8_t)(16 + 24);
                    } else if (tex_y >= 6 && tex_y <= 9 && ((tex_x >= 22 && tex_x <= 24) || (tex_x >= 40 && tex_x <= 42))) {
                        draw_pixel = true; pixel_color = (uint8_t)(144 + 16);
                    } else if (tex_y >= 23 && tex_y <= 50 && tex_x >= 12 && tex_x <= 52) {
                        draw_pixel = true; pixel_color = (uint8_t)(16 + 16);
                    } else if (tex_y >= 26 && tex_y <= 36 && ((tex_x >= 4 && tex_x <= 11) || (tex_x >= 53 && tex_x <= 60))) {
                        draw_pixel = true; pixel_color = (uint8_t)(16 + 16);
                    } else if (tex_y >= 51 && tex_y <= 63 && ((tex_x >= 16 && tex_x <= 28) || (tex_x >= 36 && tex_x <= 48))) {
                        draw_pixel = true; pixel_color = (uint8_t)(16 + 12);
                    }
                }

                if (draw_pixel)
                    doom_screen[y][x] = apply_shade(pixel_color, shade);
            }
        }
    }
}

void doom_init(void) {
    build_trig_tables();
    init_doom_palette();
    build_e1m1_map();
    build_level();
    generate_textures();
    init_enemies();
    doom_running = false;
    game_state = DOOM_MENU;
    player.z = 0; player.pitch = 0;
    player.velocity_x = 0; player.velocity_y = 0;
    player.health = 100; player.armor = 50; player.ammo = 50; player.weapon = 1;
    player.sector = 0;
}

void doom_cleanup(void) {
    doom_running = false;
    game_state = DOOM_MENU;
}

void doom_draw_frame(void) {
    memset(doom_screen, 0, sizeof(doom_screen));
    if (game_state == DOOM_MENU) {
        draw_menu();
    } else if (game_state == DOOM_PLAYING) {
        render_view();
        draw_enemies();
        draw_weapon();
        draw_muzzle_flash();
        draw_hud();
    }
    blit_to_fb();
}

void doom_run(void) {
    if (doom_running) return;
    doom_running = true;

    
    while (keyboard_getchar() != 0) yield();

    game_state = DOOM_MENU;
    doom_set_kernel_mode();
    doom_draw_frame();

    const uint32_t FRAME_INTERVAL = 16; 
    uint32_t last_frame_time = uptime_ticks;

    while (doom_running) {
        
        char c;
        int keys_processed = 0;
        while (keys_processed < 4 && (c = keyboard_getchar()) != 0) {
            keys_processed++;
            doom_handle_key((int)c);
            if (game_state == DOOM_QUIT) {
                doom_running = false;
                break;
            }
        }
        
        
        if (game_state == DOOM_PLAYING) {
            update_player();
            update_enemies();
        }

        
        uint32_t now = uptime_ticks;
        if (now - last_frame_time >= FRAME_INTERVAL) {
            doom_draw_frame();
            last_frame_time = now;
        }
        
        yield(); 
    }
    
    doom_cleanup();
    doom_restore_kernel_mode();
}

void doom_set_kernel_mode(void) {
    /* Initialize with a reasonable scale for 320x200 screen */
    doom_scale_factor = 2;
}

void doom_tick(void) {
    if (game_state == DOOM_QUIT) return;
    
    while (keyboard_getchar() != 0) {
        char c = keyboard_getchar();
        doom_handle_key((int)c);
        if (game_state == DOOM_QUIT) {
            doom_running = false;
            doom_restore_kernel_mode();
            return;
        }
    }
    
    if (game_state == DOOM_PLAYING) {
        update_player();
        update_enemies();
    }
    doom_draw_frame();
}

void doom_restore_kernel_mode(void) {
    if (current_kernel_mode == KERNEL_MODE_DESKTOP) {
        desktop.dirty = true;
        return;
    }
    terminal_initialize();
    redraw_all_panes();
    print_prompt();
}
