#ifndef KERNEL_H
#define KERNEL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum vga_color {
    VGA_COLOR_BLACK = 0,
    VGA_COLOR_BLUE = 1,
    VGA_COLOR_GREEN = 2,
    VGA_COLOR_CYAN = 3,
    VGA_COLOR_RED = 4,
    VGA_COLOR_MAGENTA = 5,
    VGA_COLOR_BROWN = 6,
    VGA_COLOR_LIGHT_GREY = 7,
    VGA_COLOR_DARK_GREY = 8,
    VGA_COLOR_LIGHT_BLUE = 9,
    VGA_COLOR_LIGHT_GREEN = 10,
    VGA_COLOR_LIGHT_CYAN = 11,
    VGA_COLOR_LIGHT_RED = 12,
    VGA_COLOR_LIGHT_MAGENTA = 13,
    VGA_COLOR_LIGHT_BROWN = 14,
    VGA_COLOR_WHITE = 15,
};

extern uint32_t vga_to_rgb[];
static inline uint8_t vga_entry_color(uint8_t fg, uint8_t bg) {
    return fg | bg << 4;
}

static inline void outb(uint16_t port, uint8_t val) {
    asm volatile ( "outb %0, %1" : : "a"(val), "Nd"(port) );
}
static inline void outw(uint16_t port, uint16_t val) {
    asm volatile ( "outw %0, %1" : : "a"(val), "Nd"(port) );
}
static inline void outl(uint16_t port, uint32_t val) {
    asm volatile ( "outl %0, %1" : : "a"(val), "Nd"(port) );
}
static inline uint16_t inw(uint16_t port) {
    uint16_t ret; asm volatile ("inw %1, %0" : "=a"(ret) : "Nd"(port)); return ret;
}
static inline uint8_t inb(uint16_t port) {
    uint8_t ret; asm volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port)); return ret;
}
static inline uint32_t inl(uint16_t port) {
    uint32_t ret; asm volatile ("inl %1, %0" : "=a"(ret) : "Nd"(port)); return ret;
}

extern uint32_t* lfbptr;
extern uint64_t screen_width;
extern uint64_t screen_height;
extern uint64_t screen_pitch;
extern uint64_t total_system_memory;

extern uint32_t font_scale;
extern uint32_t font_cell_w;
extern uint32_t font_cell_h;
extern uint32_t ui_tab_y;
extern uint32_t ui_chrome_top;
extern uint32_t ui_footer_y;
extern uint32_t ui_footer_h;
extern size_t term_cols;
extern size_t term_max_row;
extern size_t content_first_row;

#define THEME_SHARKOS 0
#define THEME_BLUE    1
#define THEME_TEMPLEOS 2
#define MAX_THEMES    3

typedef struct {
    uint32_t bg;
    uint32_t surface;
    uint32_t header;
    uint32_t tab_active;
    uint32_t tab_inactive;
    uint32_t border;
    uint32_t title;
    uint32_t text;
    uint32_t dim;
    uint32_t accent;
    uint32_t label;
    uint32_t answer;
} theme_t;
extern theme_t themes[MAX_THEMES];
extern uint32_t UI_BG;
extern uint32_t UI_SURFACE;
extern uint32_t UI_HEADER;
extern uint32_t UI_TAB_ACTIVE;
extern uint32_t UI_TAB_INACTIVE;
extern uint32_t UI_BORDER;
extern uint32_t UI_TITLE;
extern uint32_t UI_TEXT;
extern uint32_t UI_DIM;
extern uint32_t UI_ACCENT;
extern uint32_t UI_LABEL;
extern uint32_t UI_ANSWER;

typedef enum { TASK_RUNNING, TASK_READY, TASK_SLEEPING, TASK_ZOMBIE } task_state_t;
typedef struct task {
    int id;
    uint32_t esp;
    task_state_t state;
    int cpu_id;
    char name[16];
    struct task* next;
} task_t;
typedef struct {
    int id;
    task_t* current_task;
    bool started;
} cpu_t;
#define MAX_CPUS 16
extern cpu_t cpus[MAX_CPUS];
extern task_t* task_list;
extern int next_pid;

