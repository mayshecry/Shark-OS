/* net.c - SharkOS network drivers (RTL8139 / AMD PCnet) and TCP/IP stack.
 *
 * Fixed in this revision:
 *
 * RTL8139
 *  - RCR was 0x0000, so the chip accepted no frames at all. It is now
 *    0x0000000F (accept physical + multicast + broadcast).
 *  - ISR is cleared and CAPR initialised at startup.
 *  - RX frames that wrap around the 8K ring are copied with wraparound
 *    instead of being read raw (previously corrupt whenever a frame
 *    straddled the end of the buffer).
 *  - TX now bounces through an aligned buffer; the old code handed the
 *    chip arbitrary (possibly unaligned, stack) addresses.
 *  - Link state is read from the Media Status register instead of being
 *    assumed up, and RX overruns re-arm the ring.
 *
 * AMD PCnet
 *  - Descriptor BCNT fields are now stored as the two's-complement byte
 *    count the hardware requires. The old code wrote the raw length on TX
 *    and zero on RX, so the NIC saw garbage on transmit and zero-length
 *    receive buffers.
 *  - The init block now encodes the ring sizes (log2 3 = 8 entries).
 *  - BCR88 selects 32-bit software style explicitly instead of relying on
 *    autoselect.
 *  - CSR4 gets APAD_XMT so short frames (ARP, 42 bytes) are padded to the
 *    64-byte Ethernet minimum instead of being dropped as runts.
 *
 * Stack
 *  - snprintf: the hand-rolled varargs reader skipped the first argument;
 *    replaced with a __builtin_va_list implementation (%s %d %u %%).
 *  - DHCP: OFFER (type 2) is now handled and followed by a REQUEST; the old
 *    code only looked for ACK so configuration never completed.
 *  - DHCP option walk no longer reads a length byte for pad (0) options.
 *  - DNS: QTYPE/QCLASS are written as proper 16-bit fields (were 8-bit),
 *    which made every query malformed.
 *  - TCP: the reply to SYN|ACK is now ACK (0x10); the old code answered with
 *    SYN|ACK (0x12) so handshakes never completed.
 *  - Local-subnet targets are ARP-resolved directly instead of always going
 *    through the gateway.
 *  - wget/http no longer stack two 8 KB buffers on the 8 KB kernel stack.
 *  - The host now answers ARP requests for its own address.
 */

#include "kernel.h"
#include "net.h"
#include "tls.h"
#include <stdint.h>

uint8_t net_mac[6] = {0,0,0,0,0,0};       /* filled in by the driver */
int net_has_link = 0;
char net_driver_name[32] = "none";

uint8_t net_ip[4] = {0,0,0,0};
uint8_t net_mask[4] = {0,0,0,0};
uint8_t net_gw[4] = {0,0,0,0};
uint8_t net_dns[4] = {0,0,0,0};
int net_configured = 0;

static net_rx_cb_t net_rx_cb = NULL;

/* ------------------------------------------------------------------ printf */

/* Minimal formatted printer. The previous version walked the stack by hand
 * and skipped the first variadic argument, so "GET %s ... Host: %s" produced
 * a garbage Host header. */
static int net_snprintf(char* buf, int size, const char* fmt, ...) {
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    int pos = 0;

    for (int i = 0; fmt[i] && pos < size - 1; i++) {
        if (fmt[i] != '%') { buf[pos++] = fmt[i]; continue; }
        i++;
        if (fmt[i] == 's') {
            const char* s = __builtin_va_arg(ap, const char*);
            if (!s) s = "(null)";
            for (int j = 0; s[j] && pos < size - 1; j++) buf[pos++] = s[j];
        } else if (fmt[i] == 'd' || fmt[i] == 'u') {
            uint32_t v;
            int neg = 0;
            if (fmt[i] == 'd') {
                int d = __builtin_va_arg(ap, int);
                if (d < 0) { neg = 1; v = (uint32_t)(-d); }
                else v = (uint32_t)d;
            } else {
                v = __builtin_va_arg(ap, uint32_t);
            }
            char tmp[12];
            int n = 0;
            if (v == 0) tmp[n++] = '0';
            while (v) { tmp[n++] = (char)('0' + v % 10); v /= 10; }
            if (neg && pos < size - 1) buf[pos++] = '-';
            while (n-- && pos < size - 1) buf[pos++] = tmp[n];
        } else if (fmt[i] == '%') {
            buf[pos++] = '%';
        } else if (fmt[i] == '\0') {
            break;
        } else {
            buf[pos++] = fmt[i];
        }
    }
    buf[pos] = '\0';
    __builtin_va_end(ap);
    return pos;
}

/* ------------------------------------------------------------------ consts */

#define ETH_IP   0x0800
#define ETH_ARP  0x0806
#define IP_ICMP  1
#define IP_TCP   6
#define IP_UDP   17
#define ARP_REQ  1
#define ARP_REP  2

/* ================================================================ RTL8139 */

static uint32_t rtl_base = 0;
static uint8_t* rtl_rx_buf = NULL;
static uint32_t rtl_rx_pos = 0;
static uint32_t rtl_tx_next = 0;
static bool rtl_present = false;
#define RTL_RX_SIZE 8192
#define RTL_TX_N 4

/* Aligned bounce buffer: the TSAD register requires DWORD alignment and the
 * old code passed it stack arrays of arbitrary alignment. */
static uint8_t rtl_tx_buf[1800] __attribute__((aligned(16)));

/* RTL8139 register map (offsets from BAR0). Keeping the names next to the
 * numbers matters: the old driver wrote the receive configuration to 0x40
 * (TCR) and zero to 0x44 (RCR), so the chip was told to accept no frames at
 * all and DHCP never saw an OFFER. */
#define RTL_IDR0    0x00
#define RTL_TSD0    0x10
#define RTL_TSAD0   0x20
#define RTL_RBSTART 0x30
#define RTL_CR      0x37
#define RTL_CAPR    0x38
#define RTL_CBR     0x3A
#define RTL_IMR     0x3C
#define RTL_ISR     0x3E
#define RTL_TCR     0x40
#define RTL_RCR     0x44
#define RTL_9346CR  0x50
#define RTL_CONFIG1 0x52
#define RTL_MSR     0x58
#define RTL_BMCR    0x62
#define RTL_BMSR    0x64

#define RTL_CR_RST  0x10
#define RTL_CR_RE   0x08
#define RTL_CR_TE   0x04
#define RTL_CR_BUFE 0x01                /* RX buffer empty */

#define RTL_ISR_ROK  0x0001
#define RTL_ISR_RER  0x0002
#define RTL_ISR_TOK  0x0004
#define RTL_ISR_TER  0x0008
#define RTL_ISR_RXOVW 0x0010
#define RTL_ISR_FOVW  0x0040

/* RCR: accept broadcast | multicast | physical match, WRAP (bit 7) so a frame
 * that reaches the end of the ring continues into the 16-byte slack instead
 * of wrapping mid-frame, 8K ring (RBLEN=00), unlimited DMA burst (MXDMA=111)
 * and no early RX threshold (RXFTH=111 = none). */
#define RTL_RCR_VALUE (0x0000000Eu | (1u << 7) | (7u << 8) | (7u << 13))
/* TCR: max DMA burst 2048 (MXDMA=111), standard IFG. */
#define RTL_TCR_VALUE ((7u << 8) | (3u << 24))

static void rtl_reset(void) {
    /* Wake the chip (power management may have it asleep on real hardware),
     * then issue a software reset and WAIT for RST to clear instead of
     * sleeping a fixed 100 ms and hoping. */
    outb(rtl_base + RTL_CONFIG1, 0x00);
    outb(rtl_base + RTL_CR, RTL_CR_RST);
    for (int i = 0; i < 1000; i++) {
        if ((inb(rtl_base + RTL_CR) & RTL_CR_RST) == 0) break;
        delay_ms(1);
    }
}

static void rtl_read_link(void) {
    /* MSR bit 2 (LINKB) is 1 when the link is DOWN. */
    uint8_t msr = inb(rtl_base + RTL_MSR);
    net_has_link = (msr & 0x04) ? 0 : 1;
}

static void rtl_init_chip(void) {
    /* 8 KB ring + 16 bytes of slack for WRAP + one extra frame so a
     * wrapped frame can never run off the end of the allocation. */
    rtl_rx_buf = (uint8_t*)kmalloc(RTL_RX_SIZE + 16 + 1536);
    memset(rtl_rx_buf, 0, RTL_RX_SIZE + 16 + 1536);
    rtl_rx_pos = 0;
    rtl_tx_next = 0;

    for (int i = 0; i < 6; i++) net_mac[i] = inb(rtl_base + RTL_IDR0 + i);

    outl(rtl_base + RTL_RBSTART, (uint32_t)virt_to_phys(rtl_rx_buf));

    /* Enable RX/TX *before* configuring RCR/TCR: on the real chip those
     * registers are write-protected while the DMA engines are off. */
    outb(rtl_base + RTL_CR, RTL_CR_RE | RTL_CR_TE);
    outl(rtl_base + RTL_RCR, RTL_RCR_VALUE);
    outl(rtl_base + RTL_TCR, RTL_TCR_VALUE);

    /* Clean interrupt state. We poll, so IMR stays 0: no shared-IRQ
     * storms with the mouse on IRQ12/IRQ11 boards. */
    outw(rtl_base + RTL_ISR, 0xFFFF);
    outw(rtl_base + RTL_IMR, 0);
    outw(rtl_base + RTL_CAPR, (uint16_t)(0 - 16));

    /* No blocking wait for auto-negotiation here: net_poll() re-reads the
     * link every 10 ms and the DHCP client starts as soon as it comes up. */
    rtl_read_link();
}

static void rtl_send(const uint8_t* data, uint32_t len) {
    if (len > 1792) len = 1792;
    uint32_t tsd = rtl_base + RTL_TSD0 + rtl_tx_next * 4;

    /* TSD.OWN (bit 13) is 1 while the descriptor belongs to us. If the chip
     * is stuck on it (aborted transmit, no carrier) give up after 20 ms
     * rather than hanging the desktop. */
    for (int i = 0; i < 20 && !(inl(tsd) & 0x2000); i++) delay_ms(1);

    memcpy(rtl_tx_buf, data, len);
    /* The 8139 does not pad: bring short frames up to the Ethernet minimum. */
    while (len < 60) rtl_tx_buf[len++] = 0;

    outl(rtl_base + RTL_TSAD0 + rtl_tx_next * 4, (uint32_t)virt_to_phys(rtl_tx_buf));
    outl(tsd, len & 0x1FFF);                /* clears OWN and starts the DMA */
    rtl_tx_next = (rtl_tx_next + 1) % RTL_TX_N;

    /* One shared bounce buffer: wait until the chip has pulled the frame into
     * its FIFO (OWN back to 1) before the caller can reuse it. Microseconds
     * on hardware, immediate on QEMU. */
    for (int i = 0; i < 100000 && !(inl(tsd) & 0x2000); i++) { }
}

static void rtl_poll(void) {
    /* The ISR is advisory: drain the ring whenever BUFE is clear so we never
     * miss a frame that arrived between an ISR read and its acknowledge. */
    uint16_t isr = inw(rtl_base + RTL_ISR);
    if (isr) outw(rtl_base + RTL_ISR, isr);   /* write-1-to-clear */

    if (isr & (RTL_ISR_RXOVW | RTL_ISR_FOVW)) {
        /* Overrun: the ring pointers are unreliable, restart RX cleanly. */
        outb(rtl_base + RTL_CR, RTL_CR_TE);
        outb(rtl_base + RTL_CR, RTL_CR_RE | RTL_CR_TE);
        outl(rtl_base + RTL_RCR, RTL_RCR_VALUE);
        rtl_rx_pos = 0;
        outw(rtl_base + RTL_CAPR, (uint16_t)(0 - 16));
        return;
    }

    int guard = 0;
    while (!(inb(rtl_base + RTL_CR) & RTL_CR_BUFE) && guard++ < 64) {
        /* With WRAP set the chip writes each frame contiguously, so the
         * header at rx_pos is never split. */
        const uint8_t* hdr = rtl_rx_buf + rtl_rx_pos;
        uint16_t status = (uint16_t)(hdr[0] | (hdr[1] << 8));
        uint16_t flen   = (uint16_t)(hdr[2] | (hdr[3] << 8));

        if (flen == 0xFFF0) break;            /* chip still DMA-ing this one */

        /* ROK and a sane length; anything else (RUNT/LONG/CRC/FAE) is skipped
         * but the pointer must still advance or the ring jams for ever. */
        if ((status & 0x0001) && flen >= 4 + 14 && flen <= 4 + 1514) {
            uint32_t framelen = flen - 4;     /* strip CRC */
            if (net_rx_cb) net_rx_cb(hdr + 4, framelen);
        } else if (flen < 4 || flen > 4 + 1514) {
            /* Garbage header: the only safe recovery is a ring reset. */
            outb(rtl_base + RTL_CR, RTL_CR_TE);
            outb(rtl_base + RTL_CR, RTL_CR_RE | RTL_CR_TE);
            outl(rtl_base + RTL_RCR, RTL_RCR_VALUE);
            rtl_rx_pos = 0;
            outw(rtl_base + RTL_CAPR, (uint16_t)(0 - 16));
            return;
        }

        rtl_rx_pos = (rtl_rx_pos + 4 + flen + 3) & ~3u;
        if (rtl_rx_pos >= RTL_RX_SIZE) rtl_rx_pos -= RTL_RX_SIZE;
        /* CAPR lags the read pointer by 16 bytes by hardware convention. */
        outw(rtl_base + RTL_CAPR, (uint16_t)(rtl_rx_pos - 16));
    }

    rtl_read_link();
}

