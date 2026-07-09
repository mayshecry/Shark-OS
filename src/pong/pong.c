#include "kernel.h"
#include "pong.h"

uint8_t pong_screen[PONG_SCREEN_H][PONG_SCREEN_W];
uint32_t pong_palette[256];

#define COL_BLACK   0
#define COL_WHITE   15
#define COL_GREY    8
#define COL_P1      240
#define COL_CPU     80
#define COL_BALL    252
#define COL_NET     144
#define COL_ACCENT  255
#define COL_BG      1

#define PADDLE_W 8
#define PADDLE_H 40
#define PADDLE_PLAYER_X 20
#define PADDLE_CPU_X 292

#define BALL_SIZE 8

typedef enum { MENU, PLAYING, GAMEOVER, QUIT } state_t;

static state_t state = MENU;

static int player_y, cpu_y;
static int ball_x, ball_y;
static int ball_vx, ball_vy;
static int player_score, cpu_score;
static int running;
static unsigned int pong_rng_state;

/* butter to move smooth*/
static int key_up_pressed = 0;
static int key_down_pressed = 0;

static void set_px(int x, int y, uint8_t c) {
    if (x >= 0 && x < 320 && y >= 0 && y < 200)
        pong_screen[y][x] = c;
}

static void fill_rect(int x, int y, int w, int h, uint8_t c) {
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++)
            set_px(x + i, y + j, c);
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
    if (val > 999) val = 999;
    char tmp[4];
    int len = 0;
    if (val == 0) {
        tmp[len++] = '0';
    } else {
        int v = val;
        while (v > 0 && len < 3) {
            tmp[len++] = '0' + (v % 10);
            v /= 10;
        }
    }
    for (int i = len - 1; i >= 0; i--) {
        draw_font_char(x, y, tmp[i], col);
        x += 8;
    }
}

static void build_pal(void) {
    for (int i = 0; i < 256; i++)
        pong_palette[i] = 0;
    pong_palette[0]      = 0x000000;
    pong_palette[1]      = 0x222222;
    pong_palette[8]      = 0x555555;
    pong_palette[15]     = 0xFFFFFF;
    pong_palette[COL_P1] = 0xFFD700;
    pong_palette[COL_CPU]= 0x4169E1;
    pong_palette[COL_BALL]=0xFF00FF;
    pong_palette[COL_NET]= 0x00FFFF;
    pong_palette[COL_ACCENT]=0x00FF00;
    for (int i = 0; i < 256; i++)
        if (pong_palette[i] == 0 && i != 0)
            pong_palette[i] = 0x333333;
    pong_palette[0] = 0x000000;
}

static unsigned int simple_rand(void) {
    pong_rng_state = pong_rng_state * 1103515245 + 12345;
    return (pong_rng_state >> 16) & 0x7FFF;
}

static void reset_ball(void) {
    ball_x = 160;
    ball_y = 100;
    ball_vx = 2;
    if (simple_rand() % 2) ball_vx = -ball_vx;
    ball_vy = 1 + (simple_rand() % 3) - 1;
}

static void init_game(void) {
    player_y = 80;
    cpu_y = 80;
    player_score = 0;
    cpu_score = 0;
    reset_ball();
}

static void draw_menu(void) {
    fill_rect(0, 0, 320, 200, COL_BG);

    int tx = 320 / 2 - 4 * 4;
    int ty = 50;
    const char* title = "PONG";
    for (int i = 0; title[i]; i++)
        draw_font_char(tx + i * 8, ty, title[i], COL_WHITE);

    tx = 320 / 2 - 11 * 4;
    ty = 80;
    const char* sub = "SharkOS Edition";
    for (int i = 0; sub[i]; i++)
        draw_font_char(tx + i * 8, ty, sub[i], COL_NET);

    ty = 110;
    const char* inst1 = "Press ENTER to start";
    for (int i = 0; inst1[i]; i++)
        draw_font_char(320 / 2 - 14 * 4 + i * 8, ty, inst1[i], COL_WHITE);

    ty = 125;
    const char* inst2 = "W/S or UP/DOWN to move";
    for (int i = 0; inst2[i]; i++)
        draw_font_char(320 / 2 - 16 * 4 + i * 8, ty, inst2[i], COL_GREY);

    ty = 140;
    const char* inst3 = "ESC to quit";
    for (int i = 0; inst3[i]; i++)
        draw_font_char(320 / 2 - 9 * 4 + i * 8, ty, inst3[i], COL_GREY);
}

static void draw_game(void) {
    fill_rect(0, 0, 320, 200, COL_BG);

    for (int y = 0; y < 200; y += 16)
        fill_rect(159, y, 2, 8, COL_NET);

    fill_rect(PADDLE_PLAYER_X, player_y, PADDLE_W, PADDLE_H, COL_P1);
    fill_rect(PADDLE_CPU_X, cpu_y, PADDLE_W, PADDLE_H, COL_CPU);

    fill_rect(ball_x, ball_y, BALL_SIZE, BALL_SIZE, COL_BALL);

    draw_number(130, 10, player_score, COL_P1);
    draw_number(180, 10, cpu_score, COL_CPU);
}

