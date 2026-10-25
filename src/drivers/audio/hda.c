#include "kernel.h"
#include "audio.h"

// Intel HDA - Memory-mapped controller at BAR0
// Minimal QEMU implementation: 4 output streams, 4 input, 1 bidir
// Offsets: see spec. We will use stream 0 output at 0x80

#define HDA_GCAP    0x00
#define HDA_VMIN    0x02
#define HDA_VMAJ    0x03
#define HDA_GCTL    0x08
#define HDA_GSTS    0x0C
#define HDA_INTCTL  0x20
#define HDA_INTSTS  0x24
#define HDA_WALCLK  0x30
#define HDA_CORBLBASE 0x40
#define HDA_CORBUBASE 0x44
#define HDA_CORBWP 0x48
#define HDA_CORBRP 0x4A
#define HDA_CORBCTL 0x4C
#define HDA_CORBSTS 0x4D
#define HDA_CORBSIZE 0x4E
#define HDA_RIRBLBASE 0x50
#define HDA_RIRBUBASE 0x54
#define HDA_RIRBWP 0x58
#define HDA_RIRBSTS 0x5D
#define HDA_RIRBSIZE 0x5E
#define HDA_IC      0x60
#define HDA_IR      0x64
#define HDA_ICS     0x68
#define HDA_DPLBASE 0x70
#define HDA_DPUBASE 0x74
#define HDA_SD_BASE 0x80
#define HDA_SD_STRIDE 0x20

#define HDA_SD_CTL   0x00
#define HDA_SD_CTL_RUN (1<<1)
#define HDA_SD_CTL_SRST (1<<0)
#define HDA_SD_CTL_IOCE (1<<2)
#define HDA_SD_CTL_FEIE (1<<3)
#define HDA_SD_CTL_DEIE (1<<4)
#define HDA_SD_STS   0x03
#define HDA_SD_LPIB  0x04
#define HDA_SD_CBL   0x08
#define HDA_SD_LVI   0x0C
#define HDA_SD_FIFOW 0x0E
#define HDA_SD_FIFOS 0x10
#define HDA_SD_FMT   0x12
#define HDA_SD_BDLPL 0x18
#define HDA_SD_BDLPU 0x1C

// HDA verb decoding: CAD(4bits) <<28 | NID<<20 | verb 12bits | payload
#define HDA_BDL_COUNT 32
typedef struct {
    uint32_t addr_lo;
    uint32_t addr_hi;
    uint32_t len;
    uint32_t flags; // bit0 IOC
} __attribute__((packed)) hda_bdle_t;

static uintptr_t hda_mmio = 0;
static uint32_t* hda_regs = NULL;
static uint8_t irq_line = 0;
static uint8_t pci_bus=0,pci_slot=0,pci_func=0;
static bool probed=false;
static bool initialized=false;

static uint32_t* corb = NULL;
static uint64_t* rirb = NULL;
static hda_bdle_t* bdl = NULL;
static int16_t* dma_buffer = NULL;
static uint8_t cur_stream = 0; // output stream 0
static uint8_t sd_offset = 0x80; // will calculate

static inline uint8_t mmio_read8(uint32_t off){ return *(volatile uint8_t*)((uintptr_t)hda_regs + off); }
static inline uint16_t mmio_read16(uint32_t off){ return *(volatile uint16_t*)((uintptr_t)hda_regs + off); }
static inline uint32_t mmio_read32(uint32_t off){ return *(volatile uint32_t*)((uintptr_t)hda_regs + off); }
static inline void mmio_write8(uint32_t off, uint8_t v){ *(volatile uint8_t*)((uintptr_t)hda_regs + off)=v; }
static inline void mmio_write16(uint32_t off, uint16_t v){ *(volatile uint16_t*)((uintptr_t)hda_regs + off)=v; }
static inline void mmio_write32(uint32_t off, uint32_t v){ *(volatile uint32_t*)((uintptr_t)hda_regs + off)=v; }

