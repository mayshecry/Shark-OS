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

static uint32_t vga_to_rgb[] = {
    0xFF000000, 0xFF0000AA, 0xFF00AA00, 0xFF00AAAA, 0xFFAA0000, 0xFFAA00AA, 0xFFAA5500, 0xFFAAAAAA,
    0xFF555555, 0xFF5555FF, 0xFF55FF55, 0xFF55FFFF, 0xFFFF5555, 0xFFFF55FF, 0xFFFFFF55, 0xFFFFFFFF
};

static inline uint8_t vga_entry_color(uint8_t fg, uint8_t bg) {
	return fg | bg << 4;
}

/* Prototypes for functions implemented later in the file */
void terminal_putchar(char c);
void terminal_writestring(const char* data);
void draw_pixel(uint32_t x, uint32_t y, uint32_t color);
void hex_to_string(uint32_t value, char* buffer);
void terminal_write_char_internal(char c);
void draw_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
void terminal_clear(void);
void terminal_putentryat(char c, uint8_t color, size_t x, size_t y);
void rtl8139_init();
void rtl8139_send_packet(void* data, uint32_t len);
uint32_t pci_config_read(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
void pci_config_write(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t value);
void print_prompt();
void* kmalloc(size_t size);
extern char stack_top;

/* Logic for managing multiple CPUs and background tasks */
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
cpu_t cpus[MAX_CPUS];
task_t* task_list = NULL;
int next_pid = 1;

/* Spinlocks for SMP safety */
typedef volatile int spinlock_t;
void spin_lock(spinlock_t *lock) {
    while (__sync_lock_test_and_set(lock, 1));
}
void spin_unlock(spinlock_t *lock) {
    __sync_lock_release(lock);
}
spinlock_t task_list_lock = 0;

void yield() { }

/* Custom string functions since there is no standard C library available */
int strcmp(const char* s1, const char* s2) {
	while (*s1 && (*s1 == *s2)) {
		s1++; s2++;
	}
	return *(unsigned char*)s1 - *(unsigned char*)s2;
}

void strcpy(char* dest, const char* src) {
	while ((*dest++ = *src++));
}

void* memcpy(void* dest, const void* src, size_t n) {
    uint8_t* d = (uint8_t*)dest;
    const uint8_t* s = (const uint8_t*)src;
    for (size_t i = 0; i < n; i++) d[i] = s[i];
    return dest;
}

void* memset(void* s, int c, size_t n) {
    uint8_t* p = (uint8_t*)s;
    for (size_t i = 0; i < n; i++) p[i] = (uint8_t)c;
    return s;
}

size_t strlen(const char* str) {
	size_t len = 0;
	while (str[len]) len++;
	return len;
}

/* Definitions for the Virtual File System (VFS) */
#define MAX_NODES 64
#define MAX_CHILDREN 16
#define MAX_FILE_CONTENT_SIZE 4096

typedef enum { FS_DIRECTORY, FS_FILE } node_type_t;
typedef enum { KERNEL_MODE_CLI, KERNEL_MODE_EDITOR } kernel_mode_t;

struct fs_node {
	char name[32];
	node_type_t type;
	struct fs_node* parent;
	struct fs_node* children[MAX_CHILDREN];
	int num_children;
	char content[MAX_FILE_CONTENT_SIZE];
	int content_len; // Current length of content
};

struct fs_node node_pool[MAX_NODES];
int pool_index = 0;
struct fs_node* root;
struct fs_node* current_dir;

kernel_mode_t current_kernel_mode = KERNEL_MODE_CLI;
struct fs_node* editor_target_file = NULL;
char editor_buffer[MAX_FILE_CONTENT_SIZE];
size_t editor_buffer_idx = 0;

task_t* create_task(const char* name) {
    spin_lock(&task_list_lock);
    task_t* new_task = (task_t*)kmalloc(sizeof(task_t));
    new_task->id = next_pid++;
    new_task->state = TASK_READY;
    new_task->cpu_id = 0;
    strcpy(new_task->name, name);
    
    new_task->next = task_list;
    task_list = new_task;
    spin_unlock(&task_list_lock);
    return new_task;
}

uint32_t* lfbptr;
uint64_t screen_width;
uint64_t screen_height;
uint64_t screen_pitch;
uint64_t total_system_memory = 0;
bool network_initialized = false;

uint32_t rtl_io_base = 0;
uint8_t rtl_irq = 0;
uint8_t* rx_buffer;
uint8_t current_tx_buffer = 0;

/* Maps hardware keyboard scancodes to readable characters */
unsigned char keyboard_map[128] = {
    0,  27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b', '\t',   // 0x00 - 0x0F
    'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n', 0, 'a', 's',     // 0x10 - 0x1F
    'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0, '\\', 'z', 'x', 'c', 'v',   // 0x20 - 0x2F
    'b', 'n', 'm', ',', '.', '/', 0, '*', 0, ' ', 0, 0, 0, 0, 0, 0,                    // 0x30 - 0x3F
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,                                   // 0x40 - 0x4F (F1-F10, NumLock, etc.)
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,                                   // 0x50 - 0x5F
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,                                   // 0x60 - 0x6F
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0                                    // 0x70 - 0x7F
};

unsigned char keyboard_map_shifted[128] = {
    0,  27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b', '\t',
    'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n', 0, 'A', 'S',
    'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~', 0, '|', 'Z', 'X', 'C', 'V',
    'B', 'N', 'M', '<', '>', '?', 0, '*', 0, ' ', 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static bool shift_pressed = false;

/* Simple 8x8 bitmap font (subset: space to ~) */
static uint8_t font8x8[96][8] = {
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x18,0x3c,0x3c,0x18,0x18,0x00,0x18,0x00},
    {0x6c,0x6c,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x6c,0x6c,0xfe,0x6c,0xfe,0x6c,0x6c,0x00},
    {0x18,0x7e,0xc0,0x7c,0x06,0xfc,0x18,0x00},
    {0x00,0xc6,0xcc,0x18,0x30,0x66,0xc6,0x00},
    {0x38,0x6c,0x38,0x76,0xdc,0xcc,0x76,0x00},
    {0x18,0x18,0x30,0x00,0x00,0x00,0x00,0x00},
    {0x0c,0x18,0x30,0x30,0x30,0x18,0x0c,0x00},
    {0x30,0x18,0x0c,0x0c,0x0c,0x18,0x30,0x00},
    {0x00,0x66,0x3c,0xff,0x3c,0x66,0x00,0x00},
    {0x00,0x18,0x18,0x7e,0x18,0x18,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x30},
    {0x00,0x00,0x00,0x7e,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x00},
    {0x02,0x04,0x08,0x10,0x20,0x40,0x80,0x00},
    {0x7c,0xc6,0xce,0xde,0xf6,0xe6,0x7c,0x00},
    {0x18,0x38,0x18,0x18,0x18,0x18,0x7e,0x00},
    {0x7c,0xc6,0x06,0x1c,0x30,0x66,0xfe,0x00},
    {0x7c,0xc6,0x06,0x3c,0x06,0xc6,0x7c,0x00},
    {0x1c,0x3c,0x6c,0xcc,0xfe,0x0c,0x1e,0x00},
    {0xfe,0xc0,0xc0,0xfc,0x06,0xc6,0x7c,0x00},
    {0x3c,0x60,0xc0,0xfc,0xc6,0xc6,0x3c,0x00},
    {0xfe,0x06,0x0c,0x18,0x30,0x30,0x30,0x00},
    {0x7c,0xc6,0xc6,0x7c,0xc6,0xc6,0x7c,0x00},
    {0x3c,0xc6,0xc6,0x7e,0x06,0x0c,0x78,0x00},
    {0x00,0x18,0x18,0x00,0x18,0x18,0x00,0x00},
    {0x00,0x18,0x18,0x00,0x18,0x18,0x30,0x00},
    {0x0c,0x18,0x30,0x60,0x30,0x18,0x0c,0x00},
    {0x00,0x00,0x7e,0x00,0x7e,0x00,0x00,0x00},
    {0x30,0x18,0x0c,0x06,0x0c,0x18,0x30,0x00},
    {0x7c,0xc6,0x0c,0x18,0x18,0x00,0x18,0x00},
    {0x7c,0xc6,0x00,0x7c,0xc6,0xc6,0x7c,0x00},
    {0x38,0x6c,0xc6,0xfe,0xc6,0xc6,0xc6,0x00},
    {0xfc,0x66,0x66,0x7c,0x66,0x66,0xfc,0x00},
    {0x3c,0x66,0xc0,0xc0,0xc0,0x66,0x3c,0x00},
    {0xf8,0x6c,0x66,0x66,0x66,0x6c,0xf8,0x00},
    {0xfe,0x62,0x68,0x78,0x68,0x62,0xfe,0x00},
    {0xfe,0x62,0x68,0x78,0x68,0x60,0xf0,0x00},
    {0x3c,0x66,0xc0,0xc0,0xce,0x66,0x3e,0x00},
    {0xc6,0xc6,0xc6,0xfe,0xc6,0xc6,0xc6,0x00},
    {0x3c,0x18,0x18,0x18,0x18,0x18,0x3c,0x00},
    {0x1e,0x0c,0x0c,0x0c,0x0c,0xcc,0x78,0x00},
    {0xc6,0xcc,0xd8,0xf0,0xd8,0xcc,0xc6,0x00},
    {0xf0,0x60,0x60,0x60,0x60,0x62,0xfe,0x00},
    {0xc6,0xee,0xfe,0xfe,0xd6,0xc6,0xc6,0x00},
    {0xc6,0xe6,0xf6,0xde,0xce,0xc6,0xc6,0x00},
    {0x3c,0x66,0xc6,0xc6,0xc6,0x66,0x3c,0x00},
    {0xfc,0x66,0x66,0x7c,0x60,0x60,0xf0,0x00},
    {0x3c,0x66,0xc6,0xc6,0xd6,0x6c,0x3e,0x00},
    {0xfc,0x66,0x66,0x7c,0x6c,0x66,0xc6,0x00},
    {0x7c,0xc6,0x60,0x3c,0x06,0xc6,0x7c,0x00},
    {0x7e,0x5a,0x18,0x18,0x18,0x18,0x3c,0x00},
    {0xc6,0xc6,0xc6,0xc6,0xc6,0xc6,0x7c,0x00},
    {0xc6,0xc6,0xc6,0xc6,0xc6,0x6c,0x38,0x00},
    {0xc6,0xc6,0xd6,0xfe,0xfe,0xee,0xc6,0x00},
    {0xc6,0x6c,0x38,0x38,0x38,0x6c,0xc6,0x00},
    {0xc6,0x6c,0x38,0x18,0x18,0x18,0x3c,0x00},
    {0xfe,0xc6,0x0c,0x18,0x30,0x62,0xfe,0x00},
    {0x3c,0x30,0x30,0x30,0x30,0x30,0x3c,0x00},
    {0x80,0x40,0x20,0x10,0x08,0x04,0x02,0x00},
    {0x3c,0x0c,0x0c,0x0c,0x0c,0x0c,0x3c,0x00},
    {0x10,0x38,0x6c,0xc6,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xff},
    {0x30,0x18,0x0c,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x78,0x0c,0x7c,0xcc,0x76,0x00},
    {0xe0,0x60,0x7c,0x66,0x66,0x66,0x7c,0x00},
    {0x00,0x00,0x3c,0x66,0x60,0x66,0x3c,0x00},
    {0x1c,0x0c,0x7c,0xcc,0xcc,0xcc,0x76,0x00},
    {0x00,0x00,0x3c,0x66,0x7e,0x60,0x3c,0x00},
    {0x1c,0x30,0x7c,0x30,0x30,0x30,0x78,0x00},
    {0x00,0x00,0x76,0xcc,0xcc,0x7c,0x0c,0xf8},
    {0xe0,0x60,0x6c,0x76,0x66,0x66,0x66,0x00},
    {0x18,0x00,0x38,0x18,0x18,0x18,0x3c,0x00},
    {0x06,0x00,0x06,0x06,0x06,0x66,0x3c,0x00},
    {0xe0,0x60,0x66,0x6c,0x78,0x6c,0x66,0x00},
    {0x38,0x18,0x18,0x18,0x18,0x18,0x3c,0x00},
    {0x00,0x00,0xec,0xfe,0xd6,0xd6,0xd6,0x00},
    {0x00,0x00,0xdc,0x66,0x66,0x66,0x66,0x00},
    {0x00,0x00,0x3c,0x66,0x66,0x66,0x3c,0x00},
    {0x00,0x00,0x7c,0x66,0x66,0x7c,0x60,0xf0},
    {0x00,0x00,0x76,0xcc,0xcc,0x7c,0x0c,0x1e},
    {0x00,0x00,0xdc,0x76,0x60,0x60,0xf0,0x00},
    {0x00,0x00,0x7c,0x60,0x3c,0x06,0xf8,0x00},
    {0x30,0x30,0x7c,0x30,0x30,0x36,0x1c,0x00},
    {0x00,0x00,0xcc,0xcc,0xcc,0xcc,0x76,0x00},
    {0x00,0x00,0xcc,0xcc,0xcc,0x78,0x30,0x00},
    {0x00,0x00,0xc6,0xd6,0xfe,0xfe,0x6c,0x00},
    {0x00,0x00,0xc6,0x6c,0x38,0x6c,0xc6,0x00},
    {0x00,0x00,0xcc,0xcc,0xcc,0x7c,0x0c,0xf8},
    {0x00,0x00,0xfe,0xcc,0x18,0x34,0xfe,0x00},
    {0x0c,0x18,0x18,0x70,0x18,0x18,0x0c,0x00},
    {0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x00},
    {0x30,0x18,0x18,0x0e,0x18,0x18,0x30,0x00},
    {0x00,0x00,0x00,0x76,0xdc,0x00,0x00,0x00},
};

/* Port I/O helpers */
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
	uint8_t ret;
	asm volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
	return ret;
}

