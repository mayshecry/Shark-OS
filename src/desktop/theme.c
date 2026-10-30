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
    0xFF303849u,
    true,
    true,
    true,
    true,
    true,
    true,
    true,
    12
};

                                                                  
                                                                   
                                                                    
                             
const desktop_theme_t theme_kawaii = {
    "Kawaii theme",
    0xFFF3C1D3u,                                         
    0xFFEDADC2u,                                         
    0xFFFDF0F5u,                                         
    0xFFFFFFFFu,                                         
    0xFFF9D8E4u,                                         
    0xFFD885B1u,                                         
    0xFF91355Eu,                                         
    0xFF4A2038u,                                         
    0xFFB98CA6u,                                         
    0xFFD25D89u,                                         
    0xFFCE75A1u,                                         
    0xFFF3C1D3u,                                         
    0xFFF9D8E4u,                                         
    0xFFFFFFFFu,                                         
    0xFFA6688Cu,                                         
    0xFFD25D89u,                                         
    0xFFFFFFFFu,                                         
    0xFFFFF7FAu,                                         
    0xFF4A2038u,                                         
    0xFFCE75A1u,                                         
    0xFFD25D89u,                                         
    0xFFFDF0F5u,                                         
    0xFFFFFFFFu,                                         
    0xFFD25D89u,                                         
    true,                                                
    false,                                               
    true,                                                
    true,                                                
    false,                                               
    true,                                                
    false,                                               
    0
};

const desktop_theme_t theme_windowmaker = {
    "WindowMaker",
    0xFF5B5E82u,   
    0xFF4A4E6Au,   
    0xFFBFBFBFu,   
    0xFFE8E8E8u,   
    0xFFC8C8C8u,   
    0xFF808080u,   
    0xFF000000u,   
    0xFF000000u,   
    0xFF808080u,   
    0xFF000000u,   
    0xFF000000u,   
    0xFF4A4A4Au,   
    0xFF4A4A4Au,   
    0xFFFFFFFFu,   
    0xFFC8C8C8u,   
    0xFF000000u,   
    0xFFFFFFFFu,  
    0xFFC8C8C8u,   
    0xFF000000u,   
    0xFF000000u,   
    0xFF000000u,   
    0xFFC8C8C8u,   
    0xFFFFFFFFu,   
    0xFF000000u,   
    false,        
    true,         
    false,        
    false,        
    true,         
    false,        
    false,        
    0             
};

const desktop_theme_t* theme_current = &theme_win98;
static int theme_active_id = THEME_ID_WIN98;
static int kawaii_saved_wallpaper = WP_ID_CLASSIC;

int theme_get_id(void) {
    return theme_active_id;
}

const char* theme_get_name(int id) {
    if (id == THEME_ID_MODERN) return theme_modern.name;
    if (id == THEME_ID_KAWAII) return theme_kawaii.name;
    if (id == THEME_ID_WINDOWMAKER) return theme_windowmaker.name;
    return theme_win98.name;
}

void theme_set(int id) {
    if (id < 0 || id >= THEME_COUNT) return;
    if (id == theme_active_id) return;
    int prev = theme_active_id;
    theme_active_id = id;
    if (id == THEME_ID_MODERN) theme_current = &theme_modern;
    else if (id == THEME_ID_KAWAII) theme_current = &theme_kawaii;
    else if (id == THEME_ID_WINDOWMAKER) theme_current = &theme_windowmaker;
    else theme_current = &theme_win98;
    window_title_icons_invalidate();
    if (desktop.initialized) desktop_icons_load();
                                                                      
                                                                    
                                                                        
    if (id == THEME_ID_KAWAII) {
        kawaii_saved_wallpaper = desktop.wallpaper_id;
        desktop_set_wallpaper(WP_ID_KAWAII);
    } else if (prev == THEME_ID_KAWAII &&
               desktop.wallpaper_id == WP_ID_KAWAII) {
        desktop_set_wallpaper(kawaii_saved_wallpaper);
    } else {
        desktop_set_wallpaper_mode(desktop.wallpaper_mode);
    }
    for (int i = 0; i < desktop.window_count; i++) {
        desktop.windows[i].needs_redraw = true;
    }
    desktop.dirty = true;
}
