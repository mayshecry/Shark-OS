#include "kernel.h"
#include "audio.h"

audio_backend_t* g_audio_backend = NULL;

static audio_backend_t* backends[3];
static int backend_count = 0;

bool audio_available(void){
    return g_audio_backend && g_audio_backend->is_ready && g_audio_backend->is_ready();
}
const char* audio_backend_name(void){
    if (g_audio_backend) return g_audio_backend->name;
    return "none";
}

 
bool pci_find_audio_device(uint8_t* bus, uint8_t* slot, uint8_t* func, uint16_t* vendor, uint16_t* device, uint32_t* classcode){
    for (uint32_t b=0;b<256;b++){
        for (uint32_t s=0;s<32;s++){
            for (uint32_t f=0;f<8;f++){
                uint32_t d = pci_config_read(b,s,f,0);
                if (d==0xFFFFFFFF) continue;
                uint16_t ven = d & 0xFFFF;
                uint16_t dev = (d>>16)&0xFFFF;
                uint32_t cls = pci_config_read(b,s,f,0x08);
                uint8_t base = (cls>>24)&0xFF;
                uint8_t sub = (cls>>16)&0xFF;
                if (base==0x04 && (sub==0x01 || sub==0x03)){
                    if (bus) *bus=b;
                    if (slot) *slot=s;
                    if (func) *func=f;
                    if (vendor) *vendor=ven;
                    if (device) *device=dev;
                    if (classcode) *classcode=cls;
                    return true;
                }
            }
        }
    }
    return false;
}

uint32_t pci_read_bar(uint8_t bus, uint8_t slot, uint8_t func, int bar){
    uint32_t off = 0x10 + bar*4;
    return pci_config_read(bus,slot,func,off);
}

void pic_unmask_irq(uint8_t irq){
    if (irq < 8){
        uint8_t mask = inb(0x21);
        mask &= ~(1u << irq);
        outb(0x21, mask);
    } else if (irq < 16){
        irq -= 8;
        uint8_t mask = inb(0xA1);
        mask &= ~(1u << irq);
        outb(0xA1, mask);
         
        uint8_t m = inb(0x21);
        m &= ~(1u << 2);
        outb(0x21, m);
    }
}
void pic_mask_irq(uint8_t irq){
    if (irq < 8){ uint8_t m=inb(0x21); m|=1u<<irq; outb(0x21,m);}
    else { irq-=8; uint8_t m=inb(0xA1); m|=1u<<irq; outb(0xA1,m);}
}

 
static uint64_t audio_read_tsc(void){
    uint32_t lo;
    uint32_t hi;
    asm volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

static uint64_t audio_tsc_frequency(void){
    static uint64_t cached_frequency = 0;
    if (cached_frequency) return cached_frequency;

    uint32_t eax;
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;
    asm volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(0), "c"(0));
    uint32_t max_leaf = eax;

    if (max_leaf >= 0x15){
        eax = 0x15;
        ecx = 0;
        asm volatile("cpuid" : "+a"(eax), "=b"(ebx), "+c"(ecx), "=d"(edx));
        if (eax && ebx && ecx) cached_frequency = ((uint64_t)ecx * ebx) / eax;
    }

    if (!cached_frequency && max_leaf >= 0x16){
        eax = 0x16;
        ecx = 0;
        asm volatile("cpuid" : "+a"(eax), "=b"(ebx), "+c"(ecx), "=d"(edx));
        if (eax) cached_frequency = (uint64_t)eax * 1000000ULL;
    }

    if (!cached_frequency) cached_frequency = 2500000000ULL;
    return cached_frequency;
}