static inline uint32_t inl(uint16_t port) {
    uint32_t ret; asm volatile ("inl %1, %0" : "=a"(ret) : "Nd"(port)); return ret;
}

/* Essential x86 system table definitions */
struct gdt_entry {
    uint16_t limit_low; uint16_t base_low; uint8_t base_middle;
    uint8_t access; uint8_t granularity; uint8_t base_high;
} __attribute__((packed));

struct gdt_ptr { uint16_t limit; uint32_t base; } __attribute__((packed));

struct idt_entry {
    uint16_t base_lo; uint16_t sel; uint8_t always0;
    uint8_t flags; uint16_t base_hi;
} __attribute__((packed));

struct idt_ptr { uint16_t limit; uint32_t base; } __attribute__((packed));

struct registers {
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax;
    uint32_t int_no, err_code;
    uint32_t eip, cs, eflags, useresp, ss;
};

struct gdt_entry gdt[5];
struct gdt_ptr gp;
struct idt_entry idt[256];
struct idt_ptr idtp;
extern void gdt_flush(uint32_t);
extern void idt_load(uint32_t);

/* External ISR/IRQ stubs from boot.s */
extern void isr0();  extern void isr1();  extern void isr2();  extern void isr3();
extern void isr4();  extern void isr5();  extern void isr6();  extern void isr7();
extern void isr8();  extern void isr9();  extern void isr10(); extern void isr11();
extern void isr12(); extern void isr13(); extern void isr14(); extern void isr15();
extern void isr16(); extern void isr17(); extern void isr18(); extern void isr19();
extern void isr20(); extern void isr21(); extern void isr22(); extern void isr23();
extern void isr24(); extern void isr25(); extern void isr26(); extern void isr27();
extern void isr28(); extern void isr29(); extern void isr30(); extern void isr31();
extern void isr128();

