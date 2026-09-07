/* startmenu.c - Windows 98 style start menu.
 *
 * Layout: a vertical gradient sidebar with the product name rotated 90
 * degrees on the left, then a column of items (16x16 icon + label) and a
 * Shut Down entry under an etched separator.
 *
 * The draw, hover and click paths all use start_menu_item_rect() so they
 * can never disagree about where an item is. The item tables are 16 wide
 * (STARTMENU_MAX); the old code declared 8 slots and guarded on 10, writing
 * two elements past the end of both arrays.
 */

#include "kernel.h"
#include "desktop.h"
#include "win98_theme.h"
#include "plugin_manager.h"
#include "icon_data.h"

#define MENU_USER_H 0     /* header kept for binary-compatible callers */
#define MENU_HEADER_H 0

static const char* start_menu_items[STARTMENU_MAX];
static window_type_t start_menu_types[STARTMENU_MAX];
static int start_menu_count = 0;

/* ------------------------------------------------------------------ layout */

static int start_menu_height(void) {
    /* Sidebar is full height; items run from the top edge. The shutdown row
     * and its separator sit at the bottom. */
    int items = start_menu_count + 1;      /* +1 for Shut Down */
    return items * W98_MENU_ITEM_H + 4 + 2;
}

static void start_menu_anchor(int* mx, int* my) {
    *mx = 0;
    *my = (int)screen_height - TASKBAR_HEIGHT - start_menu_height();
}

/* One rectangle for draw, hover and click alike. `index == start_menu_count`
 * is the Shut Down row. */
static bool start_menu_item_rect(int index, int* rx, int* ry, int* rw,
                                 int* rh) {
    if (index < 0 || index > start_menu_count) return false;

    int mx, my;
    start_menu_anchor(&mx, &my);

    *rx = mx + 2;
    *ry = my + index * W98_MENU_ITEM_H + 2;
    *rw = STARTMENU_W - 4;
    *rh = W98_MENU_ITEM_H - 2;
    return true;
}

/* ---------------------------------------------------------------- lifecycle */

