#include "kernel.h"

static bool cpu_has_cpuid(void) {
    uint32_t before, after;
    asm volatile("pushfl; popl %0; movl %0, %1; xorl $0x200000, %1; pushl %1; popfl; pushfl; popl %1"
        : "=r"(before), "=r"(after) : : "memory");
    return ((before ^ after) & 0x200000) != 0;
}

static uint32_t cpuid_max_leaf(uint32_t leaf) {
    uint32_t eax, ebx, ecx, edx;
    asm volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(leaf));
    return eax;
}

void get_cpu_model(char* buffer) {
    uint32_t eax, ebx, ecx, edx;
    uint32_t* ptr = (uint32_t*)buffer;

    if (!cpu_has_cpuid()) {
        strcpy(buffer, "i386 (no CPUID)");
        return;
    }
    if (cpuid_max_leaf(0x80000000) < 0x80000004) {
        strcpy(buffer, "x86 (no brand string)");
        return;
    }
    for (uint32_t i = 0; i < 3; i++) {
        asm volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(0x80000002 + i));
        ptr[i * 4 + 0] = eax;
        ptr[i * 4 + 1] = ebx;
        ptr[i * 4 + 2] = ecx;
        ptr[i * 4 + 3] = edx;
    }
    buffer[48] = '\0';
    while (*buffer == ' ') buffer++;
    if (*buffer == '\0') strcpy(buffer, "x86 (empty brand string)");
}

void shutdown() {
    outw(0xB004, 0x2000);
    outw(0x604, 0x2000);
    outw(0x4004, 0x3400);
    outb(0x64, 0xFE);
    asm volatile("cli; hlt");
}
