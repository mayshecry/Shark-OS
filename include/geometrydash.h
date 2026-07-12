#ifndef GEOMETRYDASH_H
#define GEOMETRYDASH_H

#include <stdint.h>
#include <stdbool.h>

#define GD_SCREEN_W 320
#define GD_SCREEN_H 200

extern uint8_t gd_screen[GD_SCREEN_H][GD_SCREEN_W];
extern uint32_t gd_palette[256];

void gd_init(void);
void gd_cleanup(void);
void gd_run(void);
void gd_draw_frame(void);
void gd_handle_key(int key);
void gd_set_kernel_mode(void);
void gd_restore_kernel_mode(void);

#endif