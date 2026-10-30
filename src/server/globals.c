#include "kernel.h"

struct fs_node node_pool[MAX_NODES];
int pool_index;
struct fs_node* root;
struct fs_node* current_dir;
volatile uint32_t uptime_ticks;
volatile uint32_t server_idle_ticks;
volatile uint8_t server_idle_waiting;
uint64_t total_system_memory;
uintptr_t free_memory_start;
char current_user[32] = "admin";
kernel_mode_t current_kernel_mode = KERNEL_MODE_CLI;
uint32_t rtc_seconds;
uint64_t screen_width;
uint64_t screen_height;
size_t content_first_row;
pane_t panes[MAX_PANES];
int pane_count = 1;
int active_pane;
task_t* task_list;
spinlock_t task_list_lock;
uint32_t sys_cpu_percent;
uint32_t sys_cpu_history[SYS_CPU_HISTORY];
int sys_cpu_history_pos;
uint32_t sys_frames_rendered;
uint32_t sys_frames_per_sec;
bool terminal_in_desktop_window;
int desktop_terminal_cols = 80;