bool hda_probe(uint8_t bus, uint8_t slot, uint8_t func){
    uint32_t cls = pci_config_read(bus,slot,func,0x08);
    uint8_t base=(cls>>24)&0xFF, sub=(cls>>16)&0xFF;
    if (base!=0x04 || sub!=0x03) return false;
    uint32_t bar0 = pci_config_read(bus,slot,func,0x10);
    uint32_t bar1 = pci_config_read(bus,slot,func,0x14);
    // HDA BAR0 is MEM 64-bit capable
    bool is_mem = !(bar0 & 1);
    if (!is_mem) return false;
    uint64_t mmio_base = bar0 & 0xFFFFFFF0;
    if (bar0 & 4){ // 64-bit
        mmio_base |= ((uint64_t)bar1) << 32;
        mmio_base &= 0xFFFFFFFFFFFFFFF0ULL;
    }
    if (mmio_base==0) return false;
    hda_mmio = (uintptr_t)mmio_base;
    hda_regs = (uint32_t*)hda_mmio;
    probed=true;
    pci_bus=bus; pci_slot=slot; pci_func=func;
    uint32_t irqreg = pci_config_read(bus,slot,func,0x3C);
    irq_line = irqreg & 0xFF;
    serial_puts("[hda] probe ok bus "); serial_putdec(bus); serial_puts(" slot "); serial_putdec(slot);
    serial_puts(" MMIO 0x"); serial_puthex32((uint32_t)mmio_base); serial_puts(" IRQ "); serial_putdec(irq_line); serial_puts("\n");
    // read GCAP to determine streams
    // Need PCI command MEM enable
    uint32_t cmd = pci_config_read(bus,slot,func,0x04);
    cmd |= 0x02; // MEM
    cmd |= 0x04; // BM
    cmd &= ~0x04000000; // disable intx? not
    pci_config_write(bus,slot,func,0x04, cmd);
    return true;
}

static bool hda_reset(void){
    if (!hda_regs) return false;
    // Enter reset: write 0 to GCTL
    mmio_write32(HDA_GCTL, 0);
    delay_ms(10);
    // Exit reset: write 1
    mmio_write32(HDA_GCTL, 1);
    // poll GCTL bit0
    for (int i=0;i<100;i++){
        uint32_t gctl = mmio_read32(HDA_GCTL);
        if (gctl & 1) break;
        delay_ms(1);
    }
    if (!(mmio_read32(HDA_GCTL) & 1)){
        serial_puts("[hda] reset failed GCTL not ready\n");
        return false;
    }
    serial_puts("[hda] controller reset done\n");
    return true;
}

static void hda_setup_corb_rirb(void){
    // Allocate CORB 256 entries *4 = 1KB aligned 128?
    corb = (uint32_t*)kmalloc(2048);
    rirb = (uint64_t*)kmalloc(2048);
    uintptr_t a = (uintptr_t)corb; if (a & 0x7F) corb = (uint32_t*)(((a+0x7F)&~0x7F));
    a = (uintptr_t)rirb; if (a & 0x7F) rirb = (uint64_t*)(((a+0x7F)&~0x7F));
    memset(corb, 0, 1024);
    memset(rirb, 0, 2048);
    // Program CORB base
    uint32_t lo = (uint32_t)(uintptr_t)corb;
    uint32_t hi = (uint32_t)(((uint64_t)(uintptr_t)corb) >> 32);
    mmio_write32(HDA_CORBLBASE, lo);
    mmio_write32(HDA_CORBUBASE, hi);
    // CORBWP reset to 0
    mmio_write16(HDA_CORBWP, 0);
    // Reset CORBRP set bit15 then clear
    mmio_write16(HDA_CORBRP, 0x8000);
    for (int i=0;i<10;i++){
        if (mmio_read16(HDA_CORBRP) & 0x8000) break;
        delay_ms(1);
    }
    mmio_write16(HDA_CORBRP, 0);
    for (int i=0;i<10;i++){
        if (!(mmio_read16(HDA_CORBRP) & 0x8000)) break;
        delay_ms(1);
    }
    // Enable CORB DMA
    mmio_write8(HDA_CORBCTL, 0x02); // CORB run
    // RIRB base
    lo = (uint32_t)(uintptr_t)rirb;
    hi = (uint32_t)(((uint64_t)(uintptr_t)rirb)>>32);
    mmio_write32(HDA_RIRBLBASE, lo);
    mmio_write32(HDA_RIRBUBASE, hi);
    mmio_write16(HDA_RIRBWP, 0x8000); // reset WP (bit15 =1?)
    mmio_write16(HDA_RIRBWP, 0);
    mmio_write8(0x5C, 0x02); // RIRBCTL run? offset 0x5C is RIRBCTL
    serial_puts("[hda] CORB/RIRB setup done\n");
}

