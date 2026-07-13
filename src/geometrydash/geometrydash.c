#include "kernel.h"
#include "geometrydash.h"
#include "desktop.h"

uint8_t gd_screen[GD_SCREEN_H][GD_SCREEN_W];
uint32_t gd_palette[256];

#define COL_BLACK   0
#define COL_WHITE   15
#define COL_GREY    8
#define COL_DGREY   1
#define COL_SKY     144
#define COL_GROUND  80
#define COL_GRASS   48
#define COL_PLATFORM 112
#define COL_PBRIM   120
#define COL_CUBE    240
#define COL_SPIKE   244
#define COL_ACCENT  252
#define COL_WALL    60
#define COL_EYE_WHITE 250
#define COL_MOUTH   255
#define COL_PUPIL   0

#define FIX_SHIFT 8
#define FIX_ONE (1 << FIX_SHIFT)
#define to_fix(x) ((x) * FIX_ONE)
#define to_int(x) ((x) >> FIX_SHIFT)

#define PLAYER_W 16
#define PLAYER_H 16
#define GROUND_Y 155
#define PLAYER_X 60

#define MAX_OBSTACLES 64

typedef enum { MENU, PLAYING, GAMEOVER, QUIT } state_t;

typedef struct {
    int x;
    int y;
    int w;
    int h;
    int type;
} obstacle_t;

static state_t state = MENU;

static int player_y_fix, player_vel_fix;
static int on_ground;

static int window_x = 0, window_y = 0, window_w = 640, window_h = 480;
static int alive;
static obstacle_t obstacles[MAX_OBSTACLES];
static int obstacle_count;
static int score, best_score;
static int running;
static int scroll_speed;
static int level_pos;
static int cube_rotation;
static int cube_target_rot;

static void set_px(int x, int y, uint8_t c) {
    if (x >= 0 && x < 320 && y >= 0 && y < 200)
        gd_screen[y][x] = c;
}

static void fill_rect(int x, int y, int w, int h, uint8_t c) {
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++)
            set_px(x + i, y + j, c);
}

static void draw_face(int x, int y, int rot) {
    int cx = x + PLAYER_W / 2;
    int cy = y + PLAYER_H / 2;
    int r = rot % 4;
    if (r < 0) r += 4;

    if (r == 0 || r == 2) {
        set_px(cx - 3, cy - 3, COL_EYE_WHITE);
        set_px(cx - 2, cy - 3, COL_EYE_WHITE);
        set_px(cx - 3, cy - 2, COL_EYE_WHITE);
        set_px(cx - 2, cy - 2, COL_EYE_WHITE);
        set_px(cx - 2, cy - 2, COL_PUPIL);

        set_px(cx + 1, cy - 3, COL_EYE_WHITE);
        set_px(cx + 2, cy - 3, COL_EYE_WHITE);
        set_px(cx + 1, cy - 2, COL_EYE_WHITE);
        set_px(cx + 2, cy - 2, COL_EYE_WHITE);
        set_px(cx + 2, cy - 2, COL_PUPIL);

        set_px(cx - 1, cy + 2, COL_MOUTH);
        set_px(cx,     cy + 2, COL_MOUTH);
        set_px(cx + 1, cy + 2, COL_MOUTH);
        set_px(cx - 1, cy + 3, COL_MOUTH);
        set_px(cx + 1, cy + 3, COL_MOUTH);
    } else {
        int ox, oy;
        if (r == 1) { ox = cx + 2; oy = cy; }
        else { ox = cx - 2; oy = cy; }

        if (r == 1) {
            set_px(ox - 1, oy - 3, COL_EYE_WHITE);
            set_px(ox - 1, oy - 2, COL_EYE_WHITE);
            set_px(ox - 1, oy - 2, COL_PUPIL);
            set_px(ox - 1, oy + 1, COL_EYE_WHITE);
            set_px(ox - 1, oy + 2, COL_EYE_WHITE);
            set_px(ox - 1, oy + 2, COL_PUPIL);
            set_px(ox - 1, oy, COL_MOUTH);
        } else {
            set_px(ox + 1, oy - 3, COL_EYE_WHITE);
            set_px(ox + 1, oy - 2, COL_EYE_WHITE);
            set_px(ox + 1, oy - 2, COL_PUPIL);
            set_px(ox + 1, oy + 1, COL_EYE_WHITE);
            set_px(ox + 1, oy + 2, COL_EYE_WHITE);
            set_px(ox + 1, oy + 2, COL_PUPIL);
            set_px(ox + 1, oy, COL_MOUTH);
        }
    }
}

