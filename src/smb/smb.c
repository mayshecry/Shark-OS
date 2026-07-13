#include "kernel.h"
#include "smb.h"
#include "desktop.h"

uint8_t smb_screen[SMB_SCREEN_H][SMB_SCREEN_W];
uint32_t smb_palette[256];

#define COL_SKY    144
#define COL_GROUND 80
#define COL_GRASS  48
#define COL_BRICK  112
#define COL_PIPE   48
#define COL_PBRIM  112
#define COL_QB     244
#define COL_MARIO  240
#define COL_GOOMBA 244
#define COL_EYE    255
#define COL_BLACK  0
#define COL_WHITE  15
#define COL_GREY   8
#define COL_COIN   252
#define COL_FLAG   250
#define COL_RED    244

#define TILE_AIR    0
#define TILE_BRICK  1
#define TILE_GROUND 2
#define TILE_QB     3
#define TILE_PIPE_TL 4
#define TILE_PIPE_TR 5
#define TILE_PIPE_BL 6
#define TILE_PIPE_BR 7
#define TILE_FLAG    8
#define TILE_GRASS   9

#define FX_SHIFT 8
#define FX_ONE (1 << FX_SHIFT)
#define to_fx(x) ((x) * FX_ONE)
#define to_int(x) ((x) >> FX_SHIFT)
#define fx_mul(a,b) (((int)(a)) * ((int)(b)) >> FX_SHIFT)

#define LEVEL_W 220
#define LEVEL_H 15
#define TILE_SZ 16
#define SCREEN_TILES_W 20
#define SCREEN_TILES_H 12

#define MAX_GOOMBAS 32
#define MAX_COINS 16

typedef struct {
    int x_fx, y_fx;
    int vx_fx, vy_fx;
    int on_ground;
    int facing;
    int lives;
    int coins;
    int anim_frame;
    int anim_timer;
    int can_jump;
    int dead_timer;
} player_t;

typedef struct {
    int x_fx, y_fx;
    int alive;
    int anim_frame;
    int vx_fx;
} goomba_t;

typedef struct {
    int x, y;
    int collected;
} coin_t;

static uint8_t level[LEVEL_H][LEVEL_W];
static int window_x = 0, window_y = 0, window_w = 640, window_h = 480;
static player_t player;
static goomba_t goombas[MAX_GOOMBAS];
static int goomba_count;
static int unused_coins;
static int camera_x;
static int level_end_x;
static int score;
static int running;
static int game_state;
static int timer;

#define set_px(x,y,c) do { \
    if ((x) >= 0 && (x) < 320 && (y) >= 0 && (y) < 200) \
        smb_screen[y][x] = (c); \
} while(0)

static void fill_rect(int x, int y, int w, int h, uint8_t c) {
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++)
            set_px(x + i, y + j, c);
}

static void draw_chr(int x, int y, char c, uint8_t fg) {
    extern uint8_t font8x8[96][8];
    unsigned char uc = (unsigned char)c;
    if (uc < 32 || uc >= 128) return;
    int idx = uc - 32;
    for (int r = 0; r < 8; r++) {
        uint8_t bits = font8x8[idx][r];
        for (int b = 0; b < 8; b++) {
            if (bits & (0x80 >> b))
                set_px(x + b, y + r, fg);
        }
    }
}

static void draw_num(int x, int y, int val, uint8_t c) {
    char tmp[8];
    int len = 0;
    if (val == 0) { tmp[len++] = '0'; }
    else { int v = val; while (v > 0 && len < 7) { tmp[len++] = '0' + (v % 10); v /= 10; } }
    for (int i = len - 1; i >= 0; i--) { draw_chr(x, y, tmp[i], c); x += 8; }
}

static void draw_str(int x, int y, const char* s, uint8_t c) {
    for (int i = 0; s[i]; i++) { draw_chr(x + i*8, y, s[i], c); }
}

static void blit(void);

