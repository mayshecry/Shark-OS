

#include "kernel.h"
#include "desktop.h"
#include "icon_data.h"
#include "modern_icon_data.h"

typedef struct { uint32_t pixels[ICON_SIZE * ICON_SIZE]; } icon_image_t;
static icon_image_t icon_images[MAX_DESKTOP_ICONS];
static int icon_images_loaded = 0;

uint32_t* desktop_icon_get_pixels(int idx) {
    if (idx < 0 || idx >= icon_images_loaded || idx >= MAX_DESKTOP_ICONS) {
        return NULL;
    }
    return icon_images[idx].pixels;
}

uint32_t* desktop_icon_pixels_for_type(window_type_t type) {
    bool m = theme_current->modern_icons;
    switch (type) {
    case WINDOW_TYPE_TERMINAL:
        return (uint32_t*)(m ? micon_terminal : icon_data_terminal);
    case WINDOW_TYPE_DOOM:        return (uint32_t*)icon_data_doom;
    case WINDOW_TYPE_FLAPPYBIRD:  return (uint32_t*)icon_data_flappybird;
    case WINDOW_TYPE_PONG:        return (uint32_t*)icon_data_pong;
    case WINDOW_TYPE_GDASH:       return (uint32_t*)icon_data_gdash;
    case WINDOW_TYPE_SMB:         return (uint32_t*)icon_data_smb;
    case WINDOW_TYPE_SETTINGS:
        return (uint32_t*)(m ? micon_settings : icon_data_settings);
    case WINDOW_TYPE_NOTEPAD:
        return (uint32_t*)(m ? micon_notepad : icon_data_notepad);
    case WINDOW_TYPE_FILEMANAGER:
        return (uint32_t*)(m ? micon_filemanager : icon_data_filemanager);
    case WINDOW_TYPE_NETWORK:
        return (uint32_t*)(m ? micon_network : icon_data_network);
    case WINDOW_TYPE_TASKMANAGER:
        return (uint32_t*)(m ? micon_taskmanager : icon_data_taskmanager);
    case WINDOW_TYPE_BROWSER:
        return (uint32_t*)(m ? micon_browser : icon_data_browser);
    case WINDOW_TYPE_DISKMGMT:
        return (uint32_t*)(m ? micon_disk : icon_data_taskmanager);
    case WINDOW_TYPE_FAQ:
    case WINDOW_TYPE_FASTFETCH:
    case WINDOW_TYPE_ABOUT:
    default:
        return (uint32_t*)(m ? micon_info : icon_data_info);
    }
}

const uint32_t* desktop_icon_get_start(void) {
    return theme_current->modern_icons ? micon_start : icon_data_start;
}

const uint32_t* desktop_icon_get_shutdown(void) {
    return theme_current->modern_icons ? micon_shutdown : icon_data_shutdown;
}

const uint32_t* desktop_icon_get_folder(void) {
    return theme_current->modern_icons ? micon_folder : icon_data_folder;
}

const uint32_t* desktop_icon_get_file(void) {
    return theme_current->modern_icons ? micon_file : icon_data_file;
}

void desktop_icons_load(void) {
    icon_images_loaded = 0;

    for (int i = 0; i < desktop.icon_count && i < MAX_DESKTOP_ICONS; i++) {
        const uint32_t* src = desktop_icon_pixels_for_type(desktop.icons[i].type);

        for (int j = 0; j < ICON_SIZE * ICON_SIZE; j++) {
            icon_images[i].pixels[j] = src[j];
        }
        icon_images_loaded++;
    }
}
