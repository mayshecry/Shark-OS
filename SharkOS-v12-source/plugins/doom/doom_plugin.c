#include "sharkapi.h"
#include "doom.h"

plugin_info_t doom_plugin_info = {
    .version = 1,
    .name = "doom",
    .author = "DoomGeneric and SharkOS",
    .description = "Doom engine with Freedoom Phase 1. Arrow keys move and turn, Ctrl fires, Space uses, Shift runs.",
    .major = 1,
    .minor = 0
};

int doom_plugin_init(void) {
    return 0;
}

void doom_plugin_cleanup(void) {
}

int doom_plugin_command(int argc, char** argv) {
    doom_set_kernel_mode();
    doom_init();
    doom_run();
    doom_restore_kernel_mode();
    return 0;
}

plugin_info_t* doom_plugin_get_info(void) {
    return &doom_plugin_info;
}
