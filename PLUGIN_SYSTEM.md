# SharkOS Plugin System Documentation

## Overview

SharkOS includes a powerful plugin system that allows users to extend the operating system with custom functionality. Plugins are written in C and compiled into the kernel, then managed via the `spkg` (SharkOS Package Manager) command.

## Architecture

- **SharkAPI** (`include/sharkapi.h`) - Comprehensive API for plugin development, similar to WinAPI
- **Plugin Manager** (`src/lib/plugin_manager.c`) - Handles plugin lifecycle (load/unload)
- **spkg** (`src/shell/spkg.c`) - Package manager for installing/removing plugins
- **Plugins Directory** (`plugins/`) - Location for plugin source code

## Creating a Plugin

### Plugin Structure

Every plugin must implement these functions:

```c
#include "sharkapi.h"

/* Plugin metadata */
plugin_info_t plugin_info = {
    .version = SHARKAPI_VERSION,
    .name = "Your Plugin Name",
    .author = "Your Name",
    .description = "Plugin description",
    .major = 1,
    .minor = 0
};

/* Called on plugin load */
int plugin_init(void) {
    sharkapi_println("Plugin initialized!");
    return 0;
}

/* Called on plugin unload */
void plugin_cleanup(void) {
    sharkapi_println("Plugin cleaned up");
}

/* Called when user runs: plugin-name [args] */
int plugin_command(int argc, char** argv) {
    sharkapi_printf("Plugin received %d arguments\n", argc);
    return 0;
}

/* Export symbols */
int plugin_init_entry(void) { return plugin_init(); }
void plugin_cleanup_entry(void) { plugin_cleanup(); }
int plugin_command_entry(int argc, char** argv) { return plugin_command(argc, argv); }
plugin_info_t* plugin_get_info(void) { return &plugin_info; }
```

### Plugin Example: Hello World

Create `plugins/hello/hello.c`:

```c
#include "sharkapi.h"

plugin_info_t plugin_info = {
    .version = SHARKAPI_VERSION,
    .name = "Hello Plugin",
    .author = "You",
    .description = "A simple hello world plugin",
    .major = 1,
    .minor = 0
};

int plugin_init(void) {
    sharkapi_println("Hello plugin loaded!");
    return 0;
}

void plugin_cleanup(void) {
    sharkapi_println("Hello plugin unloaded");
}

int plugin_command(int argc, char** argv) {
    sharkapi_println("Hello, World!");
    return 0;
}

int plugin_init_entry(void) { return plugin_init(); }
void plugin_cleanup_entry(void) { plugin_cleanup(); }
int plugin_command_entry(int argc, char** argv) { return plugin_command(argc, argv); }
plugin_info_t* plugin_get_info(void) { return &plugin_info; }
```

### Building Plugins

1. Create your plugin in `plugins/your-plugin/plugin.c`
2. Run `make` - plugins are automatically compiled and added to the ISO
3. Plugins are not pre-loaded; install via `spkg install <name>`

## SharkAPI Reference

### Console Output
- `void sharkapi_print(const char* text)` - Print text
- `void sharkapi_println(const char* text)` - Print with newline
- `void sharkapi_printf(const char* fmt, ...)` - Formatted print
- `void sharkapi_putchar(char c)` - Print single character
- `void sharkapi_clear_screen(void)` - Clear screen

### Memory Management
- `void* sharkapi_malloc(size_t size)` - Allocate memory
- `void sharkapi_free(void* ptr)` - Free memory
- `void* sharkapi_realloc(void* ptr, size_t size)` - Reallocate
- `void* sharkapi_calloc(size_t count, size_t size)` - Allocate and zero

### Strings
- `size_t sharkapi_strlen(const char* str)` - Get string length
- `char* sharkapi_strcpy(char* dst, const char* src)` - Copy string
- `char* sharkapi_strcat(char* dst, const char* src)` - Concatenate
- `int sharkapi_strcmp(const char* a, const char* b)` - Compare
- `char* sharkapi_strdup(const char* str)` - Duplicate string

### File I/O
- `file_handle_t sharkapi_fopen(const char* path, const char* mode)` - Open file
- `void sharkapi_fclose(file_handle_t file)` - Close file
- `size_t sharkapi_fread(void* buf, size_t sz, size_t cnt, file_handle_t f)` - Read
- `size_t sharkapi_fwrite(const void* buf, size_t sz, size_t cnt, file_handle_t f)` - Write

### Graphics
- `void sharkapi_draw_pixel(int x, int y, uint32_t color)` - Draw pixel
- `void sharkapi_draw_rect(rect_t rect, uint32_t color)` - Draw rectangle
- `void sharkapi_draw_line(int x1, int y1, int x2, int y2, uint32_t color)` - Draw line
- `void sharkapi_get_screen_size(int* width, int* height)` - Get screen dimensions

### Input
- `char sharkapi_getchar(void)` - Get character from keyboard
- `void sharkapi_register_key_handler(key_callback_t callback)` - Register key handler
- `void sharkapi_unregister_key_handler(void)` - Unregister key handler

### Filesystem
- `file_info_t* sharkapi_list_directory(const char* path, int* count)` - List files
- `int sharkapi_mkdir(const char* path)` - Create directory
- `int sharkapi_rmdir(const char* path)` - Remove directory
- `int sharkapi_file_exists(const char* path)` - Check if file exists
- `uint32_t sharkapi_file_size(const char* path)` - Get file size

### Tasks/Processes
- `uint32_t sharkapi_create_task(task_func_t func, const char* name)` - Create task
- `void sharkapi_kill_task(uint32_t task_id)` - Kill task
- `void sharkapi_yield(void)` - Yield CPU
- `void sharkapi_sleep_ms(uint32_t ms)` - Sleep

### Time
- `uint32_t sharkapi_get_ticks(void)` - Get system ticks
- `void sharkapi_delay_ms(uint32_t ms)` - Delay in milliseconds

## Using spkg Package Manager

### List Available Packages
```
spkg list
```

### Install a Package
```
spkg install python
```

### Uninstall a Package
```
spkg uninstall python
```

### Get Package Info
```
spkg info python
```

### Search Packages
```
spkg search interpreter
```

## Plugin Directory Structure

```
plugins/
├── TEMPLATE.c              - Template for new plugins
├── python-interp/
│   └── python.c            - Python interpreter plugin
├── hello/
│   └── hello.c             - Example hello world plugin
└── your-plugin/
    └── plugin.c            - Your custom plugin
```

## Built-in Plugins

### Python Interpreter
- **Name:** python
- **Description:** Simple Python 3 interpreter
- **Usage:** `spkg install python` then `python` or `python -i`
- **Features:**
  - Interactive shell (REPL)
  - Print statements
  - Variable assignment
  - Basic arithmetic expressions
  - File execution support

## Best Practices

1. **Error Handling**: Always check return values from SharkAPI calls
2. **Memory**: Clean up allocations in `plugin_cleanup()`
3. **Documentation**: Include comments explaining your plugin's functionality
4. **Testing**: Test plugin on both virtual and real hardware if possible
5. **Naming**: Use descriptive names for functions and variables

## Plugin Limitations

- Plugins run in kernel space - be careful with pointer access
- Maximum 16 plugins can be loaded simultaneously
- Plugins cannot be reloaded dynamically (must restart system)
- No inter-plugin communication API (yet)

## Future Extensions

Planned features for the plugin system:
- Dynamic plugin loading from filesystem
- Plugin dependency management
- Plugin versioning and compatibility checking
- Inter-plugin communication API
- Plugin configuration files
- Plugin data persistence
