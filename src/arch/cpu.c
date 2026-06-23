#include "kernel.h"

void get_cpu_model(char* buffer) {
    uint32_t eax, ebx, ecx, edx;
    uint32_t* ptr = (uint32_t*)buffer;

    /* Check if CPUID is supported by trying to flip the ID bit */
    asm volatile("pushfl; popl %0; movl %0, %1; xorl $0x200000, %1; pushl %1; popfl; pushfl; popl %1"
        : "=r"(eax), "=r"(ebx) : : "memory");
    if ((eax ^ ebx) & 0x200000) {
        /* CPUID supported */
        for (uint32_t i = 0; i < 3; i++) {
            asm volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(0x80000002 + i));
            ptr[i * 4 + 0] = eax;
            ptr[i * 4 + 1] = ebx;
            ptr[i * 4 + 2] = ecx;
            ptr[i * 4 + 3] = edx;
        }
        buffer[48] = '\0';
    } else {
        strcpy(buffer, "i386 (no CPUID)");
    }
}

void shutdown() {
    outw(0xB004, 0x2000);  
    outw(0x604, 0x2000);   
    outw(0x4004, 0x3400);  
    outb(0x64, 0xFE);     
    asm volatile("cli; hlt");
}