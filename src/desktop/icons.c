#include "kernel.h"
#include "desktop.h"
#include "theme.h"
#include "icon_data.h"
#include "modern_icon_data.h"
#include "kawaii_icon_data.h"
#include "windowmaker_icon_data.h"
#include "sharkcraft_icon_data.h"
#include "modern_ext_icon_data.h"

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
    int tid = theme_get_id();
     
    if (tid == THEME_ID_WIN98) {
        switch (type) {
        case WINDOW_TYPE_TERMINAL:    return (uint32_t*)icon_data_terminal;
        case WINDOW_TYPE_DOOM:        return (uint32_t*)icon_data_doom;
        case WINDOW_TYPE_FLAPPYBIRD:  return (uint32_t*)icon_data_flappybird;
        case WINDOW_TYPE_PONG:        return (uint32_t*)icon_data_pong;
        case WINDOW_TYPE_GDASH:       return (uint32_t*)icon_data_gdash;
        case WINDOW_TYPE_SHARKCRAFT:  return (uint32_t*)icon_data_sharkcraft_w98;
        case WINDOW_TYPE_SHARKCUBE:   return (uint32_t*)icon_data_sharkcraft_w98;  
        case WINDOW_TYPE_SMB:         return (uint32_t*)icon_data_smb;
        case WINDOW_TYPE_FNAF:        return (uint32_t*)icon_data_fnaf;
        case WINDOW_TYPE_SETTINGS:    return (uint32_t*)icon_data_settings;
        case WINDOW_TYPE_NOTEPAD:     return (uint32_t*)icon_data_notepad;
        case WINDOW_TYPE_FILEMANAGER: return (uint32_t*)icon_data_filemanager;
        case WINDOW_TYPE_NETWORK:     return (uint32_t*)icon_data_network;
        case WINDOW_TYPE_TASKMANAGER: return (uint32_t*)icon_data_taskmanager;
        case WINDOW_TYPE_BROWSER:     return (uint32_t*)icon_data_browser;
        case WINDOW_TYPE_DISKMGMT:    return (uint32_t*)icon_data_folder;  
        case WINDOW_TYPE_FAQ:
        case WINDOW_TYPE_FASTFETCH:
        case WINDOW_TYPE_ABOUT:
        default:                      return (uint32_t*)icon_data_info;
        }
    }
     
    if (tid == THEME_ID_MODERN) {
        switch (type) {
        case WINDOW_TYPE_TERMINAL:    return (uint32_t*)micon_terminal;
        case WINDOW_TYPE_DOOM:        return (uint32_t*)micon_doom;
        case WINDOW_TYPE_FLAPPYBIRD:  return (uint32_t*)micon_flappybird;
        case WINDOW_TYPE_PONG:        return (uint32_t*)micon_pong;
        case WINDOW_TYPE_GDASH:       return (uint32_t*)micon_gdash;
        case WINDOW_TYPE_SHARKCRAFT:  return (uint32_t*)modern_icon_data_sharkcraft;
        case WINDOW_TYPE_SHARKCUBE:   return (uint32_t*)modern_icon_data_sharkcraft;
        case WINDOW_TYPE_SMB:         return (uint32_t*)micon_smb;
        case WINDOW_TYPE_FNAF:        return (uint32_t*)micon_fnaf;
        case WINDOW_TYPE_SETTINGS:    return (uint32_t*)micon_settings;
        case WINDOW_TYPE_NOTEPAD:     return (uint32_t*)micon_notepad;
        case WINDOW_TYPE_FILEMANAGER: return (uint32_t*)micon_filemanager;
        case WINDOW_TYPE_NETWORK:     return (uint32_t*)micon_network;
        case WINDOW_TYPE_TASKMANAGER: return (uint32_t*)micon_taskmanager;
        case WINDOW_TYPE_BROWSER:     return (uint32_t*)micon_browser;
        case WINDOW_TYPE_DISKMGMT:    return (uint32_t*)micon_disk;
        case WINDOW_TYPE_FAQ:
        case WINDOW_TYPE_FASTFETCH:
        case WINDOW_TYPE_ABOUT:
        default:                      return (uint32_t*)micon_info;
        }
    }
     
    if (tid == THEME_ID_KAWAII) {
        switch (type) {
        case WINDOW_TYPE_TERMINAL:    return (uint32_t*)kawaii_icon_data_terminal;
        case WINDOW_TYPE_DOOM:        return (uint32_t*)kawaii_icon_data_doom;
        case WINDOW_TYPE_FLAPPYBIRD:  return (uint32_t*)kawaii_icon_data_flappybird;
        case WINDOW_TYPE_PONG:        return (uint32_t*)kawaii_icon_data_pong;
        case WINDOW_TYPE_GDASH:       return (uint32_t*)kawaii_icon_data_gdash;
        case WINDOW_TYPE_SHARKCRAFT:  return (uint32_t*)kawaii_icon_data_sharkcraft;
        case WINDOW_TYPE_SHARKCUBE:   return (uint32_t*)kawaii_icon_data_sharkcraft;
        case WINDOW_TYPE_SMB:         return (uint32_t*)kawaii_icon_data_smb;
        case WINDOW_TYPE_FNAF:        return (uint32_t*)kawaii_icon_data_fnaf;
        case WINDOW_TYPE_SETTINGS:    return (uint32_t*)kawaii_icon_data_settings;
        case WINDOW_TYPE_NOTEPAD:     return (uint32_t*)kawaii_icon_data_notepad;
        case WINDOW_TYPE_FILEMANAGER: return (uint32_t*)kawaii_icon_data_filemanager;
        case WINDOW_TYPE_NETWORK:     return (uint32_t*)kawaii_icon_data_network;
        case WINDOW_TYPE_TASKMANAGER: return (uint32_t*)kawaii_icon_data_taskmanager;
        case WINDOW_TYPE_BROWSER:     return (uint32_t*)kawaii_icon_data_browser;
        case WINDOW_TYPE_DISKMGMT:    return (uint32_t*)kawaii_icon_data_disk;
        case WINDOW_TYPE_FAQ:
        case WINDOW_TYPE_FASTFETCH:
        case WINDOW_TYPE_ABOUT:
        default:                      return (uint32_t*)kawaii_icon_data_info;
        }
    }
     
    if (tid == THEME_ID_WINDOWMAKER) {
        switch (type) {
        case WINDOW_TYPE_TERMINAL:    return (uint32_t*)wm_icon_data_terminal;
        case WINDOW_TYPE_DOOM:        return (uint32_t*)wm_icon_data_doom;
        case WINDOW_TYPE_FLAPPYBIRD:  return (uint32_t*)wm_icon_data_flappybird;
        case WINDOW_TYPE_PONG:        return (uint32_t*)wm_icon_data_pong;
        case WINDOW_TYPE_GDASH:       return (uint32_t*)wm_icon_data_gdash;
        case WINDOW_TYPE_SHARKCRAFT:  return (uint32_t*)wm_icon_data_sharkcraft;
        case WINDOW_TYPE_SHARKCUBE:   return (uint32_t*)wm_icon_data_sharkcraft;
        case WINDOW_TYPE_SMB:         return (uint32_t*)wm_icon_data_smb;
        case WINDOW_TYPE_FNAF:        return (uint32_t*)wm_icon_data_fnaf;
        case WINDOW_TYPE_SETTINGS:    return (uint32_t*)wm_icon_data_settings;
        case WINDOW_TYPE_NOTEPAD:     return (uint32_t*)wm_icon_data_notepad;
        case WINDOW_TYPE_FILEMANAGER: return (uint32_t*)wm_icon_data_filemanager;
        case WINDOW_TYPE_NETWORK:     return (uint32_t*)wm_icon_data_network;
        case WINDOW_TYPE_TASKMANAGER: return (uint32_t*)wm_icon_data_taskmanager;
        case WINDOW_TYPE_BROWSER:     return (uint32_t*)wm_icon_data_browser;
        case WINDOW_TYPE_DISKMGMT:    return (uint32_t*)wm_icon_data_disk;
        case WINDOW_TYPE_FAQ:
        case WINDOW_TYPE_FASTFETCH:
        case WINDOW_TYPE_ABOUT:
        default:                      return (uint32_t*)wm_icon_data_info;
        }
    }
     
    return (uint32_t*)icon_data_info;
}

