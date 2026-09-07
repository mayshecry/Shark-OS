
#ifndef DESKTOP_H
#define DESKTOP_H

#include "kernel.h"

#include "win98_theme.h"

#define MAX_WINDOWS 16
#define WINDOW_TITLE_MAX 32

/* Desktop icons are 32x32 Win98-style, laid out top-to-bottom-then-next-column
 * like Explorer. See W98_ICON_* in win98_theme.h. */
#define DESKTOP_ICON_SIZE W98_DESKTOP_ICON

#define TASKBAR_HEIGHT      W98_TASKBAR_H
#define START_BUTTON_W      W98_STARTBTN_W

#define WINDOW_BORDER_W     W98_BORDER_W
#define WINDOW_TITLEBAR_H   W98_TITLEBAR_H
#define WINDOW_MIN_W        160
#define WINDOW_MIN_H        80

#define STARTMENU_W         168
#define STARTMENU_MAX       16

#define DESKTOP_BG_TOP      W98_DESKTOP
#define DESKTOP_BG_BOT      W98_DESKTOP

typedef enum {
    WINDOW_TYPE_TERMINAL,
    WINDOW_TYPE_DOOM,
    WINDOW_TYPE_FLAPPYBIRD,
    WINDOW_TYPE_SMB,
    WINDOW_TYPE_PONG,
    WINDOW_TYPE_GDASH,
    WINDOW_TYPE_SETTINGS,
    WINDOW_TYPE_FAQ,
    WINDOW_TYPE_FASTFETCH,
    WINDOW_TYPE_ABOUT,
    WINDOW_TYPE_NOTEPAD,
    WINDOW_TYPE_FILEMANAGER,
    WINDOW_TYPE_NETWORK,
    WINDOW_TYPE_MAX
} window_type_t;

typedef enum {
    WINDOW_STATE_NORMAL,
    WINDOW_STATE_MINIMIZED,
    WINDOW_STATE_MAXIMIZED,
    WINDOW_STATE_CLOSING
} window_state_t;

typedef struct {
    int x;
    int y;
    int width;
    int height;
    int prev_x;
    int prev_y;
    int prev_w;
    int prev_h;
    int min_w;
    int min_h;
    int client_x;
    int client_y;
    int client_w;
    int client_h;
} window_rect_t;

typedef struct window window_t;

struct window {
    char title[WINDOW_TITLE_MAX];
    window_type_t type;
    window_state_t state;
    window_rect_t rect;
    bool visible;
    bool has_focus;
    bool needs_redraw;
    bool is_dragging;
    bool is_resizing;
    int drag_off_x;
    int drag_off_y;
    int resize_edge;
    int z_order;
    /* Which caption button (if any) is currently held down, so it can be
     * painted in its pressed state. W98_GLYPHKIND_* + 1, 0 = none. */
    int pressed_button;
    void (*draw_func)(window_t* w);
    void (*keyboard_func)(window_t* w, char c);
    void (*mouse_func)(window_t* w, int mx, int my, int buttons);
    void (*close_func)(void);
    void (*game_tick)(void);
    void* user_data;
};

typedef struct {
    char label[32];
    window_type_t type;
    int icon_x;
    int icon_y;
    bool selected;
} desktop_icon_t;

#define MAX_DESKTOP_ICONS 12

typedef struct {
    bool active;
    bool visible;
    int scroll_offset;
    int hovered_item;
    int selected_item;
} start_menu_t;

typedef struct {
    bool desktop_mode;
    bool initialized;
    bool dirty;
    bool icons_dirty;
    bool taskbar_dirty;
    window_t windows[MAX_WINDOWS];
    int window_count;
    int next_z_order;
    desktop_icon_t icons[MAX_DESKTOP_ICONS];
    int icon_count;
    start_menu_t start_menu;
    int taskbar_hover;
    uint32_t* desktop_wallpaper;
    int wallpaper_w;
    int wallpaper_h;
    bool alt_tab_active;
    int alt_tab_index;
    int wallpaper_mode;          /* w98_wall_mode_t */
    int selected_icon;           /* -1 = none */
    uint32_t last_click_tick;    /* double-click detection */
    int last_click_icon;
} desktop_state_t;

extern desktop_state_t desktop;
extern uint32_t wallpaper_top;
extern uint32_t wallpaper_bot;

#define NETWORK_WINDOW_W 500
#define NETWORK_WINDOW_H 350


void boot_screen_show(void);
void boot_screen_update(const char* message, int progress);
void boot_screen_hide(void);