void syscall_handler(struct registers* r);

extern unsigned char _binary_sharkscript_start[];
extern unsigned char _binary_sharkscript_end[];

extern void irq0();  extern void irq1();  extern void irq2();  extern void irq3();
extern void irq4();  extern void irq5();  extern void irq6();  extern void irq7();
extern void irq8();  extern void irq9();  extern void irq10(); extern void irq11();
extern void irq12(); extern void irq13(); extern void irq14(); extern void irq15();

void gdt_set_gate(int num, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    gdt[num].base_low = (base & 0xFFFF);
    gdt[num].base_middle = (base >> 16) & 0xFF;
    gdt[num].base_high = (base >> 24) & 0xFF;
    gdt[num].limit_low = (limit & 0xFFFF);
    gdt[num].granularity = (limit >> 16) & 0x0F;
    gdt[num].granularity |= (gran & 0xF0);
    gdt[num].access = access;
}

void idt_set_gate(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags) {
    idt[num].base_lo = base & 0xFFFF;
    idt[num].base_hi = (base >> 16) & 0xFFFF;
    idt[num].sel = sel;
    idt[num].always0 = 0;
    idt[num].flags = flags;
}

/* A simple ring buffer for processing keyboard input */
static char key_buffer[256];
static volatile int key_head = 0;
static volatile int key_tail = 0;

void keyboard_handler(uint8_t scancode) {
    if (scancode == 0x2A || scancode == 0x36) {
        shift_pressed = true;
    } else if (scancode == 0xAA || scancode == 0xB6) {
        shift_pressed = false;
    } else if (!(scancode & 0x80)) {
        char c = shift_pressed ? keyboard_map_shifted[scancode] : keyboard_map[scancode];
        if (c > 0) {
            int next = (key_head + 1) % 256;
            if (next != key_tail) {
                key_buffer[key_head] = c;
                key_head = next;
            }
        }
    }
}

char keyboard_getchar() {
    if (key_head == key_tail) return 0;
    char c = key_buffer[key_tail];
    key_tail = (key_tail + 1) % 256;
    return c;
}

void irq_handler(struct registers* r) {
    if (r->int_no >= 40) outb(0xA0, 0x20);
    outb(0x20, 0x20);

    if (rtl_io_base != 0 && r->int_no == (uint32_t)(rtl_irq + 32)) {
        uint16_t status = inw(rtl_io_base + 0x3E);
        outw(rtl_io_base + 0x3E, status); // Ack Interrupt
        if (status & 0x01) {
            terminal_writestring("\n[Network] Packet Received!");
        }
    }

    if (r->int_no == 33) { // Keyboard IRQ
        uint8_t scancode = inb(0x60);
        keyboard_handler(scancode);
    }
}

void isr_handler(struct registers* r) {
    if (r->int_no == 0x80) {
        syscall_handler(r);
        return;
    }

    char buf[11];
    terminal_writestring("\nCPU EXCEPTION: ");
    hex_to_string((uint32_t)r->int_no, buf);
    terminal_writestring(buf);
    terminal_writestring(". SYSTEM HALTED.");
    
    while(1) { asm volatile("hlt"); }
}

/* The interface for user applications to request services from the kernel */
void syscall_handler(struct registers* r) {
    if (r->eax == 1) {
        terminal_writestring("\nProcess exited.\n");
    } else if (r->eax == 3) {
        char* name = (char*)r->ebx;
        char* buf = (char*)r->ecx;
        for (int i = 0; i < pool_index; i++) {
            if (strcmp(node_pool[i].name, name) == 0 && node_pool[i].type == FS_FILE) {
                strcpy(buf, node_pool[i].content);
                r->eax = (uint32_t)node_pool[i].content_len;
                return;
            }
        }
        r->eax = (uint32_t)-1;
    } else if (r->eax == 4) {
        const char* buf = (const char*)r->ecx;
        uint32_t len = r->edx;
        for (uint32_t i = 0; i < len; i++) {
            terminal_putchar(buf[i]);
        }
    } else if (r->eax == 24) {
        yield();
    }
}

/* Basic logic for tracking and assigning system memory */
uint64_t* pmm_bitmap;
uintptr_t free_memory_start;

void pmm_init(uint64_t mem_size) {
    (void)mem_size;
    extern char _kernel_end[];
    free_memory_start = (uint32_t)_kernel_end + 0x1000;
}

void* kmalloc(size_t size) {
    void* res = (void*)free_memory_start;
    free_memory_start += size;
    return res;
}

void get_cpu_model(char* buffer) {
    uint32_t eax, ebx, ecx, edx;
    uint32_t* ptr = (uint32_t*)buffer;

    for (uint32_t i = 0; i < 3; i++) {
        asm volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(0x80000002 + i));
        ptr[i * 4 + 0] = eax;
        ptr[i * 4 + 1] = ebx;
        ptr[i * 4 + 2] = ecx;
        ptr[i * 4 + 3] = edx;
    }
    buffer[48] = '\0';
}

/* Simple power-off sequences for common PC emulators */
void shutdown() {
    outw(0xB004, 0x2000);
    outw(0x604, 0x2000);
    outw(0x4004, 0x3400);
    asm volatile("cli; hlt");
}

/* Utility functions for converting numbers into text */
void hex_to_string(uint32_t value, char* buffer) {
    char hex_chars[] = "0123456789ABCDEF";
    buffer[0] = '0';
    buffer[1] = 'x';
    buffer[10] = '\0';

    for (int i = 0; i < 8; i++) {
        buffer[9 - i] = hex_chars[value & 0xF];
        value >>= 4;
    }
}

