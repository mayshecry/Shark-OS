

#include "kernel.h"

#define MTRR_TYPE_UC 0x0
#define MTRR_TYPE_WC 0x1
#define MTRR_TYPE_WT 0x4
#define MTRR_TYPE_WP 0x5
#define MTRR_TYPE_WB 0x6

#define IA32_MTRRCAP          0x00FE
#define IA32_MTRR_PHYSBASE(n) (0x0200 + 2 * (n))
#define IA32_MTRR_PHYSMASK(n) (0x0201 + 2 * (n))

#define MTRR_MASK_VALID (1ULL << 11)

static inline uint64_t rdmsr(uint32_t msr) {
    uint32_t lo, hi;
    asm volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | (uint64_t)lo;
}

static inline void wrmsr(uint32_t msr, uint64_t val) {
    uint32_t lo = (uint32_t)val;
    uint32_t hi = (uint32_t)(val >> 32);
    asm volatile("wrmsr" : : "a"(lo), "d"(hi), "c"(msr));
}

static bool cpu_has_mtrr(void) {
    uint32_t eax, ebx, ecx, edx;
    uint32_t base_max, base_mid;


    uint32_t before, after;
    asm volatile("pushfl; popl %0; movl %0, %1; xorl $0x200000, %1;"
                 "pushl %1; popfl; pushfl; popl %1"
                 : "=r"(before), "=r"(after) : : "memory");
    if (before == after) return false;

    asm volatile("cpuid" : "=a"(base_max), "=b"(base_mid), "=c"(ecx), "=d"(edx)
                 : "a"(0));
    if (base_max < 1) return false;

    asm volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                 : "a"(1));
    return (edx & (1u << 12)) != 0;
}

static uint32_t round_up_pow2(uint32_t size) {
    uint32_t v = 4096;
    while (v < size && v < 0x80000000u) v <<= 1;
    return v;
}

void framebuffer_enable_write_combining(uintptr_t fb, uint32_t size) {
    if (!fb || size == 0) return;
    if (!cpu_has_mtrr()) return;

    uint64_t cap = rdmsr(IA32_MTRRCAP);
    int vcnt = (int)(cap & 0xFF);
    if (vcnt <= 0) return;


    uint32_t region = round_up_pow2(size);
    uintptr_t base = fb & ~(uintptr_t)(region - 1);
    uint64_t base_phys = ((uint64_t)base & 0xFFFFFFFFu) | MTRR_TYPE_WC;
    uint64_t mask_phys = ((uint64_t)(~(region - 1)) & 0xFFFFFFFFu)
                         | MTRR_MASK_VALID;

    int free_slot = -1;

    for (int i = 0; i < vcnt; i++) {
        uint64_t m = rdmsr(IA32_MTRR_PHYSMASK(i));
        if (m & MTRR_MASK_VALID) {
            uint64_t b = rdmsr(IA32_MTRR_PHYSBASE(i));
            uint64_t rbase = b & 0x0000000FFFFFF000ull;
            uint64_t rmask = m & 0x0000000FFFFFF000ull;
            uint64_t rsize = (~rmask & 0xFFFFFFFFFull) + 1;


            if (rbase <= (uint64_t)base &&
                (uint64_t)base + region <= rbase + rsize) {
                uint8_t type = (uint8_t)(b & 0xFF);
                if (type == MTRR_TYPE_WB || type == MTRR_TYPE_WC) {
                    return;
                }

            }
        } else if (free_slot < 0) {
            free_slot = i;
        }
    }

    if (free_slot < 0) return;


    for (int i = 0; i < vcnt; i++) {
        uint64_t m = rdmsr(IA32_MTRR_PHYSMASK(i));
        if (!(m & MTRR_MASK_VALID)) continue;
        uint64_t b = rdmsr(IA32_MTRR_PHYSBASE(i));
        uint64_t rbase = b & 0x0000000FFFFFF000ull;
        uint64_t rmask = m & 0x0000000FFFFFF000ull;
        uint64_t rsize = (~rmask & 0xFFFFFFFFFull) + 1;
        if ((uint64_t)base < rbase + rsize &&
            rbase < (uint64_t)base + region) {
            return;
        }
    }


    uint32_t cr0;
    asm volatile("mov %%cr0, %0" : "=r"(cr0) : : "memory");
    asm volatile("mov %0, %%cr0" : : "r"(cr0 | (1u << 30) | (1u << 29))
                 : "memory");
    asm volatile("wbinvd" ::: "memory");

    wrmsr(IA32_MTRR_PHYSBASE(free_slot), base_phys);
    wrmsr(IA32_MTRR_PHYSMASK(free_slot), mask_phys);

    asm volatile("wbinvd" ::: "memory");
    asm volatile("mov %0, %%cr0" : : "r"(cr0) : "memory");
}
