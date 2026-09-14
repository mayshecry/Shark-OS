#ifndef SHARKOS_THEME_H
#define SHARKOS_THEME_H

#include <stdbool.h>
#include <stdint.h>

#define THEME_ID_WIN98   0
#define THEME_ID_MODERN  1
#define THEME_COUNT      2

typedef struct {
    const char* name;
    uint32_t desktop;
    uint32_t desktop_dark;
    uint32_t btnface;
    uint32_t btnhilite;
    uint32_t btnlight;
    uint32_t btnshadow;
    uint32_t btndkshadow;
    uint32_t btntext;
    uint32_t graytext;
    uint32_t active_title;
    uint32_t active_title2;
    uint32_t inactive_title;
    uint32_t inactive_title2;
    uint32_t titletext;
    uint32_t titletext_inact;
    uint32_t highlight;
    uint32_t highlighttext;
    uint32_t window;
    uint32_t windowtext;
    uint32_t caption_start;
    uint32_t caption_end;
    uint32_t menu_bg;
    uint32_t icon_label;
    bool title_gradient;
    bool flat_bevels;
    bool nice_font;
    bool animations;
    bool flat_taskbar;
    bool title_accent_line;
    bool modern_icons;
    int ui_font_size;
} desktop_theme_t;

extern const desktop_theme_t theme_win98;
extern const desktop_theme_t theme_modern;
extern const desktop_theme_t* theme_current;

int theme_get_id(void);
void theme_set(int id);
const char* theme_get_name(int id);

#endif