/* ================================================================== PCnet */

/* AMD PCnet-PCI II / FAST III (Am79C970A/973/975): VirtualBox's default NIC.
 *
 * The previous driver never exchanged a single frame:
 *  - it used RAP=0x10 / RDP=0x0C / RESET=0x14; 0x0C is inside the MAC PROM,
 *    so every "CSR read" returned PROM bytes (0x57570201) and every CSR
 *    write was lost. The IDON wait therefore spun for its full 10 s at boot;
 *  - the init block put the ring-length code into address bits 13-15;
 *  - descriptors were laid out 32-bit style while the chip was left in its
 *    16-bit power-on software style.
 * This version switches the chip to DWIO + SWSTYLE 2 (32-bit) and uses the
 * matching 28-byte init block and 16-byte descriptors. */

#define PN_RDP      0x10      /* DWIO offsets */
#define PN_RAP      0x14
#define PN_RESET    0x18
#define PN_BDP      0x1C
#define PN_WIO_RESET 0x14     /* 16-bit mode reset port */

#define CSR0_INIT   0x0001
#define CSR0_STRT   0x0002
#define CSR0_STOP   0x0004
#define CSR0_TDMD   0x0008
#define CSR0_IENA   0x0040
#define CSR0_IDON   0x0100
#define CSR0_TINT   0x0200
#define CSR0_RINT   0x0400
#define CSR0_MISS   0x1000

#define PD_OWN      0x80000000u
#define PD_ERR      0x40000000u
#define PD_STP      0x02000000u
#define PD_ENP      0x01000000u
#define PD_ONES     0x0000F000u
#define PD_BCNT(n)  (((uint32_t)(-(int32_t)(n))) & 0x0FFFu)

#define PN_TX  8
#define PN_RX  16
#define PN_BUF 1536
#define PN_LOG2_TX 3
#define PN_LOG2_RX 4

struct pn_desc {
    uint32_t addr;
    uint32_t status;      /* OWN | flags | ONES | BCNT */
    uint32_t misc;        /* RX: MCNT in low 12 bits */
    uint32_t user;
} __attribute__((packed));

struct pn_initblk {
    uint16_t mode;
    uint8_t  rlen;        /* log2(ring size) in the upper nibble */
    uint8_t  tlen;
    uint8_t  padr[6];
    uint16_t reserved;
    uint8_t  ladrf[8];
    uint32_t rdra;
    uint32_t tdra;
} __attribute__((packed));

static uint32_t  pcnet_io = 0;
static uintptr_t pcnet_mmio = 0;
static bool      pcnet_present = false;
static struct pn_desc* pn_tx = NULL;
static struct pn_desc* pn_rx = NULL;
static uint8_t*  pn_txb[PN_TX];
static uint8_t*  pn_rxb[PN_RX];
static struct pn_initblk* pn_ib = NULL;
static int pn_rx_next = 0;
static int pn_tx_next = 0;

static void pn_w32(uint32_t off, uint32_t v) {
    if (pcnet_mmio) *(volatile uint32_t*)(pcnet_mmio + off) = v;
    else outl(pcnet_io + off, v);
}
static uint32_t pn_r32(uint32_t off) {
    if (pcnet_mmio) return *(volatile uint32_t*)(pcnet_mmio + off);
    return inl(pcnet_io + off);
}
static void     pn_csrw(int c, uint32_t v) { pn_w32(PN_RAP, (uint32_t)c); pn_w32(PN_RDP, v); }
static uint32_t pn_csrr(int c)             { pn_w32(PN_RAP, (uint32_t)c); return pn_r32(PN_RDP) & 0xFFFF; }
static void     pn_bcrw(int c, uint32_t v) { pn_w32(PN_RAP, (uint32_t)c); pn_w32(PN_BDP, v); }
static uint32_t pn_bcrr(int c)             { pn_w32(PN_RAP, (uint32_t)c); return pn_r32(PN_BDP) & 0xFFFF; }

static void* pn_alloc16(uint32_t size) {
    uint8_t* m = (uint8_t*)kmalloc(size + 16);
    if (!m) return NULL;
    uintptr_t a = ((uintptr_t)m + 15) & ~(uintptr_t)15;
    memset((void*)a, 0, size);
    return (void*)a;
}

static void pn_read_link(void) {
    /* BCR4 = LED0 = link status by default (LNKSTE, bit 6); bit 15 mirrors
     * the LED. If someone reprogrammed the LED just assume the link is up. */
    uint32_t bcr4 = pn_bcrr(4);
    net_has_link = (bcr4 & 0x0040) ? ((bcr4 & 0x8000) ? 1 : 0) : 1;
}

static void pn_init_chip(void) {
    /* Reset works in whichever I/O mode the chip is in: a 32-bit read of
     * 0x18 (DWIO) or a 16-bit read of 0x14 (WIO). Do both. */
    (void)pn_r32(PN_RESET);
    if (pcnet_mmio) (void)*(volatile uint16_t*)(pcnet_mmio + PN_WIO_RESET);
    else (void)inw(pcnet_io + PN_WIO_RESET);
    delay_ms(2);

    /* A 32-bit write to RDP while in WIO mode switches the chip to DWIO. */
    pn_w32(PN_RDP, 0);

    /* MAC from the address PROM (bytes 0-5 of the BAR). */
    for (int i = 0; i < 6; i++) {
        if (pcnet_mmio) net_mac[i] = *(volatile uint8_t*)(pcnet_mmio + i);
        else net_mac[i] = inb(pcnet_io + i);
    }

    /* SWSTYLE 2: PCnet-PCI 32-bit descriptors and init block. */
    pn_bcrw(20, 0x0002);

    pn_ib = (struct pn_initblk*)pn_alloc16(sizeof(*pn_ib));
    pn_tx = (struct pn_desc*)pn_alloc16(sizeof(struct pn_desc) * PN_TX);
    pn_rx = (struct pn_desc*)pn_alloc16(sizeof(struct pn_desc) * PN_RX);
    if (!pn_ib || !pn_tx || !pn_rx) { pcnet_present = false; return; }

    for (int i = 0; i < PN_TX; i++) {
        pn_txb[i] = (uint8_t*)pn_alloc16(PN_BUF);
        pn_tx[i].addr = (uint32_t)virt_to_phys(pn_txb[i]);
        pn_tx[i].status = PD_ONES;                /* host owns, idle */
        pn_tx[i].misc = 0;
    }
    for (int i = 0; i < PN_RX; i++) {
        pn_rxb[i] = (uint8_t*)pn_alloc16(PN_BUF);
        pn_rx[i].addr = (uint32_t)virt_to_phys(pn_rxb[i]);
        pn_rx[i].status = PD_OWN | PD_ONES | PD_BCNT(PN_BUF);   /* NIC owns */
        pn_rx[i].misc = 0;
    }
    pn_rx_next = 0;
    pn_tx_next = 0;

    pn_ib->mode = 0;                              /* normal RX/TX, no PROM */
    pn_ib->rlen = (uint8_t)(PN_LOG2_RX << 4);
    pn_ib->tlen = (uint8_t)(PN_LOG2_TX << 4);
    for (int i = 0; i < 6; i++) pn_ib->padr[i] = net_mac[i];
    for (int i = 0; i < 8; i++) pn_ib->ladrf[i] = 0;
    pn_ib->rdra = (uint32_t)virt_to_phys(pn_rx);
    pn_ib->tdra = (uint32_t)virt_to_phys(pn_tx);

    uint32_t ib = (uint32_t)virt_to_phys(pn_ib);
    pn_csrw(1, ib & 0xFFFF);
    pn_csrw(2, ib >> 16);
    /* CSR3: mask every interrupt source; we poll. */
    pn_csrw(3, 0x5F00);
    /* CSR4: APAD_XMT (pad short frames to 64 bytes) + mask the misc
     * interrupt sources (JAB, TXSTRT, RCVCCO, MFCO). */
    pn_csrw(4, 0x0915);

    pn_csrw(0, CSR0_INIT);
    int ok = 0;
    for (int t = 0; t < 1000; t++) {
        if (pn_csrr(0) & CSR0_IDON) { ok = 1; break; }
        delay_ms(1);
    }
    if (!ok) { pcnet_present = false; strcpy(net_driver_name, "AMD PCnet (init failed)"); return; }
    pn_csrw(0, CSR0_IDON | CSR0_STRT);            /* ack IDON, start */
    pn_read_link();
}

static void pn_send(const uint8_t* data, uint32_t len) {
    struct pn_desc* d = &pn_tx[pn_tx_next];
    /* Wait (briefly) for the slot to come back from the chip. */
    for (int i = 0; i < 20 && (d->status & PD_OWN); i++) delay_ms(1);
    if (d->status & PD_OWN) return;               /* TX stuck; drop */
    if (len > PN_BUF) len = PN_BUF;
    memcpy(pn_txb[pn_tx_next], data, len);
    if (len < 60) { memset(pn_txb[pn_tx_next] + len, 0, 60 - len); len = 60; }
    d->misc = 0;
    d->status = PD_OWN | PD_STP | PD_ENP | PD_ONES | PD_BCNT(len);
    pn_csrw(0, CSR0_TDMD);                        /* transmit demand, IENA=0 */
    pn_tx_next = (pn_tx_next + 1) % PN_TX;
}

static void pn_poll(void) {
    /* Acknowledge status bits (write-1-to-clear); never touch STOP/STRT/INIT
     * which are in the low byte. */
    uint32_t csr0 = pn_csrr(0);
    if (csr0 & 0x7F00) pn_csrw(0, csr0 & 0x7F00);

    for (int n = 0; n < PN_RX; n++) {
        struct pn_desc* d = &pn_rx[pn_rx_next];
        if (d->status & PD_OWN) break;            /* nothing more */
        uint32_t st = d->status;
        uint32_t mcnt = d->misc & 0x0FFF;         /* includes 4-byte FCS */
        if (!(st & PD_ERR) && (st & PD_STP) && (st & PD_ENP) && mcnt > 18 && mcnt <= PN_BUF) {
            if (net_rx_cb) net_rx_cb(pn_rxb[pn_rx_next], mcnt - 4);
        }
        d->misc = 0;
        d->status = PD_OWN | PD_ONES | PD_BCNT(PN_BUF);
        pn_rx_next = (pn_rx_next + 1) % PN_RX;
    }

    static uint32_t last_link_tick = 0;
    if (uptime_ticks - last_link_tick >= 500) {   /* twice a second is plenty */
        last_link_tick = uptime_ticks;
        pn_read_link();
    }
}

/* ================================================================== e1000 */

/* Intel 8254x (82540EM "e1000" = QEMU's default NIC, VMware's default, and
 * a large share of 2000s-era desktops/laptops). Legacy descriptors, polled. */

#define E1000_CTRL     0x0000
#define E1000_STATUS   0x0008
#define E1000_EERD     0x0014
#define E1000_ICR      0x00C0
#define E1000_IMS      0x00D0
#define E1000_IMC      0x00D8
#define E1000_RCTL     0x0100
#define E1000_TCTL     0x0400
#define E1000_TIPG     0x0410
#define E1000_RDBAL    0x2800
#define E1000_RDBAH    0x2804
#define E1000_RDLEN    0x2808
#define E1000_RDH      0x2810
#define E1000_RDT      0x2818
#define E1000_TDBAL    0x3800
#define E1000_TDBAH    0x3804
#define E1000_TDLEN    0x3808
#define E1000_TDH      0x3810
#define E1000_TDT      0x3818
#define E1000_MTA      0x5200
#define E1000_RAL0     0x5400
#define E1000_RAH0     0x5404

#define E1000_CTRL_ASDE   (1u << 5)
#define E1000_CTRL_SLU    (1u << 6)
#define E1000_CTRL_ILOS   (1u << 7)
#define E1000_CTRL_RST    (1u << 26)
#define E1000_CTRL_PHYRST (1u << 31)
#define E1000_CTRL_LRST   (1u << 3)

#define E1000_RCTL_EN     (1u << 1)
#define E1000_RCTL_BAM    (1u << 15)
#define E1000_RCTL_SECRC  (1u << 26)
#define E1000_TCTL_EN     (1u << 1)
#define E1000_TCTL_PSP    (1u << 3)

#define E1000_NRX 32
#define E1000_NTX 32
#define E1000_BUF 2048

struct e1000_rxd {
    uint32_t addr_lo, addr_hi;
    uint16_t length;
    uint16_t csum;
    uint8_t  status;
    uint8_t  errors;
    uint16_t special;
} __attribute__((packed));

struct e1000_txd {
    uint32_t addr_lo, addr_hi;
    uint16_t length;
    uint8_t  cso;
    uint8_t  cmd;
    uint8_t  status;
    uint8_t  css;
    uint16_t special;
} __attribute__((packed));

static uintptr_t e1000_mmio = 0;
static bool e1000_present = false;
static struct e1000_rxd* e1000_rx = NULL;
static struct e1000_txd* e1000_tx = NULL;
static uint8_t* e1000_rxb[E1000_NRX];
static uint8_t* e1000_txb[E1000_NTX];
static int e1000_rx_head = 0;
static int e1000_tx_tail = 0;

