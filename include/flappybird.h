#ifndef FLAPPYBIRD_H
#define FLAPPYBIRD_H

#include <stdint.h>
#include <stdbool.h>

#define FLAPPYBIRD_SCREEN_W 320
#define FLAPPYBIRD_SCREEN_H 200

extern uint8_t flappybird_screen[FLAPPYBIRD_SCREEN_H][FLAPPYBIRD_SCREEN_W];
extern uint32_t flappybird_palette[256];

void flappybird_init(void);
void flappybird_cleanup(void);
void flappybird_run(void);
void flappybird_draw_frame(void);
void flappybird_handle_key(int key);
void flappybird_set_kernel_mode(void);
void flappybird_restore_kernel_mode(void);

#endif