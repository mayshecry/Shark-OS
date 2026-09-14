

#include "kernel.h"

#define MTRR_TYPE_UC 0x0
#define MTRR_TYPE_WC 0x1
#define MTRR_TYPE_WT 0x4
#define MTRR_TYPE_WP 0x5
#define MTRR_TYPE_WB 0x6
#define MTRR_TYPE_UCMINUS 0x7

#define IA32_MTRRCAP          0x00FE
#define IA32_MTRR_DEF_TYPE    0x02FF
#define IA32_MTRR_PHYSBASE(n) (0x0200 + 2 * (n))
#define IA32_MTRR_PHYSMASK(n) (0x0201 + 2 * (n))

#define MTRR_MASK_VALID (1ULL << 11)
#define MTRR_DEF_VAR_ENABLE (1ULL << 11)

/* Diagnostics for Task Manager (`mtrr_fb_cached`, `mtrr_status_str()`)
   and the `mtrr` shell command (`mtrr_dump()`). */
int mtrr_fb_cached = 0;
char mtrr_status_text[96] = "MTRR: not probed yet";

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

/* MAXPHYADDR (physical address width). Sandy Bridge = 36, Coffee Lake = 39.
   The old code assumed 36 bits everywhere, which broke on any machine with
   RAM/MTRRs above 4 GB (see below). */
static uint32_t cpuid_maxphyaddr(void) {
    uint32_t max_ext, a, b, c, d;
    asm volatile("cpuid" : "=a"(max_ext), "=b"(b), "=c"(c), "=d"(d)
                 : "a"(0x80000000));
    if (max_ext < 0x80000008) return 36;
    asm volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d)
                 : "a"(0x80000008));
    uint32_t w = a & 0xFF;
    if (w < 32) w = 32;
    if (w > 52) w = 52;
    return w;
}

static uint32_t round_up_pow2(uint32_t size) {
    uint32_t v = 4096;
    while (v < size && v < 0x80000000u) v <<= 1;
    return v;
}

static const char* mtrr_type_name(uint8_t t) {
    switch (t) {
        case MTRR_TYPE_UC: return "UC";
        case MTRR_TYPE_WC: return "WC";
        case MTRR_TYPE_WT: return "WT";
        case MTRR_TYPE_WP: return "WP";
        case MTRR_TYPE_WB: return "WB";
        case MTRR_TYPE_UCMINUS: return "UC-";
        default: return "??";
    }
}

static void status_set(const char* s) {
    int i = 0;
    while (s[i] && i < 95) { mtrr_status_text[i] = s[i]; i++; }
    mtrr_status_text[i] = '\0';
}

static void status_add(const char* s) {
    int i = 0;
    while (mtrr_status_text[i]) i++;
    int j = 0;
    while (s[j] && i < 95) { mtrr_status_text[i++] = s[j++]; }
    mtrr_status_text[i] = '\0';
}

static void status_add_u32(uint32_t v) {
    char t[12];
    int_to_string(v, t);
    status_add(t);
}

const char* mtrr_status_str(void) {
    return mtrr_status_text;
}

