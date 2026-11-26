#ifndef BROWSER_H
#define BROWSER_H

#include "kernel.h"
#include "desktop.h"

void app_window_draw_browser(window_t* w);
void app_window_mouse_browser(window_t* w, int mx, int my, int buttons);
void app_window_keyboard_browser(window_t* w, char c);
void browser_close(void);

void browser_open_url(const char* url);

void browser_tick(void);

#endif
