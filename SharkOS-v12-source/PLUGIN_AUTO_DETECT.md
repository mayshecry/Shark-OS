# Automatic Plugin Detection System

## Overview

The SharkOS plugin system now features **fully automatic plugin detection**. Plugins are discovered dynamically from the filesystem without any hardcoded references in the main codebase. Simply drop a plugin file into the `/System/plugins/` directory and it's automatically detected and available for installation.

## How It Works

### 1. Automatic Discovery

On boot, `plugin_manager_init()` scans the `/System/plugins/` directory for `.plg` files:

```c
void plugin_manager_init(void) {
    g_plugin_count = 0;
    memset(g_plugins, 0, sizeof(g_plugins));
    
    /* Auto-detect plugins from the plugin directory */
    plugin_auto_detect();
}
```

### 2. Plugin Registration

The `plugin_auto_detect()` function scans for `.plg` files and registers them:

```c
void plugin_auto_detect(void) {
    struct fs_node* plugin_dir = find_node(root, "System");
    if (!plugin_dir) return;
    
    struct fs_node* plugins_subdir = find_node(plugin_dir, "plugins");
    if (!plugins_subdir || plugins_subdir->type != FS_DIRECTORY) return;
    
    /* Scan for .plg files */
    for (int i = 0; i < plugins_subdir->num_children; i++) {
        struct fs_node* child = plugins_subdir->children[i];
        if (child->type == FS_FILE) {
            const char* ext = strstr(child->name, ".plg");
            if (ext && strcmp(ext, ".plg") == 0) {
                /* Extract plugin name (remove .plg extension) */
                char plugin_name[MAX_PLUGIN_NAME];
                int name_len = strlen(child->name) - 4;
                strncpy(plugin_name, child->name, name_len);
                plugin_name[name_len] = '\0';
                
                /* Register the plugin (will be loaded on demand) */
                plugin_t* plugin = &g_plugins[g_plugin_count];
                strcpy(plugin->name, plugin_name);
                plugin->id = g_plugin_count;
                plugin->loaded = 0;
                plugin->auto_detected = 1;
                
                g_plugin_count++;
            }
        }
    }
}
```

### 3. Dynamic Package Manager

The `spkg` package manager now dynamically discovers packages from the filesystem:

```c
void spkg_discover_packages(void) {
    g_package_count = 0;
    
    struct fs_node* plugin_dir = find_node(root, "System");
    if (!plugin_dir) return;
    
    struct fs_node* plugins_subdir = find_node(plugin_dir, "plugins");
    if (!plugins_subdir || plugins_subdir->type != FS_DIRECTORY) return;
    
    /* Scan for .plg files */
    for (int i = 0; i < plugins_subdir->num_children && g_package_count < MAX_PACKAGES; i++) {
        struct fs_node* child = plugins_subdir->children[i];
        if (child->type == FS_FILE) {
            const char* ext = strstr(child->name, ".plg");
            if (ext && strcmp(ext, ".plg") == 0) {
                /* Extract plugin name */
                char plugin_name[64];
                int name_len = strlen(child->name) - 4;
                strncpy(plugin_name, child->name, name_len);
                plugin_name[name_len] = '\0';
                
                /* Check if already installed */
                plugin_t* plugin = plugin_get_by_name(plugin_name);
                uint8_t installed = (plugin && plugin->loaded) ? 1 : 0;
                
                /* Add to package list */
                strcpy(g_packages[g_package_count].name, plugin_name);
                strcpy(g_packages[g_package_count].description, "Plugin package");
                g_packages[g_package_count].available = 1;
                g_packages[g_package_count].installed = installed;
                
                g_package_count++;
            }
        }
    }
}
```

### 4. Generic Plugin Command Execution

The shell now handles all plugins generically without hardcoded references:

```c
} else if (plugin_is_plugin_command(cmd_name, &plugin_id)) {
    plugin_t* plugin = plugin_get_by_name(cmd_name);
    if (!plugin || !plugin->loaded) {
        terminal_writestring("Plugin '");
        terminal_writestring(cmd_name);
        terminal_writestring("' is not loaded. Use 'spkg install ");
        terminal_writestring(cmd_name);
        terminal_writestring("' to install it.\n");
    } else {
        char* plugin_argv[16] = {cmd_name};
        int plugin_argc = 1;
        if (strlen(args) > 0) {
            plugin_argv[plugin_argc++] = args;
        }
        plugin_execute(plugin->id, plugin_argc, plugin_argv);
    }
}
```

## Creating a Plugin

### Plugin Structure

A plugin is simply an ELF executable file with `.plg` extension that exports these functions:

