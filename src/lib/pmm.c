#include "kernel.h"

uint64_t* pmm_bitmap;
uintptr_t free_memory_end;

void pmm_init(uint64_t mem_size_bytes) {
    extern char _kernel_end[];
    free_memory_start = (uintptr_t)_kernel_end + 0x1000;
    uintptr_t mem_end = (uintptr_t)mem_size_bytes;
    free_memory_end = mem_end;
    if (free_memory_end < free_memory_start + 32768) {
        free_memory_end = free_memory_start + 32768;
    }
    
    pmm_bitmap = (uint64_t*)free_memory_start;
    memset(pmm_bitmap, 0xFF, 4096);
    free_memory_start += 4096;
}

void* kmalloc(size_t size) {
    if (size == 0) return NULL;
    uintptr_t aligned = (free_memory_start + 15) & ~15;
    if (aligned + size < aligned) return NULL;
    if (aligned + size > free_memory_end) {
        terminal_writestring("\n[PMM] OUT OF MEMORY\n");
        return NULL;
    }
    void* res = (void*)aligned;
    free_memory_start = aligned + size;
    return res;
}

void kfree(void* ptr) {
    if (!ptr) return;
    uintptr_t addr = (uintptr_t)ptr;
    if (addr < free_memory_start) {
        terminal_writestring("\n[PMM] DOUBLE FREE DETECTED\n");
        return;
    }
}

uintptr_t virt_to_phys(void* addr) {
    return (uintptr_t)addr;
}
