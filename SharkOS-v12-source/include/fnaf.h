#ifndef FNAF_H
#define FNAF_H

#include <stdint.h>
#include <stdbool.h>

#define FNAF_SCREEN_W 320
#define FNAF_SCREEN_H 200

extern uint8_t fnaf_screen[FNAF_SCREEN_H][FNAF_SCREEN_W];
extern uint32_t fnaf_palette[256];

void fnaf_init(void);
void fnaf_cleanup(void);
void fnaf_tick(void);
void fnaf_draw_frame(void);
void fnaf_handle_key(int key);
void fnaf_handle_mouse(int x, int y, int buttons);
void fnaf_set_window_rect(int x, int y, int w, int h);
void fnaf_set_kernel_mode(void);
void fnaf_restore_kernel_mode(void);
bool fnaf_is_active(void);

#endif
