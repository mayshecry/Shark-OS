
#include "kernel.h"
#include "desktop.h"
#include "plugin_manager.h"

#define MENU_ITEM_H 28
#define MENU_HEADER_H 72
#define MENU_USER_H 48
#define MENU_ICON_SIZE 20

static const char* start_menu_items[] = {
    "Terminal",
    "Settings",
    "FAQ",
    "System Info",
    "About",
    NULL
};

static window_type_t start_menu_types[] = {
    WINDOW_TYPE_TERMINAL,
    WINDOW_TYPE_SETTINGS,
    WINDOW_TYPE_FAQ,
    WINDOW_TYPE_FASTFETCH,
    WINDOW_TYPE_ABOUT
};

static int start_menu_count = 5;

void start_menu_open(void) {
    desktop.start_menu.active = true;
    desktop.start_menu.visible = true;
    desktop.start_menu.scroll_offset = 0;
    desktop.start_menu.hovered_item = -1;
    desktop.start_menu.selected_item = -1;
    
    
    start_menu_count = 0;
    
    
    start_menu_items[start_menu_count] = "Terminal";
    start_menu_types[start_menu_count] = WINDOW_TYPE_TERMINAL;
    start_menu_count++;
    
    
    int plugin_count = 0;
    plugin_t* plugins = plugin_get_list(&plugin_count);
    for (int i = 0; i < plugin_count; i++) {
        if (plugins[i].loaded) {
            if (strcmp(plugins[i].name, "doom") == 0 && start_menu_count < 10) {
                start_menu_items[start_menu_count] = "Doom";
                start_menu_types[start_menu_count] = WINDOW_TYPE_DOOM;
                start_menu_count++;
            } else if (strcmp(plugins[i].name, "flappybird") == 0 && start_menu_count < 10) {
                start_menu_items[start_menu_count] = "Flappy Bird";
                start_menu_types[start_menu_count] = WINDOW_TYPE_FLAPPYBIRD;
                start_menu_count++;
            } else if (strcmp(plugins[i].name, "smb") == 0 && start_menu_count < 10) {
                start_menu_items[start_menu_count] = "Super Mario Bros";
                start_menu_types[start_menu_count] = WINDOW_TYPE_SMB;
                start_menu_count++;
            } else if (strcmp(plugins[i].name, "pong") == 0 && start_menu_count < 10) {
                start_menu_items[start_menu_count] = "Pong";
                start_menu_types[start_menu_count] = WINDOW_TYPE_PONG;
                start_menu_count++;
            } else if (strcmp(plugins[i].name, "gdash") == 0 && start_menu_count < 10) {
                start_menu_items[start_menu_count] = "Geometry Dash";
                start_menu_types[start_menu_count] = WINDOW_TYPE_GDASH;
                start_menu_count++;
            }
        }
    }
    
    
    if (start_menu_count < 10) {
        start_menu_items[start_menu_count] = "Settings";
        start_menu_types[start_menu_count] = WINDOW_TYPE_SETTINGS;
        start_menu_count++;
    }
    if (start_menu_count < 10) {
        start_menu_items[start_menu_count] = "FAQ";
        start_menu_types[start_menu_count] = WINDOW_TYPE_FAQ;
        start_menu_count++;
    }
    if (start_menu_count < 10) {
        start_menu_items[start_menu_count] = "System Info";
        start_menu_types[start_menu_count] = WINDOW_TYPE_FASTFETCH;
        start_menu_count++;
    }
    if (start_menu_count < 10) {
        start_menu_items[start_menu_count] = "About";
        start_menu_types[start_menu_count] = WINDOW_TYPE_ABOUT;
        start_menu_count++;
    }
    
    start_menu_items[start_menu_count] = NULL;
}

void start_menu_close(void) {
    desktop.start_menu.active = false;
    desktop.start_menu.visible = false;
    desktop.start_menu.hovered_item = -1;
}

void start_menu_toggle(void) {
    if (desktop.start_menu.active) {
        start_menu_close();
    } else {
        start_menu_open();
    }
}

void start_menu_handle_hover(int mx, int my) {
    if (!desktop.start_menu.visible) return;
    
    int menu_x = 0;
    int menu_y = screen_height - TASKBAR_HEIGHT - STARTMENU_H;
    
    if (mx < menu_x || mx > menu_x + STARTMENU_W ||
        my < menu_y || my > menu_y + STARTMENU_H) {
        desktop.start_menu.hovered_item = -1;
        return;
    }
    
    int rel_y = my - (menu_y + MENU_HEADER_H);
    if (rel_y < 0) {
        desktop.start_menu.hovered_item = -1;
        return;
    }
    
    int item = rel_y / MENU_ITEM_H;
    if (item >= 0 && item < start_menu_count) {
        desktop.start_menu.hovered_item = item;
    } else {
        desktop.start_menu.hovered_item = -1;
    }
}

