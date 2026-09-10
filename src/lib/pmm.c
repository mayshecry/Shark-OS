#include "kernel.h"

#define PMM_HDR        16
#define PMM_MINPAY     32
#define PMM_MAGIC      0x53484B46u
#define PMM_MAXREGION  64
#define PMM_MAXRESERVE 16

typedef struct {
    uintptr_t base;
    uintptr_t end;
} pmm_region_t;

typedef struct pmm_block {
    uint32_t magic;
    uint32_t size;
    struct pmm_block* next;
    uint32_t pad;
} pmm_block_t;

struct multiboot_mmap_ent {
    uint32_t size, addr_low, addr_high, len_low, len_high, type;
} __attribute__((packed));

extern char _kernel_end[];

static pmm_region_t pmm_regions[PMM_MAXREGION];
static int pmm_region_count = 0;
static int pmm_region_cur = 0;
static pmm_block_t* pmm_free = NULL;

static uintptr_t reserve_base[PMM_MAXRESERVE];
static uintptr_t reserve_end[PMM_MAXRESERVE];
static int reserve_count = 0;

uintptr_t free_memory_end;

static uintptr_t pmm_heap_floor(void) {
    return ((uintptr_t)_kernel_end + 0x1000u + 15u) & ~(uintptr_t)15u;
}

static void pmm_insert_sorted(uintptr_t base, uintptr_t end) {
    if (pmm_region_count >= PMM_MAXREGION) return;
    int i = pmm_region_count;
    while (i > 0 && pmm_regions[i - 1].base > base) {
        pmm_regions[i] = pmm_regions[i - 1];
        i--;
    }
    pmm_regions[i].base = base;
    pmm_regions[i].end = end;
    pmm_region_count++;
}

/* Regions are kept sorted and disjoint. Loaders are allowed to report
 * overlapping usable ranges, and without this two overlapping entries would
 * let the same physical page be handed out twice. */
static void pmm_add_region(uintptr_t base, uintptr_t end) {
    uintptr_t floor_addr = pmm_heap_floor();
    if (base < floor_addr) base = floor_addr;
    base = (base + 15u) & ~(uintptr_t)15u;
    end &= ~(uintptr_t)15u;
    if (base >= end) return;

    for (int i = 0; i < pmm_region_count; i++) {
        uintptr_t rb = pmm_regions[i].base;
        uintptr_t re = pmm_regions[i].end;
        if (end <= rb || base >= re) continue;
        if (base < rb) pmm_add_region(base, rb);
        if (re < end) pmm_add_region(re, end);
        return;
    }
    pmm_insert_sorted(base, end);
}

static void pmm_remove_at(int i) {
    for (int j = i; j < pmm_region_count - 1; j++) pmm_regions[j] = pmm_regions[j + 1];
    pmm_region_count--;
}

static void pmm_carve(uintptr_t rb, uintptr_t re) {
    int i = 0;
    while (i < pmm_region_count) {
        uintptr_t ob = pmm_regions[i].base;
        uintptr_t oe = pmm_regions[i].end;
        if (re <= ob || rb >= oe) { i++; continue; }
        pmm_remove_at(i);
        if (ob < rb) pmm_add_region(ob, rb);
        if (re < oe) pmm_add_region(re, oe);
        i = 0;
    }
}

void pmm_reserve(uintptr_t base, size_t len) {
    if (len == 0) return;
    if (reserve_count < PMM_MAXRESERVE) {
        reserve_base[reserve_count] = base;
        reserve_end[reserve_count] = base + len;
        reserve_count++;
    }
    pmm_carve(base, base + len);
}

void pmm_init(uint32_t mmap_addr, uint32_t mmap_len, uint64_t mem_size_bytes) {
    uintptr_t cap = (uintptr_t)mem_size_bytes;
    pmm_region_count = 0;
    pmm_region_cur = 0;
    pmm_free = NULL;

    if (mmap_addr && mmap_len >= sizeof(struct multiboot_mmap_ent)) {
        uint32_t off = 0;
        while (off + 4 <= mmap_len && pmm_region_count < PMM_MAXREGION) {
            struct multiboot_mmap_ent* e = (struct multiboot_mmap_ent*)(uintptr_t)(mmap_addr + off);
            uint32_t esz = e->size;
            if (esz < sizeof(struct multiboot_mmap_ent) - 4) break;
            uint32_t stride = (esz >= sizeof(struct multiboot_mmap_ent)) ? esz : esz + 4;
            off += stride;
            if (e->type != 1) continue;
            if (e->addr_high != 0) continue;
            uint64_t len64 = ((uint64_t)e->len_high << 32) | (uint64_t)e->len_low;
            if (len64 == 0) continue;
            uintptr_t base = (uintptr_t)e->addr_low;
            uintptr_t end = base + (uintptr_t)(len64 > 0xFFFFFFFFull ? 0xFFFFFFFFull : len64);
            if (end < base) end = 0xFFFFFFFFu;
            if (cap && end > cap) end = cap;
            if (base < 0x100000u) base = 0x100000u;
            pmm_add_region(base, end);
        }
    }

    if (pmm_region_count == 0) {
        pmm_add_region(0x100000u, cap ? cap : (pmm_heap_floor() + 32768u));
    }

    for (int i = 0; i < reserve_count; i++) {
        pmm_carve(reserve_base[i], reserve_end[i]);
    }

    if (pmm_region_count == 0) {
        free_memory_start = pmm_heap_floor();
        free_memory_end = free_memory_start;
        return;
    }

    free_memory_start = pmm_regions[0].base;
    free_memory_end = pmm_regions[pmm_region_count - 1].end;
    if (free_memory_end < free_memory_start + 32768u) {
        free_memory_end = free_memory_start + 32768u;
    }
}

