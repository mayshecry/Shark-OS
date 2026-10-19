#include "sharkapi.h"

plugin_info_t fnaf_plugin_info = {
    .version = SHARKAPI_VERSION,
    .name = "fnaf",
    .author = "SharkOS",
    .description = "Five Nights at Freddy's 1 - survive from 12AM to 6AM. Monitor cameras, manage power, close doors (no audio, base game).",
    .major = 1, .minor = 0
};

int fnaf_plugin_init(void) {
    return 0;
}
void fnaf_plugin_cleanup(void) {
}

int fnaf_plugin_command(int argc, char **argv) {
    (void)argc; (void)argv;
    return 0;
}
plugin_info_t *fnaf_plugin_get_info(void) { return &fnaf_plugin_info; }

#ifndef FNAF_BUILTIN
int plugin_init_entry(void) { return fnaf_plugin_init(); }
void plugin_cleanup_entry(void) { fnaf_plugin_cleanup(); }
int plugin_command_entry(int argc, char **argv) { return fnaf_plugin_command(argc, argv); }
plugin_info_t *plugin_get_info(void) { return fnaf_plugin_get_info(); }
#endif
