/*
So, Uhh i wanted to name it flappybirb but then i remembered i need a J*B so like uhh this is flappybird
So don't fap with the bird hehe (i fucking hate myself for this comment)
Cleanest code in the world on the best OS im the best for this flappybirb
nvidia fuck you - big torvalds
*/

#include "kernel.h"
#include "flappybird.h"

uint8_t flappybird_screen[FLAPPYBIRD_SCREEN_H][FLAPPYBIRD_SCREEN_W];
uint32_t flappybird_palette[256];

/*https://youtu.be/2VZH5WQLAGo holy fucking banger cuz those colors rock that body*/
#define COL_BLACK   0
#define COL_WHITE   15
#define COL_GREY    8
#define COL_DGREY   1
#define COL_SKY     144
#define COL_GROUND  80
#define COL_GRASS   48
#define COL_PIPE    112
#define COL_PBRIM   120
#define COL_BIRD    240
#define COL_WING    250
#define COL_EYE     255
#define COL_DEAD    244
#define COL_ACCENT  252

#define FIX_SHIFT 8
#define FIX_ONE (1 << FIX_SHIFT)
#define to_fix(x) ((x) * FIX_ONE)
#define to_int(x) ((x) >> FIX_SHIFT)

typedef enum { MENU, PLAYING, GAMEOVER, QUIT } state_t;
typedef struct { int x; int y; } pipe_t;

static state_t state = MENU;

static int bird_x_fix, bird_y_fix, bird_vel_fix;
static int alive;
static pipe_t pipes[16];
static int pipe_count;
static int score, best_score;
static int running;
static int auto_play = 0;
static void set_px(int x, int y, uint8_t c) {
    if (x >= 0 && x < 320 && y >= 0 && y < 200)
        flappybird_screen[y][x] = c;
}

static void fill_rect(int x, int y, int w, int h, uint8_t c) {
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++)
            set_px(x + i, y + j, c);
}

static void draw_bird(int x, int y, uint8_t bc) {
    fill_rect(x, y, 12, 8, bc);
    fill_rect(x + 8, y + 2, 4, 3, bc - 10);
    set_px(x + 2, y + 2, COL_WHITE);
    set_px(x + 2, y + 3, COL_BLACK);
    set_px(x + 10, y + 2, bc + 5);
    set_px(x + 11, y + 2, bc + 5);
    set_px(x + 10, y + 3, bc + 5);
    set_px(x + 11, y + 3, bc + 5);
    fill_rect(x - 2, y + 2, 2, 3, bc - 5);
}

static void draw_pipe(pipe_t *p) {
    int gt = p->y - 40;
    int gb = p->y + 40;

    fill_rect(p->x, 0, 28, gt, COL_PIPE);
    fill_rect(p->x - 3, gt - 8, 34, 8, COL_PBRIM);
    for (int r = 0; r < gt; r += 8)
        fill_rect(p->x + 10, r, 3, 4, COL_PBRIM);

    int lh = 200 - gb;
    if (lh > 0) {
        fill_rect(p->x, gb, 28, lh, COL_PIPE);
        fill_rect(p->x - 3, gb, 34, 8, COL_PBRIM);
        for (int r = gb + 8; r < 200; r += 8)
            fill_rect(p->x + 10, r, 3, 4, COL_PBRIM);
    }
}

