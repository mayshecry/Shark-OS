#include "kernel.h"

void execute_elf(uint8_t* data, const char* arg) {
    Elf32_Ehdr* header = (Elf32_Ehdr*)data;
    if (header->e_ident[0] != 0x7F || header->e_ident[1] != 'E' ||
        header->e_ident[2] != 'L' || header->e_ident[3] != 'F') {
        terminal_writestring("Not a valid ELF binary.\n");
        return;
    }

    if (header->e_type == 2 || header->e_type == 3) {
        Elf32_Phdr* phdr = (Elf32_Phdr*)((uint32_t)data + header->e_phoff);
        for (int i = 0; i < header->e_phnum; i++) {
            if (phdr[i].p_type == 1) {
                void* dest = (header->e_type == 3) ? (void*)((uint32_t)data + phdr[i].p_vaddr) : (void*)phdr[i].p_vaddr;
                memcpy(dest, (void*)((uint32_t)data + phdr[i].p_offset), phdr[i].p_filesz);
                if (phdr[i].p_memsz > phdr[i].p_filesz) {
                    memset((void*)((uintptr_t)dest + phdr[i].p_filesz), 0, phdr[i].p_memsz - phdr[i].p_filesz);
                }
            }
        }
    }

    uint32_t entry_addr = (header->e_type == 3) ? ((uint32_t)data + header->e_entry) : header->e_entry;
    void (*entry)(const char*) = (void (*)(const char*))entry_addr;

    terminal_writestring("Executing ELF binary... \n");
    asm volatile(
        "mov %0, %%esp\n"
        "push %1\n"
        "call *%2\n"
        : : "r"((uint32_t)&stack_top), "r"(arg), "r"(entry) : "memory"
    );
}