#include "kernel.h"

uint64_t* pmm_bitmap;

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