static inline void e1000_w(uint32_t reg, uint32_t v) { *(volatile uint32_t*)(e1000_mmio + reg) = v; }
static inline uint32_t e1000_r(uint32_t reg) { return *(volatile uint32_t*)(e1000_mmio + reg); }

static int e1000_eeprom_read(uint8_t addr, uint16_t* out) {
    /* 82540/82545/82546: START = bit 0, DONE = bit 4, address at bit 8. */
    e1000_w(E1000_EERD, ((uint32_t)addr << 8) | 1);
    for (int i = 0; i < 100000; i++) {
        uint32_t v = e1000_r(E1000_EERD);
        if (v & (1u << 4)) { *out = (uint16_t)(v >> 16); return 1; }
    }
    /* 82541/82547/82574: START = bit 0, DONE = bit 1, address at bit 2. */
    e1000_w(E1000_EERD, ((uint32_t)addr << 2) | 1);
    for (int i = 0; i < 100000; i++) {
        uint32_t v = e1000_r(E1000_EERD);
        if (v & (1u << 1)) { *out = (uint16_t)(v >> 16); return 1; }
    }
    return 0;
}

static void e1000_read_link(void) {
    net_has_link = (e1000_r(E1000_STATUS) & 0x2) ? 1 : 0;      /* LU */
}

static void e1000_init_chip(void) {
    /* Mask interrupts, reset, mask again (reset re-enables nothing, but be
     * explicit), then bring the link up with auto speed detection. */
    e1000_w(E1000_IMC, 0xFFFFFFFFu);
    e1000_w(E1000_CTRL, e1000_r(E1000_CTRL) | E1000_CTRL_RST);
    delay_ms(10);
    for (int i = 0; i < 1000 && (e1000_r(E1000_CTRL) & E1000_CTRL_RST); i++) delay_ms(1);
    e1000_w(E1000_IMC, 0xFFFFFFFFu);
    (void)e1000_r(E1000_ICR);

    uint32_t ctrl = e1000_r(E1000_CTRL);
    ctrl |= E1000_CTRL_SLU | E1000_CTRL_ASDE;
    ctrl &= ~(E1000_CTRL_LRST | E1000_CTRL_ILOS | E1000_CTRL_PHYRST);
    e1000_w(E1000_CTRL, ctrl);

    /* MAC: the receive-address registers are loaded from the EEPROM at
     * power-up on every 8254x; fall back to reading the EEPROM ourselves. */
    uint32_t ral = e1000_r(E1000_RAL0), rah = e1000_r(E1000_RAH0);
    if (ral == 0 && (rah & 0xFFFF) == 0) {
        uint16_t w0, w1, w2;
        if (e1000_eeprom_read(0, &w0) && e1000_eeprom_read(1, &w1) && e1000_eeprom_read(2, &w2)) {
            ral = (uint32_t)w0 | ((uint32_t)w1 << 16);
            rah = w2;
        }
    }
    net_mac[0] = ral & 0xFF; net_mac[1] = (ral >> 8) & 0xFF;
    net_mac[2] = (ral >> 16) & 0xFF; net_mac[3] = (ral >> 24) & 0xFF;
    net_mac[4] = rah & 0xFF; net_mac[5] = (rah >> 8) & 0xFF;
    e1000_w(E1000_RAL0, ral);
    e1000_w(E1000_RAH0, (rah & 0xFFFF) | (1u << 31));            /* AV */
    for (int i = 0; i < 128; i++) e1000_w(E1000_MTA + i * 4, 0);

    /* RX ring */
    e1000_rx = (struct e1000_rxd*)pn_alloc16(sizeof(struct e1000_rxd) * E1000_NRX);
    e1000_tx = (struct e1000_txd*)pn_alloc16(sizeof(struct e1000_txd) * E1000_NTX);
    if (!e1000_rx || !e1000_tx) { e1000_present = false; return; }
    for (int i = 0; i < E1000_NRX; i++) {
        e1000_rxb[i] = (uint8_t*)pn_alloc16(E1000_BUF);
        e1000_rx[i].addr_lo = (uint32_t)virt_to_phys(e1000_rxb[i]);
        e1000_rx[i].addr_hi = 0;
        e1000_rx[i].status = 0;
    }
    e1000_w(E1000_RDBAL, (uint32_t)virt_to_phys(e1000_rx));
    e1000_w(E1000_RDBAH, 0);
    e1000_w(E1000_RDLEN, sizeof(struct e1000_rxd) * E1000_NRX);
    e1000_w(E1000_RDH, 0);
    e1000_w(E1000_RDT, E1000_NRX - 1);
    e1000_rx_head = 0;
    /* EN | BAM | BSIZE=2048 (00) | SECRC; no promiscuous, no long packets. */
    e1000_w(E1000_RCTL, E1000_RCTL_EN | E1000_RCTL_BAM | E1000_RCTL_SECRC);

    /* TX ring */
    for (int i = 0; i < E1000_NTX; i++) {
        e1000_txb[i] = (uint8_t*)pn_alloc16(E1000_BUF);
        e1000_tx[i].addr_lo = (uint32_t)virt_to_phys(e1000_txb[i]);
        e1000_tx[i].addr_hi = 0;
        e1000_tx[i].cmd = 0;
        e1000_tx[i].status = 1;                                   /* DD: free */
    }
    e1000_w(E1000_TDBAL, (uint32_t)virt_to_phys(e1000_tx));
    e1000_w(E1000_TDBAH, 0);
    e1000_w(E1000_TDLEN, sizeof(struct e1000_txd) * E1000_NTX);
    e1000_w(E1000_TDH, 0);
    e1000_w(E1000_TDT, 0);
    e1000_tx_tail = 0;
    /* EN | PSP | CT=15 | COLD=64 (full duplex) */
    e1000_w(E1000_TCTL, E1000_TCTL_EN | E1000_TCTL_PSP | (15u << 4) | (64u << 12));
    e1000_w(E1000_TIPG, 10 | (8u << 10) | (6u << 20));

    e1000_read_link();
}

static void e1000_send(const uint8_t* data, uint32_t len) {
    struct e1000_txd* d = &e1000_tx[e1000_tx_tail];
    for (int i = 0; i < 20 && !(d->status & 1); i++) delay_ms(1);   /* DD */
    if (!(d->status & 1)) return;
    if (len > E1000_BUF) len = E1000_BUF;
    memcpy(e1000_txb[e1000_tx_tail], data, len);
    if (len < 60) { memset(e1000_txb[e1000_tx_tail] + len, 0, 60 - len); len = 60; }
    d->length = (uint16_t)len;
    d->cso = 0; d->css = 0; d->special = 0;
    d->status = 0;
    d->cmd = 0x01 | 0x02 | 0x08;                  /* EOP | IFCS | RS */
    e1000_tx_tail = (e1000_tx_tail + 1) % E1000_NTX;
    e1000_w(E1000_TDT, (uint32_t)e1000_tx_tail);
    /* Wait for the DMA so the caller may reuse its buffer / we ours. */
    for (int i = 0; i < 100000 && !(d->status & 1); i++) { }
}

static void e1000_poll(void) {
    (void)e1000_r(E1000_ICR);                     /* clear any pending cause */
    for (int n = 0; n < E1000_NRX; n++) {
        struct e1000_rxd* d = &e1000_rx[e1000_rx_head];
        if (!(d->status & 1)) break;              /* DD not set: empty */
        uint32_t len = d->length;
        if ((d->status & 2) && !d->errors && len >= 14 && len <= 1514) {   /* EOP */
            if (net_rx_cb) net_rx_cb(e1000_rxb[e1000_rx_head], len);
        }
        d->status = 0;
        e1000_w(E1000_RDT, (uint32_t)e1000_rx_head);   /* give it back */
        e1000_rx_head = (e1000_rx_head + 1) % E1000_NRX;
    }
    static uint32_t last_link_tick = 0;
    if (uptime_ticks - last_link_tick >= 250) {
        last_link_tick = uptime_ticks;
        e1000_read_link();
    }
}

static int e1000_is_supported(uint16_t did) {
    static const uint16_t ids[] = {
        0x100E, 0x100F, 0x1010, 0x1011, 0x1012, 0x1013, 0x1015, 0x1016, 0x1017,
        0x1018, 0x1019, 0x101A, 0x101D, 0x101E, 0x1026, 0x1027, 0x1028, 0x1075,
        0x1076, 0x1077, 0x1078, 0x1079, 0x107A, 0x107B, 0x107C, 0x108A, 0x1099,
        0x10B5, 0x10D3,
    };
    for (unsigned i = 0; i < sizeof(ids) / sizeof(ids[0]); i++) if (ids[i] == did) return 1;
    return 0;
}

/* ============================================================== dispatch */

void net_set_rx_callback(net_rx_cb_t cb) { net_rx_cb = cb; }

void net_send_raw(const uint8_t* data, uint32_t len) {
    if (rtl_present) rtl_send(data, len);
    else if (pcnet_present) pn_send(data, len);
    else if (e1000_present) e1000_send(data, len);
}

static void dhcp_tick(void);

void net_poll(void) {
    if (rtl_present) rtl_poll();
    else if (pcnet_present) pn_poll();
    else if (e1000_present) e1000_poll();
    else return;
    dhcp_tick();
}

int net_has_nic(void) { return rtl_present || pcnet_present || e1000_present; }

void net_init(void) {
    for (uint32_t bus = 0; bus < 256; bus++) {
        for (uint32_t slot = 0; slot < 32; slot++) {
            for (uint32_t func = 0; func < 8; func++) {
                uint32_t d = pci_config_read(bus, slot, func, 0);
                if (d == 0xFFFFFFFF) { if (func == 0) break; else continue; }
                uint16_t vid = d & 0xFFFF, did = (d >> 16) & 0xFFFF;
                uint32_t hdr = pci_config_read(bus, slot, func, 0x0C);

                if (vid == 0x10EC && did == 0x8139) {
                    uint32_t bar0 = pci_config_read(bus, slot, func, 0x10);
                    rtl_base = bar0 & 0xFFFFFFFC;
                    uint32_t cmd = pci_config_read(bus, slot, func, 0x04);
                    cmd |= 0x5;   /* IO space + bus master */
                    pci_config_write(bus, slot, func, 0x04, cmd);
                    strcpy(net_driver_name, "Realtek RTL8139");
                    rtl_present = true;
                    rtl_reset();
                    rtl_init_chip();
                    return;
                }
                if (vid == 0x1022 && (did == 0x2000 || did == 0x2001)) {
                    uint32_t bar0 = pci_config_read(bus, slot, func, 0x10);
                    int isio = (bar0 & 0x1) != 0;
                    uint32_t cmd = pci_config_read(bus, slot, func, 0x04);
                    cmd |= 0x7;   /* IO + memory + bus master */
                    pci_config_write(bus, slot, func, 0x04, cmd);
                    if (isio) { pcnet_io = bar0 & 0xFFFFFFFC; pcnet_mmio = 0; }
                    else { pcnet_mmio = (uintptr_t)(bar0 & 0xFFFFFFF0); pcnet_io = 0; }
                    strcpy(net_driver_name, did == 0x2001 ? "AMD PCnet-Home" : "AMD PCnet-PCI II/FAST III");
                    pcnet_present = true;
                    pn_init_chip();
                    if (pcnet_present) return;
                }
                if (vid == 0x8086 && e1000_is_supported(did)) {
                    uint32_t bar0 = pci_config_read(bus, slot, func, 0x10);
                    if (!(bar0 & 1)) {                      /* must be MMIO */
                        uint32_t cmd = pci_config_read(bus, slot, func, 0x04);
                        cmd |= 0x6;   /* memory + bus master */
                        pci_config_write(bus, slot, func, 0x04, cmd);
                        e1000_mmio = (uintptr_t)(bar0 & 0xFFFFFFF0);
                        strcpy(net_driver_name, did == 0x10D3 ? "Intel 82574L" : "Intel 8254x (e1000)");
                        e1000_present = true;
                        e1000_init_chip();
                        if (e1000_present) return;
                    }
                }
                if (func == 0 && (hdr & 0x00800000) == 0) break;  /* not multi-fn */
            }
        }
    }
    strcpy(net_driver_name, "none");
}

/* ======================================================== stack helpers */

static int my_memcmp(const void* a, const void* b, int n) {
    const unsigned char* x = (const unsigned char*)a;
    const unsigned char* y = (const unsigned char*)b;
    for (int i = 0; i < n; i++) {
        if (x[i] != y[i]) return (int)x[i] - (int)y[i];
    }
    return 0;
}

/* "a.b.c.d" -> 4 bytes; returns 0 if the string is not a plain IPv4. */
int net_parse_ip4(const char* str, uint8_t* out) {
    int oct = 0, val = -1;
    for (const char* p = str; ; p++) {
        if (*p >= '0' && *p <= '9') {
            if (val < 0) val = 0;
            val = val * 10 + (*p - '0');
            if (val > 255) return 0;
        } else if (*p == '.' || *p == '\0') {
            if (val < 0 || oct >= 4) return 0;
            out[oct++] = (uint8_t)val;
            val = -1;
            if (*p == '\0') break;
        } else {
            return 0;
        }
    }
    return oct == 4;
}

static uint16_t csum16(const uint8_t* data, uint32_t len) {
    uint32_t sum = 0;
    for (uint32_t i = 0; i < len; i += 2) {
        uint16_t w = (uint16_t)((data[i] << 8) | ((i + 1 < len) ? data[i + 1] : 0));
        sum += w;
    }
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)(~sum & 0xFFFF);
}