static void build_pal(void) {
    for (int i = 0; i < 256; i++) smb_palette[i] = 0;
    smb_palette[0]   = 0x000000;
    smb_palette[8]   = 0x555555;
    smb_palette[15]  = 0xFFFFFF;
    smb_palette[COL_SKY]    = 0x6185F8;
    smb_palette[COL_GROUND] = 0x8B4513;
    smb_palette[COL_GRASS]  = 0x228B22;
    smb_palette[COL_BRICK]  = 0xC84C0C;
    smb_palette[COL_PIPE]   = 0x00AA00;
    smb_palette[COL_PBRIM]  = 0x006600;
    smb_palette[COL_QB]     = 0xDAA520;
    smb_palette[COL_MARIO]  = 0xE82020;
    smb_palette[COL_GOOMBA] = 0x8B4513;
    smb_palette[COL_EYE]    = 0xFFFFFF;
    smb_palette[COL_COIN]   = 0xFFD700;
    smb_palette[COL_FLAG]   = 0x00FF00;
    smb_palette[COL_RED]    = 0xE82020;
    for (int i = 0; i < 256; i++)
        if (smb_palette[i] == 0 && i != 0) smb_palette[i] = 0x333333;
    smb_palette[0] = 0x000000;
}

static void set_tile(int tx, int ty, uint8_t t) {
    if (tx >= 0 && tx < LEVEL_W && ty >= 0 && ty < LEVEL_H)
        level[ty][tx] = t;
}

static uint8_t get_tile(int tx, int ty) {
    if (tx < 0 || tx >= LEVEL_W || ty < 0 || ty >= LEVEL_H) return TILE_AIR;
    return level[ty][tx];
}

static int is_solid(int tx, int ty) {
    uint8_t t = get_tile(tx, ty);
    return t == TILE_BRICK || t == TILE_GROUND ||
           t == TILE_QB ||
           t == TILE_PIPE_TL || t == TILE_PIPE_TR ||
           t == TILE_PIPE_BL || t == TILE_PIPE_BR;
}

static void build_level(void) {
    memset(level, 0, sizeof(level));

    for (int x = 0; x < LEVEL_W; x++) {
        set_tile(x, 13, TILE_GROUND);
        set_tile(x, 14, TILE_GROUND);
    }

    for (int x = 0; x < LEVEL_W; x++)
        level[12][x] = TILE_GRASS;

    for (int x = 5; x <= 7; x++) { set_tile(x, 9, TILE_BRICK); }
    set_tile(6, 8, TILE_QB);

    set_tile(14, 11, TILE_PIPE_TL); set_tile(15, 11, TILE_PIPE_TR);
    set_tile(14, 12, TILE_PIPE_BL); set_tile(15, 12, TILE_PIPE_BR);

    for (int x = 18; x <= 22; x++) set_tile(x, 7, TILE_BRICK);
    set_tile(19, 6, TILE_QB);
    set_tile(21, 6, TILE_QB);

    for (int s = 0; s < 5; s++)
        for (int x = 28; x <= 28 + s; x++)
            set_tile(x, 12 - s, TILE_BRICK);

    set_tile(36, 10, TILE_PIPE_TL); set_tile(37, 10, TILE_PIPE_TR);
    set_tile(36, 11, TILE_PIPE_BL); set_tile(37, 11, TILE_PIPE_BR);
    set_tile(36, 12, TILE_PIPE_BL); set_tile(37, 12, TILE_PIPE_BR);

    set_tile(42, 11, TILE_PIPE_TL); set_tile(43, 11, TILE_PIPE_TR);
    set_tile(42, 12, TILE_PIPE_BL); set_tile(43, 12, TILE_PIPE_BR);

    for (int x = 48; x <= 55; x++) set_tile(x, 9, TILE_BRICK);
    set_tile(50, 7, TILE_QB);
    set_tile(53, 7, TILE_QB);

    for (int s = 0; s < 8; s++)
        for (int x = 62; x <= 62 + s; x++)
            set_tile(x, 12 - s, TILE_BRICK);

    for (int x = 74; x <= 80; x++) set_tile(x, 8, TILE_BRICK);
    set_tile(76, 6, TILE_QB);

    set_tile(84, 10, TILE_PIPE_TL); set_tile(85, 10, TILE_PIPE_TR);
    set_tile(84, 11, TILE_PIPE_BL); set_tile(85, 11, TILE_PIPE_BR);
    set_tile(84, 12, TILE_PIPE_BL); set_tile(85, 12, TILE_PIPE_BR);

    for (int x = 92; x <= 96; x++) set_tile(x, 10, TILE_BRICK);
    set_tile(93, 8, TILE_QB);
    set_tile(95, 8, TILE_QB);

    for (int x = 100; x <= 106; x++) set_tile(x, 7, TILE_BRICK);

    for (int s = 0; s < 8; s++)
        for (int x = 112; x <= 112 + s; x++)
            set_tile(x, 12 - s, TILE_BRICK);

    set_tile(120, 9, TILE_PIPE_TL); set_tile(121, 9, TILE_PIPE_TR);
    set_tile(120, 10, TILE_PIPE_BL); set_tile(121, 10, TILE_PIPE_BR);
    set_tile(120, 11, TILE_PIPE_BL); set_tile(121, 11, TILE_PIPE_BR);
    set_tile(120, 12, TILE_PIPE_BL); set_tile(121, 12, TILE_PIPE_BR);

    for (int x = 128; x <= 132; x++) set_tile(x, 9, TILE_BRICK);

    for (int x = 140; x <= 142; x++) set_tile(x, 12, TILE_BRICK);
    set_tile(141, 2, TILE_FLAG);
    set_tile(141, 3, TILE_FLAG);
    set_tile(141, 4, TILE_FLAG);
    set_tile(141, 5, TILE_FLAG);
    set_tile(141, 6, TILE_FLAG);
    set_tile(141, 7, TILE_FLAG);
    set_tile(141, 8, TILE_FLAG);
    set_tile(141, 9, TILE_FLAG);
    set_tile(141, 10, TILE_FLAG);
    set_tile(141, 11, TILE_FLAG);

    for (int x = 147; x <= 153; x++) set_tile(x, 10, TILE_BRICK);
    for (int x = 148; x <= 152; x++) set_tile(x, 11, TILE_BRICK);
    set_tile(150, 7, TILE_BRICK);
    set_tile(150, 8, TILE_BRICK);
    set_tile(150, 9, TILE_BRICK);
    set_tile(149, 7, TILE_BRICK);
    set_tile(151, 7, TILE_BRICK);

    level_end_x = 148 * TILE_SZ;
}