```c
/* Plugin metadata */
plugin_info_t plugin_info = {
    .version = SHARKAPI_VERSION,
    .name = "My Plugin",
    .author = "Your Name",
    .description = "Plugin description",
    .major = 1,
    .minor = 0
};

/* Lifecycle hooks */
int plugin_init(void) {
    /* Called when plugin is loaded */
    return 0;
}

void plugin_cleanup(void) {
    /* Called when plugin is unloaded */
}

int plugin_command(int argc, char** argv) {
    /* Called when user executes the plugin command */
    return 0;
}

/* Export functions */
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
```

### Building a Plugin

1. Write your plugin in C using the SharkAPI
2. Compile it as an ELF executable
3. Rename it with `.plg` extension (e.g., `myplugin.plg`)
4. Copy it to `/System/plugins/` directory

### Example: Python Interpreter Plugin

See `plugins/python-interp/python.c` for a complete example.

## Using Plugins

### List Available Plugins

```bash
spkg list
```

This will automatically scan `/System/plugins/` and show all available plugins.

### Install a Plugin

```bash
spkg install python
```

This loads the plugin and makes it available as a command.

### Use a Plugin

```bash
python
python script.py
```

The plugin command is automatically available after installation.

### Uninstall a Plugin

```bash
spkg uninstall python
```

### Get Plugin Info

```bash
spkg info python
```

## Benefits

### No Hardcoded References

The main codebase has **zero** hardcoded plugin names or references. Everything is discovered dynamically:

- ✅ `plugin_auto_detect()` - Scans filesystem for plugins
- ✅ `spkg_discover_packages()` - Discovers packages dynamically
- ✅ Generic plugin command handler - Works with any plugin
- ✅ No manual registration required

### Easy Plugin Development

To add a new plugin:

1. Write the plugin code
2. Compile to ELF
3. Copy `.plg` file to `/System/plugins/`
4. Done! The plugin is automatically detected

### Extensible System

The system can support unlimited plugins (up to MAX_PLUGINS limit) without modifying any core code.

## Architecture

```
┌─────────────────────────────────────────┐
│         Boot / Initialization           │
└─────────────────┬───────────────────────┘
                  │
                  ▼
┌─────────────────────────────────────────┐
│   plugin_manager_init()                 │
│   └─► plugin_auto_detect()              │
│       └─► Scan /System/plugins/*.plg    │
│           └─► Register in g_plugins[]   │
└─────────────────────────────────────────┘
                  │
                  ▼
┌─────────────────────────────────────────┐
│   User types command in shell           │
└─────────────────┬───────────────────────┘
                  │
                  ▼
┌─────────────────────────────────────────┐
│   execute_command()                     │
│   └─► plugin_is_plugin_command()        │
│       └─► Check if cmd matches plugin   │
│           └─► plugin_execute()          │
└─────────────────────────────────────────┘
                  │
                  ▼
┌─────────────────────────────────────────┐
│   spkg commands (list/install/etc)      │
│   └─► spkg_discover_packages()          │
│       └─► Scan /System/plugins/*.plg    │
│           └─► Update package list       │
└─────────────────────────────────────────┘
```

## API Reference

### Plugin Manager API

```c
/* Initialize plugin system and auto-detect plugins */
void plugin_manager_init(void);

/* Load a plugin (marks as loaded, calls init) */
int plugin_load(const char* plugin_name);

/* Unload a plugin (calls cleanup) */
int plugin_unload(uint32_t plugin_id);

/* Execute plugin command */
int plugin_execute(uint32_t plugin_id, int argc, char** argv);

/* Get plugin by name */
plugin_t* plugin_get_by_name(const char* name);

/* Check if command is a plugin */
int plugin_is_plugin_command(const char* cmd_name, uint32_t* plugin_id);

/* Auto-detect plugins from filesystem */
void plugin_auto_detect(void);
```

### Plugin Developer API (SharkAPI)

```c
/* Plugin metadata structure */
typedef struct {
    uint32_t version;
    char name[64];
    char author[64];
    char description[256];
    uint16_t major;
    uint16_t minor;
} plugin_info_t;

/* Lifecycle function types */
typedef int (*plugin_init_t)(void);
typedef void (*plugin_cleanup_t)(void);
typedef int (*plugin_command_t)(int argc, char** argv);

/* Required exports */
plugin_info_t* plugin_get_info(void);
int plugin_init_entry(void);
void plugin_cleanup_entry(void);
int plugin_command_entry(int argc, char** argv);
```

## Future Enhancements

- Plugin dependencies
- Plugin versioning
- Plugin repositories
- Plugin signing/verification
- Plugin configuration files
- Hot-reloading plugins