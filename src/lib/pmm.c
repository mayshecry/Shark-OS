#include "kernel.h"

uint64_t* pmm_bitmap;
uintptr_t free_memory_end;

void pmm_init(uint64_t mem_size) {
    extern char _kernel_end[];
    free_memory_start = (uint32_t)_kernel_end + 0x1000;
    uintptr_t mem_end = (uintptr_t)(mem_size * 1024ULL);
    free_memory_end = mem_end;
    if (free_memory_end < free_memory_start + 32768) {
        free_memory_end = free_memory_start + 32768;
    }
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

uintptr_t virt_to_phys(void* addr) {
    return (uintptr_t)addr;
}