void start_menu_open(void) {
    desktop.start_menu.active = true;
    desktop.start_menu.visible = true;
    desktop.start_menu.scroll_offset = 0;
    desktop.start_menu.hovered_item = -1;
    desktop.start_menu.selected_item = -1;

    start_menu_count = 0;

    const char* names[STARTMENU_MAX];
    window_type_t types[STARTMENU_MAX];

    names[start_menu_count] = "Terminal";
    types[start_menu_count] = WINDOW_TYPE_TERMINAL;
    start_menu_count++;

    names[start_menu_count] = "Settings";
    types[start_menu_count] = WINDOW_TYPE_SETTINGS;
    start_menu_count++;

    names[start_menu_count] = "Notepad";
    types[start_menu_count] = WINDOW_TYPE_NOTEPAD;
    start_menu_count++;

    names[start_menu_count] = "File Manager";
    types[start_menu_count] = WINDOW_TYPE_FILEMANAGER;
    start_menu_count++;

    names[start_menu_count] = "Network";
    types[start_menu_count] = WINDOW_TYPE_NETWORK;
    start_menu_count++;

    int plugin_count = 0;
    plugin_t* plugins = plugin_get_list(&plugin_count);
    for (int i = 0; i < plugin_count && start_menu_count < STARTMENU_MAX - 2;
         i++) {
        if (!plugins[i].loaded) continue;
        const char* nm = NULL;
        window_type_t tp = WINDOW_TYPE_MAX;
        if (strcmp(plugins[i].name, "doom") == 0) {
            nm = "Doom"; tp = WINDOW_TYPE_DOOM;
        } else if (strcmp(plugins[i].name, "flappybird") == 0) {
            nm = "Flappy Bird"; tp = WINDOW_TYPE_FLAPPYBIRD;
        } else if (strcmp(plugins[i].name, "smb") == 0) {
            nm = "Super Mario Bros"; tp = WINDOW_TYPE_SMB;
        } else if (strcmp(plugins[i].name, "pong") == 0) {
            nm = "Pong"; tp = WINDOW_TYPE_PONG;
        } else if (strcmp(plugins[i].name, "gdash") == 0) {
            nm = "Geometry Dash"; tp = WINDOW_TYPE_GDASH;
        }
        if (nm && tp != WINDOW_TYPE_MAX) {
            names[start_menu_count] = nm;
            types[start_menu_count] = tp;
            start_menu_count++;
        }
    }

    if (start_menu_count < STARTMENU_MAX - 1) {
        names[start_menu_count] = "System Info";
        types[start_menu_count] = WINDOW_TYPE_FASTFETCH;
        start_menu_count++;
    }
    if (start_menu_count < STARTMENU_MAX - 1) {
        names[start_menu_count] = "About";
        types[start_menu_count] = WINDOW_TYPE_ABOUT;
        start_menu_count++;
    }
    if (start_menu_count < STARTMENU_MAX - 1) {
        names[start_menu_count] = "FAQ";
        types[start_menu_count] = WINDOW_TYPE_FAQ;
        start_menu_count++;
    }

    for (int i = 0; i < start_menu_count; i++) {
        start_menu_items[i] = names[i];
        start_menu_types[i] = types[i];
    }
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

/* ------------------------------------------------------------------- input */

static int start_menu_row_at(int mx, int my) {
    int ax, ay;
    start_menu_anchor(&ax, &ay);

    int mw = STARTMENU_W;
    int mh = start_menu_height();

    if (mx < ax || mx >= ax + mw || my < ay || my >= ay + mh) {
        return -1;
    }

    for (int i = 0; i <= start_menu_count; i++) {
        int rx, ry, rw, rh;
        start_menu_item_rect(i, &rx, &ry, &rw, &rh);
        if (my >= ry && my < ry + rh) {
            return i;
        }
    }
    return -1;
}

void start_menu_handle_hover(int mx, int my) {
    if (!desktop.start_menu.visible) return;
    desktop.start_menu.hovered_item = start_menu_row_at(mx, my);
}

void start_menu_handle_click(int mx, int my) {
    if (!desktop.start_menu.visible) return;

    int row = start_menu_row_at(mx, my);
    if (row < 0) {
        /* Click outside the menu closes it and is swallowed, like Win98. */
        start_menu_close();
        return;
    }

    if (row == start_menu_count) {
        /* Shut Down */
        start_menu_close();
        shutdown();
        return;
    }

    window_type_t tp = start_menu_types[row];
    const char* label = start_menu_items[row];
    start_menu_close();
    desktop_icon_launch_offset(tp, label);
}

/* ------------------------------------------------------------------- paint */

void start_menu_draw(void) {
    if (!desktop.start_menu.visible) return;

    int mx, my;
    start_menu_anchor(&mx, &my);
    int mw = STARTMENU_W;
    int mh = start_menu_height();

    w98_fill(mx, my, mw, mh, W98_MENU_BG);
    w98_bevel(mx, my, mw, mh, W98_BEVEL_RAISED);

    /* Sidebar gradient with the rotated product name. */
    int sb_w = W98_MENU_SIDEBAR_W;
    w98_vgradient(mx + 2, my + 2, sb_w - 2, mh - 4,
                  W98_CAPTION_START, W98_CAPTION_END);
    w98_text_vertical("SharkOS 98", mx + 6, my + mh - 6, W98_TITLETEXT, 1,
                      NULL);

    /* Items. */
    for (int i = 0; i <= start_menu_count; i++) {
        int rx, ry, rw, rh;
        start_menu_item_rect(i, &rx, &ry, &rw, &rh);

        bool is_shutdown = (i == start_menu_count);

        /* Etched separator above Shut Down. */
        if (is_shutdown) {
            w98_bevel(rx + 2, ry - 3, rw - 4, 2, W98_BEVEL_ETCHED);
        }

        if (desktop.start_menu.hovered_item == i) {
            w98_fill(rx + 1, ry, rw - 2, rh, W98_HIGHLIGHT);
        }

        const uint32_t* pix = NULL;
        const char* label;
        if (is_shutdown) {
            pix = desktop_icon_get_shutdown();
            label = "Shut Down...";
        } else {
            pix = desktop_icon_pixels_for_type(start_menu_types[i]);
            label = start_menu_items[i];
        }

        if (pix) {
            w98_icon_blit_small(pix, ICON_SIZE, W98_MENU_ICON,
                                rx + 6, ry + (rh - W98_MENU_ICON) / 2, NULL);
        }

        uint32_t fg = (desktop.start_menu.hovered_item == i)
                          ? W98_HIGHLIGHTTEXT
                          : W98_BTNTEXT;
        uint32_t bg = (desktop.start_menu.hovered_item == i)
                          ? W98_HIGHLIGHT
                          : W98_MENU_BG;
        w98_text(label, rx + 6 + W98_MENU_ICON + 6,
                 ry + (rh - 8) / 2, fg, bg, 1, NULL);
    }
}
