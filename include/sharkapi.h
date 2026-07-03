#ifndef SHARKAPI_H
#define SHARKAPI_H

#include <stdint.h>
#include <stddef.h>

/* ============================================================================
 * SharkAPI - SharkOS Plugin Development Kit (similar to WinAPI)
 * ============================================================================
 */

/* Plugin version and metadata */
#define SHARKAPI_VERSION 0x00010000

typedef struct {
    uint32_t version;
    char name[64];
    char author[64];
    char description[256];
    uint16_t major;
    uint16_t minor;
} plugin_info_t;

/* ============================================================================
 * Console/Terminal Output API
 * ============================================================================
 */

void sharkapi_print(const char* text);
void sharkapi_println(const char* text);
void sharkapi_printf(const char* fmt, ...);
void sharkapi_putchar(char c);
void sharkapi_clear_screen(void);

/* ============================================================================
 * Memory Management API
 * ============================================================================
 */

void* sharkapi_malloc(size_t size);
void  sharkapi_free(void* ptr);
void* sharkapi_realloc(void* ptr, size_t size);
void* sharkapi_calloc(size_t count, size_t size);

/* ============================================================================
 * String Utilities API
 * ============================================================================
 */

size_t sharkapi_strlen(const char* str);
char*  sharkapi_strcpy(char* dst, const char* src);
char*  sharkapi_strcat(char* dst, const char* src);
int    sharkapi_strcmp(const char* a, const char* b);
char*  sharkapi_strdup(const char* str);

/* ============================================================================
 * File I/O API
 * ============================================================================
 */

typedef void* file_handle_t;

file_handle_t sharkapi_fopen(const char* path, const char* mode);
void          sharkapi_fclose(file_handle_t file);
size_t        sharkapi_fread(void* buffer, size_t size, size_t count, file_handle_t file);
size_t        sharkapi_fwrite(const void* buffer, size_t size, size_t count, file_handle_t file);
int           sharkapi_fseek(file_handle_t file, long offset, int whence);
long          sharkapi_ftell(file_handle_t file);

/* ============================================================================
 * Graphics/UI API
 * ============================================================================
 */

typedef struct {
    uint32_t color;
    int x;
    int y;
    int width;
    int height;
} rect_t;

void sharkapi_draw_pixel(int x, int y, uint32_t color);
void sharkapi_draw_rect(rect_t rect, uint32_t color);
void sharkapi_draw_line(int x1, int y1, int x2, int y2, uint32_t color);
void sharkapi_draw_text(int x, int y, const char* text, uint32_t color);
void sharkapi_get_screen_size(int* width, int* height);

/* ============================================================================
 * Keyboard Input API
 * ============================================================================
 */

typedef void (*key_callback_t)(char key, uint8_t scancode);

char  sharkapi_getchar(void);
void  sharkapi_register_key_handler(key_callback_t callback);
void  sharkapi_unregister_key_handler(void);

/* ============================================================================
 * Time API
 * ============================================================================
 */

uint32_t sharkapi_get_ticks(void);
void     sharkapi_delay_ms(uint32_t ms);

/* ============================================================================
 * Filesystem API
 * ============================================================================
 */

typedef struct {
    char name[256];
    uint32_t size;
    uint8_t is_directory;
} file_info_t;

file_info_t* sharkapi_list_directory(const char* path, int* count);
int          sharkapi_mkdir(const char* path);
int          sharkapi_rmdir(const char* path);
int          sharkapi_file_exists(const char* path);
uint32_t     sharkapi_file_size(const char* path);
void         sharkapi_free_file_list(file_info_t* list, int count);

/* ============================================================================
 * Task/Process API
 * ============================================================================
 */

typedef void (*task_func_t)(void);

uint32_t sharkapi_create_task(task_func_t func, const char* name);
void     sharkapi_kill_task(uint32_t task_id);
uint32_t sharkapi_get_current_task_id(void);
void     sharkapi_yield(void);
void     sharkapi_sleep_ms(uint32_t ms);

/* ============================================================================
 * Plugin Lifecycle Hooks
 * ============================================================================
 */

/* Called when plugin is loaded */
typedef int (*plugin_init_t)(void);

/* Called when plugin is unloaded */
typedef void (*plugin_cleanup_t)(void);

/* Called by shell to execute plugin command */
typedef int (*plugin_command_t)(int argc, char** argv);

/* ============================================================================
 * Color Constants (32-bit ARGB)
 * ============================================================================
 */

#define SHARKAPI_COLOR_BLACK       0xFF000000
#define SHARKAPI_COLOR_WHITE       0xFFFFFFFF
#define SHARKAPI_COLOR_RED         0xFFFF0000
#define SHARKAPI_COLOR_GREEN       0xFF00FF00
#define SHARKAPI_COLOR_BLUE        0xFF0000FF
#define SHARKAPI_COLOR_YELLOW      0xFFFFFF00
#define SHARKAPI_COLOR_CYAN        0xFF00FFFF
#define SHARKAPI_COLOR_MAGENTA     0xFFFF00FF
#define SHARKAPI_COLOR_LIGHT_GREY  0xFFCCCCCC
#define SHARKAPI_COLOR_DARK_GREY   0xFF333333

#endif /* SHARKAPI_H */
