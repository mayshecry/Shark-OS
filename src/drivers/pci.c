#include "kernel.h"

uint32_t pci_config_read(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t address = (uint32_t)((bus << 16) | (slot << 11) | (func << 8) | (offset & 0xfc) | ((uint32_t)0x80000000));
    outl(0xCF8, address);
    return inl(0xCFC);
}

void pci_config_write(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t value) {
    uint32_t address = (uint32_t)((bus << 16) | (slot << 11) | (func << 8) | (offset & 0xfc) | ((uint32_t)0x80000000));
    outl(0xCF8, address);
    outl(0xCFC, value);
}

uint32_t pci_read_header_type(uint8_t bus, uint8_t slot, uint8_t func) {
    return pci_config_read(bus, slot, func, 0x0C) & 0xFF;
}

void pci_list_devices() {
    int count = 0;
    for (uint32_t bus = 0; bus < 256; bus++) {
        for (uint32_t slot = 0; slot < 32; slot++) {
            uint32_t id0 = pci_config_read(bus, slot, 0, 0);
            if (id0 == 0xFFFFFFFF) continue;
            count++;
            if (!(pci_read_header_type(bus, slot, 0) & 0x80)) continue;
            for (uint32_t func = 1; func < 8; func++) {
                if (pci_config_read(bus, slot, func, 0) == 0xFFFFFFFF) continue;
                count++;
            }
        }
    }
    terminal_writestring("PCI: ");
    char buf[16]; int_to_string(count, buf); terminal_writestring(buf);
    terminal_writestring(" devices found\n");
}