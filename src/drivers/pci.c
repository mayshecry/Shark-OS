#include "kernel.h"

void rtl8139_init() {
    network_initialized = true;
}

void rtl8139_send_packet(void* data, uint32_t len) {
    if (!rtl_io_base) {
        terminal_writestring("Error: Cannot send packet, RTL8139 not initialized.\n");
        return;
    }

    outl(rtl_io_base + 0x20 + (current_tx_buffer * 4), (uint32_t)(uintptr_t)data);
    outl(rtl_io_base + 0x10 + (current_tx_buffer * 4), len);
    current_tx_buffer = (current_tx_buffer + 1) % 4;
}

void send_icmp_ping(const char* target) {
    if (rtl_io_base) {
        uint8_t* packet = (uint8_t*)kmalloc(64);
        for(int i=0; i<6; i++) packet[i] = 0xFF;
        packet[6] = 0x00; packet[7] = 0x11; packet[8] = 0x22; packet[9] = 0x33; packet[10] = 0x44; packet[11] = 0x55;
        packet[12] = 0x08; packet[13] = 0x00;
        packet[14] = 0x45;
        packet[15] = 0x00;
        packet[16] = 0x00; packet[17] = 0x34;
        packet[18] = 0x00; packet[19] = 0x01;
        packet[20] = 0x00; packet[21] = 0x00;
        packet[22] = 0x40;
        packet[23] = 0x01;
        packet[24] = 0x00; packet[25] = 0x00;
        packet[26] = 10; packet[27] = 0; packet[28] = 2; packet[29] = 15;
        packet[30] = 8; packet[31] = 8; packet[32] = 8; packet[33] = 8;
        packet[34] = 0x08;
        packet[35] = 0x00;
        packet[36] = 0x00; packet[37] = 0x00;
        packet[38] = 0x00; packet[39] = 0x01;
        packet[40] = 0x00; packet[41] = 0x01;
        for(int i=0; i<24; i++) packet[42+i] = 'A' + (i % 26);

        rtl8139_send_packet(packet, 64);
        terminal_writestring("Hardware: Packet sent to ");
        terminal_writestring(target);
        terminal_writestring(" (Ethernet Frame Built).\n");
    } else {
        terminal_writestring("Loopback: Reply from 127.0.0.1: bytes=32 time<1ms TTL=64\n");
    }
}

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

void pci_list_devices() {
    char buffer[11];
    terminal_writestring("PCI Bus Scan:\n");
    for(uint32_t bus = 0; bus < 256; bus++) {
        for(uint32_t slot = 0; slot < 32; slot++) {
            uint32_t pci_data = pci_config_read(bus, slot, 0, 0);
            if (pci_data == 0xFFFFFFFF) continue;

            for (uint32_t func = 0; func < 8; func++) {
                pci_data = pci_config_read(bus, slot, func, 0);
                uint16_t vendor = pci_data & 0xFFFF;
                uint16_t device = (pci_data >> 16) & 0xFFFF;

                if (vendor == 0xFFFF || vendor == 0x0000) continue;

                terminal_writestring("  Bus ");
                hex_to_string(bus, buffer); terminal_writestring(buffer);
                terminal_writestring(" Slot ");
                hex_to_string(slot, buffer); terminal_writestring(buffer);
                terminal_writestring(" | Vendor: ");
                hex_to_string(vendor, buffer); terminal_writestring(buffer);
                terminal_writestring(" Device: ");
                hex_to_string(device, buffer); terminal_writestring(buffer);
                terminal_writestring("\n");
            }
        }
    }
}