static uint32_t build_eth(uint8_t* pkt, const uint8_t* dstmac, uint16_t ethertype) {
    for (int i = 0; i < 6; i++) pkt[i] = dstmac[i];
    for (int i = 0; i < 6; i++) pkt[6 + i] = net_mac[i];
    pkt[12] = (ethertype >> 8) & 0xFF;
    pkt[13] = ethertype & 0xFF;
    return 14;
}

/* ------------------------------------------------------------------- ARP */

static uint8_t arp_table_mac[8][6];
static uint8_t arp_table_ip[8][4];
static int arp_entries = 0;

static int arp_lookup(const uint8_t* ip, uint8_t* mac) {
    for (int i = 0; i < arp_entries; i++) {
        if (my_memcmp(arp_table_ip[i], ip, 4) == 0) {
            for (int j = 0; j < 6; j++) mac[j] = arp_table_mac[i][j];
            return 1;
        }
    }
    return 0;
}

static void arp_add(const uint8_t* ip, const uint8_t* mac) {
    for (int i = 0; i < arp_entries; i++) {
        if (my_memcmp(arp_table_ip[i], ip, 4) == 0) {
            for (int j = 0; j < 6; j++) arp_table_mac[i][j] = mac[j];
            return;
        }
    }
    if (arp_entries < 8) {
        for (int j = 0; j < 4; j++) arp_table_ip[arp_entries][j] = ip[j];
        for (int j = 0; j < 6; j++) arp_table_mac[arp_entries][j] = mac[j];
        arp_entries++;
    }
}

static void arp_send_reply(const uint8_t* dst_mac, const uint8_t* dst_ip) {
    uint8_t pkt[42];
    uint32_t off = build_eth(pkt, dst_mac, ETH_ARP);
    pkt[off + 0] = 0; pkt[off + 1] = 1;
    pkt[off + 2] = 0x08; pkt[off + 3] = 0x00;
    pkt[off + 4] = 6; pkt[off + 5] = 4;
    pkt[off + 6] = 0; pkt[off + 7] = ARP_REP;
    for (int i = 0; i < 6; i++) pkt[off + 8 + i] = net_mac[i];
    for (int i = 0; i < 4; i++) pkt[off + 14 + i] = net_ip[i];
    for (int i = 0; i < 6; i++) pkt[off + 18 + i] = dst_mac[i];
    for (int i = 0; i < 4; i++) pkt[off + 24 + i] = dst_ip[i];
    net_send_raw(pkt, 42);
}

int net_arp_resolve(const uint8_t* ip, uint8_t* mac) {
    if (arp_lookup(ip, mac)) return 1;
    if (!net_has_link) return 0;
    uint8_t bc[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
    uint8_t pkt[42];
    uint32_t off = build_eth(pkt, bc, ETH_ARP);
    pkt[off + 0] = 0; pkt[off + 1] = 1;
    pkt[off + 2] = 0x08; pkt[off + 3] = 0x00;
    pkt[off + 4] = 6; pkt[off + 5] = 4;
    pkt[off + 6] = 0; pkt[off + 7] = ARP_REQ;
    for (int i = 0; i < 6; i++) pkt[off + 8 + i] = net_mac[i];
    for (int i = 0; i < 4; i++) pkt[off + 14 + i] = net_ip[i];
    for (int i = 0; i < 6; i++) pkt[off + 18 + i] = 0;
    for (int i = 0; i < 4; i++) pkt[off + 24 + i] = ip[i];
    net_send_raw(pkt, 42);
    for (int attempt = 0; attempt < 4; attempt++) {
        delay_ms(300);
        net_poll();
        if (arp_lookup(ip, mac)) return 1;
        net_send_raw(pkt, 42);
    }
    return 0;
}

/* Same-subnet hosts are ARP-resolved directly; everything else goes to the
 * gateway. The old code always asked for the gateway, which broke pings to
 * LAN neighbours behind routers that do not forward. */
static int net_route(const uint8_t* ip, uint8_t* mac) {
    int local = net_configured;
    if (local) {
        for (int i = 0; i < 4; i++) {
            if ((ip[i] & net_mask[i]) != (net_ip[i] & net_mask[i])) {
                local = 0;
                break;
            }
        }
    }
    return net_arp_resolve(local ? ip : net_gw, mac);
}

static int my_strncmp_prefix(const char* a, const char* b) {
    for (int i = 0; b[i]; i++) {
        if (a[i] != b[i]) return 0;
    }
    return 1;
}

/* ----------------------------------------------------------- frame intake */

/* One callback per transport. The old stack had a single ip_recv_cb that the
 * active command (ping/dns/wget) installed, so a DHCP reply arriving during a
 * ping was fed to the ICMP parser and dropped - and UDP callbacks were handed
 * the UDP *header* instead of the payload, so DHCP and DNS replies never
 * parsed even when they did arrive. */
typedef void (*l4_cb_t)(const uint8_t* payload, uint32_t len, const uint8_t* src_ip);
static l4_cb_t icmp_cb = NULL;
static l4_cb_t udp_cb  = NULL;
static l4_cb_t tcp_cb  = NULL;

static void dhcp_recv(const uint8_t* payload, uint32_t len, const uint8_t* src_ip);

static uint8_t icmp_reply_buf[1536];

/* Answer ICMP echo requests: "ping the SharkOS box" is the natural first
 * test on real hardware. */
static void icmp_send_echo_reply(const uint8_t* frame, const uint8_t* icmp,
                                 uint32_t icmp_len) {
    if (icmp_len < 8 || icmp_len > 1480) return;
    uint8_t* pkt = icmp_reply_buf;
    uint32_t off = build_eth(pkt, &frame[6], ETH_IP);
    uint8_t* ip = &pkt[off];
    ip[0] = 0x45; ip[1] = 0;
    uint16_t tlen = (uint16_t)(20 + icmp_len);
    ip[2] = (tlen >> 8) & 0xFF; ip[3] = tlen & 0xFF;
    ip[4] = 0; ip[5] = 0; ip[6] = 0x40; ip[7] = 0;
    ip[8] = 0x40; ip[9] = IP_ICMP; ip[10] = 0; ip[11] = 0;
    for (int i = 0; i < 4; i++) ip[12 + i] = net_ip[i];
    for (int i = 0; i < 4; i++) ip[16 + i] = frame[14 + 12 + i];
    uint16_t ic = csum16(ip, 20);
    ip[10] = (ic >> 8) & 0xFF; ip[11] = ic & 0xFF;

    uint8_t* out = &pkt[off + 20];
    for (uint32_t i = 0; i < icmp_len; i++) out[i] = icmp[i];
    out[0] = 0;                              /* echo reply */
    out[2] = 0; out[3] = 0;
    uint16_t cc = csum16(out, icmp_len);
    out[2] = (cc >> 8) & 0xFF; out[3] = cc & 0xFF;
    net_send_raw(pkt, off + 20 + icmp_len);
}

static int ip_is_for_us(const uint8_t* dip) {
    if (dip[0] == 255 && dip[1] == 255 && dip[2] == 255 && dip[3] == 255) return 1;
    if (!net_configured) return 1;           /* DHCP replies to the offered IP */
    if (my_memcmp(dip, net_ip, 4) == 0) return 1;
    /* Directed subnet broadcast (e.g. 192.168.1.255). */
    for (int i = 0; i < 4; i++) {
        uint8_t host_bits = (uint8_t)~net_mask[i];
        if ((dip[i] & net_mask[i]) != (net_ip[i] & net_mask[i])) return 0;
        if ((dip[i] & host_bits) != host_bits) return 0;
    }
    return 1;
}

static void net_on_frame(const uint8_t* frame, uint32_t len) {
    if (len < 14) return;
    uint16_t et = (uint16_t)((frame[12] << 8) | frame[13]);

    if (et == ETH_ARP) {
        if (len < 14 + 28) return;
        uint16_t op = (uint16_t)((frame[20] << 8) | frame[21]);
        const uint8_t* smac = &frame[22];
        const uint8_t* sip = &frame[28];
        const uint8_t* tip = &frame[38];
        if (op == ARP_REP) {
            arp_add(sip, smac);
        } else if (op == ARP_REQ) {
            arp_add(sip, smac);
            /* Answer requests for our own address. */
            if (net_configured && my_memcmp(tip, net_ip, 4) == 0) {
                arp_send_reply(smac, sip);
            }
        }
    } else if (et == ETH_IP) {
        if (len < 14 + 20) return;
        if ((frame[14] >> 4) != 4) return;
        uint32_t ihl = (uint32_t)((frame[14] & 0x0F) * 4);
        if (ihl < 20) return;
        if (len < 14 + ihl) return;
        /* Fragments are not reassembled; drop anything but the first/only. */
        if ((frame[14 + 6] & 0x3F) || frame[14 + 7]) return;
        uint8_t proto = frame[14 + 9];
        const uint8_t* sip = &frame[14 + 12];
        const uint8_t* dip = &frame[14 + 16];
        const uint8_t* payload = &frame[14 + ihl];
        uint16_t total = (uint16_t)((frame[14 + 2] << 8) | frame[14 + 3]);
        uint32_t plen = (total > ihl) ? (total - ihl) : 0;
        if (plen > len - 14 - ihl) plen = len - 14 - ihl;
        if (!ip_is_for_us(dip)) return;

        if (proto == IP_ICMP) {
            if (plen < 8) return;
            if (payload[0] == 8 && net_configured && my_memcmp(dip, net_ip, 4) == 0) {
                icmp_send_echo_reply(frame, payload, plen);
            } else if (payload[0] == 0 && icmp_cb) {
                icmp_cb(payload, plen, sip);
            }
        } else if (proto == IP_TCP) {
            if (tcp_cb) tcp_cb(payload, plen, sip);
        } else if (proto == IP_UDP) {
            if (plen < 8) return;
            uint16_t sport = (uint16_t)((payload[0] << 8) | payload[1]);
            uint16_t dport = (uint16_t)((payload[2] << 8) | payload[3]);
            uint16_t ulen  = (uint16_t)((payload[4] << 8) | payload[5]);
            if (ulen < 8 || ulen > plen) return;
            const uint8_t* udata = payload + 8;
            uint32_t ulen_data = (uint32_t)ulen - 8;
            if (dport == 68 && sport == 67) {
                dhcp_recv(udata, ulen_data, sip);
            } else if (udp_cb) {
                udp_cb(udata, ulen_data, sip);
            }
        }
    }
}

/* ------------------------------------------------------------------ ICMP */

static uint8_t icmp_expect_ip[4];
static uint16_t icmp_expect_seq = 0;
static volatile int icmp_got_reply = 0;

static void icmp_recv(const uint8_t* payload, uint32_t len, const uint8_t* src_ip) {
    if (len < 8) return;
    if (payload[0] != 0) return;                        /* echo reply */
    if (payload[4] != 0x12 || payload[5] != 0x34) return;   /* our id */
    uint16_t seq = (uint16_t)((payload[6] << 8) | payload[7]);
    if (seq != icmp_expect_seq) return;
    /* icmp_expect_ip was never filled in by the old code, so every reply
     * failed this compare and ping always reported timeouts. */
    if (my_memcmp(src_ip, icmp_expect_ip, 4) != 0) return;
    icmp_got_reply = 1;
}

int net_ping(const char* ip_str, int count) {
    uint8_t tip[4];
    if (!net_configured) { terminal_writestring("net: not configured (run dhcp)\n"); return -1; }
    if (!net_parse_ip4(ip_str, tip)) {
        /* Hostname: resolve it first, like every other ping. */
        if (!net_dns_lookup(ip_str, tip)) {
            terminal_writestring("net: cannot resolve ");
            terminal_writestring(ip_str);
            terminal_writestring("\n");
            return -1;
        }
        char b[16];
        terminal_writestring("PING ");
        terminal_writestring(ip_str);
        terminal_writestring(" (");
        for (int i = 0; i < 4; i++) { int_to_string(tip[i], b); terminal_writestring(b); if (i < 3) terminal_writestring("."); }
        terminal_writestring(")\n");
    }

    uint8_t gwmac[6];
    if (!net_route(tip, gwmac)) { terminal_writestring("net: cannot reach target\n"); return -1; }
    for (int i = 0; i < 4; i++) icmp_expect_ip[i] = tip[i];

    int received = 0;
    for (int seq = 0; seq < count; seq++) {
        uint8_t pkt[14 + 20 + 40];
        uint32_t off = build_eth(pkt, gwmac, ETH_IP);
        pkt[off + 0] = 0x45; pkt[off + 1] = 0;
        uint16_t tlen = 20 + 40;
        pkt[off + 2] = (tlen >> 8) & 0xFF; pkt[off + 3] = tlen & 0xFF;
        pkt[off + 4] = 0; pkt[off + 5] = 0;
        pkt[off + 6] = 0x40; pkt[off + 7] = 0;
        pkt[off + 8] = 0x40; pkt[off + 9] = IP_ICMP;
        pkt[off + 10] = 0; pkt[off + 11] = 0;
        for (int i = 0; i < 4; i++) pkt[off + 12 + i] = net_ip[i];
        for (int i = 0; i < 4; i++) pkt[off + 16 + i] = tip[i];
        uint16_t ic = csum16(&pkt[off], 20);
        pkt[off + 10] = (ic >> 8) & 0xFF; pkt[off + 11] = ic & 0xFF;

        uint8_t* icmp = &pkt[off + 20];
        icmp[0] = 8; icmp[1] = 0; icmp[2] = 0; icmp[3] = 0;
        icmp[4] = 0x12; icmp[5] = 0x34;
        icmp[6] = (seq >> 8) & 0xFF; icmp[7] = seq & 0xFF;
        for (int i = 0; i < 32; i++) icmp[8 + i] = (uint8_t)('A' + (i % 26));
        uint16_t cc = csum16(icmp, 40);
        icmp[2] = (cc >> 8) & 0xFF; icmp[3] = cc & 0xFF;

        icmp_got_reply = 0;
        icmp_expect_seq = (uint16_t)seq;
        icmp_cb = icmp_recv;
        uint32_t t0 = uptime_ticks;
        net_send_raw(pkt, sizeof(pkt));
        /* Poll at 1 ms granularity so the RTT is meaningful (the old 50 ms
         * steps could only ever report multiples of 50). */
        while (uptime_ticks - t0 < 1000) {
            net_poll();
            if (icmp_got_reply) break;
            delay_ms(1);
        }
        uint32_t rtt = uptime_ticks - t0;
        char b[16];
        if (icmp_got_reply) {
            received++;
            terminal_writestring("PING: reply from ");
            terminal_writestring(ip_str);
            terminal_writestring(" seq=");
            int_to_string((uint32_t)(seq + 1), b); terminal_writestring(b);
            terminal_writestring(" time=");
            if (rtt < 1) terminal_writestring("<1");
            else { int_to_string(rtt, b); terminal_writestring(b); }
            terminal_writestring(" ms\n");
        } else {
            terminal_writestring("PING: timeout seq=");
            int_to_string((uint32_t)(seq + 1), b); terminal_writestring(b);
            terminal_writestring("\n");
        }
        /* Space the probes out like every other ping does. */
        if (seq + 1 < count && rtt < 1000) delay_ms(1000 - rtt);
    }
    char b[16];
    terminal_writestring("PING: ");
    int_to_string((uint32_t)received, b); terminal_writestring(b);
    terminal_writestring("/");
    int_to_string((uint32_t)count, b); terminal_writestring(b);
    terminal_writestring(" received\n");
    icmp_cb = NULL;
    return received;
}

/* ------------------------------------------------------------------- DNS */

static uint8_t dns_resp_ip[4];
static volatile int dns_got = 0;
static uint16_t dns_txid = 0xABCD;

static void dns_recv(const uint8_t* payload, uint32_t len, const uint8_t* src_ip) {
    (void)src_ip;
    if (len < 12) return;
    uint16_t tid = (uint16_t)((payload[0] << 8) | payload[1]);
    if (tid != dns_txid) return;
    uint16_t anc = (uint16_t)((payload[6] << 8) | payload[7]);
    if (anc == 0) return;

    uint32_t pos = 12;
    /* Walk the question name, following compression pointers. */
    int jumps = 0;
    while (pos < len) {
        uint8_t l = payload[pos];
        if (l == 0) { pos++; break; }
        if ((l & 0xC0) == 0xC0) { pos += 2; jumps++; break; }
        pos += (uint32_t)l + 1;
        if (jumps > 4) return;
    }
    pos += 4;    /* QTYPE + QCLASS */

    for (int i = 0; i < anc && pos + 12 <= len; i++) {
        /* Answer name may be a compression pointer too. */
        if ((payload[pos] & 0xC0) == 0xC0) pos += 2;
        else { while (pos < len && payload[pos]) pos += (uint32_t)payload[pos] + 1; pos++; }
        if (pos + 10 > len) return;
        uint16_t type = (uint16_t)((payload[pos] << 8) | payload[pos + 1]); pos += 2;
        pos += 2;                                /* class */
        pos += 4;                                /* ttl */
        uint16_t rdlen = (uint16_t)((payload[pos] << 8) | payload[pos + 1]); pos += 2;
        if (type == 1 && rdlen == 4 && pos + 4 <= len) {
            for (int j = 0; j < 4; j++) dns_resp_ip[j] = payload[pos + j];
            dns_got = 1;
        }
        pos += rdlen;
    }
}

static uint32_t dns_encode_name(uint8_t* out, const char* name) {
    uint32_t o = 0, seg = 0;
    out[o++] = 0;
    for (int i = 0; name[i] && o < 120; i++) {
        if (name[i] == '.') { out[seg] = (uint8_t)(o - seg - 1); seg = o; out[o++] = 0; }
        else out[o++] = (uint8_t)name[i];
    }
    out[seg] = (uint8_t)(o - seg - 1);
    out[o++] = 0;
    return o;
}

int net_dns_lookup(const char* hostname, uint8_t* ip_out) {
    if (!net_configured) { terminal_writestring("net: DNS not configured\n"); return -1; }
    if (net_dns[0] == 0 && net_dns[1] == 0 && net_dns[2] == 0 && net_dns[3] == 0) {
        terminal_writestring("net: no DNS server\n"); return -1;
    }
    uint8_t gwmac[6];
    if (!net_arp_resolve(net_gw, gwmac)) { terminal_writestring("net: gateway unreachable\n"); return -1; }

    uint8_t pkt[14 + 20 + 8 + 256];
    uint32_t off = build_eth(pkt, gwmac, ETH_IP);

    uint16_t sport = 0x3039;
    uint16_t dport = 53;

    uint8_t* dns = &pkt[off + 20 + 8];
    dns[0] = (dns_txid >> 8) & 0xFF; dns[1] = dns_txid & 0xFF;
    dns[2] = 0x01; dns[3] = 0;
    dns[4] = 0; dns[5] = 1;
    dns[6] = 0; dns[7] = 0; dns[8] = 0; dns[9] = 0; dns[10] = 0; dns[11] = 0;
    uint32_t np = 12;
    char namebuf[128];
    int i;
    for (i = 0; hostname[i] && i < 127; i++) namebuf[i] = hostname[i];
    namebuf[i] = 0;
    np += dns_encode_name(&dns[np], namebuf);
    /* QTYPE=A (16-bit) and QCLASS=IN (16-bit); the old code wrote one byte
     * each, producing a malformed query that servers silently drop. */
    dns[np++] = 0; dns[np++] = 1;
    dns[np++] = 0; dns[np++] = 1;
    uint16_t udplen = (uint16_t)(8 + np);

    uint8_t* udp = &pkt[off + 20];
    udp[0] = (sport >> 8) & 0xFF; udp[1] = sport & 0xFF;
    udp[2] = (dport >> 8) & 0xFF; udp[3] = dport & 0xFF;
    udp[4] = (udplen >> 8) & 0xFF; udp[5] = udplen & 0xFF;
    udp[6] = 0; udp[7] = 0;

    pkt[off + 0] = 0x45; pkt[off + 1] = 0;
    uint16_t tlen = (uint16_t)(20 + udplen);
    pkt[off + 2] = (tlen >> 8) & 0xFF; pkt[off + 3] = tlen & 0xFF;
    pkt[off + 4] = 0; pkt[off + 5] = 0;
    pkt[off + 6] = 0x40; pkt[off + 7] = 0;
    pkt[off + 8] = 0x40; pkt[off + 9] = IP_UDP;
    pkt[off + 10] = 0; pkt[off + 11] = 0;
    for (int k = 0; k < 4; k++) pkt[off + 12 + k] = net_ip[k];
    for (int k = 0; k < 4; k++) pkt[off + 16 + k] = net_dns[k];
    uint16_t ic = csum16(&pkt[off], 20);
    pkt[off + 10] = (ic >> 8) & 0xFF; pkt[off + 11] = ic & 0xFF;

    dns_got = 0;
    udp_cb = dns_recv;
    net_send_raw(pkt, off + 20 + udplen);
    for (int w = 0; w < 20; w++) { delay_ms(100); net_poll(); if (dns_got) break; }
    udp_cb = NULL;
    if (dns_got) { for (int k = 0; k < 4; k++) ip_out[k] = dns_resp_ip[k]; return 1; }
    return 0;
}

/* ------------------------------------------------------------------- TCP */

static uint8_t tcp_dst_mac[6];
static uint8_t tcp_dst_ip[4];
static uint16_t tcp_dst_port;
static uint32_t tcp_seq = 0x1000;
static uint32_t tcp_ack = 0;
static uint16_t tcp_src_port = 0x4000;
static volatile int tcp_connected = 0;
static volatile int tcp_fin = 0;
static volatile int tcp_reset = 0;
static uint8_t tcp_rx_buf[65536];         /* receive window (streamed out by net_tcp_recv) */
static volatile uint32_t tcp_rx_len = 0;  /* bytes stored */
static uint32_t tcp_rx_rd = 0;            /* bytes already handed to the caller */

static uint16_t tcp_csum(const uint8_t* ih, const uint8_t* tcp, uint32_t tcplen) {
    uint32_t sum = 0;
    uint32_t src = ((uint32_t)ih[12] << 24) | ((uint32_t)ih[13] << 16) |
                   ((uint32_t)ih[14] << 8) | ih[15];
    uint32_t dst = ((uint32_t)ih[16] << 24) | ((uint32_t)ih[17] << 16) |
                   ((uint32_t)ih[18] << 8) | ih[19];
    sum += (src >> 16) + (src & 0xFFFF) + (dst >> 16) + (dst & 0xFFFF);
    sum += 6;
    sum += tcplen;
    for (uint32_t i = 0; i < tcplen; i += 2) {
        uint16_t w = (uint16_t)((tcp[i] << 8) | ((i + 1 < tcplen) ? tcp[i + 1] : 0));
        sum += w;
    }
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)(~sum & 0xFFFF);
}