void desktop_init(void);
void desktop_draw_wallpaper(void);
void desktop_draw_icons(void);
void desktop_draw_taskbar(void);
void desktop_draw_start_menu(void);
void desktop_render(void);
void desktop_handle_mouse(int mx, int my, int buttons);
void desktop_handle_keyboard(char c);
void desktop_update_taskbar(void);
int desktop_get_taskbar_hover(int mx, int my);
void desktop_set_wallpaper_color(uint32_t top, uint32_t bottom);
void desktop_set_wallpaper_mode(int mode);
void desktop_icons_clear_selection(void);


int window_create(window_type_t type, const char* title, int x, int y, int w, int h);
void window_close(int idx);
void window_minimize(int idx);
void window_maximize(int idx);
void window_restore(int idx);
void window_focus(int idx);
void window_move(int idx, int x, int y);
void window_resize(int idx, int w, int h);
window_t* window_get_focused(void);
void window_draw_all(void);
void window_draw_frame(window_t* w);
void window_update_client_rect(window_t* w);
void window_begin_drag(int mx, int my);
void window_begin_resize(int mx, int my);
void window_drag_update(int mx, int my);
void window_resize_update(int mx, int my);
void window_end_drag(void);
void window_redraw_clients(void);
void window_close_by_ptr(window_t* w);
void window_focus_top_visible(void);

/* Where in a window the pointer is. Drives both painting and input. */
typedef enum {
    WINDOW_HIT_NONE = 0,
    WINDOW_HIT_TITLEBAR,
    WINDOW_HIT_BTN_MIN,
    WINDOW_HIT_BTN_MAX,
    WINDOW_HIT_BTN_CLOSE,
    WINDOW_HIT_CLIENT,
    WINDOW_HIT_RESIZE     /* see window_t.resize_edge for which edge */
} window_hit_t;

window_hit_t window_hit_test(window_t* w, int mx, int my, int* resize_edge);
int window_caption_pressed(void);

/* Caption-button geometry, shared by the painter and the hit tester. */
void window_caption_button_rect(window_t* w, int which, int* rx, int* ry,
                                int* rw, int* rh);


int desktop_icon_add(const char* label, window_type_t type);
void desktop_icon_remove(int idx);
int desktop_icon_hit_test(int mx, int my);
void desktop_icon_launch(int idx);
void desktop_icon_launch_offset(window_type_t type, const char* title);
void desktop_icon_redraw(int idx);
void desktop_layout_icons(void);
void desktop_icons_load(void);
uint32_t* desktop_icon_get_pixels(int idx);
/* Icon pixels for a window type, whether or not a desktop icon exists for it.
 * Used for titlebar icons. Returns NULL when there is no art. */
uint32_t* desktop_icon_pixels_for_type(window_type_t type);
const uint32_t* desktop_icon_get_start(void);
const uint32_t* desktop_icon_get_shutdown(void);
const uint32_t* desktop_icon_get_folder(void);
const uint32_t* desktop_icon_get_file(void);


void start_menu_toggle(void);
void start_menu_open(void);
void start_menu_close(void);
void start_menu_draw(void);
void start_menu_handle_click(int mx, int my);
void start_menu_handle_hover(int mx, int my);


void app_window_draw_terminal(window_t* w);
void app_window_draw_doom(window_t* w);
void app_window_draw_flappybird(window_t* w);
void app_window_draw_smb(window_t* w);
void app_window_draw_pong(window_t* w);
void app_window_draw_gdash(window_t* w);
void app_window_draw_settings(window_t* w);
void app_window_mouse_settings(window_t* w, int mx, int my, int buttons);
void app_window_mouse_filemanager(window_t* w, int mx, int my, int buttons);
void app_window_draw_faq(window_t* w);
void app_window_draw_fastfetch(window_t* w);
void app_window_draw_about(window_t* w);
void app_window_draw_notepad(window_t* w);
void app_window_draw_filemanager(window_t* w);
void app_window_draw_network(window_t* w);


void app_window_keyboard_terminal(window_t* w, char c);
void app_window_keyboard_doom(window_t* w, char c);
void app_window_keyboard_flappybird(window_t* w, char c);
void app_window_keyboard_smb(window_t* w, char c);
void app_window_keyboard_pong(window_t* w, char c);
void app_window_keyboard_gdash(window_t* w, char c);
void app_window_keyboard_notepad(window_t* w, char c);
void app_window_keyboard_filemanager(window_t* w, char c);


extern int desktop_mouse_x;
extern int desktop_mouse_y;
extern bool desktop_mouse_down;
extern int desktop_drag_window;
extern int desktop_resize_window;
extern int desktop_resize_edge;

#endif 