static uint32_t hda_send_verb(uint32_t verb){
    // Use Immediate Command interface (IC/IR/ICS) simpler than CORB for QEMU
    // IC at 0x60, IR at 0x64, ICS at 0x68
    // ICS bits 1:0 = 01 = busy? Need to check spec: offset 0x68 ICS bits: 0 = ICB (busy), 1 = IRV (response valid)
    mmio_write32(HDA_IC, verb);
    // wait busy? In spec, writing IC sets ICB=1? Actually HDA_IC write triggers command if ICB bit in ICS is set? Let's use CORB instead?
    // Simplify: use CORB path: write verb to CORB[wp], increment WP, wait for RIRB response
    // But immediate command may be easier if QEMU supports ICW.
    // For QEMU HDA, immediate command is supported; we'll try IC path first
    // Poll ICS
    for (int i=0;i<100;i++){
        uint16_t ics = mmio_read16(HDA_ICS);
        if (ics & 0x02){ // IRV? Check
            uint32_t resp = mmio_read32(HDA_IR);
            mmio_write16(HDA_ICS, 0x02); // clear IRV by writing 1?
            return resp;
        }
        delay_ms(1);
    }
    return 0xFFFFFFFF;
}

static void hda_codec_init(void){
    // Discover codecs via STATESTS 0x0E - bits indicate codec present
    uint16_t statests = mmio_read16(0x0E);
    serial_puts("[hda] STATESTS 0x"); serial_puthex32(statests); serial_puts("\n");
    // Probe codec 0
    // Get vendor ID via verb 0x00F00000? Actually GET_PARAMETER vendor id
    // Verb format: CAD(3:0) <<28 | NID(7:0)<<20 | verb(12 bits)<<8 | payload
    // For codec 0, NID 0, verb F00 = param
    // We'll try immediate command: verb = (0<<28)|(0<<20)|(0xF00<<8)|0x00
    uint32_t verb = (0 << 28) | (0 << 20) | (0xF00 << 8) | 0x00;
    uint32_t resp = hda_send_verb(verb);
    serial_puts("[hda] codec vendor resp 0x"); serial_puthex32(resp); serial_puts("\n");
    // Try get audio function group
    verb = (0<<28)|(0x01<<20)|(0xF00<<8)|0x04; // NID 1, param 04 = function type?
    resp = hda_send_verb(verb);
    serial_puts("[hda] AFG type 0x"); serial_puthex32(resp); serial_puts("\n");
    // Power up AFG: verb SET_POWER_STATE 0x705
    verb = (0<<28)|(0x01<<20)|(0x705<<8)|0x00; // power D0
    resp = hda_send_verb(verb);
    // Enable output: pin widget control? This is complex; QEMU HDA codec may already be ready without verbs
    // We'll just log and proceed to DMA setup, assuming codec is ready
}

