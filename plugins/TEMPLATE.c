
#include "sharkapi.h"
#include <stdint.h>

plugin_info_t plugin_info = {
    .version = SHARKAPI_VERSION,
    .name = "Example Plugin",
    .author = "Your Name",
    .description = "This is an example plugin for SharkOS",
    .major = 1,
    .minor = 0
};

int plugin_init(void) {
    sharkapi_println("Example plugin initialized!");
    return 0;
}

void plugin_cleanup(void) {
    sharkapi_println("Example plugin cleaning up...");
}

int plugin_command(int argc, char** argv) {
    sharkapi_printf("Example plugin called with %d arguments\n", argc);

    for (int i = 0; i < argc; i++) {
        sharkapi_printf("  argv[%d]: %s\n", i, argv[i]);
    }

    return 0;
}

int plugin_init_entry(void) {
    return plugin_init();
}

void plugin_cleanup_entry(void) {
    plugin_cleanup();
}

int plugin_command_entry(int argc, char** argv) {
    return plugin_command(argc, argv);
}

plugin_info_t* plugin_get_info(void) {
    return &plugin_info;
}
