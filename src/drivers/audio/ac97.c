#include "kernel.h"
#include "audio.h"

 
 
 
 

#define AC97_BM_PI_OFF   0x00
#define AC97_BM_PO_OFF   0x10
#define AC97_BM_MC_OFF   0x20
#define AC97_BM_GLOB_CNT 0x2C
#define AC97_BM_GLOB_STA 0x30

#define AC97_X_BDBAR 0x00
#define AC97_X_CIV   0x04
#define AC97_X_LVI   0x05
#define AC97_X_SR    0x06
#define AC97_X_PICB  0x08
#define AC97_X_PIV   0x0A
#define AC97_X_CR    0x0B

#define AC97_CR_RPBM  0x01
#define AC97_CR_RR    0x02
#define AC97_CR_LVBIE 0x04
#define AC97_CR_FEIE  0x08
#define AC97_CR_IOCE  0x10

#define AC97_SR_DCH   0x01
#define AC97_SR_CELV  0x02
#define AC97_SR_LVBCI 0x04
#define AC97_SR_BCIS  0x08
#define AC97_SR_FIFOE 0x10

#define AC97_BDL_ENTRY_SIZE 8
#define AC97_BDL_COUNT 32

typedef struct {
    uint32_t addr;  
    uint16_t len;   
    uint16_t flags;  
} __attribute__((packed)) ac97_bdle_t;

static uint16_t nambar = 0;
static uint16_t nabmbar = 0;
static uint8_t irq_line = 0;
static uint8_t pci_bus=0, pci_slot=0, pci_func=0;
static bool probed = false;
static bool running = false;

static ac97_bdle_t* bdl = NULL;  
static int16_t* dma_buffer = NULL;  
#define AC97_DMA_FRAMES (AUDIO_BUFFER_FRAMES * 4)  
static int16_t* ac97_dma = NULL;

static uint8_t cur_lvi = 0;
static uint8_t cur_civ = 0;

static inline uint32_t virt_to_phys32(void* v){ return (uint32_t)(uintptr_t)v; }

 
static void ac97_write_mixer(uint8_t reg, uint16_t val){
    outw(nambar + reg, val);
}
static uint16_t ac97_read_mixer(uint8_t reg){
    return inw(nambar + reg);
}

bool ac97_probe(uint8_t bus, uint8_t slot, uint8_t func){
    uint32_t didvid = pci_config_read(bus,slot,func,0);
    uint32_t cls = pci_config_read(bus,slot,func,0x08);
    uint8_t base = (cls>>24)&0xFF, sub=(cls>>16)&0xFF;
    if (base!=0x04) return false;
    if (sub!=0x01) return false;  
     
    uint32_t bar0 = pci_read_bar(bus,slot,func,0);
    uint32_t bar1 = pci_read_bar(bus,slot,func,1);
     
    if ((bar1 & 1)==0) {
         
         
        uint32_t tmp=bar0; bar0=bar1; bar1=tmp;
        if ((bar1 & 1)==0) return false;
    }
    nambar = (uint16_t)(bar0 & 0xFFFC);
    nabmbar = (uint16_t)(bar1 & 0xFFFC);
    pci_bus=bus; pci_slot=slot; pci_func=func;
    uint32_t irqreg = pci_config_read(bus,slot,func,0x3C);
    irq_line = irqreg & 0xFF;
     
    if (nambar==0 || nabmbar==0) return false;
     
     
    probed = true;
    serial_puts("[ac97] probe ok bus "); serial_putdec(bus); serial_puts(" slot "); serial_putdec(slot);
    serial_puts(" NAMBAR 0x"); serial_puthex32(nambar); serial_puts(" NABMBAR 0x"); serial_puthex32(nabmbar);
    serial_puts(" IRQ "); serial_putdec(irq_line); serial_puts("\n");
    return true;
}

static void ac97_reset_controller(void){
     
    uint32_t cmd = pci_config_read(pci_bus,pci_slot,pci_func,0x04);
    cmd |= 0x05;  
    if (!(cmd & 0x04)) cmd |= 0x04;  
    pci_config_write(pci_bus,pci_slot,pci_func,0x04, cmd);
     
     
    outb(nabmbar + AC97_BM_PO_OFF + AC97_X_CR, AC97_CR_RR);
     
    for (volatile int i=0;i<1000;i++) asm volatile("nop");
     
    outb(nabmbar + AC97_BM_PO_OFF + AC97_X_CR, 0x00);
     
    outl(nabmbar + AC97_BM_GLOB_CNT, 0x00000002);  
    audio_delay_ms(10);
     
    outw(nambar + 0x00, 0x0000);  
    audio_delay_ms(10);
     
    uint16_t eid = ac97_read_mixer(0x28);  
    serial_puts("[ac97] EID 0x"); serial_puthex32(eid); serial_puts("\n");
    if (eid & 0x0001){
         
        uint16_t stat = ac97_read_mixer(0x2A);
        (void)stat;
         
        uint16_t ext = ac97_read_mixer(0x2A);
        ac97_write_mixer(0x2A, ext | 0x0001);  
         
        ac97_write_mixer(0x2C, 0xBB80);  
        serial_puts("[ac97] VRA enabled, PCM rate 48000\n");
    }
     
    ac97_write_mixer(0x02, 0x0000);  
    ac97_write_mixer(0x04, 0x0000);  
    ac97_write_mixer(0x06, 0x0000);  
    ac97_write_mixer(0x18, 0x0000);  
    ac97_write_mixer(0x0E, 0x0000);  
    ac97_write_mixer(0x1C, 0x0F0F);  
}