static bool audio_probe_pci(void){
    uint32_t start_ticks = uptime_ticks;
    uint64_t start_tsc = audio_read_tsc();
    uint64_t timeout_cycles = audio_tsc_frequency() * 3ULL;

    for (uint32_t b=0;b<256;b++){
        for (uint32_t s=0;s<32;s++){
            if ((uint32_t)(uptime_ticks - start_ticks) >= 3000 ||
                audio_read_tsc() - start_tsc >= timeout_cycles){
                serial_puts("[audio] no audio device found within 3 seconds; skipping drivers\n");
                return false;
            }

            uint32_t d0 = pci_config_read(b,s,0,0);
            if (d0==0xFFFFFFFF) continue;
            uint32_t header = pci_config_read(b,s,0,0x0C);
            uint32_t function_count = ((header >> 23) & 1) ? 8 : 1;
            for (uint32_t f=0;f<function_count;f++){
                if ((uint32_t)(uptime_ticks - start_ticks) >= 3000 ||
                    audio_read_tsc() - start_tsc >= timeout_cycles){
                    serial_puts("[audio] no audio device found within 3 seconds; skipping drivers\n");
                    return false;
                }
                uint32_t d = f == 0 ? d0 : pci_config_read(b,s,f,0);
                if (d==0xFFFFFFFF) continue;
                uint32_t cls = pci_config_read(b,s,f,0x08);
                uint8_t base = (cls>>24)&0xFF;
                uint8_t sub = (cls>>16)&0xFF;
                if (base!=0x04 || (sub!=0x01 && sub!=0x03)) continue;
                uint32_t irq = pci_config_read(b,s,f,0x3C);
                serial_puts("[audio] PCI audio ");
                serial_puthex32(d); serial_puts(" class ");
                serial_puthex32(cls); serial_puts(" IRQ "); serial_putdec(irq & 0xFF); serial_puts("\n");
                if (sub==0x03 && hda_probe((uint8_t)b,(uint8_t)s,(uint8_t)f)) return true;
                if (sub==0x01 && ac97_probe((uint8_t)b,(uint8_t)s,(uint8_t)f)) return true;
            }
        }
    }

    serial_puts("[audio] no audio device found; skipping drivers\n");
    return false;
}

void audio_delay_ms(uint32_t ms){
    uint32_t start_ticks = uptime_ticks;
    uint32_t lo;
    uint32_t hi;
    asm volatile("rdtsc" : "=a"(lo), "=d"(hi));
    uint64_t start_tsc = ((uint64_t)hi << 32) | lo;
    uint64_t max_cycles = (uint64_t)ms * 4000000ULL;
    while ((uint32_t)(uptime_ticks - start_ticks) < ms){
        asm volatile("rdtsc" : "=a"(lo), "=d"(hi));
        uint64_t now_tsc = ((uint64_t)hi << 32) | lo;
        if (now_tsc - start_tsc >= max_cycles) break;
        asm volatile("pause");
    }
}

void audio_init(void){
    memset(&g_audio, 0, sizeof(g_audio));
    g_audio.master_volume = 200;  
    g_audio.next_id = 1;

    serial_puts("[audio] init: probing PCI\n");
     
    pcspkr_init();
    g_audio.has_pcspkr = true;

     
    if (!audio_probe_pci()){
        g_audio.initialized = true;
        serial_puts("[audio] init done, PC speaker fallback only\n");
        return;
    }
    serial_puts("[audio] PCI scan complete\n");

     
     
    serial_puts("[audio] HDA init begin\n");
    if (hda_init()){
        g_audio.has_hda = true;
        g_audio_backend = NULL;
         
        extern audio_backend_t hda_backend;
        g_audio_backend = &hda_backend;
        serial_puts("[audio] using HDA backend\n");
    } else {
        serial_puts("[audio] HDA init skipped or failed\n");
    }
    if (!g_audio_backend){
        serial_puts("[audio] AC97 init begin\n");
        if (ac97_init()){
            g_audio.has_ac97 = true;
            extern audio_backend_t ac97_backend;
            g_audio_backend = &ac97_backend;
            serial_puts("[audio] using AC97 backend\n");
        } else {
            serial_puts("[audio] no DMA backend, using pcspkr fallback + null sink\n");
            g_audio_backend = NULL;
        }
    }
    g_audio.initialized = true;
    serial_puts("[audio] init done, backend=");
    serial_puts(audio_backend_name());
    serial_puts("\n");
}

void audio_tick(void){
     
     
    if (!g_audio.initialized) return;
     
     
    int16_t tmp[AUDIO_BUFFER_FRAMES * AUDIO_CHANNELS];
    audio_mix_frames(tmp, AUDIO_BUFFER_FRAMES);
     
    if (g_audio_backend && g_audio_backend->submit){
         
        bool any=false;
        for (int i=0;i<AUDIO_MAX_CHANNELS;i++) if (g_audio.channels[i].active) { any=true; break; }
        if (any){
            if (!g_audio_backend->submit(tmp, AUDIO_BUFFER_FRAMES)){
                g_audio.underruns++;
            }
        } else {
             
             
        }
    }
     
    if (g_audio_backend && g_audio_backend->tick) g_audio_backend->tick();
     
}


void audio_irq_handler(uint8_t irq){
    (void)irq;
    if (g_audio_backend && g_audio_backend->tick) g_audio_backend->tick();
}

void audio_shutdown(void){
    if (g_audio_backend && g_audio_backend->shutdown) g_audio_backend->shutdown();
    g_audio.initialized=false;
}