/* physics and cpu none sense*/
static void update(void) {
    int cpu_center = cpu_y + PADDLE_H / 2;
    if (cpu_center < ball_y - 4)
        cpu_y += 3;
    else if (cpu_center > ball_y + 4)
        cpu_y -= 3;

    if (cpu_y < 0) cpu_y = 0;
    if (cpu_y > 200 - PADDLE_H) cpu_y = 200 - PADDLE_H;

    ball_x += ball_vx;
    ball_y += ball_vy;

    if (ball_y <= 0) {
        ball_y = 0;
        ball_vy = -ball_vy;
    }
    if (ball_y >= 200 - BALL_SIZE) {
        ball_y = 200 - BALL_SIZE;
        ball_vy = -ball_vy;
    }

    if (ball_vx < 0 &&
        ball_x <= PADDLE_PLAYER_X + PADDLE_W &&
        ball_x >= PADDLE_PLAYER_X &&
        ball_y + BALL_SIZE > player_y &&
        ball_y < player_y + PADDLE_H) {
        ball_vx = -ball_vx;
        if (ball_vx < 0 && ball_vx > -7) ball_vx--;
        if (ball_vx > 0 && ball_vx < 7) ball_vx++;
        ball_vy += (ball_y - (player_y + PADDLE_H / 2)) / 5;
        if (ball_vy > 4) ball_vy = 4;
        if (ball_vy < -4) ball_vy = -4;
    }

    if (ball_vx > 0 &&
        ball_x + BALL_SIZE >= PADDLE_CPU_X &&
        ball_x + BALL_SIZE <= PADDLE_CPU_X + PADDLE_W &&
        ball_y + BALL_SIZE > cpu_y &&
        ball_y < cpu_y + PADDLE_H) {
        ball_vx = -ball_vx;
        if (ball_vx < 0 && ball_vx > -7) ball_vx--;
        if (ball_vx > 0 && ball_vx < 7) ball_vx++;
        ball_vy += (ball_y - (cpu_y + PADDLE_H / 2)) / 5;
        if (ball_vy > 4) ball_vy = 4;
        if (ball_vy < -4) ball_vy = -4;
    }

    if (ball_x < 0) {
        cpu_score++;
        if (cpu_score >= 10) {
            state = GAMEOVER;
            return;
        }
        reset_ball();
    }
    if (ball_x > 320) {
        player_score++;
        if (player_score >= 10) {
            state = GAMEOVER;
            return;
        }
        reset_ball();
    }

    /* smooth speed bitch */
    if (key_up_pressed && player_y > 0)
        player_y -= 3;
    if (key_down_pressed && player_y < 200 - PADDLE_H)
        player_y += 3;
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
                uint32_t color = pong_palette[pong_screen[y][x]];
                for (int sx = 0; sx < scale && x * scale + sx < (int)screen_width; sx++) {
                    int fx = x * scale + sx;
                    lfbptr[fy * stride + fx] = color;
                }
            }
        }
    }
}

void pong_init(void) {
    running = 1;
    state = MENU;
    build_pal();
    pong_rng_state = 12345;
    init_game();
}

void pong_cleanup(void) {
    running = 0;
    state = QUIT;
}

void pong_run(void) {
    if (!running) return;

    while (keyboard_getchar() != 0) yield();

    state = MENU;

    /* 60fps goes brrrrr */
    const uint32_t STEP_INTERVAL = 16;
    uint32_t last = uptime_ticks;

    while (running) {
        yield();

        char c;
        while ((c = keyboard_getchar()) != 0) {
            if (c == 27) { running = 0; state = QUIT; break; }
            if (c == '\n') {
                if (state == MENU) {
                    init_game();
                    state = PLAYING;
                    while (keyboard_getchar() != 0);
                } else if (state == GAMEOVER) {
                    state = MENU;
                    init_game();
                }
            }
            /* Movement shizzle */
            if (c == 'w' || c == 'W') key_up_pressed = 1;
            if (c == 's' || c == 'S') key_down_pressed = 1;
        }

        uint32_t now = uptime_ticks;
        if (now - last >= STEP_INTERVAL) {
            if (state == PLAYING) {
                update();
                draw_game();
            } else if (state == MENU) {
                draw_menu();
            } else if (state == GAMEOVER) {
                fill_rect(0, 0, 320, 200, COL_BG);
                int tx = 320 / 2 - 5 * 4;
                int ty = 90;
                const char* msg = (player_score >= 10) ? "YOU WIN!" : "CPU WINS!";
                for (int i = 0; msg[i]; i++)
                    draw_font_char(tx + i * 8, ty, msg[i], COL_WHITE);
                draw_number(320 / 2 - 4, 110, player_score, COL_P1);
                draw_font_char(320 / 2 - 8, 110, '-', COL_WHITE);
                draw_number(320 / 2 + 2, 110, cpu_score, COL_CPU);
                tx = 320 / 2 - 13 * 4;
                ty = 130;
                const char* retry = "Press ENTER to retry";
                for (int i = 0; retry[i]; i++)
                    draw_font_char(tx + i * 8, ty, retry[i], COL_GREY);
            }
            blit();
            last = now;
        }
        yield();
    }

    pong_cleanup();
}

void pong_draw_frame(void) {
    if (state == PLAYING) draw_game();
    else if (state == MENU) draw_menu();
    blit();
}

void pong_handle_key(int key) {
    if (key == 27) { running = 0; state = QUIT; return; }
    if (key == '\n') {
        if (state == MENU) {
            init_game();
            state = PLAYING;
        } else if (state == GAMEOVER) {
            state = MENU;
            init_game();
        }
    }
    if (key == 'w' || key == 'W') key_up_pressed = 1;
    if (key == 's' || key == 'S') key_down_pressed = 1;
}

void pong_set_kernel_mode(void) {
    key_up_pressed = 0;
    key_down_pressed = 0;
}

void pong_restore_kernel_mode(void) {
    terminal_initialize();
    redraw_all_panes();
    print_prompt();
}