static void ac97_setup_bdl(void){
     
    bdl = (ac97_bdle_t*)kmalloc(AC97_BDL_COUNT * sizeof(ac97_bdle_t) + 8);
    dma_buffer = (int16_t*)kmalloc(AC97_DMA_FRAMES * AUDIO_FRAME_BYTES * 2);  
     
    uintptr_t a = (uintptr_t)bdl;
    if (a & 7) { bdl = (ac97_bdle_t*)(((a+7)&~7)); }
     
    ac97_dma = dma_buffer;
     
    for (int i=0;i<AC97_BDL_COUNT;i++){
        bdl[i].addr = 0;
        bdl[i].len = 0;
        bdl[i].flags = 0;
    }
     
     
     
     
     
     
     
     
     
     
     
     
     
    for (int i=0;i<AC97_BDL_COUNT;i++){
        uint32_t phys = virt_to_phys32(&ac97_dma[i * AUDIO_BUFFER_FRAMES * AUDIO_CHANNELS]);
         
         
        bdl[i].addr = phys;
        bdl[i].len = AUDIO_BUFFER_FRAMES * 2 * 2;  
         
         
        bdl[i].len = AUDIO_BUFFER_FRAMES * 4;  
        bdl[i].flags = 0x8000;  
    }
     
    outl(nabmbar + AC97_BM_PO_OFF + AC97_X_BDBAR, virt_to_phys32(bdl));
     
    outb(nabmbar + AC97_BM_PO_OFF + AC97_X_LVI, AC97_BDL_COUNT - 1);
     
    outb(nabmbar + AC97_BM_PO_OFF + AC97_X_CIV, 0);
     
    outw(nabmbar + AC97_BM_PO_OFF + AC97_X_SR, 0xFFFF);
    serial_puts("[ac97] BDL setup "); serial_putdec(AC97_BDL_COUNT); serial_puts(" entries, BDBAR 0x"); serial_puthex32(virt_to_phys32(bdl)); serial_puts("\n");
}

bool ac97_init(void){
    if (!probed){
         
         
        serial_puts("[ac97] not probed, skip\n");
        return false;
    }
    ac97_reset_controller();
    ac97_setup_bdl();

     
    if (irq_line < 16 && irq_line != 0 && irq_line != 255){
        pic_unmask_irq(irq_line);
        serial_puts("[ac97] IRQ "); serial_putdec(irq_line); serial_puts(" unmasked\n");
    } else {
         
        pic_unmask_irq(10); pic_unmask_irq(11); pic_unmask_irq(5);
    }

     
    uint8_t cr = inb(nabmbar + AC97_BM_PO_OFF + AC97_X_CR);
    cr |= AC97_CR_RPBM | AC97_CR_IOCE | AC97_CR_FEIE | AC97_CR_LVBIE;
    outb(nabmbar + AC97_BM_PO_OFF + AC97_X_CR, cr);
    running = true;
    serial_puts("[ac97] DMA started, CR=0x"); serial_puthex32(cr); serial_puts("\n");
     
    uint16_t sr = inw(nabmbar + AC97_BM_PO_OFF + AC97_X_SR);
    serial_puts("[ac97] SR=0x"); serial_puthex32(sr); serial_puts("\n");
    return true;
}

void ac97_shutdown(void){
    if (!probed) return;
    outb(nabmbar + AC97_BM_PO_OFF + AC97_X_CR, 0x00);
    running = false;
}

bool ac97_submit(const int16_t* frames, size_t frame_count){
    if (!probed || !running || !bdl || !ac97_dma) return false;
    if (frame_count != AUDIO_BUFFER_FRAMES){
         
         
    }
     
     
    uint8_t civ = inb(nabmbar + AC97_BM_PO_OFF + AC97_X_CIV);
     
    uint16_t sr = inw(nabmbar + AC97_BM_PO_OFF + AC97_X_SR);
    if (sr & AC97_SR_DCH){
         
        outw(nabmbar + AC97_BM_PO_OFF + AC97_X_SR, sr);  
        uint8_t cr = inb(nabmbar + AC97_BM_PO_OFF + AC97_X_CR);
        cr |= AC97_CR_RPBM;
        outb(nabmbar + AC97_BM_PO_OFF + AC97_X_CR, cr);
        serial_puts("[ac97] restart DMA DCH\n");
    }
     
     
    cur_lvi = inb(nabmbar + AC97_BM_PO_OFF + AC97_X_LVI);
    uint8_t next = (cur_lvi + 1) % AC97_BDL_COUNT;
     
     
     
    int16_t* dest = &ac97_dma[next * AUDIO_BUFFER_FRAMES * AUDIO_CHANNELS];
    for (size_t i=0;i<frame_count*AUDIO_CHANNELS;i++){
        dest[i] = frames[i];
    }
     
     
     
    outb(nabmbar + AC97_BM_PO_OFF + AC97_X_LVI, next);
    cur_lvi = next;
     
    outw(nabmbar + AC97_BM_PO_OFF + AC97_X_SR, AC97_SR_BCIS | AC97_SR_LVBCI);
    return true;
}

void ac97_tick(void){
    if (!probed || !running) return;
     
    uint16_t sr = inw(nabmbar + AC97_BM_PO_OFF + AC97_X_SR);
    if (sr & (AC97_SR_BCIS | AC97_SR_LVBCI | AC97_SR_FIFOE)){
        outw(nabmbar + AC97_BM_PO_OFF + AC97_X_SR, sr & (AC97_SR_BCIS|AC97_SR_LVBCI|AC97_SR_FIFOE));
    }
     
    if (sr & AC97_SR_DCH){
         
         
    }
}

 
audio_backend_t ac97_backend = {
    .name = "AC97",
    .init = ac97_init,
    .shutdown = ac97_shutdown,
    .submit = ac97_submit,
    .set_volume = NULL,
    .is_ready = NULL,  
    .tick = ac97_tick,
};
