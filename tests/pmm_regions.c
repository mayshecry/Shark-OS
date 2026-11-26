#include "kernel.h"
#include <assert.h>
char _kernel_end[1];
uintptr_t free_memory_start;
void terminal_writestring(const char *s) { (void)s; }

int main(void) {
    uintptr_t base = ((uintptr_t)_kernel_end + 0x4000u) & ~(uintptr_t)15;
    pmm_memory_region_t regions[] = {{base, base + 64}, {base + 1024, base + 1280}};
    pmm_init_regions(regions, 2);
    assert(pmm_free_memory_bytes() == 320);
    assert(!kmalloc(321) && pmm_free_memory_bytes() == 320);
    assert((uintptr_t)kmalloc(200) == base + 1024);
    assert((uintptr_t)kmalloc(64) == base);
    assert((uintptr_t)kmalloc(48) == base + 1232);
    assert(!kmalloc(1) && !pmm_free_memory_bytes());
    /* Use real storage so the lease zeroing and relocation are exercised. */
    extern void *malloc(size_t);unsigned char *arena=malloc(65536+16);assert(arena);uintptr_t a=((uintptr_t)arena+15)&~(uintptr_t)15;assert(a>(uintptr_t)_kernel_end+8192);
    pmm_memory_region_t r={a,a+65536};pmm_init_regions(&r,1);
    uint64_t initial=pmm_free_memory_bytes();void *l=pmm_lease(8192);assert(l);
    unsigned char *shadow=pmm_shadow(256);assert(shadow);shadow[3]=42;
    assert(kmalloc(512));unsigned char *moved=pmm_shadow(512);assert(moved && moved!=shadow && moved[3]==42);
    assert(pmm_free_memory_bytes()==initial-8192-512-512);
    pmm_release_leases();assert(pmm_free_memory_bytes()==initial-1024);
    assert(pmm_shadow(1024)==moved && moved[3]==42);
    assert(pmm_free_memory_bytes()==initial-1536);
    uint64_t before=pmm_free_memory_bytes();
    for(int pass=0;pass<400;pass++){
        unsigned char *x=pmm_cache_alloc(256), *y=pmm_cache_alloc(512);
        assert(x && y && x!=y);memset(y,42,512);
        void *leased=pmm_lease(2048);assert(leased);
        pmm_cache_free(x); unsigned char *z=pmm_cache_alloc(128);assert(z);
        pmm_cache_free(y);pmm_release_leases();pmm_cache_free(z);
        assert(pmm_free_memory_bytes()==before);
    }
    return 0;
}