void framebuffer_enable_write_combining(uintptr_t fb, uint32_t size) {
    mtrr_fb_cached = 0;
    status_set("MTRR: no framebuffer");
    if (!fb || size == 0) return;
    if (!cpu_has_mtrr()) { status_set("MTRR: CPU lacks MTRR"); return; }

    uint64_t cap = rdmsr(IA32_MTRRCAP);
    int vcnt = (int)(cap & 0xFF);
    if (vcnt <= 0) { status_set("MTRR: 0 variable ranges"); return; }
    if (vcnt > 32) vcnt = 32; /* conflicts tracked in a u32 bitmask */

    uint32_t maxphy = cpuid_maxphyaddr();
    uint64_t phys_all = (maxphy >= 64) ? ~0ULL : ((1ULL << maxphy) - 1);
    uint64_t phys_page = phys_all & ~0xFFFULL; /* valid addr bits [maxphy-1:12] */

    uint64_t deft = rdmsr(IA32_MTRR_DEF_TYPE);
    uint8_t def_type = (uint8_t)(deft & 0xFF);
    int var_on = (deft & MTRR_DEF_VAR_ENABLE) != 0;

    uint64_t region = round_up_pow2(size);
    uint64_t base = (uint64_t)fb & ~(region - 1);
    uint64_t end = base + region;

    /* Pass 1: classify every variable MTRR using the FULL physical width.
       BUG (old code): base/mask were truncated to 36 bits
       (0x0000000FFFFFF000). Any firmware MTRR above 4 GB (normal on a
       Coffee Lake laptop with 8-16 GB RAM, e.g. WB for [4G,8G)) was
       truncated to base 0 with a gigantic size, so the code wrongly
       concluded the framebuffer was "already WB" - and programmed
       nothing. The framebuffer was really UC, so every full-screen
       flush crawled. Older 4 GB machines have no MTRRs above 4 GB, so
       the truncation was harmless there and WC worked - exactly the
       "fast on i5, laggy on i7" symptom. */
    int free_slot = -1;
    uint32_t conflict = 0;
    int n_conflict = 0;
    int first_conflict = -1;
    uint8_t first_conflict_type = 0;
    int all_uc = 1;
    int pre_wc = -1, pre_wb = -1;

    for (int i = 0; i < vcnt; i++) {
        uint64_t m = rdmsr(IA32_MTRR_PHYSMASK(i));
        if (!(m & MTRR_MASK_VALID)) {
            if (free_slot < 0) free_slot = i;
            continue;
        }
        uint64_t b = rdmsr(IA32_MTRR_PHYSBASE(i));
        uint64_t rbase = b & phys_page;
        uint64_t rmask = m & phys_page;
        uint64_t rsize = (~rmask & phys_all) + 1;
        uint64_t rend = rbase + rsize;
        uint8_t type = (uint8_t)(b & 0xFF);

        if (rbase <= base && end <= rend) {
            if (type == MTRR_TYPE_WC) pre_wc = i;
            else if (type == MTRR_TYPE_WB) pre_wb = i;
        }
        if (base < rend && rbase < end) {
            conflict |= (1u << i);
            if (first_conflict < 0) {
                first_conflict = i;
                first_conflict_type = type;
            }
            n_conflict++;
            if (type != MTRR_TYPE_UC && type != MTRR_TYPE_UCMINUS) all_uc = 0;
        }
    }

    if (pre_wc >= 0) {
        mtrr_fb_cached = 1;
        status_set("FB already WC (MTRR ");
        status_add_u32((uint32_t)pre_wc);
        status_add(")");
        return;
    }
    if (pre_wb >= 0) {
        /* Covered by WB: overlapping WC would be undefined per the SDM,
           so leave it alone - but say so loudly, because a WB
           framebuffer needs cache flushes or the screen shows stale
           DRAM contents. */
        status_set("WARN: FB inside WB MTRR ");
        status_add_u32((uint32_t)pre_wb);
        return;
    }

    /* Overlapping WC on top of WB/WT/WP is undefined (SDM) and UC always
       wins over WC, so a conflicting non-UC MTRR blocks us. But a
       conflicting *UC* MTRR is redundant when the MTRR default type is
       already UC/UC- (the normal firmware setup): dropping it changes
       nothing for the rest of its range (still UC via the default) and
       frees the way for our WC range. Modern UEFI firmware loves to
       explicitly map MMIO holes as UC and to use every MTRR slot, which
       is the second reason WC setup used to give up on new machines. */
    int reclaim = 0;
    if (n_conflict > 0) {
        if (all_uc && (def_type == MTRR_TYPE_UC ||
                       def_type == MTRR_TYPE_UCMINUS)) {
            reclaim = n_conflict;
        } else {
            status_set("MTRR: blocked by ");
            status_add(mtrr_type_name(first_conflict_type));
            status_add(" MTRR ");
            status_add_u32((uint32_t)first_conflict);
            status_add(" (FB stays UC)");
            return;
        }
    }

    int slot = free_slot;
    if (reclaim) {
        /* Reuse the first reclaimed UC slot for our WC range. */
        for (int i = 0; i < vcnt; i++) {
            if (conflict & (1u << i)) { slot = i; break; }
        }
    }
    int evicted = 0;
    if (slot < 0 && (def_type == MTRR_TYPE_UC ||
                     def_type == MTRR_TYPE_UCMINUS)) {
        /* All slots used (typical UEFI): evict one redundant UC range -
           default-UC still covers everything outside our new WC range. */
        for (int i = 0; i < vcnt; i++) {
            uint64_t m = rdmsr(IA32_MTRR_PHYSMASK(i));
            if (!(m & MTRR_MASK_VALID)) continue;
            uint8_t t = (uint8_t)(rdmsr(IA32_MTRR_PHYSBASE(i)) & 0xFF);
            if (t == MTRR_TYPE_UC || t == MTRR_TYPE_UCMINUS) {
                slot = i;
                evicted = 1;
                break;
            }
        }
    }
    if (slot < 0) {
        status_set("MTRR: no free slot (all ");
        status_add_u32((uint32_t)vcnt);
        status_add(" used, FB stays UC)");
        return;
    }

    /* Program the WC range. Mask bits [maxphy-1:32] must be 1 so the
       range does NOT alias to every 4 GB slice (the old 32-bit mask put
       0 there, covering high-RAM aliases and creating an undefined
       WB/WC overlap on machines with >4 GB RAM). */
    uint64_t base_reg = (base & phys_page) | MTRR_TYPE_WC;
    uint64_t mask_reg = ((~(region - 1)) & phys_page) | MTRR_MASK_VALID;

    uint32_t eflags;
    asm volatile("pushf; pop %0; cli" : "=r"(eflags) :: "memory");
    uint32_t cr0;
    asm volatile("mov %%cr0, %0" : "=r"(cr0) :: "memory");
    asm volatile("mov %0, %%cr0" :: "r"(cr0 | (1u << 30) | (1u << 29))
                 : "memory");
    asm volatile("wbinvd" ::: "memory");

    if (reclaim) {
        for (int i = 0; i < vcnt; i++) {
            if ((conflict & (1u << i)) && i != slot) {
                wrmsr(IA32_MTRR_PHYSMASK(i), 0); /* invalidate redundant UC */
            }
        }
    }
    wrmsr(IA32_MTRR_PHYSBASE(slot), base_reg);
    wrmsr(IA32_MTRR_PHYSMASK(slot), mask_reg);

    if (!var_on) {
        /* Variable MTRRs disabled: our WC range (and the firmware's WB
           RAM ranges!) would be ignored. Enable them. */
        wrmsr(IA32_MTRR_DEF_TYPE, deft | MTRR_DEF_VAR_ENABLE);
    }

    asm volatile("wbinvd" ::: "memory");
    asm volatile("mov %0, %%cr0" :: "r"(cr0) : "memory");
    if (eflags & 0x200) asm volatile("sti");

    /* Verify the range really took effect. */
    uint64_t vb = rdmsr(IA32_MTRR_PHYSBASE(slot));
    uint64_t vm = rdmsr(IA32_MTRR_PHYSMASK(slot));
    if ((vm & MTRR_MASK_VALID) && (vb & 0xFF) == MTRR_TYPE_WC &&
        (vb & phys_page) == (base & phys_page)) {
        mtrr_fb_cached = 1;
        status_set("FB write-combining on (MTRR ");
        status_add_u32((uint32_t)slot);
        status_add(")");
        if (reclaim) {
            status_add(", reclaimed ");
            status_add_u32((uint32_t)reclaim);
            status_add(" UC");
        }
        if (evicted) status_add(", evicted 1 UC");
        if (!var_on) status_add(", MTRR enabled");
    } else {
        status_set("MTRR: WC verify FAILED");
    }
}

