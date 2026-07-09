#include "kernel.h"
#include "plugin_manager.h"

static plugin_t g_plugins[MAX_PLUGINS];
static int g_plugin_count = 0;

#define MAX_PLUGIN_DATA_SIZE 8192
static char g_plugin_data[MAX_PLUGINS][MAX_PLUGIN_DATA_SIZE];
static int g_plugin_data_size[MAX_PLUGINS];

void plugin_manager_init(void) {
    g_plugin_count = 0;
    memset(g_plugins, 0, sizeof(g_plugins));

    plugin_auto_detect();
}

int plugin_load(const char* plugin_name) {
    if (g_plugin_count >= MAX_PLUGINS) {
        return -1;
    }

    for (int i = 0; i < g_plugin_count; i++) {
        if (strcmp(g_plugins[i].name, (char*)plugin_name) == 0) {
            if (g_plugins[i].loaded) {
                return -2;
            }

            g_plugins[i].loaded = 1;
            if (g_plugins[i].init) {
                g_plugins[i].init();
            }
            return g_plugins[i].id;
        }
    }

    plugin_t* plugin = &g_plugins[g_plugin_count];
    strcpy(plugin->name, (char*)plugin_name);
    plugin->id = g_plugin_count;
    plugin->loaded = 1;

    g_plugin_count++;
    return plugin->id;
}

int plugin_unload(uint32_t plugin_id) {
    if (plugin_id >= (uint32_t)g_plugin_count) {
        return -1;
    }

    plugin_t* plugin = &g_plugins[plugin_id];
    if (!plugin->loaded) {
        return -2;
    }

    if (plugin->cleanup) {
        plugin->cleanup();
    }

    plugin->loaded = 0;
    return 0;
}

int plugin_execute(uint32_t plugin_id, int argc, char** argv) {
    if (plugin_id >= (uint32_t)g_plugin_count) {
        return -1;
    }

    plugin_t* plugin = &g_plugins[plugin_id];
    if (!plugin->loaded || !plugin->command) {
        return -2;
    }

    return plugin->command(argc, argv);
}

plugin_t* plugin_get_list(int* count) {
    *count = g_plugin_count;
    return g_plugins;
}

plugin_t* plugin_get_by_name(const char* name) {
    for (int i = 0; i < g_plugin_count; i++) {
        if (strcmp(g_plugins[i].name, (char*)name) == 0) {
            return &g_plugins[i];
        }
    }
    return NULL;
}

int plugin_register_builtin(const char* name, plugin_init_t init,
                             plugin_cleanup_t cleanup, plugin_command_t command) {
    if (g_plugin_count >= MAX_PLUGINS) {
        return -1;
    }

    for (int i = 0; i < g_plugin_count; i++) {
        if (strcmp(g_plugins[i].name, (char*)name) == 0) {
            return -2;
        }
    }

    plugin_t* plugin = &g_plugins[g_plugin_count];
    strcpy(plugin->name, (char*)name);
    plugin->id = g_plugin_count;
    plugin->init = init;
    plugin->cleanup = cleanup;
    plugin->command = command;
    plugin->loaded = 0;
    plugin->info = NULL;
    plugin->auto_detected = 0;

    g_plugin_count++;
    return plugin->id;
}

void plugin_auto_detect(void) {
    struct fs_node* plugin_dir = find_node(root, "System");
    if (!plugin_dir) {
        return;
    }

    struct fs_node* plugins_subdir = find_node(plugin_dir, "plugins");
    if (!plugins_subdir || plugins_subdir->type != FS_DIRECTORY) {
        return;
    }

    for (int i = 0; i < plugins_subdir->num_children; i++) {
        struct fs_node* child = plugins_subdir->children[i];
        if (child->type == FS_FILE) {
            const char* ext = strstr(child->name, ".plg");
            if (ext && strcmp(ext, ".plg") == 0) {
                char plugin_name[MAX_PLUGIN_NAME];
                int name_len = strlen(child->name) - 4;
                if (name_len >= MAX_PLUGIN_NAME) name_len = MAX_PLUGIN_NAME - 1;
                int j = 0;
                while (j < name_len && child->name[j]) {
                    plugin_name[j] = child->name[j];
                    j++;
                }
                plugin_name[j] = '\0';

                plugin_t* plugin = &g_plugins[g_plugin_count];
                strcpy(plugin->name, plugin_name);
                plugin->id = g_plugin_count;
                plugin->loaded = 0;
                plugin->auto_detected = 1;
                plugin->init = NULL;
                plugin->cleanup = NULL;
                plugin->command = NULL;
                plugin->info = NULL;

                g_plugin_count++;
            }
        }
    }
}

int plugin_load_dynamic(const char* plugin_name, const char* elf_data, int data_size) {
    if (g_plugin_count >= MAX_PLUGINS) {
        return -1;
    }

    for (int i = 0; i < g_plugin_count; i++) {
        if (strcmp(g_plugins[i].name, (char*)plugin_name) == 0) {
            g_plugins[i].loaded = 1;
            return g_plugins[i].id;
        }
    }

    Elf32_Ehdr* header = (Elf32_Ehdr*)elf_data;
    if (header->e_ident[0] != 0x7F || header->e_ident[1] != 'E' ||
        header->e_ident[2] != 'L' || header->e_ident[3] != 'F') {
        return -2;
    }

    plugin_t* plugin = &g_plugins[g_plugin_count];
    strcpy(plugin->name, (char*)plugin_name);
    plugin->id = g_plugin_count;
    plugin->loaded = 1;
    plugin->auto_detected = 1;
    plugin->info = NULL;
    plugin->init = NULL;
    plugin->cleanup = NULL;
    plugin->command = NULL;

    if (data_size < MAX_PLUGIN_DATA_SIZE) {
        g_plugin_data_size[g_plugin_count] = data_size;
        memcpy(g_plugin_data[g_plugin_count], elf_data, data_size);
    }

    g_plugin_count++;
    return plugin->id;
}

plugin_info_t* plugin_get_info(uint32_t plugin_id) {
    if (plugin_id >= (uint32_t)g_plugin_count) {
        return NULL;
    }

    plugin_t* plugin = &g_plugins[plugin_id];
    if (!plugin->loaded || !plugin->info) {
        return NULL;
    }

    return plugin->info;
}

int plugin_is_plugin_command(const char* cmd_name, uint32_t* plugin_id) {
    for (int i = 0; i < g_plugin_count; i++) {
        if (strcmp(g_plugins[i].name, (char*)cmd_name) == 0) {
            *plugin_id = g_plugins[i].id;
            return 1;
        }
    }
    return 0;
}