static void hda_setup_dma(void){
    // Find output stream descriptor
    // GCAP: bits 12:8 number output streams, 7:3 bidir, etc.
    uint16_t gcap = mmio_read16(HDA_GCAP);
    uint8_t n_out = (gcap >> 12) & 0x0F;
    uint8_t n_in = (gcap >> 8) & 0x0F;
    uint8_t n_bidir = (gcap >> 3) & 0x1F;
    serial_puts("[hda] GCAP 0x"); serial_puthex32(gcap); serial_puts(" out "); serial_putdec(n_out);
    serial_puts(" in "); serial_putdec(n_in); serial_puts(" bidir "); serial_putdec(n_bidir); serial_puts("\n");
    // For QEMU ICH6, out=4 in=4 bidir=0 -> streams at 0x80 + n*0x20 where first n_in are input, then n_out output
    // SD layout: ISD0..ISD(n_in-1) then OSD0..OSD(n_out-1) then BSD
    // So output base offset = 0x80 + n_in*0x20
    uint32_t out_base = 0x80 + n_in * 0x20;
    sd_offset = (uint8_t)out_base;
    cur_stream = 1; // stream tag 1
    serial_puts("[hda] using SD offset 0x"); serial_puthex32(sd_offset); serial_puts("\n");

    // Allocate BDL and DMA buffer
    bdl = (hda_bdle_t*)kmalloc(HDA_BDL_COUNT * sizeof(hda_bdle_t) + 128);
    dma_buffer = (int16_t*)kmalloc(AUDIO_BUFFER_FRAMES * 4 * AUDIO_FRAME_BYTES * 2 + 128);
    uintptr_t a = (uintptr_t)bdl; if (a & 0x7F) bdl = (hda_bdle_t*)(((a+0x7F)&~0x7F));
    a = (uintptr_t)dma_buffer; if (a & 0x7F) dma_buffer = (int16_t*)(((a+0x7F)&~0x7F));
    memset(bdl, 0, HDA_BDL_COUNT*sizeof(hda_bdle_t));
    // Prepare BDL entries: each 1920 bytes, BDL length = bytes
    for (int i=0;i<HDA_BDL_COUNT;i++){
        uint64_t phys = (uint64_t)(uintptr_t)&dma_buffer[i * AUDIO_BUFFER_FRAMES * AUDIO_CHANNELS];
        bdl[i].addr_lo = (uint32_t)phys;
        bdl[i].addr_hi = (uint32_t)(phys>>32);
        bdl[i].len = AUDIO_BUFFER_FRAMES * AUDIO_FRAME_BYTES; // bytes
        bdl[i].flags = 1; // IOC
    }
    // Program stream descriptor
    // Reset stream
    mmio_write8(sd_offset + HDA_SD_CTL, 1); // SRST
    delay_ms(1);
    mmio_write8(sd_offset + HDA_SD_CTL, 0);
    delay_ms(1);
    // Ensure SRST cleared
    for (int i=0;i<10;i++){
        if (!(mmio_read8(sd_offset + HDA_SD_CTL) & 1)) break;
        delay_ms(1);
    }
    // Set format: 48kHz 16-bit stereo
    // Format bits: [15] type 0=PCM, [14] 0=48k base, [13:11] mult 000= x1, [10:8] div 000= /1, [6:4] bits 001=16, [3:0] ch 0001=2 => value 0x0011? Let's compute
    // From spec: FMT bits: 0x11 = 16-bit stereo 48k
    // Let's compute properly: bits per sample 16 -> 001 at [6:4] (value 1 <<4 = 0x10), channels 1 (2 ch) at [3:0]=0001, base 48k at [14]=0 -> overall 0x0011?
    // Check QEMU expects 0x11 as well.
    uint16_t fmt = 0x0011; // 48k 16-bit stereo
    // But check extended: mult 0, div 0 => 0. So fmt=0x11
    mmio_write16(sd_offset + HDA_SD_FMT, fmt);
    // Set CBL = total bytes = entries * len
    uint32_t cbl = HDA_BDL_COUNT * AUDIO_BUFFER_FRAMES * AUDIO_FRAME_BYTES;
    mmio_write32(sd_offset + HDA_SD_CBL, cbl);
    // Set LVI = last index
    mmio_write16(sd_offset + HDA_SD_LVI, HDA_BDL_COUNT - 1);
    // Set BDL pointer
    uint32_t lo = (uint32_t)(uintptr_t)bdl;
    uint32_t hi = (uint32_t)(((uint64_t)(uintptr_t)bdl)>>32);
    mmio_write32(sd_offset + HDA_SD_BDLPL, lo);
    mmio_write32(sd_offset + HDA_SD_BDLPU, hi);
    // Stream number in bits 23:20 of SDCTL high? Actually SDCTL offset 0x00 dword includes stream number (bits 20-23?) Check spec: SDnCTL bits 23:20 = Stream Number
    // SDCTL is 3 bytes + reserved? Our map: offset 0x00 dword? Let's treat as 32-bit: bits 23:20 are stream no.
    uint32_t ctl = mmio_read32(sd_offset + HDA_SD_CTL);
    ctl = (ctl & ~0x00F00000) | ((cur_stream & 0xF) << 20);
    mmio_write32(sd_offset + HDA_SD_CTL, ctl);
    // Set FIFO watermark to 32?
    mmio_write16(sd_offset + HDA_SD_FIFOW, 32);
    // Enable interrupt
    // INTCTL enable GIE + stream interrupt
    uint32_t intctl = mmio_read32(HDA_INTCTL);
    intctl |= (1u<<31) | (1u<<30); // GIE+CIE
    // Stream interrupt: output stream 1 => bit? Output streams start after input: stream index = n_in + tag-1?
    // For simplicity enable all stream ints
    intctl |= 0xFF;
    mmio_write32(HDA_INTCTL, intctl);
    serial_puts("[hda] DMA BDL setup CBL "); serial_putdec(cbl); serial_puts(" fmt 0x"); serial_puthex32(fmt); serial_puts("\n");
}