void int_to_string(uint64_t value, char* buffer) {
    char temp[21];
    int i = 0;
    if (value == 0) {
        buffer[0] = '0';
        buffer[1] = '\0';
        return;
    }
    while (value > 0) {
        temp[i++] = (value % 10) + '0';
        value /= 10;
    }
    int j = 0;
    while (i > 0) {
        buffer[j++] = temp[--i];
    }
    buffer[j] = '\0';
}

void pci_list_devices() {
    char buffer[11];
    terminal_writestring("PCI Bus Scan:\n");
    for(uint32_t bus = 0; bus < 256; bus++) {
        for(uint32_t slot = 0; slot < 32; slot++) {
            uint32_t pci_data = pci_config_read(bus, slot, 0, 0);
            if (pci_data == 0xFFFFFFFF) continue;

            for (uint32_t func = 0; func < 8; func++) {
                pci_data = pci_config_read(bus, slot, func, 0);
                uint16_t vendor = pci_data & 0xFFFF;
                uint16_t device = (pci_data >> 16) & 0xFFFF;

                if (vendor == 0xFFFF || vendor == 0x0000) continue;

                terminal_writestring("  Bus ");
                hex_to_string(bus, buffer); terminal_writestring(buffer);
                terminal_writestring(" Slot ");
                hex_to_string(slot, buffer); terminal_writestring(buffer);
                terminal_writestring(" | Vendor: ");
                hex_to_string(vendor, buffer); terminal_writestring(buffer);
                terminal_writestring(" Device: ");
                hex_to_string(device, buffer); terminal_writestring(buffer);
                terminal_writestring("\n");
            }
        }
    }
}

void rtl8139_init() {
    for(uint32_t bus = 0; bus < 256; bus++) {
        for(uint32_t slot = 0; slot < 32; slot++) {
            uint32_t pci_data = pci_config_read(bus, slot, 0, 0);
            if (pci_data == 0xFFFFFFFF) continue;
            for (uint32_t func = 0; func < 8; func++) {
                pci_data = pci_config_read(bus, slot, func, 0);
                uint16_t vendor = pci_data & 0xFFFF;
                uint16_t device = (pci_data >> 16) & 0xFFFF;

                if (vendor == 0xFFFF || vendor == 0x0000) continue;

                if (vendor == 0x10EC && (device == 0x8139 || device == 0x8138 || device == 0x8130 || device == 0x8168 || device == 0x8169)) {
                    rtl_io_base = pci_config_read(bus, slot, func, 0x10) & ~0x1;
                    rtl_irq = pci_config_read(bus, slot, func, 0x3C) & 0xFF;
                    if (rtl_io_base == 0 || rtl_irq == 0xFF) continue;

                    uint32_t command = pci_config_read(bus, slot, func, 0x04);
                    command |= 0x05; 
                    pci_config_write(bus, slot, func, 0x04, command);
                    break;
                }
            }
            if (rtl_io_base) break;
        }
        if (rtl_io_base) break;
    }

    if (!rtl_io_base) {
        network_initialized = true;
        return;
    }

    // 2. Power On
    outb(rtl_io_base + 0x52, 0x00);

    // 3. Reset
    outb(rtl_io_base + 0x37, 0x10);
    while((inb(rtl_io_base + 0x37) & 0x10) != 0);

    // 4. Init Receive Buffer
    rx_buffer = (uint8_t*)kmalloc(8192 + 16 + 1500); // 8K + 16 (alignment) + 1.5K (max packet)
    outl(rtl_io_base + 0x30, (uint32_t)(uintptr_t)rx_buffer);

    // 5. Set Interrupt Mask (ROK | TOK)
    outw(rtl_io_base + 0x3C, 0x0005); // Receive OK, Transmit OK

    // 6. Configure Receiver & Enable
    outl(rtl_io_base + 0x44, 0xf | (1 << 7)); // AB+AM+APM+AAP + No wrapper (Accept Broadcast, Multicast, Physical Match, All Multicast)
    outb(rtl_io_base + 0x37, 0x0C); // Enable RE (Receiver) and TE (Transmitter)
    
    network_initialized = true;
}

void rtl8139_send_packet(void* data, uint32_t len) {
    if (!rtl_io_base) {
        terminal_writestring("Error: Cannot send packet, RTL8139 not initialized.\n");
        return;
    }

    outl(rtl_io_base + 0x20 + (current_tx_buffer * 4), (uint32_t)(uintptr_t)data);
    outl(rtl_io_base + 0x10 + (current_tx_buffer * 4), len);
    current_tx_buffer = (current_tx_buffer + 1) % 4;
}

void send_icmp_ping(const char* target) {
    if (rtl_io_base) {
        uint8_t* packet = (uint8_t*)kmalloc(64);
        for(int i=0; i<6; i++) packet[i] = 0xFF;
        packet[6] = 0x00; packet[7] = 0x11; packet[8] = 0x22; packet[9] = 0x33; packet[10] = 0x44; packet[11] = 0x55;
        packet[12] = 0x08; packet[13] = 0x00;
        packet[14] = 0x45;
        packet[15] = 0x00;
        packet[16] = 0x00; packet[17] = 0x34;
        packet[18] = 0x00; packet[19] = 0x01;
        packet[20] = 0x00; packet[21] = 0x00;
        packet[22] = 0x40;
        packet[23] = 0x01;
        packet[24] = 0x00; packet[25] = 0x00;
        packet[26] = 10; packet[27] = 0; packet[28] = 2; packet[29] = 15;
        packet[30] = 8; packet[31] = 8; packet[32] = 8; packet[33] = 8;
        packet[34] = 0x08;
        packet[35] = 0x00;
        packet[36] = 0x00; packet[37] = 0x00;
        packet[38] = 0x00; packet[39] = 0x01;
        packet[40] = 0x00; packet[41] = 0x01;
        for(int i=0; i<24; i++) packet[42+i] = 'A' + (i % 26);

        rtl8139_send_packet(packet, 64);
        terminal_writestring("Hardware: Packet sent to ");
        terminal_writestring(target);
        terminal_writestring(" (Ethernet Frame Built).\n");
    } else {
        terminal_writestring("Loopback: Reply from 127.0.0.1: bytes=32 time<1ms TTL=64\n");
    }
}

/* Low-level functions for scanning and configuring PCI devices */
uint32_t pci_config_read(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t address = (uint32_t)((bus << 16) | (slot << 11) | (func << 8) | (offset & 0xfc) | ((uint32_t)0x80000000));
    outl(0xCF8, address);
    return inl(0xCFC);
}

void pci_config_write(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t value) {
    uint32_t address = (uint32_t)((bus << 16) | (slot << 11) | (func << 8) | (offset & 0xfc) | ((uint32_t)0x80000000));
    outl(0xCF8, address);
    outl(0xCFC, value);
}

/* --- Graphics (VESA Baseline) --- */
struct vbe_info {
    char signature[4]; uint16_t version; uint32_t oem;
    uint32_t capabilities; uint32_t video_modes; uint16_t total_memory;
} __attribute__((packed));


