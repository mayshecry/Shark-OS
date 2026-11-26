#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static unsigned calls;
static int fail;
static size_t last;
static void *kmalloc(size_t bytes) {
    calls++;
    last = bytes;
    return fail ? NULL : malloc(bytes);
}
#include "../src/browser/js_pool.h"
JS_POOL(item, uint64_t, 513)
int main(void) {
    uint64_t *first = item_at(0);
    for (int i = 0; i < 256; i++) {
        assert(item_reserve(i));
        *item_at(i) = i + 99;
    }
    assert(!calls && item_at(0) == first);
    fail = 1;
    assert(!item_reserve(256) && *item_at(100) == 199);
    fail = 0;
    for (int i = 256; i < 513; i++) {
        assert(item_reserve(i) && *item_at(i) == 0);
        *item_at(i) = i + 99;
    }
    assert(last == sizeof(uint64_t) && !item_reserve(513));
    for (int i = 0; i < 513; i++)
        assert(item_reserve(i) && *item_at(i) == 99u + i);
    assert(calls == 3 && item_at(0) == first);
    puts("Pool growth, reuse, failed allocation and final partial block passed");
    return 0;
}