static void tcp_send_flags(uint8_t flags, const uint8_t* data, uint32_t dlen) {
    uint8_t pkt[14 + 20 + 20 + 1460];
    if (dlen > 1460) dlen = 1460;
    uint32_t off = build_eth(pkt, tcp_dst_mac, ETH_IP);
    uint8_t* ip = &pkt[off];
    ip[0] = 0x45; ip[1] = 0;
    uint16_t tlen = (uint16_t)(20 + 20 + dlen);
    ip[2] = (tlen >> 8) & 0xFF; ip[3] = tlen & 0xFF;
    ip[4] = 0; ip[5] = 0; ip[6] = 0x40; ip[7] = 0;
    ip[8] = 0x40; ip[9] = IP_TCP; ip[10] = 0; ip[11] = 0;
    for (int i = 0; i < 4; i++) ip[12 + i] = net_ip[i];
    for (int i = 0; i < 4; i++) ip[16 + i] = tcp_dst_ip[i];
    uint16_t ic = csum16(ip, 20);
    ip[10] = (ic >> 8) & 0xFF; ip[11] = ic & 0xFF;

    uint8_t* tcp = &pkt[off + 20];
    tcp[0] = (tcp_src_port >> 8) & 0xFF; tcp[1] = tcp_src_port & 0xFF;
    tcp[2] = (tcp_dst_port >> 8) & 0xFF; tcp[3] = tcp_dst_port & 0xFF;
    tcp[4] = (tcp_seq >> 24) & 0xFF; tcp[5] = (tcp_seq >> 16) & 0xFF;
    tcp[6] = (tcp_seq >> 8) & 0xFF; tcp[7] = tcp_seq & 0xFF;
    tcp[8] = (tcp_ack >> 24) & 0xFF; tcp[9] = (tcp_ack >> 16) & 0xFF;
    tcp[10] = (tcp_ack >> 8) & 0xFF; tcp[11] = tcp_ack & 0xFF;
    tcp[12] = 0x50;
    tcp[13] = flags;
    tcp[14] = 0x72; tcp[15] = 0x10;
    tcp[16] = 0; tcp[17] = 0;
    for (uint32_t i = 0; i < dlen; i++) tcp[20 + i] = data[i];
    uint16_t cs = tcp_csum(ip, tcp, 20 + dlen);
    tcp[16] = (cs >> 8) & 0xFF; tcp[17] = cs & 0xFF;
    net_send_raw(pkt, off + 20 + 20 + dlen);
}

