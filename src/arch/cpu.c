#include "kernel.h"

void get_cpu_model(char* buffer) {
    uint32_t eax, ebx, ecx, edx;
    uint32_t* ptr = (uint32_t*)buffer;
    uint64_t rflags_before, rflags_after;
    uint64_t tmp;
    asm volatile("pushfq; popq %0; movq %0, %1; xorq $0x200000, %1; pushq %1; popfq; pushfq; popq %1"
        : "=r"(rflags_before), "=r"(tmp) : : "memory");
    rflags_after = tmp;
    if ((rflags_before ^ rflags_after) & 0x200000) {
        for (uint32_t i = 0; i < 3; i++) {
            asm volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(0x80000002 + i));
            ptr[i * 4 + 0] = eax;
            ptr[i * 4 + 1] = ebx;
            ptr[i * 4 + 2] = ecx;
            ptr[i * 4 + 3] = edx;
        }
        buffer[48] = '\0';
    } else {
        strcpy(buffer, "x86_64 (no CPUID)");
    }
}

void shutdown() {
    outw(0xB004, 0x2000);
    outw(0x604, 0x2000);
    outw(0x4004, 0x3400);
    outb(0x64, 0xFE);
    asm volatile("cli; hlt");
}
