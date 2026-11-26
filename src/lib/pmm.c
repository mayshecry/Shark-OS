#include "kernel.h"

uint64_t* pmm_bitmap;
uintptr_t free_memory_end;

#define PMM_REGION_CAPACITY 128

static pmm_memory_region_t pmm_regions[PMM_REGION_CAPACITY];
static uint32_t pmm_region_count;
static uint32_t pmm_region_index;
static uint64_t pmm_free_bytes;

static uint64_t trusted_low_end;
void pmm_allow_low_desktop(uintptr_t end){trusted_low_end=end;}

static void pmm_add_region(uint64_t start, uint64_t end) {
    if (end <= start || pmm_region_count >= PMM_REGION_CAPACITY) return;
    uint64_t kernel_floor = (uint64_t)(uintptr_t)free_memory_start;
    if(trusted_low_end && end<=trusted_low_end && start>=0x200000) { /* validated Multiboot RAM below ELF */ }
    else if (start < kernel_floor) start = kernel_floor;
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
        pmm_regions[0].start = free_memory_start;
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

    for (uint32_t step = 0; step < pmm_region_count; step++) {
        uint32_t index = (pmm_region_index + step) % pmm_region_count;
        uint64_t current = pmm_regions[index].start;
        uint64_t end = pmm_regions[index].end;
        uint64_t aligned = (current + 15) & ~15ULL;
        if (aligned < current || aligned > end || requested > end - aligned) continue;
        pmm_free_bytes -= aligned - current + requested;
        pmm_regions[index].start = aligned + requested;
        pmm_activate_region(index);
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

/* Navigator leases from the high end; kmalloc continues at the low end.
 * Only these leases move region.end, and they are unwound in reverse order. */
static struct {unsigned region;uint64_t old_end;} lease[4];
static unsigned nlease;
void *pmm_lease(size_t bytes){
    if(!bytes || nlease==4)return NULL;
    bytes=(bytes+15)&~(size_t)15;
    int chosen=-1;uint64_t best=~(uint64_t)0;
    for(unsigned i=0;i<pmm_region_count;i++){
        uint64_t room=pmm_regions[i].end-pmm_regions[i].start;
        if(room>=bytes && room<best){chosen=i;best=room;}
    }
    if(chosen<0)return NULL;
    pmm_memory_region_t *r=&pmm_regions[chosen];lease[nlease].region=chosen;lease[nlease++].old_end=r->end;
    r->end-=bytes;pmm_free_bytes-=bytes;pmm_activate_region(pmm_region_index);memset((void*)(uintptr_t)r->end,0,bytes);return (void*)(uintptr_t)r->end;
}
void pmm_release_leases(void){while(nlease){--nlease;pmm_memory_region_t *r=pmm_regions+lease[nlease].region;
    pmm_free_bytes+=lease[nlease].old_end-r->end;r->end=lease[nlease].old_end;}pmm_activate_region(pmm_region_index);}

/* Only the compositor owns this buffer. Grow it in place if possible; if a
 * later allocation intervened, return its old span as a separate free region. */
void *pmm_shadow(size_t need){
    static uint8_t *base;static size_t cap;
    if(!need || need>(size_t)-16)return NULL;
    need=(need+15)&~(size_t)15;
    if(need<=cap)return base;
    for(unsigned i=0;base && i<pmm_region_count;i++){
        pmm_memory_region_t *r=&pmm_regions[i];size_t extra=need-cap;
        if(r->start==(uintptr_t)base+cap && r->end-r->start>=extra){
            r->start+=extra;pmm_free_bytes-=extra;cap=need;pmm_activate_region(i);return base;
        }
    }
    if(base && pmm_region_count==PMM_REGION_CAPACITY)return NULL;
    uint8_t *next=kmalloc(need);if(!next)return NULL;
    if(base){memcpy(next,base,cap);pmm_regions[pmm_region_count++]=(pmm_memory_region_t){(uintptr_t)base,(uintptr_t)base+cap};pmm_free_bytes+=cap;}
    base=next;cap=need;return next;
}

/* Individually releasable UI caches. Reserve a return descriptor up front:
 * freeing a cache must work even if unrelated kmalloc/leases intervene. */
#define CACHE_OWNERS 24
static struct { uintptr_t addr; size_t size; unsigned slot; } owners[CACHE_OWNERS];
static int has_lease(unsigned i) {
    for (unsigned j=0; j<nlease; j++) if (lease[j].region==i) return 1;
    return 0;
}
static int held_slot(unsigned i) {
    for (unsigned j=0; j<CACHE_OWNERS; j++) if (owners[j].addr && owners[j].slot==i) return 1;
    return 0;
}
void *pmm_cache_alloc(size_t n) {
    if (!n || n>(size_t)-16) return NULL;
    n=(n+15)&~(size_t)15;
    unsigned owner=0; while(owner<CACHE_OWNERS && owners[owner].addr)owner++;
    if(owner==CACHE_OWNERS)return NULL;
    unsigned slot=0;
    while(slot<pmm_region_count && (pmm_regions[slot].start!=pmm_regions[slot].end || has_lease(slot) || held_slot(slot)))slot++;
    if(slot==PMM_REGION_CAPACITY)return NULL;
    void *p=kmalloc(n);if(!p)return NULL;
    if(slot==pmm_region_count)pmm_region_count++;
    pmm_regions[slot]=(pmm_memory_region_t){0,0};
    owners[owner].addr=(uintptr_t)p;owners[owner].size=n;owners[owner].slot=slot;return p;
}
void pmm_cache_free(void *p) {
    if(!p)return;
    unsigned k=0;while(k<CACHE_OWNERS && owners[k].addr!=(uintptr_t)p)k++;
    if(k==CACHE_OWNERS)return;
    uint64_t a=owners[k].addr,b=a+owners[k].size;
    unsigned slot=owners[k].slot;pmm_free_bytes+=owners[k].size;owners[k].addr=0;
    /* Never move a region's high end while a Navigator lease refers to it. */
    for(unsigned i=0;i<pmm_region_count;i++)if(i!=slot && !held_slot(i) && !has_lease(i) && pmm_regions[i].end==a){a=pmm_regions[i].start;pmm_regions[i]=(pmm_memory_region_t){0,0};break;}
    for(unsigned i=0;i<pmm_region_count;i++)if(i!=slot && pmm_regions[i].start==b){pmm_regions[i].start=a;pmm_activate_region(i);return;}
    pmm_regions[slot]=(pmm_memory_region_t){a,b};pmm_activate_region(slot);
}