const uint32_t* desktop_icon_get_start(void) {
    int tid = theme_get_id();
    if (tid == THEME_ID_MODERN) return micon_start;
    if (tid == THEME_ID_KAWAII) return kawaii_icon_data_start;
    if (tid == THEME_ID_WINDOWMAKER) return wm_icon_data_start;
    return icon_data_start;
}

const uint32_t* desktop_icon_get_shutdown(void) {
    int tid = theme_get_id();
    if (tid == THEME_ID_MODERN) return micon_shutdown;
    if (tid == THEME_ID_KAWAII) return kawaii_icon_data_shutdown;
    if (tid == THEME_ID_WINDOWMAKER) return wm_icon_data_shutdown;
    return icon_data_shutdown;
}

const uint32_t* desktop_icon_get_folder(void) {
    int tid = theme_get_id();
    if (tid == THEME_ID_MODERN) return micon_folder;
    if (tid == THEME_ID_KAWAII) return kawaii_icon_data_folder;
    if (tid == THEME_ID_WINDOWMAKER) return wm_icon_data_folder;
    return icon_data_folder;
}

const uint32_t* desktop_icon_get_file(void) {
    int tid = theme_get_id();
    if (tid == THEME_ID_MODERN) return micon_file;
    if (tid == THEME_ID_KAWAII) return kawaii_icon_data_file;
    if (tid == THEME_ID_WINDOWMAKER) return wm_icon_data_file;
    return icon_data_file;
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