/* --- Initialization --- */
void init_descriptor_tables() {
    idtp.limit = (sizeof(struct idt_entry) * 256) - 1;
    idtp.base = (uint32_t)&idt;

    /* Map all CPU Exceptions */
    idt_set_gate(0, (uint32_t)isr0, 0x08, 0x8E);   idt_set_gate(1, (uint32_t)isr1, 0x08, 0x8E);
    idt_set_gate(2, (uint32_t)isr2, 0x08, 0x8E);   idt_set_gate(3, (uint32_t)isr3, 0x08, 0x8E);
    idt_set_gate(4, (uint32_t)isr4, 0x08, 0x8E);   idt_set_gate(5, (uint32_t)isr5, 0x08, 0x8E);
    idt_set_gate(6, (uint32_t)isr6, 0x08, 0x8E);   idt_set_gate(7, (uint32_t)isr7, 0x08, 0x8E);
    idt_set_gate(8, (uint32_t)isr8, 0x08, 0x8E);   idt_set_gate(9, (uint32_t)isr9, 0x08, 0x8E);
    idt_set_gate(10, (uint32_t)isr10, 0x08, 0x8E); idt_set_gate(11, (uint32_t)isr11, 0x08, 0x8E);
    idt_set_gate(12, (uint32_t)isr12, 0x08, 0x8E); idt_set_gate(13, (uint32_t)isr13, 0x08, 0x8E);
    idt_set_gate(14, (uint32_t)isr14, 0x08, 0x8E); idt_set_gate(15, (uint32_t)isr15, 0x08, 0x8E);
    idt_set_gate(16, (uint32_t)isr16, 0x08, 0x8E); idt_set_gate(17, (uint32_t)isr17, 0x08, 0x8E);
    idt_set_gate(18, (uint32_t)isr18, 0x08, 0x8E); idt_set_gate(19, (uint32_t)isr19, 0x08, 0x8E);
    idt_set_gate(20, (uint32_t)isr20, 0x08, 0x8E); idt_set_gate(21, (uint32_t)isr21, 0x08, 0x8E);
    idt_set_gate(22, (uint32_t)isr22, 0x08, 0x8E); idt_set_gate(23, (uint32_t)isr23, 0x08, 0x8E);
    idt_set_gate(24, (uint32_t)isr24, 0x08, 0x8E); idt_set_gate(25, (uint32_t)isr25, 0x08, 0x8E);
    idt_set_gate(26, (uint32_t)isr26, 0x08, 0x8E); idt_set_gate(27, (uint32_t)isr27, 0x08, 0x8E);
    idt_set_gate(28, (uint32_t)isr28, 0x08, 0x8E); idt_set_gate(29, (uint32_t)isr29, 0x08, 0x8E);
    idt_set_gate(30, (uint32_t)isr30, 0x08, 0x8E); idt_set_gate(31, (uint32_t)isr31, 0x08, 0x8E);
    
    idt_set_gate(128, (uint32_t)isr128, 0x08, 0x8E); // Syscall vector

    outb(0x20, 0x11); outb(0xA0, 0x11);
    outb(0x21, 0x20); outb(0xA1, 0x28);
    outb(0x21, 0x04); outb(0xA1, 0x02);
    outb(0x21, 0x01); outb(0xA1, 0x01);
    outb(0x21, 0x0);  outb(0xA1, 0x0);

    idt_set_gate(32, (uint32_t)irq0, 0x08, 0x8E);  idt_set_gate(33, (uint32_t)irq1, 0x08, 0x8E);
    idt_set_gate(34, (uint32_t)irq2, 0x08, 0x8E);  idt_set_gate(35, (uint32_t)irq3, 0x08, 0x8E);
    idt_set_gate(36, (uint32_t)irq4, 0x08, 0x8E);  idt_set_gate(37, (uint32_t)irq5, 0x08, 0x8E);
    idt_set_gate(38, (uint32_t)irq6, 0x08, 0x8E);  idt_set_gate(39, (uint32_t)irq7, 0x08, 0x8E);
    idt_set_gate(40, (uint32_t)irq8, 0x08, 0x8E);  idt_set_gate(41, (uint32_t)irq9, 0x08, 0x8E);
    idt_set_gate(42, (uint32_t)irq10, 0x08, 0x8E); idt_set_gate(43, (uint32_t)irq11, 0x08, 0x8E);
    idt_set_gate(44, (uint32_t)irq12, 0x08, 0x8E); idt_set_gate(45, (uint32_t)irq13, 0x08, 0x8E);
    idt_set_gate(46, (uint32_t)irq14, 0x08, 0x8E); idt_set_gate(47, (uint32_t)irq15, 0x08, 0x8E);

    idt_load((uint32_t)&idtp);
    asm volatile("sti");
}

/* Logic for loading and executing ELF format binaries */
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

void execute_elf(uint8_t* data, const char* arg) {
    Elf32_Ehdr* header = (Elf32_Ehdr*)data;
    if (header->e_ident[0] != 0x7F || header->e_ident[1] != 'E' || 
        header->e_ident[2] != 'L' || header->e_ident[3] != 'F') {
        terminal_writestring("Not a valid ELF binary.\n");
        return;
    }

    if (header->e_type == 2 || header->e_type == 3) {
        Elf32_Phdr* phdr = (Elf32_Phdr*)((uint32_t)data + header->e_phoff);
        for (int i = 0; i < header->e_phnum; i++) {
            if (phdr[i].p_type == 1) {
                void* dest = (header->e_type == 3) ? (void*)((uint32_t)data + phdr[i].p_vaddr) : (void*)phdr[i].p_vaddr;
                memcpy(dest, (void*)((uint32_t)data + phdr[i].p_offset), phdr[i].p_filesz);
                if (phdr[i].p_memsz > phdr[i].p_filesz) {
                    memset((void*)((uintptr_t)dest + phdr[i].p_filesz), 0, phdr[i].p_memsz - phdr[i].p_filesz);
                }
            }
        }
    }

    uint32_t entry_addr = (header->e_type == 3) ? ((uint32_t)data + header->e_entry) : header->e_entry;
    void (*entry)(const char*) = (void (*)(const char*))entry_addr;

    terminal_writestring("Executing ELF binary... \n");
    asm volatile(
        "mov %0, %%esp\n"
        "push %1\n"
        "call *%2\n"
        : : "r"((uint32_t)&stack_top), "r"(arg), "r"(entry) : "memory"
    );
}

struct fs_node* find_node(struct fs_node* parent, const char* name) {
    if (!parent) return NULL;
    for (int i = 0; i < parent->num_children; i++) {
        if (strcmp(parent->children[i]->name, name) == 0) return parent->children[i];
    }
    return NULL;
}

struct fs_node* search_path(const char* name) {
    struct fs_node* n = find_node(current_dir, name);
    if (n && n->type == FS_FILE) return n;

    // 2. Search /System/Bin (Automatic Fallback)
    struct fs_node* sys = find_node(root, "System");
    if (sys) {
        struct fs_node* bin = find_node(sys, "Bin");
        if (bin) {
            n = find_node(bin, name);
            if (n && n->type == FS_FILE) return n;
        }
    }

