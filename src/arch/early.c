#include "kernel.h"

                                                                    
  
                                                                   
                                                                
                                                                  
                                                                 
                                                                     
                                                               
                                                                   
                                                                    

#define COM1_BASE 0x3F8

void serial_init(void) {
    outb(COM1_BASE + 1, 0x00);                 
    outb(COM1_BASE + 3, 0x80);            
    outb(COM1_BASE + 0, 0x01);                
    outb(COM1_BASE + 1, 0x00);
    outb(COM1_BASE + 3, 0x03);        
    outb(COM1_BASE + 2, 0xC7);            
    outb(COM1_BASE + 4, 0x0B);            
}

static int serial_tx_ready(void) {
    return inb(COM1_BASE + 5) & 0x20;
}

int serial_input_ready(void) {
    uint8_t status = inb(COM1_BASE + 5);
    return status != 0xFF && (status & 1);
}

int serial_getchar_nonblocking(void) {
    if (!serial_input_ready()) return -1;
    return inb(COM1_BASE);
}

void serial_putc(char c) {
                                                                
    for (int i = 0; i < 100000 && !serial_tx_ready(); i++) { }
    if (!serial_tx_ready()) return;
    outb(COM1_BASE, (uint8_t)c);
}

void serial_puts(const char* s) {
    while (*s) {
        if (*s == '\n') serial_putc('\r');
        serial_putc(*s++);
    }
}

void serial_puthex32(uint32_t v) {
    static const char* h = "0123456789ABCDEF";
    serial_puts("0x");
    for (int i = 7; i >= 0; i--) serial_putc(h[(v >> (i * 4)) & 0xF]);
}

void serial_putdec(uint32_t v) {
    char t[12];
    int_to_string(v, t);
    serial_puts(t);
}

                                                                 
                                                         
static void early_delay(void) {
    for (volatile unsigned i = 0; i < 4000000u; i++) {
        asm volatile("" ::: "memory");
    }
}

static void speaker_on(void) {
    uint16_t div = 1193182 / 880;       
    outb(0x43, 0xB6);                                     
    outb(0x42, div & 0xFF);
    outb(0x42, (div >> 8) & 0xFF);
    outb(0x61, inb(0x61) | 0x03);
}

static void speaker_off(void) {
    outb(0x61, inb(0x61) & (uint8_t)~0x03);
}

void speaker_beeps(int n) {
    if (n <= 0 || n > 9) return;
    for (int i = 0; i < n; i++) {
        speaker_on();
        early_delay();
        speaker_off();
        early_delay();
    }
}

int early_panic_code = -1;

void early_panic(int code, const char* msg) {
    early_panic_code = code;
    asm volatile("cli");
    serial_puts("\nSHARKOS EARLY PANIC [code ");
    serial_putdec((uint32_t)code);
    serial_puts("]: ");
    serial_puts(msg);
    serial_puts("\nSystem halted.\n");
    if (code > 0) speaker_beeps(code);
                                                               
    for (;;) asm volatile("cli; hlt");
}