void start_menu_handle_click(int mx, int my) {
    if (!desktop.start_menu.visible) return;
    
    int menu_x = 0;
    int menu_y = screen_height - TASKBAR_HEIGHT - STARTMENU_H;
    
    if (mx < menu_x || mx > menu_x + STARTMENU_W ||
        my < menu_y || my > menu_y + STARTMENU_H) {
        start_menu_close();
        return;
    }
    
    int rel_y = my - (menu_y + MENU_HEADER_H);
    if (rel_y < 0) return;
    
    int item = rel_y / MENU_ITEM_H;
    if (item >= 0 && item < start_menu_count) {
        desktop.start_menu.selected_item = item;
        start_menu_close();
        
        
        desktop_icon_launch_offset(start_menu_types[item], start_menu_items[item]);
    }
}

void start_menu_draw(void) {
    if (!desktop.start_menu.visible) return;
    
    int menu_x = 0;
    int menu_y = screen_height - TASKBAR_HEIGHT - STARTMENU_H;
    int menu_w = STARTMENU_W;
    int menu_h = STARTMENU_H;
    
    uint32_t stride = (uint32_t)(screen_pitch / 4);
    
    
    draw_rect(menu_x, menu_y, menu_w, menu_h, STARTMENU_BG);
    
    
    for (int dx = 0; dx < menu_w; dx++) {
        if (menu_y < (int)screen_height) lfbptr[menu_y * stride + (menu_x + dx)] = STARTMENU_BORDER;
        if (menu_y + menu_h - 1 < (int)screen_height) lfbptr[(menu_y + menu_h - 1) * stride + (menu_x + dx)] = STARTMENU_BORDER;
    }
    for (int dy = 0; dy < menu_h; dy++) {
        if (menu_y + dy < (int)screen_height) {
            lfbptr[(menu_y + dy) * stride + menu_x] = STARTMENU_BORDER;
            lfbptr[(menu_y + dy) * stride + (menu_x + menu_w - 1)] = STARTMENU_BORDER;
        }
    }
    
    
    int user_panel_y = menu_y + 4;
    int user_panel_h = MENU_USER_H;
    draw_rect(menu_x + 4, user_panel_y, menu_w - 8, user_panel_h, 0xFF0F0F2A);
    
    
    int cx = menu_x + 30;
    int cy = user_panel_y + user_panel_h / 2;
    for (int dy = -8; dy <= 8; dy++) {
        for (int dx = -8; dx <= 8; dx++) {
            if (dx*dx + dy*dy <= 64) {
                int px = cx + dx;
                int py = cy + dy;
                if (px >= 0 && px < (int)screen_width && py >= 0 && py < (int)screen_height) {
                    lfbptr[py * stride + px] = 0xFF00FFFF;
                }
            }
        }
    }
    
    
    draw_string_px(current_user, menu_x + 48, user_panel_y + 10, 0xFFFFFFFF, 0xFF0F0F2A);
    
    
    char date_str[32];
    int p = 0;
    int_to_string(rtc_day, date_str + p); p = strlen(date_str);
    date_str[p++] = '/';
    int_to_string(rtc_month, date_str + p); p = strlen(date_str);
    date_str[p++] = '/';
    int_to_string(rtc_year, date_str + p); p = strlen(date_str);
    date_str[p] = '\0';
    draw_string_px(date_str, menu_x + 48, user_panel_y + 22, 0xFF888888, 0xFF0F0F2A);
    
    
    int sep_y = user_panel_y + user_panel_h + 2;
    for (int dx = 8; dx < menu_w - 8; dx++) {
        if (sep_y < (int)screen_height)
            lfbptr[sep_y * stride + (menu_x + dx)] = 0xFF333355;
    }
    
    
    draw_string_px("Applications", menu_x + 12, menu_y + MENU_HEADER_H - 20, 0xFF00FFFF, STARTMENU_BG);
    
    
    int items_start_y = menu_y + MENU_HEADER_H;
    for (int i = 0; i < start_menu_count; i++) {
        int item_y = items_start_y + i * MENU_ITEM_H;
        
        if (i == desktop.start_menu.hovered_item) {
            draw_rect(menu_x + 4, item_y, menu_w - 8, MENU_ITEM_H - 2, STARTMENU_HOVER);
        }
        
        uint32_t text_color = (i == desktop.start_menu.hovered_item) ? 0xFF00FFFF : 0xFFCCCCCC;
        uint32_t bg_color = (i == desktop.start_menu.hovered_item) ? STARTMENU_HOVER : STARTMENU_BG;
        draw_string_px(start_menu_items[i], menu_x + 12, item_y + (MENU_ITEM_H - 8) / 2, text_color, bg_color);
    }
    
    
    int bottom_y = menu_y + menu_h - 36;
    draw_rect(menu_x + 4, bottom_y, menu_w - 8, 28, 0xFF0F0F2A);
    draw_string_px("SharkOS v2.2", menu_x + 12, bottom_y + 8, 0xFF888888, 0xFF0F0F2A);
}