typedef volatile int spinlock_t;
extern spinlock_t task_list_lock;
void spin_lock(spinlock_t *lock);
void spin_unlock(spinlock_t *lock);
void yield(void);

extern bool network_initialized;
extern uint32_t rtl_io_base;
extern uint8_t rtl_irq;
extern uint8_t* rx_buffer;
extern uint8_t current_tx_buffer;

extern uint8_t ip_address[4];
extern uint8_t subnet_mask[4];
extern uint8_t gateway[4];
extern uint8_t dns_server[4];
extern bool dhcp_enabled;
extern uintptr_t free_memory_start;
extern uintptr_t free_memory_end;
extern char current_user[32];

extern uint8_t font8x8[96][8];

typedef enum { KERNEL_MODE_CLI, KERNEL_MODE_EDITOR, KERNEL_MODE_FAQ, KERNEL_MODE_SETTINGS } kernel_mode_t;
extern kernel_mode_t current_kernel_mode;

#define MAX_FILE_CONTENT_SIZE 2048
struct fs_node;
extern struct fs_node* editor_target_file;
extern char editor_buffer[MAX_FILE_CONTENT_SIZE];
extern size_t editor_buffer_idx;

extern bool shift_pressed;
extern bool ctrl_pressed;
extern unsigned char keyboard_map[128];
extern unsigned char keyboard_map_shifted[128];

extern bool tiling_enabled;
extern bool mouse_enabled;
extern int selected_theme;
extern bool lite_mode;

#define MAX_PANES 4
#define PANE_GAP 1
typedef struct {
    size_t row;
    size_t col;
    uint8_t color;
    char command_buffer[80];
    size_t cmd_index;
    size_t col_start;
    size_t col_end;
    size_t prompt_end_col;
} pane_t;
extern pane_t panes[MAX_PANES];
extern int pane_count;
extern int active_pane;

#define SCROLLBACK_LINES 100
#define SCROLLBACK_COLS 100
#define MAX_HISTORY 32
typedef struct {
    char lines[SCROLLBACK_LINES][SCROLLBACK_COLS];
    uint8_t colors[SCROLLBACK_LINES][SCROLLBACK_COLS];
    int count;
    int top; 
} scrollback_t;
extern scrollback_t scrollback;
extern int scrollback_offset; 
extern char command_history[MAX_HISTORY][80];
extern int history_count;
extern int history_index;
extern volatile uint32_t uptime_ticks;

#define terminal_row     (panes[active_pane].row)
#define terminal_column  (panes[active_pane].col)
#define terminal_color   (panes[active_pane].color)
#define command_buffer   (panes[active_pane].command_buffer)
#define command_index    (panes[active_pane].cmd_index)

extern uint32_t* faq_fb_backup;
extern uint32_t faq_backup_x;
extern uint32_t faq_backup_y;
extern uint32_t faq_backup_w;
extern uint32_t faq_backup_h;
extern int faq_saved_pane;

typedef struct {
    int x;
    int y;
    uint8_t buttons;
    int dx;
    int dy;
    int wheel; 
} mouse_state_t;
extern mouse_state_t mouse_state;
extern int mouse_cursor_x;
extern int mouse_cursor_y;

extern char stack_top;

struct registers {
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax;
    uint32_t int_no, err_code;
    uint32_t eip, cs, eflags, useresp, ss;
};

typedef struct {
    uint8_t  e_ident[16]; uint16_t e_type; uint16_t e_machine;
    uint32_t e_version; uint32_t e_entry; uint32_t e_phoff;
    uint32_t e_shoff; uint32_t e_flags; uint16_t e_ehsize;
    uint16_t e_phentsize; uint16_t e_phnum; uint16_t e_shentsize;
    uint16_t e_shnum; uint16_t e_shstrndx;
} Elf32_Ehdr;