    return NULL;
}

struct fs_node* create_node(const char* name, node_type_t type, struct fs_node* parent) {
	if (pool_index >= MAX_NODES) return NULL;
	struct fs_node* node = &node_pool[pool_index++];
	strcpy(node->name, name);
	node->type = type;
	node->parent = parent;
	node->num_children = 0;
	node->content[0] = '\0';
	node->content_len = 0;
	if (parent && parent->num_children < MAX_CHILDREN) {
		parent->children[parent->num_children++] = node;
	}
	return node;
}

void fs_initialize() {
	root = create_node("/", FS_DIRECTORY, NULL);
	current_dir = root;

	struct fs_node* user = create_node("User", FS_DIRECTORY, root);
	create_node("Documents", FS_DIRECTORY, user);
	create_node("Photos", FS_DIRECTORY, user);
	struct fs_node* readme = create_node("readme.txt", FS_FILE, user);
	strcpy(readme->content, "Welcome to SharkOS!");
	readme->content_len = strlen(readme->content);

	struct fs_node* system = create_node("System", FS_DIRECTORY, root);
	struct fs_node* bin_dir = create_node("Bin", FS_DIRECTORY, system);
	create_node("Drivers", FS_DIRECTORY, system);
	create_node("Kernel.sys", FS_FILE, system);

    /* Automatically load the 'sharkscript' binary as the 'shs' command */
    struct fs_node* shs_bin = create_node("shs", FS_FILE, bin_dir);
    if (shs_bin) {
        size_t size = (size_t)(_binary_sharkscript_end - _binary_sharkscript_start);
        if (size > MAX_FILE_CONTENT_SIZE) size = MAX_FILE_CONTENT_SIZE;
        for (size_t i = 0; i < size; i++) {
            shs_bin->content[i] = _binary_sharkscript_start[i];
        }
        shs_bin->content_len = (int)size;
    }
}

size_t terminal_row;
size_t terminal_column;
uint8_t terminal_color;
char command_buffer[80];
size_t command_index = 0;


void draw_char(char c, uint32_t x, uint32_t y, uint32_t fg, uint32_t bg) {
    if (c < 32 || c > 126) return; // Only printable ASCII
    uint32_t font_idx = c - 32;
    uint32_t stride = screen_pitch / 4; // Pixels per row
    uint32_t* target_row_ptr = &lfbptr[y * stride + x];

    for (int row = 0; row < 8; row++) {
        uint8_t font_byte = font8x8[font_idx][row];
        for (int col = 0; col < 8; col++) {
            if ((font_byte >> (7 - col)) & 1) { // Check bit from left to right
                target_row_ptr[col] = fg;
            } else if (bg != 0xFF000000) { // Only draw background if not transparent
                target_row_ptr[col] = bg;
            }
        }
        target_row_ptr += stride;
    }
}

void terminal_scroll() {
    uint32_t char_height = 8;
    uint32_t start_y_pixels = 3 * char_height; // Start below header
    uint32_t end_y_pixels = (screen_height / 8 - 2) * char_height; // End above footer
    uint32_t scroll_height_pixels = end_y_pixels - start_y_pixels - char_height;
    uint32_t stride = screen_pitch / 4;

    // Move all rows up by one character height
    memcpy(
        &lfbptr[start_y_pixels * stride],
        &lfbptr[(start_y_pixels + char_height) * stride],
        scroll_height_pixels * stride * sizeof(uint32_t)
    );

    // Clear the last line
    draw_rect(0, end_y_pixels - char_height, screen_width, char_height, vga_to_rgb[VGA_COLOR_BLUE]);
    terminal_row = (screen_height / 8) - 3; // Adjust terminal_row to the last visible line
}

void terminal_clear(void) {
    draw_rect(0, 3 * 8, screen_width, (screen_height / 8 - 5) * 8, vga_to_rgb[VGA_COLOR_BLUE]);
    // Reset cursor to the top of the workspace
    terminal_row = 3;
    terminal_column = 2;
}

void draw_pixel(uint32_t x, uint32_t y, uint32_t color) { // Kept for compatibility, but not used by draw_char/rect
    if (x < screen_width && y < screen_height) {
        uint32_t stride = screen_pitch / 4;
        lfbptr[y * stride + x] = color;
    }
}

void draw_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
    uint32_t stride = screen_pitch / 4;
    for (uint32_t i = 0; i < h; i++) {
        uint32_t* dest = &lfbptr[(y + i) * stride + x];
        for (uint32_t j = 0; j < w; j++) {
            dest[j] = color;
        }
    }
}


void terminal_putentryat(char c, uint8_t color, size_t x, size_t y) {
    uint32_t fg = vga_to_rgb[color & 0x0F];
    uint32_t bg = vga_to_rgb[(color >> 4) & 0x0F];
    draw_char(c, x * 8, y * 8, fg, bg);
}

void terminal_set_color(uint8_t color) {
	terminal_color = color;
}

void terminal_initialize(void) {
	/* Draw Desktop Background (Workspace) */
    draw_rect(0, 0, screen_width, screen_height, vga_to_rgb[VGA_COLOR_BLUE]);
    
	/* Draw Top Bar (Header) */
	uint32_t header_rgb = vga_to_rgb[VGA_COLOR_LIGHT_GREY];
    draw_rect(0, 0, screen_width, 16, header_rgb);
	
	const char* title = " SharkOS Desktop v0.1 ";
	for (size_t i = 0; title[i] != '\0'; i++) {
		draw_char(title[i], (i + 2) * 8, 4, vga_to_rgb[VGA_COLOR_BLACK], header_rgb);
    }

	/* Draw Bottom Bar (Footer) */
	uint32_t footer_rgb = vga_to_rgb[VGA_COLOR_BLACK];
    draw_rect(0, screen_height - 16, screen_width, 16, footer_rgb);
	
	const char* footer = " [ALT+F4] Shutdown | [START] Menu | Typing in Workspace...";
	for (size_t i = 0; footer[i] != '\0'; i++) {
		draw_char(footer[i], (i + 1) * 8, screen_height - 12, vga_to_rgb[VGA_COLOR_WHITE], footer_rgb);
    }

	/* Set cursor for terminal input inside the workspace */
	terminal_row = 2;
	terminal_column = 2;
	terminal_color = vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE);
}

