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

// PCI helpers
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
        // also ensure cascade on master enabled
        uint8_t m = inb(0x21);
        m &= ~(1u << 2);
        outb(0x21, m);
    }
}
void pic_mask_irq(uint8_t irq){
    if (irq < 8){ uint8_t m=inb(0x21); m|=1u<<irq; outb(0x21,m);}
    else { irq-=8; uint8_t m=inb(0xA1); m|=1u<<irq; outb(0xA1,m);}
}

// probe all audio PCI devices and pick best
static void audio_probe_pci(void){
    // scan all b/s/f for audio class
    for (uint32_t b=0;b<256;b++){
        for (uint32_t s=0;s<32;s++){
            for (uint32_t f=0;f<8;f++){
                uint32_t d = pci_config_read(b,s,f,0);
                if (d==0xFFFFFFFF) continue;
                uint32_t cls = pci_config_read(b,s,f,0x08);
                uint8_t base = (cls>>24)&0xFF;
                uint8_t sub = (cls>>16)&0xFF;
                if (base!=0x04) continue;
                uint16_t ven = d & 0xFFFF;
                uint16_t dev = (d>>16)&0xFFFF;
                uint32_t irq = pci_config_read(b,s,f,0x3C);
                uint8_t irq_line = irq & 0xFF;
                serial_puts("[audio] PCI audio ");
                serial_puthex32(d); serial_puts(" class ");
                serial_puthex32(cls); serial_puts(" IRQ "); serial_putdec(irq_line); serial_puts("\n");
                if (sub==0x03){
                    // HDA
                    if (hda_probe((uint8_t)b,(uint8_t)s,(uint8_t)f)){
                        // init will be done outside
                    }
                } else if (sub==0x01){
                    if (ac97_probe((uint8_t)b,(uint8_t)s,(uint8_t)f)){
                        // init
                    }
                }
                // also handle Intel vendor specific still try both
                // QEMU AC97 shows as 8086:2415 subclass 01
                // QEMU HDA 8086:2668 subclass 03
                (void)ven; (void)dev;
            }
        }
    }
}

void audio_init(void){
    memset(&g_audio, 0, sizeof(g_audio));
    g_audio.master_volume = 200; // ~78%
    g_audio.next_id = 1;

    serial_puts("[audio] init: probing PCI\n");
    // init pcspkr always available
    pcspkr_init();
    g_audio.has_pcspkr = true;

    // probe pci devices (will set internal state for ac97/hda)
    audio_probe_pci();

    // try backends in order: HDA first (better), then AC97, then null
    // backends array populated by probe? Simpler try init directly:
    if (hda_init()){
        g_audio.has_hda = true;
        g_audio_backend = NULL;
        // find hda backend struct
        extern audio_backend_t hda_backend;
        g_audio_backend = &hda_backend;
        serial_puts("[audio] using HDA backend\n");
    } else if (ac97_init()){
        g_audio.has_ac97 = true;
        extern audio_backend_t ac97_backend;
        g_audio_backend = &ac97_backend;
        serial_puts("[audio] using AC97 backend\n");
    } else {
        serial_puts("[audio] no DMA backend, using pcspkr fallback + null sink\n");
        g_audio_backend = NULL;
    }
    g_audio.initialized = true;
    serial_puts("[audio] init done, backend=");
    serial_puts(audio_backend_name());
    serial_puts("\n");
}

void audio_tick(void){
    // called from timer IRQ or main loop
    // mix next buffer
    if (!g_audio.initialized) return;
    // if no backend but channels playing, we still mix but discard? For test harness we mix anyway
    // Generate mixed frames
    int16_t tmp[AUDIO_BUFFER_FRAMES * AUDIO_CHANNELS];
    audio_mix_frames(tmp, AUDIO_BUFFER_FRAMES);
    // submit to backend if available
    if (g_audio_backend && g_audio_backend->submit){
        // only submit if any channel active
        bool any=false;
        for (int i=0;i<AUDIO_MAX_CHANNELS;i++) if (g_audio.channels[i].active) { any=true; break; }
        if (any){
            if (!g_audio_backend->submit(tmp, AUDIO_BUFFER_FRAMES)){
                g_audio.underruns++;
            }
        } else {
            // silence? still need to keep DMA fed? AC97 will keep playing last buffer with silence
            // we just submit silence to avoid underrun clicks?
        }
    }
    // backend poll
    if (g_audio_backend && g_audio_backend->tick) g_audio_backend->tick();
    // pcspkr tick not needed
}


void audio_irq_handler(uint8_t irq){
    (void)irq;
    if (g_audio_backend && g_audio_backend->tick) g_audio_backend->tick();
}

void audio_shutdown(void){
    if (g_audio_backend && g_audio_backend->shutdown) g_audio_backend->shutdown();
    g_audio.initialized=false;
}
