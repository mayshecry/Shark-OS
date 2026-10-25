#include "sharkapi.h"
#include "geometrydash.h"

plugin_info_t geometrydash_plugin_info = {
    .version = 1,
    .name = "gdash",
    .author = "SharkOS Team",
    .description = "Geometry Dash clone. Jump on platforms, avoid spikes. SPACE/W to jump, ENTER to start, ESC to quit.",
    .major = 1,
    .minor = 0
};

int geometrydash_plugin_init(void) {
    return 0;
}

void geometrydash_plugin_cleanup(void) {
}

int geometrydash_plugin_command(int argc, char** argv) {
    gd_set_kernel_mode();
    gd_init();
    gd_run();
    gd_restore_kernel_mode();
    return 0;
}

plugin_info_t* geometrydash_plugin_get_info(void) {
    return &geometrydash_plugin_info;
}