static void draw_cube_rotated(int x, int y, int rot, uint8_t c) {
    int cx = x + PLAYER_W / 2;
    int cy = y + PLAYER_H / 2;

    rot = rot % 4;
    if (rot < 0) rot += 4;

    int w = PLAYER_W;
    int h = PLAYER_H;

    if (rot == 0 || rot == 2) {
        fill_rect(x, y, w, h, c);
    } else {
        fill_rect(cx - h / 2, cy - w / 2, h, w, c);
    }

    int ix, iy, iw, ih;
    if (rot == 0) { ix = x + 2; iy = y + 2; iw = w - 4; ih = h - 4; }
    else if (rot == 1) { ix = cx - h / 2 + 2; iy = cy - w / 2 + 2; iw = h - 4; ih = w - 4; }
    else if (rot == 2) { ix = x + 2; iy = y + 2; iw = w - 4; ih = h - 4; }
    else { ix = cx - h / 2 + 2; iy = cy - w / 2 + 2; iw = h - 4; ih = w - 4; }

    fill_rect(ix, iy, iw, ih, c + 10);

    draw_face(x, y, rot);
}

static void draw_spike(int x, int y) {
    fill_rect(x, y + 8, 8, 8, COL_SPIKE);
    set_px(x, y + 8, COL_SPIKE);
    set_px(x + 1, y + 7, COL_SPIKE);
    set_px(x + 2, y + 6, COL_SPIKE);
    set_px(x + 3, y + 5, COL_SPIKE);
    set_px(x + 4, y + 5, COL_SPIKE);
    set_px(x + 5, y + 6, COL_SPIKE);
    set_px(x + 6, y + 7, COL_SPIKE);
    set_px(x + 7, y + 8, COL_SPIKE);
    set_px(x + 1, y + 8, COL_PBRIM);
    set_px(x + 2, y + 7, COL_PBRIM);
    set_px(x + 3, y + 6, COL_PBRIM);
    set_px(x + 4, y + 6, COL_PBRIM);
    set_px(x + 5, y + 7, COL_PBRIM);
    set_px(x + 6, y + 8, COL_PBRIM);
}

static void draw_scene(void) {
    fill_rect(0, 0, 320, GROUND_Y, COL_SKY);
    fill_rect(0, GROUND_Y, 320, 200 - GROUND_Y, COL_GROUND);
    fill_rect(0, GROUND_Y - 2, 320, 2, COL_GRASS);

    for (int y = GROUND_Y + 2; y < 200; y += 6)
        for (int x = 0; x < 320; x += 12)
            set_px(x, y, COL_GRASS);

    for (int i = 0; i < obstacle_count; i++) {
        obstacle_t *o = &obstacles[i];
        if (o->x + o->w < 0 || o->x > 320) continue;

        if (o->type == 0) {
            fill_rect(o->x, o->y, o->w, o->h, COL_PLATFORM);
            fill_rect(o->x - 2, o->y - 3, o->w + 4, 3, COL_PBRIM);
            for (int r = 0; r < o->h; r += 8)
                fill_rect(o->x + 4, o->y + r, 3, 4, COL_PBRIM);
        } else if (o->type == 1) {
            fill_rect(o->x, o->y, o->w, o->h, COL_WALL);
            fill_rect(o->x - 2, o->y - 3, o->w + 4, 3, COL_PBRIM);
        } else if (o->type == 2) {
            draw_spike(o->x, o->y);
        }
    }

    int py = to_int(player_y_fix);
    if (alive)
        draw_cube_rotated(PLAYER_X, py, cube_rotation, COL_CUBE);
    else
        draw_cube_rotated(PLAYER_X, py, 0, COL_SPIKE);
}

