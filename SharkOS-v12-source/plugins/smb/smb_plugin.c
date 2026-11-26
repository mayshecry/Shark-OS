#include "sharkapi.h"
#include "smb.h"

plugin_info_t smb_plugin_info = {
    .version = 1,
    .name = "smb",
    .author = "SharkOS Team",
    .description = "Super Mario Bros-style 2D platformer. ARROW KEYS to move/jump, SPACE to jump. Avoid goombas, reach the flag!",
    .major = 1,
    .minor = 0
};

int smb_plugin_init(void) {
    return 0;
}

void smb_plugin_cleanup(void) {
}

int smb_plugin_command(int argc, char** argv) {
    smb_set_kernel_mode();
    smb_init();
    smb_run();
    smb_restore_kernel_mode();
    return 0;
}

plugin_info_t* smb_plugin_get_info(void) {
    return &smb_plugin_info;
}