typedef struct {
    uint32_t p_type; uint32_t p_offset; uint32_t p_vaddr;
    uint32_t p_paddr; uint32_t p_filesz; uint32_t p_memsz;
    uint32_t p_flags; uint32_t p_align;
} Elf32_Phdr;

int strcmp(const char* s1, const char* s2);
int strcasecmp(const char* s1, const char* s2);
void strcpy(char* dest, const char* src);
char* strchr(const char* s, int c);
char* strstr(const char* haystack, const char* needle);
void* memcpy(void* dest, const void* src, size_t n);
void* memset(void* s, int c, size_t n);
size_t strlen(const char* str);
void hex_to_string(uint32_t value, char* buffer);
void int_to_string(uint32_t value, char* buffer);
void delay_ms(uint32_t ms);

void init_descriptor_tables(void);
void pmm_init(uint64_t mem_size);
void* kmalloc(size_t size);
uintptr_t virt_to_phys(void* addr);

void rtl8139_init(void);
void rtl8139_send_packet(void* data, uint32_t len);
uint32_t pci_config_read(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
void pci_config_write(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t value);
void pci_list_devices(void);
void detect_network_cards(void);
void send_icmp_ping(const char* target);
void send_dhcp_discover(void);

void get_cpu_model(char* buffer);
void shutdown(void);

void execute_elf(uint8_t* data, const char* arg);

#define MAX_NODES 32
#define MAX_CHILDREN 8
typedef enum { FS_DIRECTORY, FS_FILE } node_type_t;
struct fs_node {
    char name[32];
    node_type_t type;
    struct fs_node* parent;
    struct fs_node* children[MAX_CHILDREN];
    int num_children;
    char content[MAX_FILE_CONTENT_SIZE];
    int content_len;
};
extern struct fs_node node_pool[MAX_NODES];
extern int pool_index;
extern struct fs_node* root;
extern struct fs_node* current_dir;
struct fs_node* find_node(struct fs_node* parent, const char* name);
struct fs_node* search_path(const char* name);
struct fs_node* create_node(const char* name, node_type_t type, struct fs_node* parent);
void fs_initialize(void);
void execute_command(char* cmd);

void draw_char(char c, uint32_t x, uint32_t y, uint32_t fg, uint32_t bg);
void draw_pixel(uint32_t x, uint32_t y, uint32_t color);
void draw_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
void draw_string_px(const char* s, uint32_t x, uint32_t y, uint32_t fg, uint32_t bg);
uint32_t col_px(size_t col);
uint32_t row_px(size_t row);
void terminal_putentryat(char c, uint8_t color, size_t x, size_t y);
void terminal_set_color(uint8_t color);
void terminal_write_char_internal(char c);
void terminal_putchar_cli(char c);
void terminal_putchar_editor(char c);
void terminal_putchar(char c);
void draw_cursor(void);
void terminal_writestring(const char* data);
void terminal_scroll(void);
void terminal_clear(void);
void terminal_initialize(void);
void terminal_draw_scrollback(void);
void ui_init_metrics(void);
void draw_pane_tabs(void);
void split_active_pane(void);
void close_active_pane(void);
void redraw_all_panes(void);
void print_prompt(void);
void faq_open(void);
void faq_close(void);
void settings_open(void);
void settings_close(void);
void settings_draw(void);
void apply_theme(int theme_idx);
extern int settings_selected;

void show_fastfetch(void);
void show_welcome_tour(void);

void keyboard_handler(uint8_t scancode);
char keyboard_getchar(void);

void mouse_init(void);
void mouse_handler(void);
void mouse_wait(void);
uint8_t mouse_read(void);
void mouse_update_cursor(void);

void irq_handler(struct registers* r);
void isr_handler(struct registers* r);
void syscall_handler(struct registers* r);

#endif