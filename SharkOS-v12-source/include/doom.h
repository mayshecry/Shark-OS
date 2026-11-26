#ifndef DOOM_H
#define DOOM_H

#include <stdint.h>
#include <stdbool.h>

void doom_init(void);
void doom_cleanup(void);
void doom_run(void);
void doom_tick(void);
void doom_draw_frame(void);
void doom_handle_key(int key);
void doom_keyboard_scancode(uint8_t scancode, uint8_t extended);
void doom_mouse_packet(int dx, int dy, int buttons);
void doom_set_kernel_mode(void);
void doom_restore_kernel_mode(void);
bool doom_is_active(void);
void doom_set_window_rect(int x, int y, int w, int h);
void doom_engine_set_error(void);
void doom_engine_exit(void);
void doom_draw_fullscreen(void);

#endif