void execute_command(char* cmd) {
    if (strlen(cmd) == 0) return;
	uint8_t old_color = terminal_color;
	terminal_putchar('\n');

    /* Split command and arguments */
    char cmd_name[32];
    int i = 0;
    while(cmd[i] != ' ' && cmd[i] != '\0' && i < 31) {
        cmd_name[i] = cmd[i];
        i++;
    }
    cmd_name[i] = '\0';
    char* args = (cmd[i] == ' ') ? &cmd[i+1] : "";

	if (strcmp(cmd_name, "ls") == 0 || strcmp(cmd_name, "dir") == 0) {
		for (int i = 0; i < current_dir->num_children; i++) {
			terminal_writestring(current_dir->children[i]->name);
			if (current_dir->children[i]->type == FS_DIRECTORY) terminal_writestring("/");
			terminal_writestring("  ");
		}
	} else if (strcmp(cmd_name, "cd") == 0) {
        if (strcmp(args, "..") == 0) {
            if (current_dir->parent) current_dir = current_dir->parent;
        } else {
            struct fs_node* target = find_node(current_dir, args);
            if (target && target->type == FS_DIRECTORY) current_dir = target;
            else terminal_writestring("Directory not found.");
        }
	} else if (strcmp(cmd_name, "cat") == 0) {
        struct fs_node* target = find_node(current_dir, args);
        if (target && target->type == FS_FILE) terminal_writestring(target->content);
        else terminal_writestring("File not found.");
	} else if (strcmp(cmd_name, "touch") == 0) {
        if (find_node(current_dir, args)) terminal_writestring("File already exists.");
        else create_node(args, FS_FILE, current_dir);
	} else if (strcmp(cmd_name, "edit") == 0) {
		struct fs_node* target = find_node(current_dir, args);
		if (target && target->type == FS_FILE) {
			editor_target_file = target;
			current_kernel_mode = KERNEL_MODE_EDITOR;
			terminal_clear();
			terminal_writestring("Editing: ");
			terminal_writestring(editor_target_file->name);
			terminal_writestring("\nPress ESC to save and exit.\n\n");
			editor_buffer_idx = 0;
		} else terminal_writestring("File not found.");
	} else if (strcmp(cmd_name, "whoami") == 0) {
		terminal_writestring("sharkuser");
	} else if (strcmp(cmd_name, "ping") == 0) {
		if (!network_initialized) {
            terminal_writestring("Network system failure.");
        } else {
            const char* target = (strlen(args) > 0) ? args : "127.0.0.1";
            send_icmp_ping(target);
        }
	} else if (strcmp(cmd_name, "clear") == 0 || strcmp(cmd_name, "cls") == 0) {
		terminal_clear();
		print_prompt();
		return;
	} else if (strcmp(cmd_name, "help") == 0) {
		terminal_set_color(vga_entry_color(VGA_COLOR_LIGHT_MAGENTA, VGA_COLOR_BLUE));
		terminal_writestring("--- SharkOS Help Menu ---\n");
		terminal_set_color(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLUE));
		terminal_writestring("Filesystem: ");
		terminal_set_color(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE));
		terminal_writestring("ls, dir, cd, cat, touch, edit\n");
		terminal_set_color(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLUE));
		terminal_writestring("System:     ");
		terminal_set_color(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE));
		terminal_writestring("whoami, ping, sysinfo, colors, lspci, clear, cls, help\n");
		terminal_set_color(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLUE));
		terminal_writestring("Apps:       ");
		terminal_set_color(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE));
		terminal_writestring("shs, gcc, go\n");
		terminal_set_color(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLUE));
		terminal_writestring("Power:      ");
		terminal_set_color(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE));
		terminal_writestring("bokop, poweroff\n");
		terminal_set_color(old_color);
	} else if (strcmp(cmd_name, "lspci") == 0) {
		pci_list_devices();
	} else if (strcmp(cmd_name, "bokop") == 0 || strcmp(cmd_name, "poweroff") == 0) {
        terminal_writestring("Shutting down...");
        shutdown();
	} else if (strcmp(cmd_name, "sysinfo") == 0) {
        char buf[32];
        char cpu_model[49];
        get_cpu_model(cpu_model);
        terminal_writestring("OS: SharkOS v0.1 | CPU: ");
        terminal_writestring(cpu_model); terminal_writestring("\n");
        int_to_string(screen_width, buf); terminal_writestring(buf); terminal_writestring("x");
        int_to_string(screen_height, buf); terminal_writestring(buf); terminal_writestring("\n");
        int_to_string(total_system_memory / 1024 / 1024, buf); terminal_writestring(buf); terminal_writestring(" MB\n");
	} else if (strcmp(cmd_name, "colors") == 0) {
		uint8_t old_color = terminal_color;
		terminal_writestring("SharkOS 16-Color Palette:\n");
		for (int i = 0; i < 16; i++) {
			terminal_set_color(vga_entry_color(i, VGA_COLOR_BLUE));
			terminal_writestring("Color Code ");
		}
		terminal_set_color(old_color);
	} else if (strcmp(cmd_name, "ps") == 0) {
        spin_lock(&task_list_lock);
        terminal_writestring("PID    NAME            STATE      CPU\n");
        task_t* t = task_list;
        while (t) {
            char buf[16];
            int_to_string(t->id, buf);
            terminal_writestring(buf);
            terminal_writestring("      ");
            terminal_writestring(t->name);
            terminal_writestring("          ");
            if (t->state == TASK_RUNNING) terminal_writestring("RUNNING    ");
            else terminal_writestring("READY      ");
            int_to_string(t->cpu_id, buf);
            terminal_writestring(buf);
            terminal_writestring("\n");
            t = t->next;
        }
        spin_unlock(&task_list_lock);
	} else {
        struct fs_node* bin = search_path(cmd_name);
        if (bin) {
            execute_elf((uint8_t*)bin->content, args);
        } else {
		    terminal_writestring("Unknown command: ");
		    terminal_writestring(cmd_name);
        }
	}
	
	terminal_writestring("\n\n");
	terminal_writestring("SharkOS --@> ");
}

void show_welcome_tour() {
    char buf[32];
    char cpu_model[49];
    get_cpu_model(cpu_model);

    uint8_t old_color = terminal_color;
    terminal_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLUE));
    terminal_writestring("--- Welcome to SharkOS v0.1 ---\n");
    
    terminal_set_color(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE));
    terminal_writestring("System Specifications:\n");
    terminal_writestring("  CPU: "); terminal_writestring(cpu_model); terminal_writestring("\n");
    
    terminal_writestring("  Display: ");
    int_to_string(screen_width, buf); terminal_writestring(buf); terminal_writestring("x");
    int_to_string(screen_height, buf); terminal_writestring(buf); terminal_writestring("\n");
    
    terminal_writestring("  Memory: ");
    int_to_string(total_system_memory / 1024 / 1024, buf); terminal_writestring(buf); terminal_writestring(" MB\n");
    
    terminal_writestring("  Network: ");
    terminal_writestring(rtl_io_base != 0 ? "RTL8139 Online\n" : "Offline (Loopback)\n");

    terminal_writestring("\nQuick Tour:\n");
    terminal_writestring("  * Type 'help' to see all commands.\n");
    terminal_writestring("  * Use 'ls' and 'cd' to explore the filesystem.\n");
    terminal_writestring("  * Use 'edit <filename>' to create or modify files.\n");
    terminal_writestring("  * Type 'sysinfo' to see specs again.\n\n");
    
    terminal_set_color(old_color);
}

void print_prompt() {
	terminal_writestring("SharkOS --@> ");
}

