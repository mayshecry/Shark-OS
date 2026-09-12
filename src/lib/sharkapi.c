#include "kernel.h"
#include "sharkapi.h"
#include <stdarg.h>

extern task_t* create_task(const char* name);
extern void yield(void);
extern volatile uint32_t uptime_ticks;

void sharkapi_print(const char* text) {
    terminal_writestring((char*)text);
}

void sharkapi_println(const char* text) {
    terminal_writestring((char*)text);
    terminal_writestring("\n");
}

static void sharkapi_vsnprintf(char* buf, size_t size, const char* fmt, va_list args) {
    size_t pos = 0;
    for (size_t i = 0; fmt[i] && pos < size - 1; i++) {
        if (fmt[i] == '%') {
            i++;
            if (fmt[i] == 's') {
                const char* s = va_arg(args, const char*);
                if (!s) s = "(null)";
                for (size_t j = 0; s[j] && pos < size - 1; j++)
                    buf[pos++] = s[j];
            } else if (fmt[i] == 'd' || fmt[i] == 'i') {
                int val = va_arg(args, int);
                char tmp[16];
                int neg = 0, len = 0;
                if (val < 0) { neg = 1; val = -val; }
                if (val == 0) tmp[len++] = '0';
                while (val > 0) { tmp[len++] = '0' + (val % 10); val /= 10; }
                if (neg) buf[pos++] = '-';
                for (int k = len - 1; k >= 0 && pos < size - 1; k--)
                    buf[pos++] = tmp[k];
            } else if (fmt[i] == 'c') {
                char c = (char)va_arg(args, int);
                if (pos < size - 1) buf[pos++] = c;
            } else if (fmt[i] == '%') {
                if (pos < size - 1) buf[pos++] = '%';
            }
        } else {
            buf[pos++] = fmt[i];
        }
    }
    buf[pos] = '\0';
}

void sharkapi_printf(const char* fmt, ...) {
    char buffer[512];
    va_list args;
    va_start(args, fmt);
    sharkapi_vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    terminal_writestring(buffer);
}

void sharkapi_putchar(char c) {
    terminal_putchar(c);
}

void sharkapi_clear_screen(void) {
    terminal_clear();
}

void* sharkapi_malloc(size_t size) {
    return kmalloc(size);
}

void sharkapi_free(void* ptr) {
}

void* sharkapi_realloc(void* ptr, size_t size) {
    void* new_ptr = kmalloc(size);
    if (new_ptr && ptr) {
        memcpy(new_ptr, ptr, size);
    }
    return new_ptr;
}

void* sharkapi_calloc(size_t count, size_t size) {
    void* ptr = kmalloc(count * size);
    if (ptr) {
        memset(ptr, 0, count * size);
    }
    return ptr;
}

size_t sharkapi_strlen(const char* str) {
    return strlen((char*)str);
}

char* sharkapi_strcpy(char* dst, const char* src) {
    strcpy(dst, (char*)src);
    return dst;
}

char* sharkapi_strcat(char* dst, const char* src) {
    int dlen = strlen(dst);
    int slen = strlen((char*)src);
    for (int i = 0; i < slen; i++) {
        dst[dlen + i] = ((char*)src)[i];
    }
    dst[dlen + slen] = '\0';
    return dst;
}

int sharkapi_strcmp(const char* a, const char* b) {
    return strcmp((char*)a, (char*)b);
}

char* sharkapi_strdup(const char* str) {
    int len = strlen((char*)str);
    char* copy = kmalloc(len + 1);
    if (copy) {
        strcpy(copy, (char*)str);
    }
    return copy;
}

file_handle_t sharkapi_fopen(const char* path, const char* mode) {
    struct fs_node* node = find_node(current_dir, (char*)path);
    if (!node) {
        node = find_node(root, (char*)path);
    }
    return (file_handle_t)node;
}

void sharkapi_fclose(file_handle_t file) {
}

size_t sharkapi_fread(void* buffer, size_t size, size_t count, file_handle_t file) {
    struct fs_node* node = (struct fs_node*)file;
    if (!node || !node->content) return 0;

    size_t to_read = size * count;
    if (to_read > node->content_len) {
        to_read = node->content_len;
    }

    memcpy(buffer, node->content, to_read);
    return to_read / size;
}