static void tcp_recv(const uint8_t* payload, uint32_t len, const uint8_t* src_ip) {
    (void)src_ip;
    if (len < 20) return;
    uint16_t dport = (uint16_t)((payload[2] << 8) | payload[3]);
    if (dport != tcp_src_port) return;
    uint32_t seq = ((uint32_t)payload[4] << 24) | ((uint32_t)payload[5] << 16) |
                   ((uint32_t)payload[6] << 8) | payload[7];
    uint8_t data_off = (uint8_t)(((payload[12] >> 4) & 0xF) * 4);
    uint8_t flags = payload[13];

    if (flags & 0x04) {                       /* RST: peer gave up */
        tcp_reset = 1;
        return;
    }
    if (flags & 0x02) {
        /* SYN|ACK: complete the handshake with a plain ACK. The old code
         * answered with SYN|ACK (0x12), so servers reset the connection. */
        tcp_ack = seq + 1;
        /* Our SYN consumed one sequence number. Without this the request
         * went out at the SYN's own sequence, the server trimmed the
         * "already received" first byte and saw "ET / HTTP/1.1". */
        tcp_seq += 1;
        tcp_send_flags(0x10, NULL, 0);
        tcp_connected = 1;
        return;
    }
    if (len > data_off) {
        uint32_t dlen = len - data_off;
        if (seq == tcp_ack) {                 /* in order: accept */
            if (tcp_rx_len + dlen > sizeof(tcp_rx_buf) && tcp_rx_rd > 0) {
                /* make room: drop what the caller has already consumed */
                uint32_t keep = tcp_rx_len - tcp_rx_rd;
                for (uint32_t i = 0; i < keep; i++) tcp_rx_buf[i] = tcp_rx_buf[tcp_rx_rd + i];
                tcp_rx_len = keep; tcp_rx_rd = 0;
            }
            if (tcp_rx_len + dlen <= sizeof(tcp_rx_buf)) {
                for (uint32_t i = 0; i < dlen; i++) {
                    tcp_rx_buf[tcp_rx_len + i] = payload[data_off + i];
                }
                tcp_rx_len += dlen;
                tcp_ack = seq + dlen;
            }
            /* else: no room. Leave tcp_ack alone so the dup-ACK below makes
             * the peer resend once the caller has drained the buffer. */
        }
        /* Out of order or duplicate: re-ACK what we have so the peer
         * retransmits from there. A FIN riding on in-order data counts
         * one more sequence number. */
        if ((flags & 0x01) && seq + dlen == tcp_ack) {
            tcp_ack += 1; tcp_fin = 1; tcp_send_flags(0x11, NULL, 0);
        } else {
            tcp_send_flags(0x10, NULL, 0);
        }
        return;
    }
    if (flags & 0x01) {                       /* FIN with no data */
        if (seq == tcp_ack) tcp_ack = seq + 1;
        tcp_fin = 1;
        tcp_send_flags(0x11, NULL, 0);
        return;
    }
    /* Pure ACK (e.g. of our request); nothing to do. */
}

static uint32_t rdtsc_lo(void) {
    uint32_t lo, hi;
    asm volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return lo ^ hi;
}

/* Streaming TCP: one connection at a time (the stack is single-threaded and
 * polled), used by HTTP directly and by the TLS layer through callbacks. */
int net_tcp_open(const uint8_t* ip, uint16_t port) {
    if (!net_configured) { terminal_writestring("net: not configured\n"); return -1; }
    if (!net_route(ip, tcp_dst_mac)) { terminal_writestring("net: host unreachable\n"); return -1; }
    for (int i = 0; i < 4; i++) tcp_dst_ip[i] = ip[i];
    tcp_dst_port = port;
    /* Fresh ISN and ephemeral port per connection so a quick second wget
     * is not confused with stale segments from the first. */
    tcp_seq = 0x1000 + (uptime_ticks * 2654435761u) + rdtsc_lo();
    tcp_ack = 0; tcp_connected = 0; tcp_fin = 0; tcp_reset = 0;
    tcp_rx_len = 0; tcp_rx_rd = 0;
    tcp_src_port = (uint16_t)(0x4000 + ((uptime_ticks + rdtsc_lo()) & 0x3FF));
    tcp_cb = tcp_recv;

    tcp_send_flags(0x02, NULL, 0);
    int connected = 0;
    for (int w = 0; w < 300; w++) {                          /* 3 s */
        delay_ms(10); net_poll();
        if (tcp_connected) { connected = 1; break; }
        if (tcp_reset) break;
        if (w == 100 || w == 200) tcp_send_flags(0x02, NULL, 0);   /* SYN retry */
    }
    if (!connected) {
        tcp_cb = NULL;
        terminal_writestring(tcp_reset ? "net: connection refused\n" : "net: connection timed out\n");
        return -1;
    }
    /* Packet arrival jitter is the best entropy this machine has: feed it
     * to the TLS random generator on every connection. */
    { uint32_t e[4] = { rdtsc_lo(), uptime_ticks, rtc_seconds, tcp_seq }; tls_random_seed((const uint8_t*)e, sizeof(e)); }
    return 0;
}

int net_tcp_send(const uint8_t* data, uint32_t len) {
    if (!tcp_cb || !tcp_connected || tcp_reset) return -1;
    uint32_t done = 0;
    while (done < len) {
        uint32_t n = len - done;
        if (n > 1460) n = 1460;                  /* one MSS per segment */
        tcp_send_flags(0x18, data + done, n);
        tcp_seq += n;
        done += n;
        net_poll();                              /* pick up ACKs as we go */
    }
    return (int)len;
}

/* Returns bytes copied (>0), 0 when nothing arrived within timeout_ms, or -1
 * once the peer has closed (FIN/RST) and the buffer is drained. */
int net_tcp_recv(uint8_t* out, uint32_t max, uint32_t timeout_ms) {
    if (!tcp_cb) return -1;
    uint32_t start = uptime_ticks;
    for (;;) {
        net_poll();
        if (tcp_rx_rd < tcp_rx_len) {
            uint32_t n = tcp_rx_len - tcp_rx_rd;
            if (n > max) n = max;
            for (uint32_t i = 0; i < n; i++) out[i] = tcp_rx_buf[tcp_rx_rd + i];
            tcp_rx_rd += n;
            if (tcp_rx_rd == tcp_rx_len) { tcp_rx_rd = 0; tcp_rx_len = 0; }
            return (int)n;
        }
        if (tcp_fin || tcp_reset) return -1;
        if (uptime_ticks - start >= timeout_ms) return 0;
        delay_ms(1);
    }
}

int net_tcp_peer_closed(void) { return tcp_fin || tcp_reset; }

void net_tcp_close(void) {
    if (!tcp_cb) return;
    if (!tcp_fin && !tcp_reset) {
        tcp_send_flags(0x11, NULL, 0);           /* our FIN */
        tcp_seq += 1;
        for (int w = 0; w < 40 && !tcp_fin && !tcp_reset; w++) { delay_ms(5); net_poll(); }
    } else if (!tcp_reset) {
        tcp_send_flags(0x11, NULL, 0);           /* answer the peer's FIN */
        tcp_seq += 1;
    }
    tcp_cb = NULL;
}

/* One-shot helper: connect, send a request, collect the reply until the
 * server closes, the buffer is full, or 10 s pass without progress. */
int net_tcp_connect(const uint8_t* ip, uint16_t port,
                    const uint8_t* send_data, uint32_t send_len,
                    uint8_t* resp_buf, uint32_t resp_max) {
    if (net_tcp_open(ip, port) < 0) return -1;
    if (send_data && send_len > 0) net_tcp_send(send_data, send_len);
    uint32_t total = 0;
    while (total < resp_max) {
        int n = net_tcp_recv(resp_buf + total, resp_max - total, 10 * TICKS_PER_SEC);
        if (n <= 0) break;
        total += (uint32_t)n;
    }
    net_tcp_close();
    return total > 0 ? (int)total : -1;
}

/* ------------------------------------------------------------------ HTTP */

/* Static: the old code stacked 8 KB here and another 8 KB in net_cmd_wget,
 * blowing the 8 KB kernel stack. 64 KB so the browser can load real pages. */
static uint8_t http_resp[65536];

/* Last response's status code and Location header (for redirects). */
int  net_http_last_status = 0;
char net_http_last_location[512];

/* TLS state lives here (about 27 KB) rather than on the kernel stack. */
static tls_t https_tls;
int  net_tls_last_error = 0;
char net_tls_peer_cn[64];

static int https_send(const uint8_t* data, int len) { return net_tcp_send(data, (uint32_t)len) < 0 ? -1 : len; }
static int https_recv(uint8_t* out, int max, int timeout_ms) { return net_tcp_recv(out, (uint32_t)max, (uint32_t)timeout_ms); }

/* Fetch the raw HTTP reply (headers + body) for url into rb (rmax bytes).
 * Returns the byte count or -1. Handles http:// and https://. */
static int http_fetch_raw(const char* url, uint8_t* rb, uint32_t rmax) {
    const char* p = url;
    int secure = 0;
    if (my_strncmp_prefix(p, "http://")) p += 7;
    else if (my_strncmp_prefix(p, "https://")) { p += 8; secure = 1; }
    char host[128];
    int hi = 0;
    while (*p && *p != ':' && *p != '/') { if (hi < 127) host[hi++] = *p; p++; }
    host[hi] = 0;
    uint16_t port = secure ? 443 : 80;
    if (*p == ':') {
        port = 0; p++;
        while (*p >= '0' && *p <= '9') { port = (uint16_t)(port * 10 + (*p - '0')); p++; }
    }
    char path[512];
    int pi = 0;
    if (*p != '/') path[pi++] = '/';
    while (*p && pi < 511) { path[pi++] = *p; p++; }
    path[pi] = 0;

    uint8_t ip[4];
    if (!net_parse_ip4(host, ip)) {
        /* Not a dotted quad: resolve it. */
        if (!net_dns_lookup(host, ip)) {
            terminal_writestring("net: DNS lookup failed for ");
            terminal_writestring(host);
            terminal_writestring("\n");
            return -1;
        }
    }

    char req[768];
    int rl = net_snprintf(req, sizeof(req),
                          "GET %s HTTP/1.1\r\nHost: %s\r\n"
                          "User-Agent: SharkOS98\r\n"
                          "Connection: close\r\n\r\n", path, host);

    net_tls_last_error = 0;
    net_tls_peer_cn[0] = 0;
    if (!secure) {
        return net_tcp_connect(ip, port, (uint8_t*)req, (uint32_t)rl, rb, rmax);
    }

    if (net_tcp_open(ip, port) < 0) return -1;
    int err = tls_connect(&https_tls, host, https_send, https_recv);
    if (err) {
        net_tls_last_error = err;
        terminal_writestring("tls: ");
        terminal_writestring(tls_error_string(err));
        terminal_writestring("\n");
        tls_close(&https_tls);
        net_tcp_close();
        return -1;
    }
    { int i = 0; while (https_tls.peer_cn[i] && i < 63) { net_tls_peer_cn[i] = https_tls.peer_cn[i]; i++; } net_tls_peer_cn[i] = 0; }
    tls_write(&https_tls, (const uint8_t*)req, rl);
    uint32_t total = 0;
    while (total < rmax) {
        int n = tls_read(&https_tls, rb + total, rmax - total, 10 * TICKS_PER_SEC);
        if (n <= 0) break;
        total += (uint32_t)n;
    }
    if (https_tls.error) net_tls_last_error = https_tls.error;
    tls_close(&https_tls);
    net_tcp_close();
    return total > 0 ? (int)total : -1;
}

