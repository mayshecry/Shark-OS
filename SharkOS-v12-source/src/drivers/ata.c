#include "kernel.h"
#include "ata.h"
#include "ahci.h"
#include "ata_identify.h"

#define ATA_REG_DATA   0
#define ATA_REG_ERR    1
#define ATA_REG_SECCNT 2
#define ATA_REG_LBA0   3
#define ATA_REG_LBA1   4
#define ATA_REG_LBA2   5
#define ATA_REG_DRV    6
#define ATA_REG_STAT   7

#define ATA_SR_BSY  0x80
#define ATA_SR_DRQ  0x08
#define ATA_SR_DF   0x20
#define ATA_SR_ERR  0x01

#define ATA_CMD_IDENTIFY  0xEC
#define ATA_CMD_READ      0x20
#define ATA_CMD_WRITE     0x30
#define ATA_CMD_FLUSH     0xE7

#define ATA_POLL_TIMEOUT 200000

typedef struct {
    uint16_t io;
    uint16_t ctrl;
} ata_chan_t;

static const ata_chan_t ata_chans[2] = {
    { 0x1F0, 0x3F6 },
    { 0x170, 0x376 }
};

static ata_drive_t ata_drives[ATA_MAX_DRIVES];
static int ata_count = 0;

static uint8_t ata_status(const ata_drive_t* d) {
    return inb(d->io_base + ATA_REG_STAT);
}

static void ata_io_delay(const ata_chan_t* c) {
    for (int i = 0; i < 4; i++) (void)inb(c->ctrl);
}

static bool ata_wait_ready(const ata_drive_t* d) {
    for (int i = 0; i < ATA_POLL_TIMEOUT; i++) {
        uint8_t st = ata_status(d);
        if (!(st & ATA_SR_BSY)) return true;
    }
    return false;
}

static bool ata_wait_drq(const ata_drive_t* d) {
    for (int i = 0; i < ATA_POLL_TIMEOUT; i++) {
        uint8_t st = ata_status(d);
        if (st & ATA_SR_BSY)
            continue;
        if (st & ATA_SR_ERR || st & ATA_SR_DF) return false;
        if (st & ATA_SR_DRQ) return true;
    }
    return false;
}

static void ata_probe_slot(int chan, bool master) {
    if (ata_count >= ATA_MAX_DRIVES) return;
    const ata_chan_t* c = &ata_chans[chan];

    outb(c->ctrl, 0);
    outb(c->io + ATA_REG_DRV, master ? 0xA0 : 0xB0);
    ata_io_delay(c);

    outb(c->io + ATA_REG_SECCNT, 0);
    outb(c->io + ATA_REG_LBA0, 0);
    outb(c->io + ATA_REG_LBA1, 0);
    outb(c->io + ATA_REG_LBA2, 0);
    outb(c->io + ATA_REG_STAT, ATA_CMD_IDENTIFY);

    uint8_t st = inb(c->io + ATA_REG_STAT);
    if (st == 0) return;
    if (st == 0xFF) return;

    for (int i = 0; i < ATA_POLL_TIMEOUT; i++) {
        st = inb(c->io + ATA_REG_STAT);
        if (!(st & ATA_SR_BSY)) break;
    }

    if (st == 0 || st == 0xFF || (st & (ATA_SR_BSY|ATA_SR_DF|ATA_SR_ERR))) return;

    if (!(st & ATA_SR_DRQ)) return;

    static uint16_t id[256];
    for (int i = 0; i < 256; i++) id[i] = inw(c->io + ATA_REG_DATA);

    for (unsigned i=0;i<ATA_POLL_TIMEOUT;i++) {
        st=inb(c->io+ATA_REG_STAT);
        if(!(st&(ATA_SR_BSY|ATA_SR_DRQ)))break;
    }
    if(!(st&0x40) || (st&(ATA_SR_BSY|ATA_SR_DRQ|ATA_SR_ERR|ATA_SR_DF)))return;
    ata_drive_t *d=&ata_drives[ata_count];
    if(!ata_decode(id,d))return;
    d->present=true;d->master=master;d->io_base=c->io;d->ctrl_base=c->ctrl;
    ata_count++;
}

static unsigned ahci_count;
const char *ata_scan_status(void) {
    if(ahci_count && !ata_count)return "AHCI controller present; no verified SATA disks.";
    return ata_count?"Verified ATA IDENTIFY responses.":"No verified ATA disks.";
}
void ata_number(uint64_t v,char *out){
    char rev[21];unsigned n=0;
    do{rev[n++]='0'+v%10;v/=10;}while(v);
    while(n)*out++=rev[--n];*out=0;
}
void ata_init(void) {
    if (ahci_count) return; /* active port DMA state is boot-scoped */
    ata_count=0; ahci_count=0;
    memset(ata_drives,0,sizeof ata_drives);
    unsigned channels=0,mass=0;
    for(unsigned bus=0;bus<256;bus++)for(unsigned slot=0;slot<32;slot++){
        if((pci_config_read(bus,slot,0,0)&0xffff)==0xffff)continue;
        unsigned funcs=(pci_config_read(bus,slot,0,12)&0x800000)?8:1;
        for(unsigned f=0;f<funcs;f++){
            uint32_t cc=pci_config_read(bus,slot,f,8);
            if((cc>>24)!=1)continue;
            mass++;
            if((cc>>8)==0x010601){ahci_count++; ata_count+=ahci_probe(bus,slot,f,ata_drives+ata_count,ATA_MAX_DRIVES-ata_count); continue;}
            if((cc>>16)!=0x0101 || !(pci_config_read(bus,slot,f,4)&1))continue;
            if(!(cc&0x0100))channels|=1;
            if(!(cc&0x0400))channels|=2;
        }
    }
    if(!mass)channels=3;
    for(int c=0;c<2;c++)if(channels&(1u<<c)){
        ata_probe_slot(c,true);ata_probe_slot(c,false);
    }
    serial_puts("[disks] verified=");serial_putdec(ata_count);serial_puts(" ahci=");serial_putdec(ahci_count);serial_puts("\n");
    for(int i=0;i<ata_count;i++){
        char n[24];ata_number(ata_drives[i].bytes,n);serial_puts("[disk] ");serial_puts(ata_drives[i].model);serial_puts(" bytes=");serial_puts(n);serial_puts("\n");
    }
}
void ata_show_disks(void){
    terminal_writestring(ata_scan_status());terminal_putchar('\n');
    for(int i=0;i<ata_count;i++){
        const ata_drive_t *d=ata_drives+i;char n[24];
        terminal_writestring(d->ahci?"AHCI ":"ATA ");terminal_putchar('0'+i);terminal_writestring(": ");terminal_writestring(d->model);terminal_writestring("  ");ata_number(d->bytes,n);terminal_writestring(n);terminal_writestring(" bytes; ");terminal_writestring(d->install_supported?(d->ahci?"SATA DMA48":"512-byte LBA28 I/O"):"inventory only");terminal_putchar('\n');
    }
}