static void draw_font_char(int x, int y, char c, uint8_t fg) {
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

static void draw_number(int x, int y, int val, uint8_t col) {
    if (val > 99999) val = 99999;
    char tmp[7];
    int len = 0;
    if (val == 0) {
        tmp[len++] = '0';
    } else {
        int v = val;
        while (v > 0 && len < 6) {
            tmp[len++] = '0' + (v % 10);
            v /= 10;
        }
    }
    for (int i = len - 1; i >= 0; i--) {
        draw_font_char(x, y, tmp[i], col);
        x += 8;
    }
}

static void draw_hud(void) {
    for (int i = 0; i < 5; i++)
        draw_font_char(4 + i*8, 4, "SCORE"[i], COL_WHITE);
    draw_number(4, 14, score, COL_WHITE);
    for (int i = 0; i < 4; i++)
        draw_font_char(250 + i*8, 4, "BEST"[i], COL_ACCENT);
    draw_number(250, 14, best_score, COL_ACCENT);
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
                uint32_t color = gd_palette[gd_screen[y][x]];
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

void gd_set_window_rect(int x, int y, int w, int h) {
    window_x = x;
    window_y = y;
    window_w = w;
    window_h = h;
}

static void build_pal(void) {
    for (int i = 0; i < 256; i++)
        gd_palette[i] = 0;
    gd_palette[0]  = 0x000000;
    gd_palette[1]  = 0x222222;
    gd_palette[8]  = 0x555555;
    gd_palette[15] = 0xFFFFFF;
    gd_palette[COL_SKY]      = 0x1A1A2E;
    gd_palette[COL_GROUND]   = 0x16213E;
    gd_palette[COL_GRASS]    = 0x0F3460;
    gd_palette[COL_PLATFORM] = 0x533483;
    gd_palette[COL_PBRIM]    = 0xE94560;
    gd_palette[COL_CUBE]     = 0x00FF00;
    gd_palette[COL_SPIKE]    = 0xFF0000;
    gd_palette[COL_ACCENT]   = 0x00FFFF;
    gd_palette[COL_WALL]     = 0x8B0000;
    gd_palette[COL_EYE_WHITE]= 0xFFFFFF;
    gd_palette[COL_MOUTH]    = 0xFFFF00;
    gd_palette[COL_PUPIL]    = 0x000000;
    for (int i = 0; i < 256; i++)
        if (gd_palette[i] == 0 && i != 0)
            gd_palette[i] = 0x333333;
    gd_palette[0] = 0x000000;
}

static int collision(int ax, int ay, int aw, int ah, int bx, int by, int bw, int bh) {
    if (ax + aw <= bx) return 0;
    if (ax >= bx + bw) return 0;
    if (ay + ah <= by) return 0;
    if (ay >= by + bh) return 0;
    return 1;
}

static void spawn_obstacle(void) {
    if (obstacle_count >= MAX_OBSTACLES) return;

    int type = (level_pos % 7);
    if (type > 2) type = 2;

    if (type == 0) {
        int w = 24 + ((level_pos * 3) % 16);
        int h = 8 + ((level_pos * 7) % 16);
        int gap = 12 + ((level_pos * 13) % 8);
        int y = GROUND_Y - h - gap;
        if (y < 40) y = 40;
        if (y > GROUND_Y - h - 4) y = GROUND_Y - h - 4;
        obstacles[obstacle_count].x = 320;
        obstacles[obstacle_count].y = y;
        obstacles[obstacle_count].w = w;
        obstacles[obstacle_count].h = h;
        obstacles[obstacle_count].type = 0;
        obstacle_count++;
        if (level_pos > 5 && (level_pos % 3) == 0) {
            if (obstacle_count < MAX_OBSTACLES) {
                obstacles[obstacle_count].x = 320 + w + 20;
                obstacles[obstacle_count].y = GROUND_Y - 16;
                obstacles[obstacle_count].w = 8;
                obstacles[obstacle_count].h = 16;
                obstacles[obstacle_count].type = 2;
                obstacle_count++;
            }
        }
    } else if (type == 1) {
        int h = 16 + ((level_pos * 11) % 32);
        int y = GROUND_Y - h;
        if (y < 20) y = 20;
        obstacles[obstacle_count].x = 320;
        obstacles[obstacle_count].y = y;
        obstacles[obstacle_count].w = 20;
        obstacles[obstacle_count].h = h;
        obstacles[obstacle_count].type = 1;
        obstacle_count++;
        if (level_pos > 3 && (level_pos % 2) == 0) {
            if (obstacle_count < MAX_OBSTACLES) {
                obstacles[obstacle_count].x = 320 + 20 + 16;
                obstacles[obstacle_count].y = y + h - 16;
                obstacles[obstacle_count].w = 8;
                obstacles[obstacle_count].h = 16;
                obstacles[obstacle_count].type = 2;
                obstacle_count++;
            }
        }
    } else {
        int gap = 16 + ((level_pos * 9) % 12);
        obstacles[obstacle_count].x = 320;
        obstacles[obstacle_count].y = GROUND_Y - 16;
        obstacles[obstacle_count].w = 8;
        obstacles[obstacle_count].h = 16;
        obstacles[obstacle_count].type = 2;
        obstacle_count++;
        if (level_pos > 2 && (level_pos % 3) != 0) {
            if (obstacle_count < MAX_OBSTACLES) {
                obstacles[obstacle_count].x = 320 + 8 + gap;
                obstacles[obstacle_count].y = GROUND_Y - 16;
                obstacles[obstacle_count].w = 8;
                obstacles[obstacle_count].h = 16;
                obstacles[obstacle_count].type = 2;
                obstacle_count++;
            }
        }
    }

    level_pos++;
}

static void init_game(void) {
    player_y_fix = to_fix(GROUND_Y - PLAYER_H);
    player_vel_fix = 0;
    on_ground = 1;
    alive = 1;
    obstacle_count = 0;
    score = 0;
    scroll_speed = 3;
    level_pos = 0;
    cube_rotation = 0;
    cube_target_rot = 0;
}

static void update(void) {
    if (!alive) return;

    player_vel_fix += to_fix(1);
    if (player_vel_fix > to_fix(12)) player_vel_fix = to_fix(12);

    on_ground = 0;

    player_y_fix += player_vel_fix;

    int py = to_int(player_y_fix);

    if (py + PLAYER_H >= GROUND_Y) {
        py = GROUND_Y - PLAYER_H;
        player_y_fix = to_fix(py);
        player_vel_fix = 0;
        on_ground = 1;
    }

    if (py < 0) {
        py = 0;
        player_y_fix = 0;
        player_vel_fix = 0;
    }

    for (int i = 0; i < obstacle_count; i++) {
        obstacles[i].x -= scroll_speed;

        if (obstacles[i].type == 2) {
            if (collision(PLAYER_X, py, PLAYER_W, PLAYER_H,
                         obstacles[i].x, obstacles[i].y, obstacles[i].w, obstacles[i].h)) {
                alive = 0;
                if (score > best_score) best_score = score;
                return;
            }
        } else {
            if (collision(PLAYER_X, py, PLAYER_W, PLAYER_H,
                         obstacles[i].x, obstacles[i].y, obstacles[i].w, obstacles[i].h)) {
                int prev_py = to_int(player_y_fix - player_vel_fix);
                int prev_bottom = prev_py + PLAYER_H;

                if (prev_bottom <= obstacles[i].y + 4 && player_vel_fix >= 0) {
                    py = obstacles[i].y - PLAYER_H;
                    player_y_fix = to_fix(py);
                    player_vel_fix = 0;
                    on_ground = 1;
                } else {
                    alive = 0;
                    if (score > best_score) best_score = score;
                    return;
                }
            }
        }
    }

    if (!alive) {
        if (score > best_score) best_score = score;
        return;
    }

    {
        int wi = 0;
        for (int i = 0; i < obstacle_count; i++) {
            if (obstacles[i].x > -40) {
                if (obstacles[i].x + obstacles[i].w < PLAYER_X) {
                    if (obstacles[i].x + obstacles[i].w + scroll_speed >= PLAYER_X) {
                        score++;
                    }
                }
                obstacles[wi++] = obstacles[i];
            }
        }
        obstacle_count = wi;
    }

    if (obstacle_count == 0 || obstacles[obstacle_count - 1].x < 200) {
        spawn_obstacle();
    }

    if (!on_ground) {
        cube_target_rot = 1;
    } else {
        cube_target_rot = 0;
    }

    if (cube_rotation < cube_target_rot) cube_rotation++;
    else if (cube_rotation > cube_target_rot) cube_rotation--;
}

static void jump(void) {
    if (!alive) return;
    if (!on_ground) return;
    player_vel_fix = to_fix(-10);
    on_ground = 0;
}

static void gd_clear_keys(void) {
    int max_clear = 256;
    while (keyboard_getchar() != 0 && max_clear > 0) {
        yield();
        max_clear--;
    }
}

static void draw_menu(void) {
    fill_rect(0, 0, 320, GROUND_Y, COL_SKY);
    fill_rect(0, GROUND_Y, 320, 200 - GROUND_Y, COL_GROUND);
    fill_rect(0, GROUND_Y - 2, 320, 2, COL_GRASS);
    for (int y = GROUND_Y + 2; y < 200; y += 6)
        for (int x = 0; x < 320; x += 12)
            set_px(x, y, COL_GRASS);

    int tx = 320 / 2 - 7 * 4;
    int ty = 30;
    const char* title = "GEOMETRY";
    for (int i = 0; title[i]; i++)
        draw_font_char(tx + i * 8, ty, title[i], COL_CUBE);

    tx = 320 / 2 - 4 * 4;
    ty = 42;
    const char* sub = "DASH";
    for (int i = 0; sub[i]; i++)
        draw_font_char(tx + i * 8, ty, sub[i], COL_ACCENT);

    tx = 320 / 2 - 8 * 4;
    ty = 54;
    const char* ed = "SharkOS Edition";
    for (int i = 0; ed[i]; i++)
        draw_font_char(tx + i * 8, ty, ed[i], COL_GREY);

    draw_cube_rotated(320 / 2 - 8, 68, 0, COL_CUBE);

    tx = 320 / 2 - 6 * 4;
    ty = 95;
    const char* best = "BEST SCORE:";
    for (int i = 0; best[i]; i++)
        draw_font_char(tx + i * 8, ty, best[i], COL_WHITE);
    draw_number(320 / 2 - 2 * 4, 107, best_score, COL_ACCENT);

    ty = 128;
    const char* inst1 = "Press SPACE to start";
    for (int i = 0; inst1[i]; i++)
        draw_font_char(320 / 2 - 10 * 4 + i * 8, ty, inst1[i], COL_WHITE);

    ty = 138;
    const char* inst2 = "ESC to quit";
    for (int i = 0; inst2[i]; i++)
        draw_font_char(320 / 2 - 5 * 4 + i * 8, ty, inst2[i], COL_GREY);

    blit();
}

static void render_frame(void) {
    if (state == MENU) {
        draw_menu();
    } else {
        draw_scene();
        draw_hud();
        blit();
    }
}

void gd_init(void) {
    running = 1;
    best_score = 0;
    score = 0;
    obstacle_count = 0;
    state = MENU;
    build_pal();
    init_game();
    memset(gd_screen, 0, sizeof(gd_screen));
}

void gd_cleanup(void) {
    running = 0;
    state = QUIT;
}

void gd_run(void) {
    if (!running) return;

    gd_clear_keys();

    state = MENU;

    const uint32_t STEP_INTERVAL = 16; 
    uint32_t last = uptime_ticks;

    while (running) {
        
        char c;
        int action = 0;
        int keys_processed = 0;
        while (keys_processed < 8 && (c = keyboard_getchar()) != 0) {
            keys_processed++;
            if (c == 27) {
                running = 0;
                state = QUIT;
                gd_restore_kernel_mode();
                return;
            }
            if ((c == ' ' || c == '\n' || c == 'w' || c == 'W') && !action) {
                action = 1;
                if (state == MENU) {
                    gd_clear_keys();
                    init_game();
                    state = PLAYING;
                } else if (state == PLAYING) {
                    jump();
                } else if (state == GAMEOVER) {
                    state = MENU;
                    init_game();
                }
            }
        }

        
        uint32_t now = uptime_ticks;
        if (now - last >= STEP_INTERVAL) {
            if (state == PLAYING) {
                update();
                if (!alive) state = GAMEOVER;
            }
            render_frame();
            last = now;
        }
        
        yield(); 
    }

    gd_cleanup();
    gd_restore_kernel_mode();
}

void gd_draw_frame(void) {
    render_frame();
}

void gd_handle_key(int key) {
    if (key == 27) { running = 0; state = QUIT; return; }

    if (key == ' ' || key == '\n') {
        if (state == MENU) {
            init_game();
            state = PLAYING;
        } else if (state == PLAYING) {
            jump();
        } else if (state == GAMEOVER) {
            state = MENU;
            init_game();
        }
    }
}

void gd_set_kernel_mode(void) {
}

void gd_tick(void) {
    if (state == QUIT) return;
    
    if (state == PLAYING) {
        update();
        if (!alive) state = GAMEOVER;
    }
    render_frame();
}

void gd_restore_kernel_mode(void) {
    if (current_kernel_mode == KERNEL_MODE_DESKTOP) {
        desktop.dirty = true;
        return;
    }
    terminal_initialize();
    redraw_all_panes();
    print_prompt();
}