static void spawn_goomba(int tx, int ty) {
    if (goomba_count >= MAX_GOOMBAS) return;
    goombas[goomba_count].x_fx = to_fx(tx * TILE_SZ);
    goombas[goomba_count].y_fx = to_fx(ty * TILE_SZ);
    goombas[goomba_count].alive = 1;
    goombas[goomba_count].vx_fx = to_fx(-1);
    goombas[goomba_count].anim_frame = 0;
    goomba_count++;
}

static void init_enemies(void) {
    goomba_count = 0;
    spawn_goomba(10, 12);
    spawn_goomba(21, 12);
    spawn_goomba(30, 12);
    spawn_goomba(38, 12);
    spawn_goomba(50, 12);
    spawn_goomba(55, 12);
    spawn_goomba(65, 12);
    spawn_goomba(76, 12);
    spawn_goomba(82, 12);
    spawn_goomba(94, 12);
    spawn_goomba(102, 12);
    spawn_goomba(108, 12);
    spawn_goomba(115, 12);
    spawn_goomba(125, 12);
}

static void init_player(void) {
    player.x_fx = to_fx(3 * TILE_SZ);
    player.y_fx = to_fx(10 * TILE_SZ);
    player.vx_fx = 0;
    player.vy_fx = 0;
    player.on_ground = 0;
    player.facing = 1;
    player.lives = 3;
    player.coins = 0;
    player.anim_frame = 0;
    player.anim_timer = 0;
    player.can_jump = 1;
    player.dead_timer = 0;
}

static int check_tile_collision(int x_fx, int y_fx, int w, int h) {
    int tx1 = to_int(x_fx) / TILE_SZ;
    int ty1 = to_int(y_fx) / TILE_SZ;
    int tx2 = to_int(x_fx + to_fx(w - 1)) / TILE_SZ;
    int ty2 = to_int(y_fx + to_fx(h - 1)) / TILE_SZ;
    for (int ty = ty1; ty <= ty2; ty++)
        for (int tx = tx1; tx <= tx2; tx++)
            if (is_solid(tx, ty)) return 1;
    return 0;
}