static void add_text(char **end,const char *s){while(*s)*(*end)++=*s++;**end=0;}
void ata_report_files(void){
    struct fs_node *sys=find_node(root,"System");
    if(!sys || sys->type!=FS_DIRECTORY)return;
    for(int list=0;list<2;list++){
        struct fs_node *f=find_node(sys,list?"devices":"partitions");
        if(!f || f->type!=FS_FILE || f->disk_backed)continue;
        char text[1024],*end=text,num[24];
        add_text(&end,list?"System memory: RAM\nTerminal: tty0\nInput: PS/2 keyboard and mouse\n":"Disk inventory (whole devices, not a partition table):\n");
        add_text(&end,ata_scan_status());add_text(&end,"\n");
        for(int i=0;i<ata_count;i++){
            add_text(&end,ata_drives[i].ahci?"AHCI ":"ATA ");*end++='0'+i;add_text(&end,": ");add_text(&end,ata_drives[i].model);add_text(&end,"  ");
            ata_number(ata_drives[i].bytes,num);add_text(&end,num);add_text(&end," bytes\n");
        }
        uint32_t len=end-text;
        if(!fs_node_write(f,0,text,len))fs_node_truncate(f,len);
    }
}

int ata_drive_count(void) {
    return ata_count;
}

const ata_drive_t* ata_drive_get(int idx) {
    if (idx < 0 || idx >= ata_count) return NULL;
    return &ata_drives[idx];
}

static bool ata_transfer(int drive, uint64_t lba, uint8_t count,
                         uint8_t* buf, bool write) {
    const ata_drive_t* d = ata_drive_get(drive);
    if (!d || !d->install_supported || count == 0) return false;
    if(d->ahci)return ahci_io(d,lba,count,buf,write);
    if (lba >= (1u << 28) || count > (1u << 28) - lba || lba >= d->sectors || count > d->sectors - lba)
        return false;

    if (!ata_wait_ready(d)) return false;

    outb(d->io_base + ATA_REG_DRV,
         (uint8_t)(0xE0 | (d->master ? 0x00 : 0x10) | ((lba >> 24) & 0x0F)));
    ata_io_delay(&ata_chans[d->io_base == ata_chans[0].io ? 0 : 1]);
    if (!ata_wait_ready(d)) return false;
    outb(d->io_base + ATA_REG_ERR, 0);
    outb(d->io_base + ATA_REG_SECCNT, count);
    outb(d->io_base + ATA_REG_LBA0, (uint8_t)(lba & 0xFF));
    outb(d->io_base + ATA_REG_LBA1, (uint8_t)((lba >> 8) & 0xFF));
    outb(d->io_base + ATA_REG_LBA2, (uint8_t)((lba >> 16) & 0xFF));
    outb(d->io_base + ATA_REG_STAT, write ? ATA_CMD_WRITE : ATA_CMD_READ);

    uint16_t* w = (uint16_t*)buf;
    for (int s = 0; s < count; s++) {
        if (!ata_wait_drq(d)) return false;
        if (write) {
            const uint16_t *src = w;
            size_t words = 256;
            asm volatile("rep outsw" : "+S"(src), "+c"(words) : "d"(d->io_base) : "memory");
        } else {
            uint16_t *dst = w;
            size_t words = 256;
            asm volatile("rep insw" : "+D"(dst), "+c"(words) : "d"(d->io_base) : "memory");
        }
        w += 256;
        ata_io_delay(&ata_chans[d->io_base == ata_chans[0].io ? 0 : 1]);
    }


    {
        if (!ata_wait_ready(d))
            return false;
        uint8_t st = ata_status(d);
        if (st & (ATA_SR_ERR | ATA_SR_DF)) return false;
    }

    if (write) {
        outb(d->io_base + ATA_REG_STAT, ATA_CMD_FLUSH);
        ata_io_delay(&ata_chans[d->io_base == ata_chans[0].io ? 0 : 1]);
        if (!ata_wait_ready(d) || (ata_status(d) & (ATA_SR_ERR | ATA_SR_DF)))
            return false;
    }
    return true;
}

bool ata_read_sectors(int drive, uint64_t lba, uint8_t count, uint8_t* dst) {
    return ata_transfer(drive, lba, count, dst, false);
}

bool ata_write_sectors(int drive, uint64_t lba, uint8_t count,
                       const uint8_t* src) {
    return ata_transfer(drive, lba, count, (uint8_t*)src, true);
}
