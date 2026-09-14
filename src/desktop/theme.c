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

/* Kawaii theme — pink colour scheme sampled from the bundled anime
 * wallpaper: sakura hair pink, sailor-uniform pink and a dark plum
 * "ribbon" accent. The SharkOS shark logo keeps its shape but wears
 * the sailor-uniform pink. */
const desktop_theme_t theme_kawaii = {
    "Kawaii theme",
    0xFFF3C1D3u,   /* desktop: soft hair pink            */
    0xFFEDADC2u,   /* desktop_dark                       */
    0xFFFDF0F5u,   /* btnface: blossom white             */
    0xFFFFFFFFu,   /* btnhilite                          */
    0xFFF9D8E4u,   /* btnlight                           */
    0xFFD885B1u,   /* btnshadow: skirt pink              */
    0xFF91355Eu,   /* btndkshadow: dark skirt pink       */
    0xFF4A2038u,   /* btntext: dark plum                 */
    0xFFB98CA6u,   /* graytext                           */
    0xFFD25D89u,   /* active_title: sailor collar pink   */
    0xFFCE75A1u,   /* active_title2: hair pink           */
    0xFFF3C1D3u,   /* inactive_title                     */
    0xFFF9D8E4u,   /* inactive_title2                    */
    0xFFFFFFFFu,   /* titletext                          */
    0xFFA6688Cu,   /* titletext_inact                    */
    0xFFD25D89u,   /* highlight: sailor pink             */
    0xFFFFFFFFu,   /* highlighttext                      */
    0xFFFFF7FAu,   /* window                             */
    0xFF4A2038u,   /* windowtext                         */
    0xFFCE75A1u,   /* caption_start                      */
    0xFFD25D89u,   /* caption_end                        */
    0xFFFDF0F5u,   /* menu_bg                            */
    0xFFFFFFFFu,   /* icon_label                         */
    0xFFD25D89u,   /* logo: the shark, in sailor pink    */
    true,          /* title_gradient                     */
    false,         /* flat_bevels: keep the 98 bevels    */
    true,          /* nice_font                          */
    true,          /* animations                         */
    false,         /* flat_taskbar                       */
    true,          /* title_accent_line                  */
    false,         /* modern_icons                       */
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
    return theme_win98.name;
}

void theme_set(int id) {
    if (id < 0 || id >= THEME_COUNT) return;
    if (id == theme_active_id) return;
    int prev = theme_active_id;
    theme_active_id = id;
    if (id == THEME_ID_MODERN) theme_current = &theme_modern;
    else if (id == THEME_ID_KAWAII) theme_current = &theme_kawaii;
    else theme_current = &theme_win98;
    window_title_icons_invalidate();
    if (desktop.initialized) desktop_icons_load();
    /* The Kawaii theme ships with its own wallpaper: entering it swaps
     * the desk to the kawaii image (remembering the user's choice),
     * leaving it restores that choice; any switch rebuilds the cache. */
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
