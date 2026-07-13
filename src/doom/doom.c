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
static int32_t z_buffer[DOOM_SCREEN_W];

#define FP_ONE 65536
static inline int32_t int_to_fp(int x) { return (int32_t)x << 16; }
static inline int fp_to_int(int32_t x) { return (int)(x >> 16); }
static inline int32_t fp_mul(int32_t a, int32_t b) {
    return (int32_t)(((int64_t)a * (int64_t)b) >> 16);
}
static inline int32_t fp_div(int32_t a, int32_t b) {
    if (b == 0) return (a >= 0) ? 0x7FFFFFFF : (int32_t)0x80000001;
    if (b < 0) { a = -a; b = -b; }
    if (a < 0) {
        uint32_t ua = (uint32_t)(-a);
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
static inline int32_t fp_abs(int32_t x) { return (x < 0) ? -x : x; }

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

static void render_view(void) {
    int half_h = (SCREEN_H - HUD_HEIGHT) / 2;
    int view_h = SCREEN_H - HUD_HEIGHT;
    int32_t px = player.x, py = player.y;
    uint32_t pa = player.angle;

    for (int i = 0; i < SCREEN_W; i++) {
        z_buffer[i] = 0x7FFFFFFF;
    }

    int player_cell_x = fp_to_int(px) / CELL_SIZE;
    int player_cell_y = fp_to_int(py) / CELL_SIZE;

    uint32_t fov_bams = 787480840;
    uint32_t half_fov = fov_bams / 2;
    uint32_t angle_step = fov_bams / SCREEN_W;

    for (int col = 0; col < SCREEN_W; col++) {
        uint32_t ray_angle = pa - half_fov + (uint32_t)col * angle_step;
        int32_t ray_dx = fp_sin(ray_angle);
        int32_t ray_dy = fp_cos(ray_angle);

        int map_x = player_cell_x;
        int map_y = player_cell_y;

        int32_t delta_x = (ray_dx == 0) ? 0x7FFFFFFF : fp_abs(fp_div(FP_ONE, ray_dx));
        int32_t delta_y = (ray_dy == 0) ? 0x7FFFFFFF : fp_abs(fp_div(FP_ONE, ray_dy));

        int step_x, step_y;
        int32_t side_dist_x, side_dist_y;

        int32_t px_in_cell = px - int_to_fp(player_cell_x * CELL_SIZE);
        int32_t py_in_cell = py - int_to_fp(player_cell_y * CELL_SIZE);

        if (ray_dx < 0) {
            step_x = -1;
            side_dist_x = fp_mul(px_in_cell, delta_x) / CELL_SIZE;
        } else {
            step_x = 1;
            side_dist_x = fp_mul(int_to_fp(CELL_SIZE) - px_in_cell, delta_x) / CELL_SIZE;
        }
        if (ray_dy < 0) {
            step_y = -1;
            side_dist_y = fp_mul(py_in_cell, delta_y) / CELL_SIZE;
        } else {
            step_y = 1;
            side_dist_y = fp_mul(int_to_fp(CELL_SIZE) - py_in_cell, delta_y) / CELL_SIZE;
        }

        int hit = 0, side = 0, wall_type = 0;
        int hit_mx = map_x, hit_my = map_y;
        int max_steps = 64;

        while (!hit && max_steps-- > 0) {
            if (side_dist_x < side_dist_y) {
                side_dist_x += fp_mul(delta_x, int_to_fp(CELL_SIZE)) / CELL_SIZE;
                map_x += step_x;
                side = 0;
            } else {
                side_dist_y += fp_mul(delta_y, int_to_fp(CELL_SIZE)) / CELL_SIZE;
                map_y += step_y;
                side = 1;
            }
            if (map_x < 0 || map_x >= MAP_W || map_y < 0 || map_y >= MAP_H) {
                break;
            }
            if (game_map[map_y][map_x] > 0) {
                hit = 1;
                wall_type = game_map[map_y][map_x];
                if (wall_type > 7) wall_type = 7;
                wall_type--;
                hit_mx = map_x;
                hit_my = map_y;
            }
        }

        int sector = player.sector;
        if (map_x >= 0 && map_x < MAP_W && map_y >= 0 && map_y < MAP_H) {
            int s = cell_sector[map_y][map_x];
            if (s >= 0 && s < num_sectors) sector = s;
        }

        if (!hit) {

            for (int y = 0; y < half_h; y++) {

                int p = half_h - y;
                if (p < 1) p = 1;
                int32_t dist = fp_div(int_to_fp(PLAYER_HEIGHT * SCREEN_W), int_to_fp(p * 2));
                int32_t fx_fp = px + fp_mul(dist, ray_dx);
                int32_t fy_fp = py + fp_mul(dist, ray_dy);
                int fx = (fp_to_int(fx_fp) % TEX_W + TEX_W) % TEX_W;
                int fy = (fp_to_int(fy_fp) % TEX_H + TEX_H) % TEX_H;
                int32_t shade = 160 - (p * 100 / half_h);
                if (shade < 30) shade = 30;
                int c = (ceil_tex[sector & 7][fy][fx] * shade) >> 8;
                if (c > 200) c = 200;
                doom_screen[y][col] = (uint8_t)c;
            }
            for (int y = half_h; y < view_h; y++) {

                int p = y - half_h;
                if (p < 1) p = 1;
                int32_t dist = fp_div(int_to_fp(PLAYER_HEIGHT * SCREEN_W), int_to_fp(p * 2));
                int32_t fx_fp = px + fp_mul(dist, ray_dx);
                int32_t fy_fp = py + fp_mul(dist, ray_dy);
                int fx = (fp_to_int(fx_fp) % TEX_W + TEX_W) % TEX_W;
                int fy = (fp_to_int(fy_fp) % TEX_H + TEX_H) % TEX_H;
                int32_t shade = 160 - (p * 100 / half_h);
                if (shade < 30) shade = 30;
                int c = (floor_tex[sector & 7][fy][fx] * shade) >> 8;
                if (c > 200) c = 200;
                doom_screen[y][col] = (uint8_t)c;
            }
            continue;
        }

        int32_t perp_dist;
        if (side == 0) {
            int32_t cell_frac = px_in_cell;
            if (step_x > 0) cell_frac = int_to_fp(CELL_SIZE) - px_in_cell;
            int32_t steps_taken = (hit_mx - player_cell_x);
            if (steps_taken < 0) steps_taken = -steps_taken;
            perp_dist = fp_div(int_to_fp(steps_taken * CELL_SIZE) + cell_frac, fp_abs(ray_dx));
        } else {
            int32_t cell_frac = py_in_cell;
            if (step_y > 0) cell_frac = int_to_fp(CELL_SIZE) - py_in_cell;
            int32_t steps_taken = (hit_my - player_cell_y);
            if (steps_taken < 0) steps_taken = -steps_taken;
            perp_dist = fp_div(int_to_fp(steps_taken * CELL_SIZE) + cell_frac, fp_abs(ray_dy));
        }
        z_buffer[col] = perp_dist;

        {
            uint32_t ray_diff = ray_angle - pa;
            int32_t cos_diff = fp_cos(ray_diff);
            if (cos_diff < 0) cos_diff = -cos_diff;
            if (cos_diff > 0) perp_dist = fp_mul(perp_dist, cos_diff);
        }

        if (perp_dist < FP_ONE / 4) perp_dist = FP_ONE / 4;

        int line_height = fp_to_int(fp_div(int_to_fp(CELL_SIZE * view_h), perp_dist));
        if (line_height > view_h * 4) line_height = view_h * 4;
        if (line_height < 1) line_height = 1;

        int draw_start = half_h - line_height / 2;
        int draw_end = half_h + line_height / 2;

        int32_t wall_hit_pos;
        if (side == 0) {
            wall_hit_pos = py + fp_mul(perp_dist, ray_dy);
        } else {
            wall_hit_pos = px + fp_mul(perp_dist, ray_dx);
        }
        int tex_x = (fp_to_int(wall_hit_pos) % TEX_W + TEX_W) % TEX_W;

        if (side == 0 && ray_dx > 0) tex_x = TEX_W - tex_x - 1;
        if (side == 1 && ray_dy < 0) tex_x = TEX_W - tex_x - 1;

        int32_t dist_units = fp_to_int(perp_dist);
        if (dist_units < 1) dist_units = 1;
        int shade = 8000 / (dist_units / CELL_SIZE + 1);
        if (shade > 255) shade = 255;
        if (shade < 40) shade = 40;

        if (side == 1) shade = shade * 3 / 4;

        int ceil_end = draw_start;
        if (ceil_end > view_h) ceil_end = view_h;
        for (int y = 0; y < ceil_end; y++) {
            int p = half_h - y;
            if (p < 1) p = 1;
            int32_t dist = fp_div(int_to_fp(PLAYER_HEIGHT * SCREEN_W), int_to_fp(p * 2));
            int32_t fx_fp = px + fp_mul(dist, ray_dx);
            int32_t fy_fp = py + fp_mul(dist, ray_dy);

            int fx = (fp_to_int(fx_fp) % TEX_W + TEX_W) % TEX_W;
            int fy = (fp_to_int(fy_fp) % TEX_H + TEX_H) % TEX_H;
            int32_t c_shade = 160 - (p * 100 / half_h);
            if (c_shade < 30) c_shade = 30;
            int c = (ceil_tex[sector & 7][fy][fx] * c_shade) >> 8;
            if (c > 200) c = 200;
            doom_screen[y][col] = (uint8_t)c;
        }

        int ds = draw_start < 0 ? 0 : draw_start;
        int de = draw_end > view_h ? view_h : draw_end;
        for (int y = ds; y < de; y++) {
            int d = (y - draw_start) * 256;
            int tex_y = (d * TEX_H / line_height) / 256;
            if (tex_y < 0) tex_y = 0;
            if (tex_y >= TEX_H) tex_y = TEX_H - 1;
            int color = wall_tex[wall_type][tex_y][tex_x];
            int shaded = (color * shade) >> 8;
            if (shaded > 255) shaded = 255;
            doom_screen[y][col] = (uint8_t)shaded;
        }

        int floor_start = draw_end;
        if (floor_start < 0) floor_start = 0;
        for (int y = floor_start; y < view_h; y++) {
            int p = y - half_h;
            if (p < 1) p = 1;
            int32_t dist = fp_div(int_to_fp(PLAYER_HEIGHT * SCREEN_W), int_to_fp(p * 2));
            int32_t fx_fp = px + fp_mul(dist, ray_dx);
            int32_t fy_fp = py + fp_mul(dist, ray_dy);

            int fx = (fp_to_int(fx_fp) % TEX_W + TEX_W) % TEX_W;
            int fy = (fp_to_int(fy_fp) % TEX_H + TEX_H) % TEX_H;
            int32_t f_shade = 160 - (p * 100 / half_h);
            if (f_shade < 30) f_shade = 30;
            int c = (floor_tex[sector & 7][fy][fx] * f_shade) >> 8;
            if (c > 200) c = 200;
            doom_screen[y][col] = (uint8_t)c;
        }
    }
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

static void draw_hud(void) {
    int hud_y = SCREEN_H - HUD_HEIGHT;

    for (int y = hud_y; y < SCREEN_H; y++) {
        uint8_t bg = (uint8_t)(2 + (y - hud_y) / 8);
        for (int x = 0; x < SCREEN_W; x++)
            doom_screen[y][x] = bg;
    }

    for (int x = 0; x < SCREEN_W; x++)
        doom_screen[hud_y][x] = 7;

    hud_str(5, hud_y + 4, "HEALTH", 250, 0);
    char buf[8];
    buf[0] = '0' + (player.health / 100) % 10;
    buf[1] = '0' + (player.health / 10) % 10;
    buf[2] = '0' + (player.health) % 10;
    buf[3] = '%'; buf[4] = '\0';

    uint8_t hcolor = 250;
    if (player.health < 50) hcolor = 248;
    if (player.health < 25) hcolor = 245;
    hud_str(5, hud_y + 16, buf, hcolor, 0);

    hud_str(75, hud_y + 4, "ARMOR", 251, 0);
    buf[0] = '0' + (player.armor / 100) % 10;
    buf[1] = '0' + (player.armor / 10) % 10;
    buf[2] = '0' + (player.armor) % 10;
    buf[3] = '%'; buf[4] = '\0';
    hud_str(75, hud_y + 16, buf, 251, 0);

    hud_str(150, hud_y + 4, "AMMO", 252, 0);
    buf[0] = '0' + (player.ammo / 100) % 10;
    buf[1] = '0' + (player.ammo / 10) % 10;
    buf[2] = '0' + (player.ammo) % 10;
    buf[3] = '\0';
    hud_str(150, hud_y + 16, buf, 252, 0);

    const char* weapons[] = {"FIST", "PISTOL", "SHOTGUN", "CHAINGUN", "ROCKET"};
    int w_idx = player.weapon;
    if (w_idx < 0) w_idx = 0;
    if (w_idx > 4) w_idx = 4;
    hud_str(SCREEN_W - 72, hud_y + 4, "ARMS", 253, 0);
    hud_str(SCREEN_W - 72, hud_y + 16, weapons[w_idx], 255, 0);

    int hx = SCREEN_W / 2, hy = (SCREEN_H - HUD_HEIGHT) / 2;
    if (hy > 0 && hy < SCREEN_H - HUD_HEIGHT) {
        for (int i = -3; i <= 3; i++) {
            if (hx + i >= 0 && hx + i < SCREEN_W)
                doom_screen[hy][hx + i] = 250;
            if (hy + i >= 0 && hy + i < SCREEN_H - HUD_HEIGHT)
                doom_screen[hy + i][hx] = 250;
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
            if (game_map[iy][ix] > 0) return true;
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
                    if (game_map[cy][cx] > 0) { los = false; break; }
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
            if (game_map[iy][ix] > 0) return true;
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

    int mx = fp_to_int(player.x) / CELL_SIZE;
    int my = fp_to_int(player.y) / CELL_SIZE;
    if (mx >= 0 && mx < MAP_W && my >= 0 && my < MAP_H) {
        int s = cell_sector[my][mx];
        if (s >= 0 && s < num_sectors) player.sector = s;
    }

}

static void draw_enemies(void) {
    int32_t cos_a = fp_cos(player.angle);
    int32_t sin_a = fp_sin(player.angle);
    int32_t cos_a90 = fp_cos(player.angle + ANG90);
    int32_t sin_a90 = fp_sin(player.angle + ANG90);
    int32_t inv_det = fp_div(FP_ONE, fp_mul(cos_a, sin_a90) - fp_mul(sin_a, cos_a90));

    for (int i = 0; i < num_enemies; i++) {
        enemy_t *e = &enemies[i];
        if (!e->active) continue;

        int32_t dx = e->x - player.x;
        int32_t dy = e->y - player.y;

        int32_t t1 = fp_mul(cos_a, dx) - fp_mul(sin_a, dy);
        int32_t t2 = fp_mul(sin_a90, dx) + fp_mul(cos_a90, dy);
        int32_t transform_x = fp_mul(t1, inv_det);
        int32_t transform_y = fp_mul(t2, inv_det);

        if (transform_y <= 0) continue;

        int screen_x = SCREEN_W / 2 + (fp_div(transform_x, transform_y) * (SCREEN_W / 2) >> 16);
        int sprite_h = 13120000 / transform_y;
        if (sprite_h > SCREEN_H) sprite_h = SCREEN_H;

        int draw_start_y = (SCREEN_H - HUD_HEIGHT) / 2 - sprite_h / 2;
        int draw_end_y = draw_start_y + sprite_h;
        int draw_start_x = screen_x - sprite_h / 2;
        int draw_end_x = draw_start_x + sprite_h;

        int shade_base = fp_to_int(transform_y * 2);

        for (int y = draw_start_y; y < draw_end_y; y++) {
            if (y < 0 || y >= SCREEN_H - HUD_HEIGHT) continue;
            for (int x = draw_start_x; x < draw_end_x; x++) {
                if (x < 0 || x >= SCREEN_W) continue;
                if (transform_y >= z_buffer[x]) continue;

                int tex_x = (x - draw_start_x) * 64 / sprite_h;
                int tex_y = (y - draw_start_y) * 64 / sprite_h;

                bool draw_pixel = false;
                uint8_t pixel_color = 0;

                if (e->type == ENEMY_ZOMBIE) {
                    if (tex_y >= 10 && tex_y <= 22 && tex_x >= 24 && tex_x <= 40) {
                        draw_pixel = true;
                        bool eye = ((tex_x >= 27 && tex_x <= 29 && tex_y >= 15 && tex_y <= 17) ||
                                    (tex_x >= 35 && tex_x <= 37 && tex_y >= 15 && tex_y <= 17));
                        bool mouth = (tex_x >= 30 && tex_x <= 34 && tex_y >= 20 && tex_y <= 21);
                        if (eye) {
                            pixel_color = 245;
                        } else if (mouth) {
                            pixel_color = 0;
                        } else {
                            pixel_color = 80 + 16;
                        }
                    } else if (tex_y >= 23 && tex_y <= 50 && tex_x >= 16 && tex_x <= 48) {
                        draw_pixel = true;
                        pixel_color = 48 + 8;
                    } else if (tex_y >= 26 && tex_y <= 32 && ((tex_x >= 8 && tex_x <= 15) || (tex_x >= 49 && tex_x <= 56))) {
                        draw_pixel = true;
                        pixel_color = 80 + 12;
                    } else if (tex_y >= 51 && tex_y <= 63 && ((tex_x >= 20 && tex_x <= 28) || (tex_x >= 36 && tex_x <= 44))) {
                        draw_pixel = true;
                        pixel_color = 176 + 12;
                    }
                } else if (e->type == ENEMY_IMP) {
                    if (tex_y >= 10 && tex_y <= 22 && tex_x >= 24 && tex_x <= 40) {
                        draw_pixel = true;
                        bool eye = ((tex_x >= 27 && tex_x <= 29 && tex_y >= 15 && tex_y <= 17) ||
                                    (tex_x >= 35 && tex_x <= 37 && tex_y >= 15 && tex_y <= 17));
                        if (eye) {
                            pixel_color = 245;
                        } else {
                            pixel_color = 144 + 4;
                        }
                    } else if (tex_y >= 23 && tex_y <= 50 && tex_x >= 16 && tex_x <= 48) {
                        draw_pixel = true;
                        pixel_color = 144 + 2;
                    } else if (tex_y >= 26 && tex_y <= 32 && ((tex_x >= 8 && tex_x <= 15) || (tex_x >= 49 && tex_x <= 56))) {
                        draw_pixel = true;
                        pixel_color = 144 + 2;
                    } else if (tex_y >= 51 && tex_y <= 63 && ((tex_x >= 20 && tex_x <= 28) || (tex_x >= 36 && tex_x <= 44))) {
                        draw_pixel = true;
                        pixel_color = 144 + 1;
                    }
                } else if (e->type == ENEMY_DEMON) {
                    if (tex_y >= 10 && tex_y <= 22 && tex_x >= 22 && tex_x <= 42) {
                        draw_pixel = true;
                        bool eye = ((tex_x >= 26 && tex_x <= 28 && tex_y >= 14 && tex_y <= 16) ||
                                    (tex_x >= 36 && tex_x <= 38 && tex_y >= 14 && tex_y <= 16));
                        if (eye) {
                            pixel_color = 255;
                        } else {
                            pixel_color = 16 + 24;
                        }
                    } else if (tex_y >= 6 && tex_y <= 9 && ((tex_x >= 22 && tex_x <= 24) || (tex_x >= 40 && tex_x <= 42))) {
                        draw_pixel = true;
                        pixel_color = 144 + 16;
                    } else if (tex_y >= 23 && tex_y <= 50 && tex_x >= 12 && tex_x <= 52) {
                        draw_pixel = true;
                        pixel_color = 16 + 16;
                    } else if (tex_y >= 26 && tex_y <= 36 && ((tex_x >= 4 && tex_x <= 11) || (tex_x >= 53 && tex_x <= 60))) {
                        draw_pixel = true;
                        pixel_color = 16 + 16;
                    } else if (tex_y >= 51 && tex_y <= 63 && ((tex_x >= 16 && tex_x <= 28) || (tex_x >= 36 && tex_x <= 48))) {
                        draw_pixel = true;
                        pixel_color = 16 + 12;
                    }
                }

                if (draw_pixel) {
                    int shade = 255 - shade_base;
                    if (shade < 40) shade = 40;
                    int final_color;
                    if (pixel_color >= 16 && pixel_color < 240) {
                        int offset = (pixel_color - 16) & 31;
                        final_color = pixel_color - offset + ((offset * shade) >> 8);
                    } else {
                        final_color = (pixel_color * shade) >> 8;
                    }
                    if (final_color > 255) final_color = 255;
                    doom_screen[y][x] = (uint8_t)final_color;
                }
            }
        }
    }
}

void doom_init(void) {
    build_trig_tables();
    init_doom_palette();
    build_e1m1_map();
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
    for (int i = 0; i < SCREEN_W; i++) {
        z_buffer[i] = 0x7FFFFFFF;
    }
    memset(doom_screen, 0, sizeof(doom_screen));
    if (game_state == DOOM_MENU) {
        draw_menu();
    } else if (game_state == DOOM_PLAYING) {
        render_view();
        draw_enemies();
        draw_weapon();
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
