#include "app.h"
#include <elf.h>
#include <string.h>

int app_tag(const unsigned char *p, size_t n) {
    return n >= 4 && p[0] == 'S' && p[1] == 'A' && p[2] == 'P' && p[3] == '1';
}
static int fits(uint64_t off, uint64_t bytes, uint64_t limit) { return off <= limit && bytes <= limit - off; }
static int inside(const Elf64_Phdr *p, uint64_t addr, uint64_t bytes) {
    return addr >= p->p_vaddr && fits(addr - p->p_vaddr, bytes, p->p_memsz);
}
const char *app_load(const unsigned char *file, size_t size, uintptr_t (*resolve)(const char *),
                     unsigned char *image, size_t room, uintptr_t *entry) {
    const char *invalid = "invalid SharkOS application";
    if (size < sizeof(Elf64_Ehdr) || !app_tag(file, size))
        return invalid;
    Elf64_Ehdr h;
    memcpy(&h, file, sizeof h);
    if (h.e_ident[4] != ELFCLASS64 || h.e_ident[5] != ELFDATA2LSB || h.e_machine != EM_X86_64 ||
        h.e_type != ET_DYN || h.e_phentsize != sizeof(Elf64_Phdr) || h.e_shentsize != sizeof(Elf64_Shdr) ||
        h.e_phnum > 16 || h.e_shnum > 256 || !fits(h.e_phoff, h.e_phnum * sizeof(Elf64_Phdr), size) ||
        !fits(h.e_shoff, h.e_shnum * sizeof(Elf64_Shdr), size))
        return invalid;
    Elf64_Phdr rx = {0}, rw = {0};
    for (unsigned i = 0; i < h.e_phnum; i++) {
        Elf64_Phdr p;
        memcpy(&p, file + h.e_phoff + i * sizeof p, sizeof p);
        if (p.p_type != PT_LOAD)
            continue;
        if (!fits(p.p_offset, p.p_filesz, size) || p.p_filesz > p.p_memsz || !fits(p.p_vaddr, p.p_memsz, room))
            return "invalid or oversized load segment";
        Elf64_Phdr *target = p.p_flags == (PF_R | PF_X) ? &rx : p.p_flags == (PF_R | PF_W) ? &rw : NULL;
        if (!target || target->p_type)
            return invalid;
        *target = p;
    }
    if (!rx.p_type || !rw.p_type || rx.p_vaddr || rx.p_memsz > rw.p_vaddr)
        return invalid;
    Elf64_Shdr syms = {0}, strings;
    unsigned symtab = 0;
    for (unsigned i = 0; i < h.e_shnum; i++) {
        Elf64_Shdr s;
        memcpy(&s, file + h.e_shoff + i * sizeof s, sizeof s);
        if ((s.sh_type == SHT_INIT_ARRAY || s.sh_type == SHT_FINI_ARRAY || (s.sh_flags & SHF_TLS)) && s.sh_size)
            return "constructors, destructors and TLS are not supported";
        if (s.sh_type != SHT_DYNSYM)
            continue;
        if (symtab || !i || s.sh_link >= h.e_shnum || !fits(s.sh_offset, s.sh_size, size) ||
            s.sh_entsize != sizeof(Elf64_Sym) || s.sh_size % sizeof(Elf64_Sym))
            return invalid;
        syms = s;
        symtab = i;
    }
    if (!symtab)
        return invalid;
    memcpy(&strings, file + h.e_shoff + syms.sh_link * sizeof strings, sizeof strings);
    if (strings.sh_type != SHT_STRTAB || !strings.sh_size || !fits(strings.sh_offset, strings.sh_size, size) ||
        file[strings.sh_offset + strings.sh_size - 1])
        return invalid;
    size_t count = syms.sh_size / sizeof(Elf64_Sym);
    const char *names = (const char *)file + strings.sh_offset;
    uint64_t main_off = 0;
    for (unsigned i = 1; i < count; i++) {
        Elf64_Sym s;
        memcpy(&s, file + syms.sh_offset + i * sizeof s, sizeof s);
        if (s.st_name >= strings.sh_size)
            return invalid;
        const char *name = names + s.st_name;
        if (s.st_shndx == SHN_UNDEF && !resolve(name))
            return *name ? name : "unnamed unresolved symbol";
        if (!strcmp(name, "main") && s.st_shndx != SHN_UNDEF && ELF64_ST_TYPE(s.st_info) == STT_FUNC &&
            inside(&rx, s.st_value, 1) && s.st_value < rx.p_filesz)
            main_off = s.st_value;
    }
    if (!main_off)
        return "missing function: main";
    if (image) {
        memset(image, 0, rw.p_vaddr + rw.p_memsz);
        memcpy(image, file + rx.p_offset, rx.p_filesz);
        memcpy(image + rw.p_vaddr, file + rw.p_offset, rw.p_filesz);
    }
    for (unsigned i = 0; i < h.e_shnum; i++) {
        Elf64_Shdr r;
        memcpy(&r, file + h.e_shoff + i * sizeof r, sizeof r);
        if (r.sh_type != SHT_RELA || !r.sh_size)
            continue;
        if (r.sh_link != symtab || r.sh_entsize != sizeof(Elf64_Rela) || r.sh_size % sizeof(Elf64_Rela) ||
            !fits(r.sh_offset, r.sh_size, size))
            return invalid;
        for (size_t j = 0; j < r.sh_size; j += sizeof(Elf64_Rela)) {
            Elf64_Rela rel;
            memcpy(&rel, file + r.sh_offset + j, sizeof rel);
            uint32_t type = ELF64_R_TYPE(rel.r_info), idx = ELF64_R_SYM(rel.r_info);
            if (!type)
                continue;
            if (!inside(&rw, rel.r_offset, 8) || idx >= count)
                return invalid;
            uintptr_t value;
            if (type == R_X86_64_RELATIVE) {
                if (idx || !(inside(&rw, rel.r_addend, 0) || inside(&rx, rel.r_addend, 0)))
                    return invalid;
                value = (uintptr_t)image + rel.r_addend;
            } else if (type == R_X86_64_64 || type == R_X86_64_GLOB_DAT || type == R_X86_64_JUMP_SLOT) {
                Elf64_Sym sym;
                memcpy(&sym, file + syms.sh_offset + idx * sizeof sym, sizeof sym);
                if (!idx || sym.st_name >= strings.sh_size)
                    return invalid;
                if (sym.st_shndx == SHN_UNDEF)
                    value = resolve(names + sym.st_name);
                else if (inside(&rw, sym.st_value, 0) || inside(&rx, sym.st_value, 0))
                    value = (uintptr_t)image + sym.st_value;
                else
                    return invalid;
                if (type == R_X86_64_64)
                    value += (uint64_t)rel.r_addend;
            } else
                return "unsupported x86-64 relocation";
            if (image)
                memcpy(image + rel.r_offset, &value, sizeof value);
        }
    }
    if (entry)
        *entry = (uintptr_t)image + main_off;
    return NULL;
}
