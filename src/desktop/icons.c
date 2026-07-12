
#include "kernel.h"
#include "desktop.h"
#include "icon_data.h"

#define MAX_ICON_IMAGE 1024


typedef struct { uint32_t pixels[ICON_SIZE * ICON_SIZE]; } icon_image_t;
static icon_image_t icon_images[MAX_DESKTOP_ICONS];
static int icon_images_loaded = 0;


uint32_t* desktop_icon_get_pixels(int idx) {
    if (idx < 0 || idx >= icon_images_loaded || idx >= MAX_DESKTOP_ICONS) return NULL;
    return icon_images[idx].pixels;
}

void desktop_icons_load(void) {
    icon_images_loaded = 0;
    for (int i = 0; i < desktop.icon_count && i < MAX_DESKTOP_ICONS; i++) {
        const uint32_t* src = NULL;
        
        
        switch (desktop.icons[i].type) {
            case WINDOW_TYPE_TERMINAL:
                src = icon_data_terminal;
                break;
            case WINDOW_TYPE_DOOM:
                src = icon_data_doom;
                break;
            case WINDOW_TYPE_FLAPPYBIRD:
                src = icon_data_flappybird;
                break;
            case WINDOW_TYPE_PONG:
                src = icon_data_pong;
                break;
            case WINDOW_TYPE_GDASH:
                src = icon_data_gdash;
                break;
            case WINDOW_TYPE_SMB:
                src = icon_data_smb;
                break;
            case WINDOW_TYPE_SETTINGS:
                src = icon_data_settings;
                break;
            case WINDOW_TYPE_FAQ:
            case WINDOW_TYPE_FASTFETCH:
            case WINDOW_TYPE_ABOUT:
                src = icon_data_info;
                break;
            default:
                src = icon_data_info;
                break;
        }
        
        if (src) {
            
            for (int j = 0; j < ICON_SIZE * ICON_SIZE; j++) {
                icon_images[i].pixels[j] = src[j];
            }
            icon_images_loaded++;
        }
    }
}