int net_http_get(const char* url, uint8_t* out_buf, uint32_t out_max) {
    net_http_last_status = 0;
    net_http_last_location[0] = 0;
    /* Big destinations (images, web fonts) receive the raw reply directly and
     * are de-headered in place; small ones go through http_resp. */
    uint8_t* rb = http_resp;
    uint32_t rmax = sizeof(http_resp);
    int in_place = 0;
    if (out_max > sizeof(http_resp)) { rb = out_buf; rmax = out_max; in_place = 1; }
    int n = http_fetch_raw(url, rb, rmax);
    if (n <= 0) return -1;
    int header_end = -1;
    for (int i = 0; i + 4 <= n; i++) {                    /* "<=": a bodiless reply ends in CRLFCRLF */
        if (rb[i] == '\r' && rb[i + 1] == '\n' &&
            rb[i + 2] == '\r' && rb[i + 3] == '\n') {
            header_end = i + 4;
            break;
        }
    }
    int body_start = (header_end >= 0) ? header_end : 0;

    /* Status line "HTTP/1.x NNN ..." and a Location: header, if any. */
    if (header_end > 0 && my_strncmp_prefix((const char*)rb, "HTTP/")) {
        int i = 0;
        while (i < header_end && rb[i] != ' ') i++;
        while (i < header_end && rb[i] == ' ') i++;
        int st = 0;
        while (i < header_end && rb[i] >= '0' && rb[i] <= '9') { st = st * 10 + (rb[i] - '0'); i++; }
        net_http_last_status = st;
        for (int k = 0; k + 10 < header_end; k++) {
            if (rb[k] == '\n' &&
                (my_strncmp_prefix((const char*)&rb[k + 1], "Location: ") ||
                 my_strncmp_prefix((const char*)&rb[k + 1], "location: "))) {
                int q = k + 11, li = 0;
                while (q < header_end && rb[q] != '\r' && rb[q] != '\n' &&
                       li < (int)sizeof(net_http_last_location) - 1) {
                    net_http_last_location[li++] = (char)rb[q++];
                }
                net_http_last_location[li] = 0;
                break;
            }
        }
    }

    /* Transfer-Encoding: chunked (HTTP/1.1 default for dynamic pages):
     * strip the "<hex-len>\r\n" markers so the user sees the document,
     * not "22f ... 0". */
    int chunked = 0;
    for (int i = 0; i + 18 < body_start; i++) {
        /* header names are case-insensitive and the value may be padded with
         * any amount of whitespace ("Transfer-Encoding:  chunked" - bing) */
        if (rb[i] != '\n') continue;
        const char* h = (const char*)&rb[i + 1];
        static const char name[] = "transfer-encoding:";
        int k = 0;
        while (name[k] && i + 1 + k < body_start) {
            char c = h[k];
            if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
            if (c != name[k]) break;
            k++;
        }
        if (name[k]) continue;
        const char* v = h + k;
        while (*v == ' ' || *v == '\t') v++;
        if (my_strncmp_prefix(v, "chunked") || my_strncmp_prefix(v, "Chunked")) { chunked = 1; break; }
    }

    int out_len = 0;
    if (!chunked) {
        int body_len = n - body_start;
        if (body_len > (int)out_max) body_len = (int)out_max;
        /* forward copy is safe in place (dst <= src) */
        for (int i = 0; i < body_len; i++) out_buf[i] = rb[body_start + i];
        return body_len;
    }

    int p2 = body_start;
    while (p2 < n) {
        uint32_t clen = 0;
        int digits = 0;
        while (p2 < n) {
            uint8_t c = rb[p2];
            if (c >= '0' && c <= '9') clen = clen * 16 + (c - '0');
            else if (c >= 'a' && c <= 'f') clen = clen * 16 + (c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') clen = clen * 16 + (c - 'A' + 10);
            else break;
            digits++; p2++;
        }
        if (!digits) break;
        while (p2 < n && rb[p2] != '\n') p2++;    /* rest of size line */
        p2++;
        if (clen == 0) break;                            /* last chunk */
        for (uint32_t k = 0; k < clen && p2 < n && out_len < (int)out_max; k++) {
            out_buf[out_len++] = rb[p2++];               /* out_len <= p2 always: in-place safe */
        }
        p2 += 2;                                         /* CRLF after chunk */
    }
    (void)in_place;
    return out_len;
}

/* ------------------------------------------------------------- init/plumb */

void net_stack_init(void) {
    net_set_rx_callback(net_on_frame);
    { uint32_t e[4] = { rdtsc_lo(), uptime_ticks, rtc_seconds, (uint32_t)(uintptr_t)&e }; tls_random_seed((const uint8_t*)e, sizeof(e)); }
    { tls_random_seed(net_mac, 6); }
    /* DHCP runs from boot; nothing to type. net_poll() finishes the job. */
    net_dhcp_start();
}

/* ---------------------------------------------------------------- commands */

static void print_ip4(const char* label, const uint8_t* ip) {
    char b[16];
    terminal_writestring(label);
    for (int i = 0; i < 4; i++) {
        int_to_string(ip[i], b);
        terminal_writestring(b);
        if (i < 3) terminal_writestring(".");
    }
    terminal_writestring("\n");
}

/* "52:54:00:12:34:56" - hex_to_string() always emits 0x + 8 digits, which is
 * why ifconfig used to print 00000052:00000054:... */
void net_format_mac(char* out) {
    static const char hx[] = "0123456789abcdef";
    int o = 0;
    for (int i = 0; i < 6; i++) {
        out[o++] = hx[net_mac[i] >> 4];
        out[o++] = hx[net_mac[i] & 15];
        if (i < 5) out[o++] = ':';
    }
    out[o] = '\0';
}

void net_cmd_ifconfig(void) {
    char b[24];
    terminal_writestring("NIC: "); terminal_writestring(net_driver_name); terminal_writestring("\n");
    net_format_mac(b);
    terminal_writestring("MAC: "); terminal_writestring(b); terminal_writestring("\n");
    terminal_writestring("Link: "); terminal_writestring(net_has_link ? "up\n" : "down\n");
    print_ip4("IP: ", net_ip);
    print_ip4("Mask: ", net_mask);
    print_ip4("Gateway: ", net_gw);
    print_ip4("DNS: ", net_dns);
    terminal_writestring("DHCP: "); terminal_writestring(net_dhcp_status());
    uint32_t left = net_dhcp_lease_left();
    if (left) {
        terminal_writestring(" (lease "); int_to_string(left, b); terminal_writestring(b);
        terminal_writestring("s left)");
    }
    terminal_writestring("\n");
}

void net_cmd_ping(const char* target) { net_ping(target, 4); }

void net_cmd_dns(const char* host) {
    uint8_t ip[4];
    if (net_dns_lookup(host, ip)) {
        char b[16];
        terminal_writestring(host);
        terminal_writestring(" -> ");
        for (int i = 0; i < 4; i++) {
            int_to_string(ip[i], b);
            terminal_writestring(b);
            if (i < 3) terminal_writestring(".");
        }
        terminal_writestring("\n");
    } else {
        terminal_writestring("DNS lookup failed\n");
    }
}

static uint8_t wget_out[8192];

void net_cmd_wget(const char* url) {
    int n = net_http_get(url, wget_out, sizeof(wget_out));
    if (n > 0) {
        char b[16];
        terminal_writestring("Received ");
        int_to_string((uint32_t)n, b);
        terminal_writestring(b);
        terminal_writestring(" bytes\n");
        for (int i = 0; i < n; i++) {
            if (wget_out[i] >= 32 && wget_out[i] < 127) terminal_putchar((char)wget_out[i]);
            else if (wget_out[i] == '\n') terminal_putchar('\n');
        }
        terminal_writestring("\n");
    } else {
        terminal_writestring("HTTP GET failed\n");
    }
}

void net_cmd_netstat(void) {
    net_cmd_ifconfig();
}

/* ------------------------------------------------------------------- DHCP */

/* Non-blocking DHCP client (RFC 2131), driven from net_poll() so it runs from
 * boot without anyone typing `dhcp`, and without stalling the desktop while
 * it waits: the old client sat in delay_ms() loops for up to 6.4 s.
 *
 *   INIT -> SELECTING (DISCOVER, retry with exponential backoff 1..16 s)
 *        -> REQUESTING (REQUEST for the offer, 4 tries)
 *        -> BOUND      (T1 = lease/2 -> RENEWING unicast REQUEST,
 *                       T2 = 7/8 lease -> REBINDING broadcast REQUEST,
 *                       expiry -> INIT)
 *   NAK or link loss  -> INIT
 *   No server after ~40 s -> link-local 169.254.x.y (still keeps retrying
 *   DISCOVER in the background every 60 s in case a server shows up). */

enum { DHCP_INIT = 0, DHCP_SELECTING, DHCP_REQUESTING, DHCP_BOUND,
       DHCP_RENEWING, DHCP_REBINDING, DHCP_LINKLOCAL };

static volatile int dhcp_state = DHCP_INIT;
static uint32_t dhcp_xid = 0;
static uint8_t  dhcp_srv[4];
static uint8_t  dhcp_offered[4];
static uint8_t  dhcp_offer_mask[4], dhcp_offer_gw[4], dhcp_offer_dns[4];
static uint32_t dhcp_lease_secs = 0;
static uint32_t dhcp_bound_tick = 0;          /* uptime_ticks when ACK arrived */
static uint32_t dhcp_next_tick = 0;           /* when to act next */
static uint32_t dhcp_backoff_ms = 1000;
static int      dhcp_tries = 0;
static int      dhcp_had_link = 0;
static int      dhcp_verbose = 0;             /* print to terminal (dhcp cmd) */
static char     dhcp_status[48] = "starting";
static uint32_t dhcp_seed = 0x5A17C0DE;
static uint32_t dhcp_start_tick = 0;          /* for the BOOTP "secs" field */

static uint32_t dhcp_rand(void) {
    dhcp_seed = dhcp_seed * 1103515245u + 12345u + uptime_ticks;
    return dhcp_seed;
}

static void dhcp_log(const char* msg) {
    int n = 0;
    while (msg[n] && n < 47) { dhcp_status[n] = msg[n]; n++; }
    dhcp_status[n] = '\0';
    if (dhcp_verbose) {
        terminal_writestring("DHCP: ");
        terminal_writestring(msg);
        terminal_writestring("\n");
    }
}

const char* net_dhcp_status(void) { return dhcp_status; }
int net_dhcp_state(void) { return dhcp_state; }
uint32_t net_dhcp_lease_left(void) {
    if (dhcp_state < DHCP_BOUND || dhcp_state == DHCP_LINKLOCAL || !dhcp_lease_secs) return 0;
    uint32_t used = (uptime_ticks - dhcp_bound_tick) / TICKS_PER_SEC;
    return used >= dhcp_lease_secs ? 0 : dhcp_lease_secs - used;
}

static void dhcp_send(int type, const uint8_t* req_ip, const uint8_t* srv, int unicast) {
    uint8_t bc[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
    static uint8_t pkt[14 + 20 + 8 + 312];
    uint8_t dstmac[6];
    int use_unicast = 0;
    if (unicast && srv && net_configured && arp_lookup(srv, dstmac)) use_unicast = 1;
    uint32_t off = build_eth(pkt, use_unicast ? dstmac : bc, ETH_IP);
    uint16_t sport = 68, dport = 67;
    uint8_t* d = &pkt[off + 20 + 8];
    memset(d, 0, 312);
    d[0] = 1; d[1] = 1; d[2] = 6; d[3] = 0;             /* BOOTREQUEST, eth */
    d[4] = (dhcp_xid >> 24) & 0xFF; d[5] = (dhcp_xid >> 16) & 0xFF;
    d[6] = (dhcp_xid >> 8) & 0xFF;  d[7] = dhcp_xid & 0xFF;
    /* secs elapsed: some servers (ISC) prioritise clients that have been
     * trying for a while. */
    uint16_t secs = (uint16_t)((uptime_ticks - dhcp_start_tick) / TICKS_PER_SEC);
    d[8] = (secs >> 8) & 0xFF; d[9] = secs & 0xFF;
    /* Broadcast flag: we can receive unicast before we have an address (the
     * RTL8139 matches on MAC, not IP) but some relays behave better with it
     * set; it is harmless. */
    d[10] = 0x80; d[11] = 0;
    if (use_unicast || type == 3) {
        /* ciaddr must be filled in RENEWING/REBINDING, and must be 0 in
         * SELECTING/REQUESTING. */
        if (dhcp_state == DHCP_RENEWING || dhcp_state == DHCP_REBINDING) {
            for (int i = 0; i < 4; i++) d[12 + i] = net_ip[i];
            d[10] = 0;
        }
    }
    for (int i = 0; i < 6; i++) d[28 + i] = net_mac[i];
    d[236] = 0x63; d[237] = 0x82; d[238] = 0x53; d[239] = 0x63;
    int o = 240;
    d[o++] = 53; d[o++] = 1; d[o++] = (uint8_t)type;               /* message type */
    d[o++] = 61; d[o++] = 7; d[o++] = 1;                             /* client id */
    for (int i = 0; i < 6; i++) d[o++] = net_mac[i];
    if (type == 3 && dhcp_state == DHCP_REQUESTING && req_ip) {
        d[o++] = 50; d[o++] = 4; for (int i = 0; i < 4; i++) d[o++] = req_ip[i];
        if (srv) { d[o++] = 54; d[o++] = 4; for (int i = 0; i < 4; i++) d[o++] = srv[i]; }
    }
    d[o++] = 12; d[o++] = 7;                                         /* host name */
    d[o++] = 's'; d[o++] = 'h'; d[o++] = 'a'; d[o++] = 'r'; d[o++] = 'k'; d[o++] = 'o'; d[o++] = 's';
    d[o++] = 55; d[o++] = 5; d[o++] = 1; d[o++] = 3; d[o++] = 6; d[o++] = 51; d[o++] = 54; /* param list */
    d[o++] = 57; d[o++] = 2; d[o++] = 0x02; d[o++] = 0x40;           /* max msg 576 */
    d[o++] = 0xFF;
    /* BOOTP servers expect at least 300 bytes of payload. */
    if (o < 300) o = 300;
    uint16_t udplen = (uint16_t)(8 + o);

    uint8_t* udp = &pkt[off + 20];
    udp[0] = (sport >> 8) & 0xFF; udp[1] = sport & 0xFF;
    udp[2] = (dport >> 8) & 0xFF; udp[3] = dport & 0xFF;
    udp[4] = (udplen >> 8) & 0xFF; udp[5] = udplen & 0xFF;
    udp[6] = 0; udp[7] = 0;                                          /* no UDP csum */

    pkt[off + 0] = 0x45; pkt[off + 1] = 0;
    uint16_t tlen = (uint16_t)(20 + udplen);
    pkt[off + 2] = (tlen >> 8) & 0xFF; pkt[off + 3] = tlen & 0xFF;
    pkt[off + 4] = (uint8_t)(dhcp_xid >> 8); pkt[off + 5] = (uint8_t)dhcp_xid;   /* id */
    pkt[off + 6] = 0; pkt[off + 7] = 0;
    pkt[off + 8] = 0x40; pkt[off + 9] = IP_UDP;
    pkt[off + 10] = 0; pkt[off + 11] = 0;
    for (int i = 0; i < 4; i++) pkt[off + 12 + i] = use_unicast ? net_ip[i] : 0;
    for (int i = 0; i < 4; i++) pkt[off + 16 + i] = use_unicast ? srv[i] : 0xFF;
    uint16_t ic = csum16(&pkt[off], 20);
    pkt[off + 10] = (ic >> 8) & 0xFF; pkt[off + 11] = ic & 0xFF;
    net_send_raw(pkt, off + 20 + udplen);
}

static void dhcp_apply_lease(void) {
    for (int i = 0; i < 4; i++) {
        net_ip[i]   = dhcp_offered[i];
        net_mask[i] = dhcp_offer_mask[i];
        net_gw[i]   = dhcp_offer_gw[i];
        net_dns[i]  = dhcp_offer_dns[i];
    }
    if (!net_mask[0]) { net_mask[0] = 255; net_mask[1] = 255; net_mask[2] = 255; net_mask[3] = 0; }
    if (!net_dns[0] && !net_dns[1] && !net_dns[2] && !net_dns[3]) {
        for (int i = 0; i < 4; i++) net_dns[i] = net_gw[i];      /* home routers */
    }
    net_configured = 1;
    dhcp_bound_tick = uptime_ticks;
    if (dhcp_lease_secs < 60) dhcp_lease_secs = 60;
    if (dhcp_lease_secs > 30u * 86400u) dhcp_lease_secs = 30u * 86400u;   /* "infinite" */
    dhcp_state = DHCP_BOUND;
    dhcp_next_tick = uptime_ticks + (dhcp_lease_secs / 2) * TICKS_PER_SEC;   /* T1 */

    char msg[48];
    char b[8];
    msg[0] = '\0';
    strcpy(msg, "bound ");
    for (int i = 0; i < 4; i++) {
        int_to_string(net_ip[i], b);
        int l = (int)strlen(msg);
        strcpy(msg + l, b);
        if (i < 3) { l = (int)strlen(msg); msg[l] = '.'; msg[l + 1] = '\0'; }
    }
    dhcp_log(msg);
    /* In the text shells a lease notice is worth a line on the console; in
     * the desktop the Network window and tray icon show it instead. */
    if (!dhcp_verbose && current_kernel_mode != KERNEL_MODE_DESKTOP) {
        terminal_writestring("\nDHCP: ");
        terminal_writestring(msg);
        terminal_writestring("\n");
    }
    /* Pre-warm the ARP cache for the gateway so the first ping/wget does
     * not eat a round trip. Non-blocking: just a request. */
    if (net_gw[0] || net_gw[1] || net_gw[2] || net_gw[3]) {
        uint8_t dummy[6];
        if (!arp_lookup(net_gw, dummy)) {
            uint8_t bcm[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
            uint8_t apkt[42];
            uint32_t aoff = build_eth(apkt, bcm, ETH_ARP);
            apkt[aoff + 0] = 0; apkt[aoff + 1] = 1; apkt[aoff + 2] = 0x08; apkt[aoff + 3] = 0x00;
            apkt[aoff + 4] = 6; apkt[aoff + 5] = 4; apkt[aoff + 6] = 0; apkt[aoff + 7] = ARP_REQ;
            for (int i = 0; i < 6; i++) apkt[aoff + 8 + i] = net_mac[i];
            for (int i = 0; i < 4; i++) apkt[aoff + 14 + i] = net_ip[i];
            for (int i = 0; i < 6; i++) apkt[aoff + 18 + i] = 0;
            for (int i = 0; i < 4; i++) apkt[aoff + 24 + i] = net_gw[i];
            net_send_raw(apkt, 42);
        }
    }
}

static void dhcp_recv(const uint8_t* payload, uint32_t len, const uint8_t* src_ip) {
    (void)src_ip;
    if (len < 240) return;
    if (payload[0] != 2) return;                            /* BOOTREPLY only */
    uint32_t xid = ((uint32_t)payload[4] << 24) | ((uint32_t)payload[5] << 16) |
                   ((uint32_t)payload[6] << 8) | payload[7];
    if (xid != dhcp_xid) return;
    if (my_memcmp(&payload[28], net_mac, 6) != 0) return;   /* chaddr must be ours */
    if (payload[236] != 0x63 || payload[237] != 0x82 ||
        payload[238] != 0x53 || payload[239] != 0x63) return;

    uint32_t msgtype = 0;
    uint8_t  mask[4] = {0,0,0,0}, gw[4] = {0,0,0,0}, dns[4] = {0,0,0,0}, srv[4] = {0,0,0,0};
    uint32_t lease = 0;
    uint32_t p = 240;
    while (p + 1 < len) {
        uint8_t opt = payload[p];
        if (opt == 0xFF) break;
        if (opt == 0x00) { p++; continue; }                 /* pad: no length byte */
        uint8_t ol = payload[p + 1];
        if (p + 2 + ol > len) break;
        const uint8_t* v = &payload[p + 2];
        if (opt == 53 && ol == 1) msgtype = v[0];
        else if (opt == 1  && ol >= 4) { for (int i = 0; i < 4; i++) mask[i] = v[i]; }
        else if (opt == 3  && ol >= 4) { for (int i = 0; i < 4; i++) gw[i] = v[i]; }
        else if (opt == 6  && ol >= 4) { for (int i = 0; i < 4; i++) dns[i] = v[i]; }
        else if (opt == 54 && ol >= 4) { for (int i = 0; i < 4; i++) srv[i] = v[i]; }
        else if (opt == 51 && ol >= 4) {
            lease = ((uint32_t)v[0] << 24) | ((uint32_t)v[1] << 16) | ((uint32_t)v[2] << 8) | v[3];
        }
        p += 2 + ol;
    }
    if (!srv[0] && !srv[1] && !srv[2] && !srv[3]) {
        for (int i = 0; i < 4; i++) srv[i] = src_ip[i];
    }

    if (msgtype == 2 && dhcp_state == DHCP_SELECTING) {              /* OFFER */
        for (int i = 0; i < 4; i++) {
            dhcp_offered[i] = payload[16 + i];                       /* yiaddr */
            dhcp_offer_mask[i] = mask[i];
            dhcp_offer_gw[i] = gw[i];
            dhcp_offer_dns[i] = dns[i];
            dhcp_srv[i] = srv[i];
        }
        dhcp_lease_secs = lease;
        dhcp_state = DHCP_REQUESTING;
        dhcp_tries = 0;
        dhcp_log("offer received, requesting");
        dhcp_send(3, dhcp_offered, dhcp_srv, 0);
        dhcp_next_tick = uptime_ticks + 2 * TICKS_PER_SEC;
    } else if (msgtype == 5 && (dhcp_state == DHCP_REQUESTING ||
                                dhcp_state == DHCP_RENEWING ||
                                dhcp_state == DHCP_REBINDING)) {     /* ACK */
        for (int i = 0; i < 4; i++) {
            dhcp_offered[i] = payload[16 + i];
            if (mask[0]) dhcp_offer_mask[i] = mask[i];
            if (gw[0] || gw[1] || gw[2] || gw[3]) dhcp_offer_gw[i] = gw[i];
            if (dns[0] || dns[1] || dns[2] || dns[3]) dhcp_offer_dns[i] = dns[i];
            dhcp_srv[i] = srv[i];
        }
        if (lease) dhcp_lease_secs = lease;
        dhcp_apply_lease();
    } else if (msgtype == 6) {                                       /* NAK */
        dhcp_log("NAK from server, restarting");
        net_configured = 0;
        for (int i = 0; i < 4; i++) net_ip[i] = 0;
        dhcp_state = DHCP_INIT;
        dhcp_next_tick = uptime_ticks + 2 * TICKS_PER_SEC;
    }
}

static void dhcp_start_discover(void) {
    dhcp_xid = dhcp_rand();
    dhcp_start_tick = uptime_ticks;
    dhcp_state = DHCP_SELECTING;
    dhcp_tries = 0;
    dhcp_backoff_ms = 1000;
    dhcp_log("discovering");
    dhcp_send(1, NULL, NULL, 0);
    dhcp_next_tick = uptime_ticks + dhcp_backoff_ms + (dhcp_rand() % 300);
}

static void dhcp_use_linklocal(void) {
    net_ip[0] = 169; net_ip[1] = 254;
    net_ip[2] = (uint8_t)(1 + (net_mac[4] % 254));
    net_ip[3] = (uint8_t)(1 + (net_mac[5] % 254));
    net_mask[0] = 255; net_mask[1] = 255; net_mask[2] = 0; net_mask[3] = 0;
    for (int i = 0; i < 4; i++) { net_gw[i] = 0; net_dns[i] = 0; }
    net_configured = 1;
    dhcp_state = DHCP_LINKLOCAL;
    dhcp_log("no server, using link-local");
    dhcp_next_tick = uptime_ticks + 60 * TICKS_PER_SEC;   /* keep trying quietly */
}

/* Called from net_poll(): cheap when nothing is due. */
static void dhcp_tick(void) {
    /* Link tracking: a cable (re)plug restarts the lease. */
    if (net_has_link != dhcp_had_link) {
        dhcp_had_link = net_has_link;
        if (net_has_link) {
            dhcp_log("link up");
            dhcp_state = DHCP_INIT;
            dhcp_next_tick = uptime_ticks + 200;          /* let the PHY settle */
        } else {
            dhcp_log("link down");
            net_configured = 0;
            dhcp_state = DHCP_INIT;
        }
    }
    if (!net_has_link) return;
    if ((int32_t)(uptime_ticks - dhcp_next_tick) < 0) return;

    switch (dhcp_state) {
    case DHCP_INIT:
        dhcp_start_discover();
        break;

    case DHCP_SELECTING:
        if (++dhcp_tries >= 6) {                          /* ~1+2+4+8+16 s */
            dhcp_use_linklocal();
            break;
        }
        if (dhcp_backoff_ms < 16000) dhcp_backoff_ms *= 2;
        dhcp_send(1, NULL, NULL, 0);
        dhcp_next_tick = uptime_ticks + dhcp_backoff_ms + (dhcp_rand() % 500);
        break;

    case DHCP_REQUESTING:
        if (++dhcp_tries >= 4) {
            dhcp_log("no ACK, retrying discover");
            dhcp_state = DHCP_INIT;
            break;
        }
        dhcp_send(3, dhcp_offered, dhcp_srv, 0);
        dhcp_next_tick = uptime_ticks + 2 * TICKS_PER_SEC;
        break;

    case DHCP_BOUND:                                      /* T1 reached */
        dhcp_state = DHCP_RENEWING;
        dhcp_tries = 0;
        dhcp_send(3, dhcp_offered, dhcp_srv, 1);
        dhcp_next_tick = uptime_ticks + 10 * TICKS_PER_SEC;
        break;

    case DHCP_RENEWING: {
        uint32_t used = (uptime_ticks - dhcp_bound_tick) / TICKS_PER_SEC;
        if (used >= dhcp_lease_secs * 7 / 8) {            /* T2: broadcast */
            dhcp_state = DHCP_REBINDING;
            dhcp_send(3, dhcp_offered, dhcp_srv, 0);
        } else {
            dhcp_send(3, dhcp_offered, dhcp_srv, 1);
        }
        dhcp_next_tick = uptime_ticks + 10 * TICKS_PER_SEC;
        break;
    }

    case DHCP_REBINDING: {
        uint32_t used = (uptime_ticks - dhcp_bound_tick) / TICKS_PER_SEC;
        if (used >= dhcp_lease_secs) {                    /* expired */
            dhcp_log("lease expired");
            net_configured = 0;
            for (int i = 0; i < 4; i++) net_ip[i] = 0;
            dhcp_state = DHCP_INIT;
        } else {
            dhcp_send(3, dhcp_offered, dhcp_srv, 0);
            dhcp_next_tick = uptime_ticks + 10 * TICKS_PER_SEC;
        }
        break;
    }

    case DHCP_LINKLOCAL:
        /* Still no server? Try a fresh DISCOVER once a minute. */
        dhcp_xid = dhcp_rand();
        dhcp_send(1, NULL, NULL, 0);
        dhcp_state = DHCP_SELECTING;
        dhcp_tries = 5;                                   /* one shot, then back */
        dhcp_backoff_ms = 16000;
        dhcp_next_tick = uptime_ticks + 3 * TICKS_PER_SEC;
        break;
    }
}

/* Kick off (or restart) the client: used at boot and by the `dhcp` command. */
void net_dhcp_start(void) {
    if (!net_has_nic()) return;
    net_configured = 0;
    for (int i = 0; i < 4; i++) net_ip[i] = 0;
    dhcp_state = DHCP_INIT;
    dhcp_had_link = net_has_link;
    dhcp_next_tick = uptime_ticks;
    if (net_has_link) dhcp_tick();
}

/* Blocking variant for the shell: run the state machine until it settles
 * (bound or link-local) or the timeout passes. */
int net_dhcp(void) {
    if (!net_has_nic()) { terminal_writestring("net: no network card\n"); return -1; }
    if (!net_has_link) {
        /* Give the PHY a moment; the user may have just plugged the cable. */
        for (int i = 0; i < 20 && !net_has_link; i++) { delay_ms(100); net_poll(); }
        if (!net_has_link) { terminal_writestring("net: no link (cable unplugged?)\n"); return -1; }
    }
    dhcp_verbose = 1;
    net_dhcp_start();
    uint32_t deadline = uptime_ticks + 45 * TICKS_PER_SEC;
    while ((int32_t)(uptime_ticks - deadline) < 0) {
        delay_ms(20);
        net_poll();
        if (dhcp_state == DHCP_BOUND || dhcp_state == DHCP_LINKLOCAL) break;
    }
    dhcp_verbose = 0;
    if (dhcp_state == DHCP_BOUND) {
        char b[16];
        terminal_writestring("DHCP: IP ");
        for (int i = 0; i < 4; i++) { int_to_string(net_ip[i], b); terminal_writestring(b); if (i < 3) terminal_writestring("."); }
        terminal_writestring("  gateway ");
        for (int i = 0; i < 4; i++) { int_to_string(net_gw[i], b); terminal_writestring(b); if (i < 3) terminal_writestring("."); }
        terminal_writestring("  dns ");
        for (int i = 0; i < 4; i++) { int_to_string(net_dns[i], b); terminal_writestring(b); if (i < 3) terminal_writestring("."); }
        terminal_writestring("  lease ");
        int_to_string(dhcp_lease_secs, b); terminal_writestring(b);
        terminal_writestring("s\n");
        return 1;
    }
    if (dhcp_state == DHCP_LINKLOCAL) return 0;
    terminal_writestring("DHCP: timed out\n");
    return -1;
}

void net_cmd_dhcp(void) {
    net_dhcp();
}