size_t sharkapi_fwrite(const void* buffer, size_t size, size_t count, file_handle_t file) {
    struct fs_node* node = (struct fs_node*)file;
    if (!node) return 0;

    size_t to_write = size * count;
    if (to_write > MAX_FILE_CONTENT_SIZE) {
        to_write = MAX_FILE_CONTENT_SIZE;
    }

    memcpy(node->content, buffer, to_write);
    node->content_len = to_write;
    return to_write / size;
}

int sharkapi_fseek(file_handle_t file, long offset, int whence) {
    return 0;
}

long sharkapi_ftell(file_handle_t file) {
    struct fs_node* node = (struct fs_node*)file;
    if (!node) return -1;
    return (long)node->content_len;
}

void sharkapi_draw_pixel(int x, int y, uint32_t color) {
    draw_pixel(x, y, color);
}

void sharkapi_draw_rect(rect_t rect, uint32_t color) {
    draw_rect(rect.x, rect.y, rect.width, rect.height, color);
}

void sharkapi_draw_line(int x1, int y1, int x2, int y2, uint32_t color) {
    int dx = (x2 > x1) ? (x2 - x1) : (x1 - x2);
    int dy = (y2 > y1) ? (y2 - y1) : (y1 - y2);
    int sx = (x1 < x2) ? 1 : -1;
    int sy = (y1 < y2) ? 1 : -1;
    int err = dx - dy;

    int x = x1, y = y1;
    while (1) {
        draw_pixel(x, y, color);
        if (x == x2 && y == y2) break;

        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x += sx; }
        if (e2 < dx)  { err += dx; y += sy; }
    }
}

void sharkapi_draw_text(int x, int y, const char* text, uint32_t color) {
    terminal_writestring((char*)text);
}

void sharkapi_get_screen_size(int* width, int* height) {
    *width = screen_width;
    *height = screen_height;
}

char sharkapi_getchar(void) {
    return keyboard_getchar();
}

static key_callback_t g_key_handler = NULL;

void sharkapi_register_key_handler(key_callback_t callback) {
    g_key_handler = callback;
}

void sharkapi_unregister_key_handler(void) {
    g_key_handler = NULL;
}

uint32_t sharkapi_get_ticks(void) {
    return uptime_ticks;
}

void sharkapi_delay_ms(uint32_t ms) {
    delay_ms(ms);
}

file_info_t* sharkapi_list_directory(const char* path, int* count) {
    struct fs_node* node = find_node(root, (char*)path);
    if (!node || !node->children) {
        *count = 0;
        return NULL;
    }

    file_info_t* list = kmalloc(sizeof(file_info_t) * MAX_CHILDREN);
    int idx = 0;

    for (int i = 0; i < MAX_CHILDREN && node->children[i]; i++) {
        strcpy(list[idx].name, node->children[i]->name);
        list[idx].size = node->children[i]->content_len;
        list[idx].is_directory = (node->children[i]->children != NULL);
        idx++;
    }

    *count = idx;
    return list;
}

int sharkapi_mkdir(const char* path) {
    return 0;
}

int sharkapi_rmdir(const char* path) {
    return 0;
}

int sharkapi_file_exists(const char* path) {
    struct fs_node* node = find_node(current_dir, (char*)path);
    if (!node) node = find_node(root, (char*)path);
    return (node != NULL) ? 1 : 0;
}

uint32_t sharkapi_file_size(const char* path) {
    struct fs_node* node = find_node(current_dir, (char*)path);
    if (!node) node = find_node(root, (char*)path);
    if (!node) return 0;
    return node->content_len;
}

void sharkapi_free_file_list(file_info_t* list, int count) {
}

uint32_t sharkapi_create_task(task_func_t func, const char* name) {
    task_t* task = create_task((char*)name);
    if (task) return task->id;
    return 0;
}

void sharkapi_kill_task(uint32_t task_id) {
}

uint32_t sharkapi_get_current_task_id(void) {
    return 0;
}

void sharkapi_yield(void) {
    yield();
}

void sharkapi_sleep_ms(uint32_t ms) {
    delay_ms(ms);
}