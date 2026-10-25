#include "sharkapi.h"
#include "flappybird.h"

plugin_info_t flappybird_plugin_info = {
    .version = 1,
    .name = "flappybird",
    .author = "SharkOS Team",
    .description = "Classic Flappy Bird arcade game. SPACE/W to flap, ESC to quit. Avoid pipes and score points!",
    .major = 1,
    .minor = 0
};

int flappybird_plugin_init(void) {
    return 0;
}

void flappybird_plugin_cleanup(void) {
}

int flappybird_plugin_command(int argc, char** argv) {
    flappybird_set_kernel_mode();
    flappybird_init();
    flappybird_run();
    flappybird_restore_kernel_mode();
    return 0;
}

plugin_info_t* flappybird_plugin_get_info(void) {
    return &flappybird_plugin_info;
}