static void update_player(void) {
    if (player.dead_timer > 0) {
        player.dead_timer--;
        if (player.dead_timer == 0) {
            player.lives--;
            if (player.lives > 0) {
                init_player();
                camera_x = 0;
            } else {
                game_state = 2;
            }
        }
        return;
    }

    player.x_fx += player.vx_fx;

    if (player.vx_fx > 0) player.facing = 1;
    else if (player.vx_fx < 0) player.facing = -1;

    if (check_tile_collision(player.x_fx, player.y_fx, 12, 16)) {
        if (player.vx_fx > 0) {
            player.x_fx = to_int(player.x_fx) / TILE_SZ * TILE_SZ + TILE_SZ - 1 - 12;
            player.x_fx = to_fx(player.x_fx);
        } else {
            player.x_fx = to_int(player.x_fx) / TILE_SZ * TILE_SZ + TILE_SZ;
            player.x_fx = to_fx(player.x_fx);
        }
        player.x_fx -= player.vx_fx;
        player.vx_fx = 0;
    }

    player.vy_fx += to_fx(1);
    if (player.vy_fx > to_fx(8)) player.vy_fx = to_fx(8);

    player.y_fx += player.vy_fx;

    if (check_tile_collision(player.x_fx, player.y_fx, 12, 16)) {
        if (player.vy_fx > 0) {

            int ny = to_int(player.y_fx) / TILE_SZ * TILE_SZ;
            player.y_fx = to_fx(ny);
            player.vy_fx = 0;
            player.on_ground = 1;
            player.can_jump = 1;
        } else {

            int ny = to_int(player.y_fx + to_fx(16 - 1)) / TILE_SZ * TILE_SZ + TILE_SZ - 16;
            player.y_fx = to_fx(ny);
            player.vy_fx = 0;
            player.on_ground = 0;
        }
    }

    if (check_tile_collision(player.x_fx + to_fx(1), player.y_fx + to_fx(17), 10, 1)) {
        player.on_ground = 1;
        player.can_jump = 1;
    } else {
        player.on_ground = 0;
    }

    player.anim_timer++;
    if (player.anim_timer > 8) {
        player.anim_timer = 0;
        player.anim_frame = (player.anim_frame + 1) % 4;
    }

    if (to_int(player.y_fx) > 240) {
        player.dead_timer = 60;
    }

    for (int i = 0; i < goomba_count; i++) {
        if (!goombas[i].alive) continue;
        int gx = to_int(goombas[i].x_fx);
        int gy = to_int(goombas[i].y_fx);
        int px = to_int(player.x_fx);
        int py = to_int(player.y_fx);

        if (px + 12 > gx && px < gx + 16 && py + 16 > gy && py < gy + 16) {

            if (player.vy_fx > 0 && py + 16 - gy < 12) {
                goombas[i].alive = 0;
                player.vy_fx = to_fx(-6);
                score += 100;
            } else {

                player.dead_timer = 60;
                player.vy_fx = to_fx(-6);
            }
        }
    }

    {
        int px = to_int(player.x_fx) / TILE_SZ;
        int py = to_int(player.y_fx) / TILE_SZ;
        if (get_tile(px, py) == TILE_FLAG || get_tile(px, py + 1) == TILE_FLAG) {
            game_state = 3;
            score += 1000;
        }
    }

    camera_x = to_int(player.x_fx) - 160;
    if (camera_x < 0) camera_x = 0;
    if (camera_x > LEVEL_W * TILE_SZ - 320) camera_x = LEVEL_W * TILE_SZ - 320;
}

static void update_goombas(void) {
    for (int i = 0; i < goomba_count; i++) {
        if (!goombas[i].alive) continue;

        goombas[i].x_fx += goombas[i].vx_fx;

        int gtx = to_int(goombas[i].x_fx) / TILE_SZ;
        int gty = to_int(goombas[i].y_fx) / TILE_SZ + 1;

        if (goombas[i].vx_fx > 0) gtx += 2;
        else gtx -= 1;

        if (!is_solid(gtx, gty) || is_solid(gtx + (goombas[i].vx_fx > 0 ? 1 : -1), gty - 1)) {
            goombas[i].vx_fx = -goombas[i].vx_fx;
        }

        goombas[i].y_fx += to_fx(2);

        if (to_int(goombas[i].y_fx) > 12 * TILE_SZ) {
            goombas[i].y_fx = to_fx(12 * TILE_SZ);
        }
    }
}