static void hex64(uint64_t v, char* out) {
    static const char* h = "0123456789ABCDEF";
    for (int i = 0; i < 16; i++) out[i] = h[(v >> (60 - i * 4)) & 0xF];
    out[16] = '\0';
}

void mtrr_dump(void) {
    char buf[24];
    if (!cpu_has_mtrr()) {
        terminal_writestring("MTRR: not supported by CPU\n");
        return;
    }
    uint64_t cap = rdmsr(IA32_MTRRCAP);
    uint32_t vcnt = (uint32_t)(cap & 0xFF);
    uint64_t deft = rdmsr(IA32_MTRR_DEF_TYPE);
    terminal_writestring("MTRR: ");
    int_to_string(vcnt, buf); terminal_writestring(buf);
    terminal_writestring(" variable, default ");
    terminal_writestring(mtrr_type_name((uint8_t)(deft & 0xFF)));
    terminal_writestring((deft & MTRR_DEF_VAR_ENABLE) ? " E=1\n" : " E=0\n");
    for (uint32_t i = 0; i < vcnt && i < 32; i++) {
        uint64_t b = rdmsr(IA32_MTRR_PHYSBASE(i));
        uint64_t m = rdmsr(IA32_MTRR_PHYSMASK(i));
        terminal_writestring(" MTRR");
        int_to_string(i, buf); terminal_writestring(buf);
        if (!(m & MTRR_MASK_VALID)) {
            terminal_writestring(": (free)\n");
            continue;
        }
        terminal_writestring(": base 0x");
        hex64(b & ~0xFFFULL, buf); terminal_writestring(buf);
        terminal_writestring(" mask 0x");
        hex64(m & ~0xFFFULL, buf); terminal_writestring(buf);
        terminal_writestring(" ");
        terminal_writestring(mtrr_type_name((uint8_t)(b & 0xFF)));
        terminal_writestring("\n");
    }
    terminal_writestring(" FB: ");
    terminal_writestring(mtrr_status_str());
    terminal_writestring("\n");
}
