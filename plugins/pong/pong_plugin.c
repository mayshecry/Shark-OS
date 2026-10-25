#include "sharkapi.h"
#include "pong.h"

plugin_info_t pong_plugin_info = {
    .version = 1,
    .name = "pong",
    .author = "SharkOS Team",
    .description = "Classic Pong arcade game. Player vs CPU. W/S or UP/DOWN to move, ENTER to start, ESC to quit.",
    .major = 1,
    .minor = 0
};

int pong_plugin_init(void) {
    return 0;
}

void pong_plugin_cleanup(void) {
}

int pong_plugin_command(int argc, char** argv) {
    pong_set_kernel_mode();
    pong_init();
    pong_run();
    pong_restore_kernel_mode();
    return 0;
}

plugin_info_t* pong_plugin_get_info(void) {
    return &pong_plugin_info;
}