#define VIEW_Y_OFFSET 40

static void draw_tile(int screen_x, int screen_y, uint8_t tile) {
    switch (tile) {
        case TILE_BRICK:
            fill_rect(screen_x, screen_y, 16, 16, COL_BRICK);

            fill_rect(screen_x, screen_y, 8, 8, COL_BRICK - 5);
            fill_rect(screen_x + 8, screen_y + 8, 8, 8, COL_BRICK - 5);

            set_px(screen_x + 7, screen_y, COL_BLACK);
            set_px(screen_x + 15, screen_y + 8, COL_BLACK);
            set_px(screen_x + 7, screen_y + 16, COL_BLACK);
            break;
        case TILE_GROUND:
            fill_rect(screen_x, screen_y, 16, 16, COL_GROUND);
            for (int j = 0; j < 4; j++)
                for (int i = 0; i < 2; i++)
                    set_px(screen_x + 3 + i*8, screen_y + 3 + j*4, 0);
            break;
        case TILE_QB:
            fill_rect(screen_x, screen_y, 16, 16, COL_QB);

            set_px(screen_x + 6, screen_y + 3, COL_WHITE);
            set_px(screen_x + 7, screen_y + 3, COL_WHITE);
            set_px(screen_x + 8, screen_y + 3, COL_WHITE);
            set_px(screen_x + 9, screen_y + 4, COL_WHITE);
            set_px(screen_x + 9, screen_y + 5, COL_WHITE);
            set_px(screen_x + 8, screen_y + 6, COL_WHITE);
            set_px(screen_x + 7, screen_y + 7, COL_WHITE);
            set_px(screen_x + 7, screen_y + 9, COL_WHITE);

            for (int i = 0; i < 16; i++) {
                set_px(screen_x + i, screen_y, COL_BLACK);
                set_px(screen_x + i, screen_y + 15, COL_BLACK);
                set_px(screen_x, screen_y + i, COL_BLACK);
                set_px(screen_x + 15, screen_y + i, COL_BLACK);
            }
            break;
        case TILE_PIPE_TL:
            fill_rect(screen_x, screen_y, 16, 8, COL_PBRIM);
            fill_rect(screen_x, screen_y + 8, 16, 8, COL_PIPE);
            set_px(screen_x, screen_y, COL_BLACK);
            break;
        case TILE_PIPE_TR:
            fill_rect(screen_x, screen_y, 16, 8, COL_PBRIM);
            fill_rect(screen_x, screen_y + 8, 16, 8, COL_PIPE);
            set_px(screen_x + 15, screen_y, COL_BLACK);
            break;
        case TILE_PIPE_BL:
            fill_rect(screen_x, screen_y, 16, 16, COL_PIPE);
            fill_rect(screen_x, screen_y, 3, 16, COL_PBRIM);
            break;
        case TILE_PIPE_BR:
            fill_rect(screen_x, screen_y, 16, 16, COL_PIPE);
            fill_rect(screen_x + 13, screen_y, 3, 16, COL_PBRIM);
            break;
        case TILE_FLAG:
            fill_rect(screen_x + 7, screen_y, 2, 16, 8);
            break;
        case TILE_GRASS:
            fill_rect(screen_x, screen_y, 16, 16, COL_GRASS);
            break;
        default:
            break;
    }
}

static void draw_mario(int screen_x, int screen_y) {

    if (player.dead_timer > 0 && player.dead_timer % 4 < 2) return;

    fill_rect(screen_x + 2, screen_y + 4, 8, 8, COL_MARIO);

    fill_rect(screen_x + 3, screen_y, 6, 5, 15 - 3);

    fill_rect(screen_x + 2, screen_y, 8, 3, COL_MARIO);

    if (player.facing > 0) {
        set_px(screen_x + 7, screen_y + 1, COL_BLACK);
        fill_rect(screen_x + 8, screen_y + 3, 2, 2, 15 - 3);
    } else {
        set_px(screen_x + 6, screen_y + 1, COL_BLACK);
        fill_rect(screen_x + 4, screen_y + 3, 2, 2, 15 - 3);
    }

    if (player.on_ground && player.vx_fx != 0) {

        if (player.anim_frame % 2 == 0) {
            fill_rect(screen_x + 2, screen_y + 12, 5, 4, COL_BLACK);
            fill_rect(screen_x + 7, screen_y + 12, 4, 4, COL_MARIO);
        } else {
            fill_rect(screen_x + 1, screen_y + 12, 4, 4, COL_MARIO);
            fill_rect(screen_x + 6, screen_y + 12, 5, 4, COL_BLACK);
        }
    } else {
        fill_rect(screen_x + 2, screen_y + 12, 8, 4, COL_MARIO);
        fill_rect(screen_x + 1, screen_y + 14, 3, 2, COL_BLACK);
        fill_rect(screen_x + 8, screen_y + 14, 3, 2, COL_BLACK);
    }

    fill_rect(screen_x + 3, screen_y + 8, 2, 4, 0);
    fill_rect(screen_x + 7, screen_y + 8, 2, 4, 0);
}

