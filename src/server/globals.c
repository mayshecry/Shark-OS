#include "kernel.h"

struct fs_node node_pool[MAX_NODES];
int pool_index;
struct fs_node* root;
struct fs_node* current_dir;
volatile uint32_t uptime_ticks;
uint64_t total_system_memory;
uintptr_t free_memory_start;
char current_user[32] = "admin";
kernel_mode_t current_kernel_mode = KERNEL_MODE_CLI;
uint32_t rtc_seconds;
