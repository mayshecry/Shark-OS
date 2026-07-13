
#ifndef DESKTOP_H
#define DESKTOP_H

#include "kernel.h"

#define MAX_WINDOWS 16
#define WINDOW_TITLE_MAX 32
#define DESKTOP_ICON_SIZE 64
#define DESKTOP_ICON_GAP 16
#define DESKTOP_ICONS_PER_ROW 6
#define TASKBAR_HEIGHT 32
#define TASKBAR_BG 0xFF1A1A2E
#define TASKBAR_BORDER 0xFF00FFFF
#define TASKBAR_TEXT 0xFFFFFF00
#define START_BUTTON_W 80
#define WINDOW_BORDER_COLOR 0xFF00FFFF
#define WINDOW_TITLE_BG 0xFF0F0F2A
#define WINDOW_BG 0xFF1A1A2E
#define WINDOW_SHADOW_ALPHA 0x40
#define WINDOW_MIN_W 160
#define WINDOW_MIN_H 80
#define STARTMENU_W 220
#define STARTMENU_H 300
#define STARTMENU_BG 0xFF1A1A2E
#define STARTMENU_BORDER 0xFF00FFFF
#define STARTMENU_HOVER 0xFF2A2A4E
#define DESKTOP_BG_TOP 0xFF0A0A2A
#define DESKTOP_BG_BOT 0xFF050520

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
    uint32_t* fb_backup;
    int backup_w;
    int backup_h;
    int z_order;
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
    bool hovered;
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
void window_begin_drag(int mx, int my);
void window_begin_resize(int mx, int my);
void window_drag_update(int mx, int my);
void window_resize_update(int mx, int my);
void window_end_drag(void);
void window_redraw_clients(void);
void window_save_background(window_t* w);
void window_restore_background(window_t* w);
void window_close_by_ptr(window_t* w);


int desktop_icon_add(const char* label, window_type_t type);
void desktop_icon_remove(int idx);
int desktop_icon_hit_test(int mx, int my);
void desktop_icon_launch(int idx);
void desktop_icon_launch_offset(window_type_t type, const char* title);
void desktop_icon_redraw(int idx);
void desktop_layout_icons(void);
void desktop_icons_load(void);
uint32_t* desktop_icon_get_pixels(int idx);


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