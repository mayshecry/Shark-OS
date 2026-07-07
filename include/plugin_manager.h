#ifndef PLUGIN_MANAGER_H
#define PLUGIN_MANAGER_H

#include <stdint.h>
#include "sharkapi.h"


#define MAX_PLUGINS 16
#define MAX_PLUGIN_NAME 64
#define PLUGIN_DIR "/System/plugins"

typedef struct {
    uint32_t id;
    char name[MAX_PLUGIN_NAME];
    plugin_info_t* info;
    plugin_init_t init;
    plugin_cleanup_t cleanup;
    plugin_command_t command;
    uint8_t loaded;
    uint8_t auto_detected;  
} plugin_t;


void plugin_manager_init(void);


int plugin_load(const char* plugin_name);


int plugin_unload(uint32_t plugin_id);


int plugin_execute(uint32_t plugin_id, int argc, char** argv);


plugin_t* plugin_get_list(int* count);


plugin_t* plugin_get_by_name(const char* name);


int plugin_register_builtin(const char* name, plugin_init_t init, 
                             plugin_cleanup_t cleanup, plugin_command_t command);


void plugin_auto_detect(void);


int plugin_load_dynamic(const char* plugin_name, const char* elf_data, int data_size);


int plugin_is_plugin_command(const char* cmd_name, uint32_t* plugin_id);


plugin_info_t* plugin_get_info(uint32_t plugin_id);

#endif 