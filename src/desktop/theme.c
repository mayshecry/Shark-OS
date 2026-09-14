#include "kernel.h"
#include "desktop.h"
#include "theme.h"

const desktop_theme_t theme_win98 = {
    "Windows 98 (Default)",
    0xFF008080u,
    0xFF007070u,
    0xFFC0C0C0u,
    0xFFFFFFFFu,
    0xFFDFDFDFu,
    0xFF808080u,
    0xFF000000u,
    0xFF000000u,
    0xFF808080u,
    0xFF000080u,
    0xFF1084D0u,
    0xFF808080u,
    0xFFB5B5B5u,
    0xFFFFFFFFu,
    0xFFD4D0C8u,
    0xFF000080u,
    0xFFFFFFFFu,
    0xFFFFFFFFu,
    0xFF000000u,
    0xFF000040u,
    0xFF0000A0u,
    0xFFC0C0C0u,
    0xFFFFFFFFu,
    false,
    false,
    false,
    false,
    false,
    false,
    false,
    0
};

const desktop_theme_t theme_modern = {
    "Modern Sleek",
    0xFF131722u,
    0xFF0D1119u,
    0xFF1F2430u,
    0xFF303849u,
    0xFF272E3Cu,
    0xFF141924u,
    0xFF0A0D13u,
    0xFFE8ECF4u,
    0xFF7E8798u,
    0xFF262C38u,
    0xFF262C38u,
    0xFF1A1E27u,
    0xFF1A1E27u,
    0xFFF2F5FAu,
    0xFF96A0B5u,
    0xFF2563EBu,
    0xFFFFFFFFu,
    0xFF141821u,
    0xFFE8ECF4u,
    0xFF1D4ED8u,
    0xFF0EA5E9u,
    0xFF1F2430u,
    0xFFE8ECF4u,
    true,
    true,
    true,
    true,
    true,
    true,
    true,
    12
};

const desktop_theme_t* theme_current = &theme_win98;
static int theme_active_id = THEME_ID_WIN98;

int theme_get_id(void) {
    return theme_active_id;
}

const char* theme_get_name(int id) {
    if (id == THEME_ID_MODERN) return theme_modern.name;
    return theme_win98.name;
}

void theme_set(int id) {
    if (id < 0 || id >= THEME_COUNT) return;
    if (id == theme_active_id) return;
    theme_active_id = id;
    theme_current = (id == THEME_ID_MODERN) ? &theme_modern : &theme_win98;
    window_title_icons_invalidate();
    if (desktop.initialized) desktop_icons_load();
    for (int i = 0; i < desktop.window_count; i++) {
        desktop.windows[i].needs_redraw = true;
    }
    desktop.dirty = true;
}