static void draw_goomba(int screen_x, int screen_y) {

    fill_rect(screen_x + 2, screen_y + 4, 12, 8, COL_GOOMBA);

    fill_rect(screen_x + 1, screen_y + 2, 14, 6, COL_GOOMBA);
    fill_rect(screen_x, screen_y + 4, 16, 6, COL_GOOMBA);

    fill_rect(screen_x + 3, screen_y + 4, 4, 4, COL_WHITE);
    fill_rect(screen_x + 9, screen_y + 4, 4, 4, COL_WHITE);
    set_px(screen_x + 5, screen_y + 5, COL_BLACK);
    set_px(screen_x + 5, screen_y + 6, COL_BLACK);
    set_px(screen_x + 11, screen_y + 5, COL_BLACK);
    set_px(screen_x + 11, screen_y + 6, COL_BLACK);

    set_px(screen_x + 3, screen_y + 3, COL_BLACK);
    set_px(screen_x + 4, screen_y + 3, COL_BLACK);
    set_px(screen_x + 10, screen_y + 3, COL_BLACK);
    set_px(screen_x + 11, screen_y + 3, COL_BLACK);

    fill_rect(screen_x + 1, screen_y + 12, 5, 4, COL_BLACK);
    fill_rect(screen_x + 10, screen_y + 12, 5, 4, COL_BLACK);
}

static void draw_flagpole(void) {

    int fx = 141 * TILE_SZ - camera_x;
    if (fx < -20 || fx > 340) return;

    fill_rect(fx + 7, 2 * TILE_SZ - VIEW_Y_OFFSET, 2, 10 * TILE_SZ, 8);

    fill_rect(fx + 9, 3 * TILE_SZ - VIEW_Y_OFFSET, 16, 10, COL_FLAG);

    fill_rect(fx + 5, 2 * TILE_SZ - VIEW_Y_OFFSET - 4, 6, 6, COL_COIN);
}

static void draw_hud(void) {

    fill_rect(0, 0, 320, 14, 1);
    for (int i = 0; i < 320; i++) set_px(i, 14, COL_GREY);

    draw_str(4, 3, "SCORE", COL_WHITE);
    draw_num(4, 7, score, COL_WHITE);

    draw_str(140, 3, "COINS", COL_COIN);
    draw_num(140, 7, player.coins, COL_COIN);

    draw_str(250, 3, "LIVES", COL_RED);
    draw_num(250, 7, player.lives, COL_RED);
}

static void draw_scene(void) {

    fill_rect(0, 0, 320, 200, COL_SKY);

    int start_tx = camera_x / TILE_SZ;
    int end_tx = (camera_x + 320) / TILE_SZ + 1;
    if (end_tx > LEVEL_W) end_tx = LEVEL_W;

    for (int ty = 0; ty < LEVEL_H; ty++) {
        for (int tx = start_tx; tx < end_tx; tx++) {
            uint8_t t = get_tile(tx, ty);
            if (t != TILE_AIR && t != TILE_FLAG && t != TILE_GRASS) {
                int sx = tx * TILE_SZ - camera_x;
                int sy = ty * TILE_SZ - VIEW_Y_OFFSET;
                draw_tile(sx, sy, t);
            }
        }
    }

    for (int tx = start_tx; tx < end_tx; tx++) {
        if (get_tile(tx, 12) == TILE_GRASS) {
            int sx = tx * TILE_SZ - camera_x;
            fill_rect(sx, 12 * TILE_SZ - VIEW_Y_OFFSET, 16, 16, COL_GRASS);
        }
    }

    draw_flagpole();

    for (int i = 0; i < goomba_count; i++) {
        if (!goombas[i].alive) continue;
        int gx = to_int(goombas[i].x_fx) - camera_x;
        int gy = to_int(goombas[i].y_fx) - VIEW_Y_OFFSET;
        if (gx > -20 && gx < 340) {
            draw_goomba(gx, gy);
        }
    }

    int mx = to_int(player.x_fx) - camera_x;
    int my = to_int(player.y_fx) - VIEW_Y_OFFSET;
    draw_mario(mx, my);

    draw_hud();
}

