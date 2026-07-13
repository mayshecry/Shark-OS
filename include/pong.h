#ifndef PONG_H
#define PONG_H

#include <stdint.h>
#include <stdbool.h>

#define PONG_SCREEN_W 320
#define PONG_SCREEN_H 200

extern uint8_t pong_screen[PONG_SCREEN_H][PONG_SCREEN_W];
extern uint32_t pong_palette[256];

void pong_init(void);
void pong_cleanup(void);
void pong_run(void);
void pong_draw_frame(void);
void pong_tick(void);
void pong_handle_key(int key);
void pong_set_kernel_mode(void);
void pong_restore_kernel_mode(void);
void pong_set_window_rect(int x, int y, int w, int h);

#endif