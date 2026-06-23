#include "kernel.h"

uint64_t* pmm_bitmap;
uintptr_t free_memory_end;

void pmm_init(uint64_t mem_size) {
    (void)mem_size;
    extern char _kernel_end[];
    free_memory_start = (uint32_t)_kernel_end + 0x1000;
    free_memory_end = (uintptr_t)(total_system_memory * 1024);
}

void* kmalloc(size_t size) {
    if (size == 0) return NULL;
    uintptr_t aligned = (free_memory_start + 15) & ~15;
    if (aligned + size < aligned) return NULL;
    if (aligned + size > free_memory_end) return NULL;
    void* res = (void*)aligned;
    free_memory_start = aligned + size;
    return res;
}