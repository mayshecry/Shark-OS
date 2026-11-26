/* Polled, single-command AHCI for x86 identity-mapped kernels. No hotplug,
 * NCQ or ATAPI. DMA uses dedicated low physical memory, not caller pointers. */
#include "kernel.h"
#include "ata.h"
#include "ata_identify.h"
#include "ahci.h"

#define BIT(n) (1u << (n))
#define WAIT 16000000u
#define ST BIT(0)
#define FRE BIT(4)
#define FR BIT(14)
#define CR BIT(15)
#define ERROR_MASK 0x7d000000u

enum { CLB=0, CLBU, FB, FBU, IS, IE, CMD, TFD=8, SIG, SSTS, SCTL, SERR, SACT, CI };
struct dma_area {
    uint32_t list[256];
    uint8_t fis[256];
    uint8_t table[256];
    uint8_t data[128*512];
} __attribute__((aligned(1024)));
struct port {
    volatile uint32_t *r;
    bool failed;
};
static struct dma_area dma[ATA_MAX_DRIVES];
static struct port ports[ATA_MAX_DRIVES];
static unsigned claimed;

static bool clear(volatile uint32_t *reg, uint32_t mask) {
    for (unsigned i=0; i<WAIT; ++i) {
        if (!(*reg & mask)) return true;
        asm volatile("pause");
    }
    return false;
}
static bool ready(volatile uint32_t *p) { return (p[SSTS] & 0xf0f) == 0x103; }
static bool issue(unsigned n, unsigned op, uint64_t lba, unsigned sectors,
                  unsigned bytes, bool write) {
    struct port *port=&ports[n]; volatile uint32_t *p=port->r;
    struct dma_area *a=&dma[n]; uint32_t *h=a->list, *prd=(uint32_t *)(a->table+128);
    if (port->failed || !ready(p) || p[CI] || p[SACT] || !clear(p+TFD,0x88)) goto fail;
    memset(a->table,0,sizeof a->table); memset(h,0,32);
    h[0]=5 | (write?BIT(6):0) | (bytes?BIT(16):0);
    h[2]=(uint32_t)(uintptr_t)a->table;
    uint8_t *f=a->table; f[0]=0x27; f[1]=0x80; f[2]=op;
    f[4]=lba; f[5]=lba>>8; f[6]=lba>>16; f[7]=0x40;
    f[8]=lba>>24; f[9]=lba>>32; f[10]=lba>>40; f[12]=sectors;
    if(bytes) { prd[0]=(uint32_t)(uintptr_t)a->data; prd[3]=bytes-1; }
    p[IS]=~0u; p[SERR]=~0u; __sync_synchronize(); p[CI]=1;
    for(unsigned i=0;i<WAIT;i++) {
        if(p[IS]&ERROR_MASK) goto fail;
        if(!(p[CI]&1)) {
            __sync_synchronize();
            if(!ready(p) || (p[IS]&ERROR_MASK) || (p[TFD]&0xa9) || ((volatile uint32_t*)h)[1]!=bytes) goto fail;
            return true;
        }
        asm volatile("pause");
    }
fail:
    port->failed=true; /* never reuse DMA storage after an uncertain completion */
    return false;
}
int ahci_probe(uint8_t bus,uint8_t slot,uint8_t fn,ata_drive_t *out,int room) {
    uint32_t bar=pci_config_read(bus,slot,fn,0x24);
    if (!bar || bar==~0u || (bar&7) || (bar&~15u)>0xffffe000u) return 0;
    pci_config_write(bus,slot,fn,4,(pci_config_read(bus,slot,fn,4)&0xffff)|6);
    volatile uint32_t *h=(void *)(uintptr_t)(bar&~15u);
    if(!h || h[0]==~0u || !h[4] || h[4]==~0u) return 0;
    if(h[9]&1) { h[10]|=BIT(1); if(!clear(h+10,BIT(0)|BIT(4))) return 0; }
    h[1]=(h[1]|BIT(31)) & ~BIT(1); /* AHCI enable, polled interrupts */
    if(!(h[1]&BIT(31)))return 0;
    unsigned pi=h[3]; int found=0;
    for(unsigned i=0;i<32 && found<room && claimed<ATA_MAX_DRIVES;i++) {
        if(!(pi&BIT(i)))continue;
        volatile uint32_t *p=h+(0x100+i*0x80)/4;
        if(!ready(p) || p[SIG]!=0x101 || p[SACT] || p[CI])continue;
        p[IE]=0; p[CMD]&=~ST; if(!clear(p+CMD,CR)) continue;
        p[CMD]&=~FRE; if(!clear(p+CMD,FR))continue;
        unsigned n=claimed++; struct dma_area *a=&dma[n];
        memset(a,0,sizeof *a); ports[n].r=p;
        p[CLB]=(uint32_t)(uintptr_t)a->list;p[CLBU]=0;
        p[FB]=(uint32_t)(uintptr_t)a->fis;p[FBU]=0;
        p[SERR]=~0u;p[IS]=~0u;p[CMD]|=FRE;p[CMD]|=ST;
        if(!issue(n,0xec,0,0,512,false)) continue;
        const uint16_t *id=(void*)a->data; ata_drive_t *d=out+found;
        if(!ata_decode(id,d))continue;
        d->present=true; d->ahci=true; d->ahci_unit=n; d->port=i;
        /* DMA, LBA48 and a supported cache flush command are required. */
        d->flush_cmd=(id[83]&BIT(13))?0xea:0xe7;
        d->install_supported=d->sector_bytes==512 && (id[49]&BIT(8)) &&
            (id[83]&0xc400)==0x4400 && (id[83]&(BIT(12)|BIT(13)));
        found++;
    }
    return found;
}
bool ahci_io(const ata_drive_t *d,uint64_t lba,uint8_t count,uint8_t *buf,bool write) {
    unsigned n=d->ahci_unit;
    if(n>=claimed || ports[n].failed || !d->install_supported || !count || lba>=d->sectors || count>d->sectors-lba)return false;
    while(count){
        unsigned k=count>128?128:count, bytes=k*512;
        if(write)memcpy(dma[n].data,buf,bytes);
        if(!issue(n,write?0x35:0x25,lba,k,bytes,write))return false;
        if(!write)memcpy(buf,dma[n].data,bytes);
        lba+=k; count-=k; buf+=bytes;
    }
    return !write || issue(n,d->flush_cmd,0,0,0,false);
}