static void draw_scene(void) { /*This was a headache to make anyone reading this send help*/
    fill_rect(0, 0, 320, 155, COL_SKY);
    fill_rect(0, 155, 320, 45, COL_GROUND);
    fill_rect(0, 153, 320, 2, COL_GRASS);
    for (int y = 157; y < 200; y += 6)
        for (int x = 0; x < 320; x += 12)
            set_px(x, y, COL_GRASS);
    for (int i = 0; i < pipe_count; i++)
        draw_pipe(&pipes[i]);

    int by = to_int(bird_y_fix);
    if (alive)
        draw_bird(60, by, COL_BIRD);
    else
        draw_bird(60, by, COL_DEAD);
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

static void draw_number(int x, int y, int val, uint8_t col) { /*Smooth number go BRRRRR (if it breaks ur a femboy)*/
    if (val > 9999) val = 9999;
    char tmp[6];
    int len = 0;
    if (val == 0) {
        tmp[len++] = '0';
    } else {
        int v = val;
        while (v > 0 && len < 5) {
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
    int scale_x = (int)screen_width / 320;
    int scale_y = (int)screen_height / 200;
    int scale = (scale_x < scale_y) ? scale_x : scale_y;
    if (scale < 1) scale = 1;

    for (int y = 0; y < 200 && y * scale < (int)screen_height; y++) {
        for (int sy = 0; sy < scale && y * scale + sy < (int)screen_height; sy++) {
            int fy = y * scale + sy;
            for (int x = 0; x < 320 && x * scale < (int)screen_width; x++) {
                uint32_t color = flappybird_palette[flappybird_screen[y][x]];
                for (int sx = 0; sx < scale && x * scale + sx < (int)screen_width; sx++) {
                    int fx = x * scale + sx;
                    lfbptr[fy * stride + fx] = color;
                }
            }
        }
    }
}

static void build_pal(void) { /*Holy shit it's builidng a birb*/
    for (int i = 0; i < 256; i++)
        flappybird_palette[i] = 0;
    flappybird_palette[0]  = 0x000000;
    flappybird_palette[1]  = 0x222222;
    flappybird_palette[8]  = 0x555555;
    flappybird_palette[15] = 0xFFFFFF;
    flappybird_palette[COL_SKY]    = 0x87CEEB;
    flappybird_palette[COL_GROUND] = 0x8B5A2B;
    flappybird_palette[COL_GRASS]  = 0x228B22;
    flappybird_palette[COL_PIPE]   = 0x2ECC71;
    flappybird_palette[COL_PBRIM]  = 0x1A6B3C;
    flappybird_palette[COL_BIRD]   = 0xFFD700;
    flappybird_palette[COL_WING]   = 0xFF8C00;
    flappybird_palette[COL_EYE]    = 0xFFFFFF;
    flappybird_palette[COL_DEAD]   = 0xE74C3C;
    flappybird_palette[COL_ACCENT] = 0x00FFFF;
    for (int i = 0; i < 256; i++)
        if (flappybird_palette[i] == 0 && i != 0)
            flappybird_palette[i] = 0x333333;
    flappybird_palette[0] = 0x000000;
}

static void spawn_pipe(void) { /*Nintendo i know it looks like mario his pipes but i just a pijpbeurt (the dutch will understand!)*/
    if (pipe_count >= 16) return;
    pipes[pipe_count].x = 320;

    pipes[pipe_count].y = 85 + (score * 7 % 60);
    if (pipes[pipe_count].y < 85) pipes[pipe_count].y = 85;
    if (pipes[pipe_count].y > 145) pipes[pipe_count].y = 145;
    pipe_count++;
}

static void flap(void); /*foward heb je hulp nodig dat is hier*/
static void init_game(void) {
    bird_x_fix = to_fix(60);
    bird_y_fix = to_fix(80);
    bird_vel_fix = 0;
    alive = 1;
    pipe_count = 0;
    score = 0;
}

static void auto_flap(void) {
    if (!alive || !auto_play) return;

    int by = to_int(bird_y_fix);

    for (int i = 0; i < pipe_count; i++) {
        int pipe_x = pipes[i].x;
        int gap_y = pipes[i].y;

        if (pipe_x > 40 && pipe_x < 80) {
            int target_y = gap_y - 10;
            if (by > target_y && bird_vel_fix > 0) {
                flap();
                return;
            }
        }
    }

    if (by > 100 && bird_vel_fix > 0) {
        flap();
    }
}

static void update(void) {
    if (!alive) return; /*this would return false with my mental state im not alive*/

    bird_vel_fix += to_fix(1) / 2;

    if (bird_vel_fix > to_fix(6)) bird_vel_fix = to_fix(6);

    bird_y_fix += bird_vel_fix;

    int by = to_int(bird_y_fix);

    if (by < 0) { by = 0; bird_y_fix = 0; bird_vel_fix = to_fix(1); }

    if (by > 148) {
        alive = 0; /*the bird is dead and so am i insert the xxxtentacion sad music*/
        if (score > best_score) best_score = score;
        return;
    }

    for (int i = 0; i < pipe_count; i++) {
        pipes[i].x -= 2;

        if (pipes[i].x + 28 < 60 && pipes[i].y > 0 && pipes[i].y < 500) {
            score++;
            pipes[i].y += 1000;
        }

        if (60 + 3 < pipes[i].x + 28 && 60 + 9 > pipes[i].x) {
            int gt = pipes[i].y - 40;
            int gb = pipes[i].y + 40;

            if (by + 1 < gt || by + 7 > gb) {
                alive = 0;
                if (score > best_score) best_score = score;
                return;
            }
        }
    }

    {
        int wi = 0;
        for (int i = 0; i < pipe_count; i++) {
            if (pipes[i].x > -40) {
                if (pipes[i].y > 500) pipes[i].y -= 1000;
                pipes[wi++] = pipes[i];
            }
        }
        pipe_count = wi;
    }

    if (pipe_count == 0 || pipes[pipe_count - 1].x < 200) {
        spawn_pipe(); /*pipe.*/
    }
}

static void flap(void) { /*flap go up velocity is fucked (it works but it's fucked still needs a bit of magic)*/
    if (!alive) return;
    bird_vel_fix = to_fix(-7);
}

static void draw_menu(void) { /*menu goes brrrr*/

    fill_rect(0, 0, 320, 155, COL_SKY);
    fill_rect(0, 155, 320, 45, COL_GROUND);
    fill_rect(0, 153, 320, 2, COL_GRASS);
    for (int y = 157; y < 200; y += 6)
        for (int x = 0; x < 320; x += 12)
            set_px(x, y, COL_GRASS);

    int tx = 320 / 2 - 7 * 4;
    int ty = 40;
    const char* title = "FLAPPY BIRD";
    for (int i = 0; title[i]; i++)
        draw_font_char(tx + i * 8, ty, title[i], COL_BIRD);

    tx = 320 / 2 - 8 * 4;
    ty = 52;
    const char* sub = "SharkOS Edition";
    for (int i = 0; sub[i]; i++)
        draw_font_char(tx + i * 8, ty, sub[i], COL_ACCENT);

    draw_bird(320 / 2 - 6, 68, COL_BIRD);

    tx = 320 / 2 - 6 * 4;
    ty = 90;
    const char* best = "BEST SCORE:";
    for (int i = 0; best[i]; i++)
        draw_font_char(tx + i * 8, ty, best[i], COL_WHITE);
    draw_number(320 / 2 - 2 * 4, 102, best_score, COL_ACCENT);

    ty = 125;
    const char* inst1 = "Press SPACE to start";
    for (int i = 0; inst1[i]; i++)
        draw_font_char(320 / 2 - 10 * 4 + i * 8, ty, inst1[i], COL_WHITE);

    ty = 135;
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

void flappybird_init(void) { /*oi this init function is a bit bri'ish bottle o' water innit!@>!>*/
    running = 1;
    best_score = 0;
    score = 0;
    pipe_count = 0;
    state = MENU;
    build_pal();
    init_game();

    memset(flappybird_screen, 0, sizeof(flappybird_screen));
}

void flappybird_cleanup(void) {
    running = 0;
    state = QUIT;
}

void flappybird_run(void) {
    if (!running) return;

    while (keyboard_getchar() != 0) yield();

    state = MENU;
    auto_play = 0;

    const uint32_t STEP_INTERVAL = 32;
    uint32_t last = uptime_ticks;

    while (running) {
        yield();

        char c;
        int action = 0;
        while ((c = keyboard_getchar()) != 0) {
            if (c == 27) { running = 0; state = QUIT; break; }
            if ((c == ' ' || c == '\n' || c == 'w' || c == 'W') && !action) {
                action = 1;
                if (state == MENU) {
                    init_game();
                    state = PLAYING;
                    while (keyboard_getchar() != 0);
                } else if (state == PLAYING) {
                    flap();
                } else if (state == GAMEOVER) {
                    state = MENU;
                    init_game();
                }
            }
        }

        uint32_t now = uptime_ticks;
        if (now - last >= STEP_INTERVAL) {
            if (state == PLAYING) {
                if (auto_play) auto_flap();
                update();
                if (!alive) state = GAMEOVER;
            }
            render_frame();
            last = now;
        }
        yield();
    }

    flappybird_cleanup();
}

void flappybird_draw_frame(void) {
    render_frame();
}

void flappybird_handle_key(int key) {
    if (key == 27) { running = 0; state = QUIT; return; }

    if (state == MENU) {
        static int cheat_idx = 0;
        const char* target = "1337";/*Holy */
        if (key == target[cheat_idx]) {
            cheat_idx++;
            if (cheat_idx == 4) {
                auto_play = 1;
                init_game();
                state = PLAYING;
                while (keyboard_getchar() != 0);
                cheat_idx = 0;
                return;
            }
        } else {
            cheat_idx = 0;
        }
    }

    if (key == ' ' || key == '\n') {
        if (state == MENU) {
            init_game();
            state = PLAYING;
        } else if (state == PLAYING) {
            flap();
        } else if (state == GAMEOVER) {
            state = MENU;
            init_game();
        }
    }
}

void flappybird_set_kernel_mode(void) { /*don't ask why this is here it fixed my errors so ig just let it be here :3*/
}

void flappybird_restore_kernel_mode(void) {
    terminal_initialize();
    redraw_all_panes();
    print_prompt();
}