static void draw_menu(void) {
    fill_rect(0, 0, 320, 200, COL_SKY);

    draw_str(320/2 - 5*8, 40, "SUPER", COL_WHITE);
    draw_str(320/2 - 7*8, 52, "MARIO BROS", COL_RED);
    draw_str(320/2 - 8*8, 68, "SharkOS Edition", COL_COIN);

    draw_mario(320/2 - 6, 88);

    draw_str(320/2 - 10*8, 115, "Press SPACE to start", COL_WHITE);
    draw_str(320/2 - 5*8, 127, "ESC to quit", COL_GREY);

    draw_str(320/2 - 8*8, 145, "ARROWS: Move/Jump", COL_COIN);

    blit();
}

static void draw_gameover(void) {
    fill_rect(0, 0, 320, 200, COL_BLACK);

    draw_str(320/2 - 5*8, 70, "GAME OVER", COL_RED);
    draw_str(320/2 - 5*8, 90, "SCORE", COL_WHITE);
    draw_num(320/2 - 2*8, 100, score, COL_WHITE);
    draw_str(320/2 - 7*8, 130, "Press SPACE", COL_GREY);

    blit();
}

static void draw_win(void) {
    fill_rect(0, 0, 320, 200, COL_SKY);

    draw_str(320/2 - 6*8, 60, "YOU WIN!", COL_COIN);
    draw_str(320/2 - 5*8, 80, "SCORE", COL_WHITE);
    draw_num(320/2 - 2*8, 90, score, COL_WHITE);
    draw_str(320/2 - 7*8, 120, "Press SPACE", COL_GREY);

    blit();
}

static void blit(void) {
    uint32_t stride = (uint32_t)(screen_pitch / 4);
    int scale = 2;  /* Fixed scale for 320x200 to 640x400 */
    if (scale < 1) scale = 1;

    for (int y = 0; y < 200 && y * scale < window_h; y++) {
        for (int sy = 0; sy < scale && y * scale + sy < window_h; sy++) {
            int fy = window_y + y * scale + sy;
            if (fy < 0) continue;
            if (fy >= (int)screen_height) continue;
            for (int x = 0; x < 320 && x * scale < window_w; x++) {
                uint32_t color = smb_palette[smb_screen[y][x]];
                for (int sx = 0; sx < scale && x * scale + sx < window_w; sx++) {
                    int fx = window_x + x * scale + sx;
                    if (fx < 0) continue;
                    if (fx >= (int)screen_width) continue;
                    lfbptr[fy * stride + fx] = color;
                }
            }
        }
    }
}

void smb_set_window_rect(int x, int y, int w, int h) {
    window_x = x;
    window_y = y;
    window_w = w;
    window_h = h;
}

static void render(void) {
    if (game_state == 0) draw_menu();
    else if (game_state == 1) draw_scene();
    else if (game_state == 2) draw_gameover();
    else if (game_state == 3) draw_win();
}

void smb_init(void) {
    running = 1;
    game_state = 0;
    score = 0;
    build_pal();
    build_level();
    init_player();
    init_enemies();
    camera_x = 0;
}

void smb_cleanup(void) {
    running = 0;
    game_state = 4;
}