static void* pmm_bump(size_t total) {
    while (pmm_region_cur < pmm_region_count) {
        pmm_region_t* r = &pmm_regions[pmm_region_cur];
        uintptr_t p = (free_memory_start + 15u) & ~(uintptr_t)15u;
        if (p < r->base) p = r->base;
        if (p + total <= r->end && p + total >= p) {
            free_memory_start = p + total;
            return (void*)p;
        }
        pmm_region_cur++;
        if (pmm_region_cur < pmm_region_count) {
            free_memory_start = pmm_regions[pmm_region_cur].base;
        }
    }
    return NULL;
}

void* kmalloc(size_t size) {
    if (size == 0) return NULL;

    size_t need = (size + 15u) & ~(size_t)15u;
    if (need < PMM_MINPAY) need = PMM_MINPAY;

    pmm_block_t** pp = &pmm_free;
    while (*pp) {
        pmm_block_t* b = *pp;
        if (b->size >= need) {
            if (b->size >= need + PMM_HDR + PMM_MINPAY) {
                pmm_block_t* rest = (pmm_block_t*)((uintptr_t)b + PMM_HDR + need);
                rest->magic = PMM_MAGIC;
                rest->size = (uint32_t)(b->size - need - PMM_HDR);
                rest->next = b->next;
                *pp = rest;
                b->size = (uint32_t)need;
            } else {
                *pp = b->next;
            }
            b->magic = 0;
            return (void*)((uintptr_t)b + PMM_HDR);
        }
        pp = &b->next;
    }

    pmm_block_t* b = (pmm_block_t*)pmm_bump(need + PMM_HDR);
    if (!b) {
        terminal_writestring("\n[PMM] OUT OF MEMORY\n");
        return NULL;
    }
    b->size = (uint32_t)need;
    b->magic = 0;
    return (void*)((uintptr_t)b + PMM_HDR);
}

void kfree(void* ptr) {
    if (!ptr) return;
    uintptr_t addr = (uintptr_t)ptr - PMM_HDR;
    pmm_block_t* b = (pmm_block_t*)addr;

    bool in_heap = false;
    for (int i = 0; i < pmm_region_count; i++) {
        if (addr >= pmm_regions[i].base && addr + PMM_HDR <= pmm_regions[i].end) {
            in_heap = true;
            break;
        }
    }
    if (!in_heap) return;
    if (b->magic == PMM_MAGIC) return;
    if (b->size < PMM_MINPAY) return;

    pmm_block_t* prev = NULL;
    pmm_block_t* cur = pmm_free;
    while (cur && (uintptr_t)cur < addr) {
        prev = cur;
        cur = cur->next;
    }

    b->magic = PMM_MAGIC;
    b->next = cur;
    if (prev) prev->next = b; else pmm_free = b;

    if (cur && (uintptr_t)b + PMM_HDR + b->size == (uintptr_t)cur) {
        b->size += PMM_HDR + cur->size;
        b->next = cur->next;
        cur = b;
    }
    if (prev && (uintptr_t)prev + PMM_HDR + prev->size == (uintptr_t)b) {
        prev->size += PMM_HDR + b->size;
        prev->next = b->next;
    }
}

size_t ksize(void* ptr) {
    if (!ptr) return 0;
    uintptr_t addr = (uintptr_t)ptr - PMM_HDR;
    pmm_block_t* b = (pmm_block_t*)addr;
    for (int i = 0; i < pmm_region_count; i++) {
        if (addr >= pmm_regions[i].base && addr + PMM_HDR <= pmm_regions[i].end) {
            if (b->magic == PMM_MAGIC) return 0;
            return (size_t)b->size;
        }
    }
    return 0;
}

uintptr_t virt_to_phys(void* addr) {
    return (uintptr_t)addr;
}