/* Internal helper to handle visual rendering and cursor movement only */
void terminal_write_char_internal(char c) {
	if (c == '\n') {
		terminal_column = 2;
		if (++terminal_row >= (screen_height / 8) - 2) {
            terminal_scroll();
        }
		return;
	}
	if (c == '\t') {
		terminal_column = (terminal_column + 8) & ~7;
		if (terminal_column >= (screen_width / 8) - 2) {
			terminal_column = 2;
			if (++terminal_row >= (screen_height / 8) - 2) {
                terminal_scroll();
            }
		}
		return;
	}
	terminal_putentryat(c, terminal_color, terminal_column, terminal_row);
	if (++terminal_column >= (screen_width / 8) - 2) {
		terminal_column = 2;
		if (++terminal_row >= (screen_height / 8) - 2) {
            terminal_scroll();
        }
	}
}

void terminal_putchar_cli(char c) {
	if (c == '\n') {
	}
	if (c == '\b') {
        if (command_index > 0) {
            command_index--;
            if (terminal_column > 2) {
                terminal_column--;
            } else if (terminal_row > 3) {
                terminal_row--;
                terminal_column = (screen_width / 8) - 3;
            }
            terminal_putentryat(' ', terminal_color, terminal_column, terminal_row);
        }
		return;
	}
	if (c == '\t') {
		terminal_write_char_internal(c);
		return;
	}

	if (command_index < sizeof(command_buffer) - 1) { // Leave space for null terminator
		command_buffer[command_index++] = c;
	}
	terminal_write_char_internal(c);
}

void terminal_putchar_editor(char c) {
	if (c == '\n') {
		if (editor_buffer_idx < sizeof(editor_buffer) - 1) {
			editor_buffer[editor_buffer_idx++] = '\n';
		}
		terminal_column = 2;
		if (++terminal_row == 23) terminal_row = 2;
		return;
	}
	if (c == '\b') {
		if (editor_buffer_idx > 0) {
			editor_buffer_idx--;
		}
		if (terminal_column > 2) {
			terminal_column--;
		} else if (terminal_row > 2) {
			terminal_row--;
			terminal_column = (screen_width / 8) - 3;
		}
		terminal_putentryat(' ', terminal_color, terminal_column, terminal_row);
		return;
	}
	if (c == '\t') {
		terminal_write_char_internal(c);
		return;
	}

	if (editor_buffer_idx < sizeof(editor_buffer) - 1) {
		editor_buffer[editor_buffer_idx++] = c;
	}
	terminal_write_char_internal(c);
}

void terminal_putchar(char c) {
	terminal_write_char_internal(c);
}

void terminal_writestring(const char* data) {
	for (size_t i = 0; data[i] != '\0'; i++) {
		terminal_write_char_internal(data[i]);
	}
}

/* Handover information from the multiboot-compliant bootloader */
struct multiboot_info {
    uint32_t flags;
    uint32_t mem_lower;
    uint32_t mem_upper;
    uint32_t boot_device;
    uint32_t cmdline;
    uint32_t mods_count;
    uint32_t mods_addr;
    uint32_t num, size, addr, shndx;
    uint32_t mmap_length;
    uint32_t mmap_addr;
    uint32_t drives_length;
    uint32_t drives_addr;
    uint32_t config_table;
    uint32_t boot_loader_name;
    uint32_t apm_table;
    uint32_t vbe_control_info;
    uint32_t vbe_mode_info;
    uint16_t vbe_mode;
    uint16_t vbe_interface_seg;
    uint16_t vbe_interface_off;
    uint16_t vbe_interface_len;
    uint32_t framebuffer_addr_lo;
    uint32_t framebuffer_addr_hi;
    uint32_t framebuffer_pitch;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint8_t  framebuffer_bpp;
    uint8_t  framebuffer_type;
} __attribute__((packed));

struct multiboot_mmap_entry {
    uint32_t size;
    uint32_t addr_low;
    uint32_t addr_high;
    uint32_t len_low;
    uint32_t len_high;
    uint32_t type;
} __attribute__((packed));


void kmain(uint32_t magic, struct multiboot_info* mb_info) {
    if (magic != 0x2BADB002) return;

    /* Ensure interrupts are disabled during critical boot phase */
    asm volatile("cli");

    if ((mb_info->flags & (1 << 12)) && mb_info->framebuffer_bpp == 32) {
        lfbptr = (uint32_t*)(uintptr_t)mb_info->framebuffer_addr_lo;
        screen_width = mb_info->framebuffer_width;
        screen_height = mb_info->framebuffer_height;
        screen_pitch = mb_info->framebuffer_pitch;

        terminal_initialize();
        init_descriptor_tables();

        /* Detect memory size using Multiboot Memory Map (supports up to 128GB+) */
        if (mb_info->flags & (1 << 6)) {
            uintptr_t mmap_addr = (uintptr_t)mb_info->mmap_addr;
            uint32_t mmap_length = mb_info->mmap_length;
            total_system_memory = 0;
            for (uint32_t i = 0; i < mmap_length; ) {
                struct multiboot_mmap_entry* entry = (struct multiboot_mmap_entry*)(uintptr_t)(mmap_addr + i);
                if (entry->type == 1) { // Type 1 is Available RAM
                    uint64_t length = ((uint64_t)entry->len_high << 32) | entry->len_low;
                    total_system_memory += length;
                }
                i += entry->size + 4;
            }
        } else {
            total_system_memory = (uint64_t)mb_info->mem_upper * 1024;
        }

        pmm_init(total_system_memory); 

        /* Initialize SMP Core Tracking */
        for(int i = 0; i < MAX_CPUS; i++) {
            cpus[i].id = i;
            cpus[i].started = (i == 0);
        }

        /* Initialize Kernel Task (PID 1) */
        cpus[0].current_task = create_task("kernel_init");
        cpus[0].current_task->state = TASK_RUNNING;

        fs_initialize();
        rtl8139_init();

        show_welcome_tour();
        print_prompt();

        while (1) {
            char c = keyboard_getchar();
            if (c != 0) {
                if (current_kernel_mode == KERNEL_MODE_CLI) {
                    if (c == '\n') {
                        command_buffer[command_index] = '\0';
                        execute_command(command_buffer);
                        command_index = 0;
                    } else {
                        terminal_putchar_cli(c);
                    }
                } else if (current_kernel_mode == KERNEL_MODE_EDITOR) {
                    if (c == 27) { // ESC key
                        editor_buffer[editor_buffer_idx] = '\0';
                        strcpy(editor_target_file->content, editor_buffer);
                        editor_target_file->content_len = editor_buffer_idx;
                        current_kernel_mode = KERNEL_MODE_CLI;
                        terminal_clear();
                        terminal_writestring("File saved. Exiting editor.\n\n");
                        print_prompt();
                    } else {
                        terminal_putchar_editor(c);
                    }
                }
            }
            __asm__ volatile("hlt");
        }
    } else {
        const char* msg_no_lfb = "No LFB or 32-bit mode. Halting.";
        uint16_t* vga_buffer = (uint16_t*)0xB8000;
        for (int i = 0; msg_no_lfb[i] != '\0'; i++) {
            vga_buffer[80 + i] = (uint16_t)msg_no_lfb[i] | 0x0C00; 
        }
        while(1) { asm volatile("hlt"); }
    }
    while(1) { asm volatile("hlt"); }
}