void smb_run(void) {
    if (!running) return;

    while (keyboard_getchar() != 0) yield();

    const uint32_t STEP_INTERVAL = 16; /* 60 FPS */
    uint32_t last = uptime_ticks;

    while (running) {
        char c;
        int action = 0;
        int keys_processed = 0;
        while (keys_processed < 4 && (c = keyboard_getchar()) != 0) {
            keys_processed++;
            if (c == 27) { 
                running = 0; 
                game_state = 4; 
                smb_restore_kernel_mode();
                return; 
            }

            if (game_state == 0 || game_state == 2 || game_state == 3) {
                if ((c == ' ' || c == '\n') && !action) {
                    action = 1;
                    if (game_state == 0) {
                        score = 0;
                        build_level();
                        init_player();
                        init_enemies();
                        camera_x = 0;
                        game_state = 1;
                        while (keyboard_getchar() != 0);
                    } else if (game_state == 2 || game_state == 3) {
                        game_state = 0;
                        init_player();
                        while (keyboard_getchar() != 0);
                    }
                }
            }

            if (game_state == 1) {
                if (c == 0x4D) {
                    player.vx_fx = to_fx(3);
                    player.facing = 1;
                } else if (c == 0x4B) {
                    player.vx_fx = to_fx(-3);
                    player.facing = -1;
                } else if (c == 0x48) {
                    if (player.on_ground && player.can_jump) {
                        player.vy_fx = to_fx(-10);
                        player.on_ground = 0;
                        player.can_jump = 0;
                    }
                } else if (c == ' ' || c == '\n') {
                    if (player.on_ground && player.can_jump) {
                        player.vy_fx = to_fx(-10);
                        player.on_ground = 0;
                        player.can_jump = 0;
                    }
                }
            }
        }

        uint32_t now = uptime_ticks;
        if (now - last >= STEP_INTERVAL) {
            if (game_state == 1) {
                update_player();
                update_goombas();
                timer++;
            }
            render();
            last = now;
        }
        
        yield(); /* Let multitasking scheduler run other tasks */
    }

    smb_cleanup();
    smb_restore_kernel_mode();
}

void smb_draw_frame(void) {
    render();
}

void smb_handle_key(int key) {
    if (key == 27) { running = 0; game_state = 4; return; }

    if (game_state == 1) {
        if (key == 0x4D) { player.vx_fx = to_fx(3); player.facing = 1; }
        else if (key == 0x4B) { player.vx_fx = to_fx(-3); player.facing = -1; }
        else if (key == 0x48 && player.on_ground && player.can_jump) {
            player.vy_fx = to_fx(-10);
            player.on_ground = 0;
            player.can_jump = 0;
        } else if ((key == ' ' || key == '\n') && player.on_ground && player.can_jump) {
            player.vy_fx = to_fx(-10);
            player.on_ground = 0;
            player.can_jump = 0;
        }
    } else if (key == ' ' || key == '\n') {
        if (game_state == 0) {
            score = 0;
            build_level();
            init_player();
            init_enemies();
            camera_x = 0;
            game_state = 1;
        } else if (game_state == 2 || game_state == 3) {
            game_state = 0;
            init_player();
        }
    }
}

void smb_set_kernel_mode(void) {
}

void smb_tick(void) {
    if (game_state == 4) return;
    
    char c;
    int keys_processed = 0;
    while (keys_processed < 4 && (c = keyboard_getchar()) != 0) {
        keys_processed++;
        if (c == 27) { running = 0; game_state = 4; smb_restore_kernel_mode();
            if (current_kernel_mode == KERNEL_MODE_DESKTOP) desktop.dirty = true;
            return;
        }

        if (game_state == 1) {
            if (c == 0x4D) { player.vx_fx = to_fx(3); player.facing = 1; }
            else if (c == 0x4B) { player.vx_fx = to_fx(-3); player.facing = -1; }
            else if (c == 0x48) {
                if (player.on_ground && player.can_jump) {
                    player.vy_fx = to_fx(-10);
                    player.on_ground = 0;
                    player.can_jump = 0;
                }
            }
            else if (c == ' ' || c == '\n') {
                if (player.on_ground && player.can_jump) {
                    player.vy_fx = to_fx(-10);
                    player.on_ground = 0;
                    player.can_jump = 0;
                }
            }
        }
    }

    if (game_state == 1) {
        update_player();
        update_goombas();
        timer++;
    }
    render();
}

void smb_restore_kernel_mode(void) {
    if (current_kernel_mode == KERNEL_MODE_DESKTOP) {
        desktop.dirty = true;
        return;
    }
    terminal_initialize();
    redraw_all_panes();
    print_prompt();
}
