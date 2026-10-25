#include "kernel.h"
#include "audio.h"

static bool s_init = false;

bool pcspkr_init(void) {
    s_init = true;
    serial_puts("[audio] pcspkr: PIT channel2 at 1.193182 MHz, port 0x43/0x42/0x61\n");
    return true;
}

void pcspkr_beep(uint32_t freq_hz, uint32_t ms) {
    if (!freq_hz) return;
    uint32_t div = 1193182 / freq_hz;
    if (div == 0) div = 1;
    if (div > 0xFFFF) div = 0xFFFF;
    outb(0x43, 0xB6);
    outb(0x42, div & 0xFF);
    outb(0x42, (div >> 8) & 0xFF);
    uint8_t tmp = inb(0x61);
    outb(0x61, tmp | 0x03);
    delay_ms(ms);
    tmp = inb(0x61);
    outb(0x61, tmp & ~0x03);
}

// non-blocking tick not needed for beep; placeholder
void pcspkr_tick(void) {}
