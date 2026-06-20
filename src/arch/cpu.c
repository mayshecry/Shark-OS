#include "kernel.h"

void get_cpu_model(char* buffer) {
    uint32_t eax, ebx, ecx, edx;
    uint32_t* ptr = (uint32_t*)buffer;

    for (uint32_t i = 0; i < 3; i++) {
        asm volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(0x80000002 + i));
        ptr[i * 4 + 0] = eax;
        ptr[i * 4 + 1] = ebx;
        ptr[i * 4 + 2] = ecx;
        ptr[i * 4 + 3] = edx;
    }
    buffer[48] = '\0';
}

void shutdown() {
    outw(0xB004, 0x2000);
    outw(0x604, 0x2000);
    outw(0x4004, 0x3400);
    asm volatile("cli; hlt");
}