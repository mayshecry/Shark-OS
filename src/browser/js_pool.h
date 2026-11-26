#ifndef SHARK_JS_POOL_H
#define SHARK_JS_POOL_H

#define JS_POOL_WIDTH 256u
#define JS_POOL(name, type, limit)                                                                       \
    static type name##_first[JS_POOL_WIDTH];                                                             \
    static type *name##_blocks[((limit) + JS_POOL_WIDTH - 1) / JS_POOL_WIDTH] = {name##_first};          \
    static inline type *name##_at(unsigned i) {                                                          \
        return &name##_blocks[i / JS_POOL_WIDTH][i % JS_POOL_WIDTH];                                     \
    }                                                                                                    \
    static int name##_reserve(unsigned i) {                                                              \
        if (i >= (limit))                                                                                \
            return 0;                                                                                    \
        type **slot = &name##_blocks[i / JS_POOL_WIDTH];                                                 \
        if (!*slot) {                                                                                    \
            unsigned remaining = (limit) - (i / JS_POOL_WIDTH) * JS_POOL_WIDTH;                          \
            size_t bytes = (remaining < JS_POOL_WIDTH ? remaining : JS_POOL_WIDTH) * sizeof(type);       \
            *slot = kmalloc(bytes);                                                                      \
            if (!*slot)                                                                                  \
                return 0;                                                                                \
            memset(*slot, 0, bytes);                                                                     \
        }                                                                                                \
        return 1;                                                                                        \
    }

#endif
