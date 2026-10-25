#include "kernel.h"

void execute_elf(uint8_t* data, const char* arg) {
    uint8_t ei_class = data[4];
    uint8_t ei_data = data[5];
    (void)ei_data;
    // Check ELF magic
    if (data[0] != 0x7F || data[1] != 'E' || data[2] != 'L' || data[3] != 'F') {
        terminal_writestring("Not a valid ELF binary.\n");
        return;
    }
    if (ei_class == 2) { // ELF64
        Elf64_Ehdr* header = (Elf64_Ehdr*)data;
        if (header->e_type == 2 || header->e_type == 3) {
            Elf64_Phdr* phdr = (Elf64_Phdr*)(data + header->e_phoff);
            for (int i = 0; i < header->e_phnum; i++) {
                if (phdr[i].p_type == 1) {
                    void* dest = (header->e_type == 3) ? (void*)(data + phdr[i].p_vaddr) : (void*)(uintptr_t)phdr[i].p_vaddr;
                    memcpy(dest, (void*)(data + phdr[i].p_offset), phdr[i].p_filesz);
                    if (phdr[i].p_memsz > phdr[i].p_filesz) {
                        memset((void*)((uintptr_t)dest + phdr[i].p_filesz), 0, phdr[i].p_memsz - phdr[i].p_filesz);
                    }
                }
            }
        }
        uintptr_t entry_addr = (header->e_type == 3) ? ((uintptr_t)data + header->e_entry) : (uintptr_t)header->e_entry;
        void (*entry)(const char*) = (void (*)(const char*))entry_addr;
        terminal_writestring("Executing ELF binary... \n");
        asm volatile(
            "mov %0, %%rsp\n"
            "push %1\n"
            "call *%2\n"
            : : "r"((uintptr_t)&stack_top), "r"(arg), "r"(entry) : "memory"
        );
        return;
    }
    // Fallback ELF32
    Elf32_Ehdr* header = (Elf32_Ehdr*)data;
    if (header->e_type == 2 || header->e_type == 3) {
        Elf32_Phdr* phdr = (Elf32_Phdr*)(data + header->e_phoff);
        for (int i = 0; i < header->e_phnum; i++) {
            if (phdr[i].p_type == 1) {
                void* dest = (header->e_type == 3) ? (void*)(data + phdr[i].p_vaddr) : (void*)(uintptr_t)phdr[i].p_vaddr;
                memcpy(dest, (void*)(data + phdr[i].p_offset), phdr[i].p_filesz);
                if (phdr[i].p_memsz > phdr[i].p_filesz) {
                    memset((void*)((uintptr_t)dest + phdr[i].p_filesz), 0, phdr[i].p_memsz - phdr[i].p_filesz);
                }
            }
        }
    }
    uintptr_t entry_addr = (header->e_type == 3) ? ((uintptr_t)data + header->e_entry) : (uintptr_t)header->e_entry;
    void (*entry)(const char*) = (void (*)(const char*))entry_addr;
    terminal_writestring("Executing ELF binary... \n");
    asm volatile(
        "mov %0, %%rsp\n"
        "push %1\n"
        "call *%2\n"
        : : "r"((uintptr_t)&stack_top), "r"(arg), "r"(entry) : "memory"
    );
}