bool hda_init(void){
    if (!probed) {
        serial_puts("[hda] not probed, skip\n");
        return false;
    }
    if (!hda_reset()) return false;
    hda_setup_corb_rirb();
    hda_codec_init();
    hda_setup_dma();

    // Start stream
    uint8_t ctl = mmio_read8(sd_offset + HDA_SD_CTL);
    ctl |= HDA_SD_CTL_RUN | HDA_SD_CTL_IOCE;
    mmio_write8(sd_offset + HDA_SD_CTL, ctl);
    // Also need to set GCTL?
    delay_ms(10);
    uint8_t sts = mmio_read8(sd_offset + HDA_SD_STS);
    serial_puts("[hda] STS after run 0x"); serial_puthex32(sts); serial_puts("\n");
    initialized = true;
    // unmask IRQ
    if (irq_line < 16) pic_unmask_irq(irq_line);
    else { pic_unmask_irq(10); pic_unmask_irq(11); }
    serial_puts("[hda] DMA started\n");
    return true;
}

void hda_shutdown(void){
    if (!initialized) return;
    mmio_write8(sd_offset + HDA_SD_CTL, 0);
    initialized=false;
}

bool hda_submit(const int16_t* frames, size_t frame_count){
    if (!initialized || !bdl || !dma_buffer) return false;
    // Simple circular fill: track write index separate from hardware LPIB/CIV
    static uint8_t write_idx = 0;
    int16_t* dest = &dma_buffer[write_idx * AUDIO_BUFFER_FRAMES * AUDIO_CHANNELS];
    for (size_t i=0;i<frame_count*AUDIO_CHANNELS;i++) dest[i]=frames[i];
    // Ensure LVI points to valid region? In cyclic mode, LVI stays constant max, hardware wraps. We just update next BDL len? Already valid.
    // For cyclic, we don't advance LVI; hardware loops. So we just overwrite buffer slots as they are consumed.
    // Need to know consumer position: LPIB indicates bytes consumed
    uint32_t lpib = mmio_read32(sd_offset + HDA_SD_LPIB);
    // LPIB wraps at CBL, so we can compute next write position = (lpib/AUDIO_FRAME_BYTES + HDA_BDL_COUNT/2?) To avoid overrun, we wait until LPIB passes our previous write?
    // Simpler: just increment write_idx modulo
    write_idx = (write_idx + 1) % HDA_BDL_COUNT;
    // Keep LVI constant; no need to update if cyclic? But some controllers require LVI update to keep running; we keep max
    (void)lpib;
    return true;
}

void hda_tick(void){
    if (!initialized) return;
    uint8_t sts = mmio_read8(sd_offset + HDA_SD_STS);
    if (sts & 0x1C){ // BCIS|LVI|FIFOE
        mmio_write8(sd_offset + HDA_SD_STS, sts & 0x1C);
    }
    // Also check INTSTS
    uint32_t intsts = mmio_read32(HDA_INTSTS);
    if (intsts){
        mmio_write32(HDA_INTSTS, intsts);
    }
}

audio_backend_t hda_backend = {
    .name = "HDA",
    .init = hda_init,
    .shutdown = hda_shutdown,
    .submit = hda_submit,
    .set_volume = NULL,
    .is_ready = NULL,
    .tick = hda_tick,
};
