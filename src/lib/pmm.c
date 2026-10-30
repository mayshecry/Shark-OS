#include "kernel.h"

uint64_t* pmm_bitmap;
uintptr_t free_memory_end;

#define PMM_REGION_CAPACITY 128

static pmm_memory_region_t pmm_regions[PMM_REGION_CAPACITY];
static uint32_t pmm_region_count;
static uint32_t pmm_region_index;
static uint64_t pmm_free_bytes;

static void pmm_add_region(uint64_t start, uint64_t end) {
    if (end <= start || pmm_region_count >= PMM_REGION_CAPACITY) return;
    uint64_t kernel_floor = (uint64_t)(uintptr_t)free_memory_start;
    if (start < kernel_floor) start = kernel_floor;
    start = (start + 15) & ~15ULL;
    end &= ~15ULL;
    if (end <= start) return;
    pmm_regions[pmm_region_count].start = start;
    pmm_regions[pmm_region_count].end = end;
    pmm_region_count++;
}

static void pmm_activate_region(uint32_t index) {
    pmm_region_index = index;
    free_memory_start = (uintptr_t)pmm_regions[index].start;
    free_memory_end = (uintptr_t)pmm_regions[index].end;
}

void pmm_init_regions(const pmm_memory_region_t* regions, uint32_t count) {
    extern char _kernel_end[];
    pmm_region_count = 0;
    pmm_region_index = 0;
    pmm_free_bytes = 0;
    pmm_bitmap = NULL;
    free_memory_start = (uintptr_t)_kernel_end + 0x2000;
    free_memory_end = free_memory_start;

    for (uint32_t i = 0; regions && i < count; i++) {
        pmm_add_region(regions[i].start, regions[i].end);
    }

    for (uint32_t i = 1; i < pmm_region_count; i++) {
        pmm_memory_region_t value = pmm_regions[i];
        uint32_t j = i;
        while (j && pmm_regions[j - 1].start > value.start) {
            pmm_regions[j] = pmm_regions[j - 1];
            j--;
        }
        pmm_regions[j] = value;
    }

    if (!pmm_region_count) {
        uint64_t start = (uint64_t)(uintptr_t)_kernel_end + 0x2000;
        pmm_add_region(start, start + 32768);
    }

    for (uint32_t i = 0; i < pmm_region_count; i++) {
        pmm_free_bytes += pmm_regions[i].end - pmm_regions[i].start;
    }

    pmm_activate_region(0);
    if (free_memory_end - free_memory_start >= 4096) {
        pmm_bitmap = (uint64_t*)free_memory_start;
        memset(pmm_bitmap, 0, 4096);
        free_memory_start += 4096;
        pmm_free_bytes -= 4096;
    }
}

void pmm_init(uint64_t mem_size_bytes) {
    extern char _kernel_end[];
    pmm_memory_region_t region;
    region.start = (uint64_t)(uintptr_t)_kernel_end + 0x1000;
    region.end = mem_size_bytes;
    if (region.end < region.start + 32768) region.end = region.start + 32768;
    pmm_init_regions(&region, 1);
}

uint64_t pmm_free_memory_bytes(void) {
    return pmm_free_bytes;
}

void* kmalloc(size_t size) {
    if (!size) return NULL;
    uint64_t requested = (uint64_t)size;

    while (pmm_region_index < pmm_region_count) {
        uint64_t current = (uint64_t)free_memory_start;
        uint64_t end = (uint64_t)free_memory_end;
        uint64_t aligned = (current + 15) & ~15ULL;
        if (aligned < current || aligned > end || requested > end - aligned) {
            pmm_free_bytes -= end - current;
            pmm_region_index++;
            if (pmm_region_index >= pmm_region_count) break;
            pmm_activate_region(pmm_region_index);
            continue;
        }

        pmm_free_bytes -= aligned - current;
        pmm_free_bytes -= requested;
        free_memory_start = (uintptr_t)(aligned + requested);
        return (void*)(uintptr_t)aligned;
    }

    terminal_writestring("\n[PMM] OUT OF MEMORY\n");
    return NULL;
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
