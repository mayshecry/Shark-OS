

#include "kernel.h"
#include "net.h"
#include "netdev.h"
#include "drivers/ne2000.h"

#define NE_CR    0x00
#define NE_PSTART 0x01
#define NE_PSTOP 0x02
#define NE_BNRY  0x03
#define NE_TPSR  0x04
#define NE_TBCR0 0x05
#define NE_TBCR1 0x06
#define NE_ISR   0x07
#define NE_RSAR0 0x08
#define NE_RSAR1 0x09
#define NE_RBCR0 0x0A
#define NE_RBCR1 0x0B
#define NE_IMR   0x0F
#define NE_RCR   0x0C
#define NE_TCR   0x0D
#define NE_DCR   0x0E
#define NE_DATA  0x10
#define NE_RESET 0x1F

#define NE_PAGE_RX_START 0x46
#define NE_PAGE_RX_END   0x60
#define NE_PAGE_TX       0x40

#define NE_CR_STOP 0x01
#define NE_CR_START 0x02
#define NE_CR_TXP 0x04
#define NE_CR_RD2 0x20
#define NE_CR_PS0 0x00
#define NE_CR_PS1 0x40
#define NE_ISR_PTX 0x08
#define NE_ISR_PRX 0x01
#define NE_ISR_RDC 0x40

typedef struct {
    uint16_t iobase;
    uint8_t  scratch[2048];
} ne2000_dev_t;

static ne2000_dev_t ne_dev;

static void ne_remote_read(uint8_t page, uint16_t off, uint8_t* dst, uint16_t len) {
    uint16_t addr = (uint16_t)(((uint16_t)page << 8) | off);
    outb(ne_dev.iobase + NE_CR, NE_CR_RD2 | NE_CR_STOP | NE_CR_PS0);
    outb(ne_dev.iobase + NE_RBCR0, (uint8_t)(len & 0xFF));
    outb(ne_dev.iobase + NE_RBCR1, (uint8_t)(len >> 8));
    outb(ne_dev.iobase + NE_RSAR0, (uint8_t)(addr & 0xFF));
    outb(ne_dev.iobase + NE_RSAR1, (uint8_t)(addr >> 8));
    outb(ne_dev.iobase + NE_CR, NE_CR_RD2 | NE_CR_START | NE_CR_PS0);
    for (uint16_t i = 0; i < len; i += 2) {
        uint16_t w = inw(ne_dev.iobase + NE_DATA);
        dst[i] = (uint8_t)(w & 0xFF);
        if (i + 1 < len) dst[i + 1] = (uint8_t)(w >> 8);
    }
    outb(ne_dev.iobase + NE_CR, NE_CR_RD2 | NE_CR_STOP | NE_CR_PS0);
}

static void ne_remote_write(uint8_t page, uint16_t off, const uint8_t* src, uint16_t len) {
    uint16_t addr = (uint16_t)(((uint16_t)page << 8) | off);
    outb(ne_dev.iobase + NE_CR, NE_CR_RD2 | NE_CR_STOP | NE_CR_PS0);
    outb(ne_dev.iobase + NE_RBCR0, (uint8_t)(len & 0xFF));
    outb(ne_dev.iobase + NE_RBCR1, (uint8_t)(len >> 8));
    outb(ne_dev.iobase + NE_RSAR0, (uint8_t)(addr & 0xFF));
    outb(ne_dev.iobase + NE_RSAR1, (uint8_t)(addr >> 8));
    outb(ne_dev.iobase + NE_CR, NE_CR_RD2 | NE_CR_START | NE_CR_PS1);
    for (uint16_t i = 0; i < len; i += 2) {
        uint16_t w = src[i] | ((i + 1 < len) ? ((uint16_t)src[i + 1] << 8) : 0);
        outw(ne_dev.iobase + NE_DATA, w);
    }
    outb(ne_dev.iobase + NE_CR, NE_CR_RD2 | NE_CR_STOP | NE_CR_PS0);
}

static void ne_wait_rdc(void) {
    for (int i = 0; i < 10000 && !(inb(ne_dev.iobase + NE_ISR) & NE_ISR_RDC); i++) { }
    outb(ne_dev.iobase + NE_ISR, NE_ISR_RDC);
}

static void ne_send_frame(const uint8_t* data, uint32_t len) {
    if (len > 1500) len = 1500;
    uint32_t plen = len < 60 ? 60 : len;
    if (plen & 1) plen++;
    memcpy(ne_dev.scratch, data, len);
    memset(ne_dev.scratch + len, 0, plen - len);

    ne_remote_write(NE_PAGE_TX, 0, ne_dev.scratch, (uint16_t)plen);
    ne_wait_rdc();

    outb(ne_dev.iobase + NE_TPSR, NE_PAGE_TX);
    outb(ne_dev.iobase + NE_TBCR0, (uint8_t)(plen & 0xFF));
    outb(ne_dev.iobase + NE_TBCR1, (uint8_t)(plen >> 8));
    outb(ne_dev.iobase + NE_CR, NE_CR_RD2 | NE_CR_TXP | NE_CR_START | NE_CR_PS0);

    for (int i = 0; i < 10000 && !(inb(ne_dev.iobase + NE_ISR) & NE_ISR_PTX); i++) { }
    outb(ne_dev.iobase + NE_ISR, NE_ISR_PTX);
}

static void ne_poll(void) {
    outb(ne_dev.iobase + NE_CR, NE_CR_RD2 | NE_CR_STOP | NE_CR_PS0);

    uint8_t curr = 0;
    uint8_t ps1 = (uint8_t)(NE_CR_RD2 | NE_CR_STOP | NE_CR_PS1);
    outb(ne_dev.iobase + NE_CR, ps1);
    curr = inb(ne_dev.iobase + NE_CR + 7);

    outb(ne_dev.iobase + NE_CR, NE_CR_RD2 | NE_CR_STOP | NE_CR_PS0);
    uint8_t bnry = inb(ne_dev.iobase + NE_BNRY);

    uint8_t page = (uint8_t)(bnry + 1);
    if (page >= NE_PAGE_RX_END) page = NE_PAGE_RX_START;

    int guard = 0;
    while (page != curr && guard++ < 64) {
        uint8_t hdr[4];
        ne_remote_read(page, 0, hdr, 4);
        uint8_t  next = hdr[1];
        uint16_t flen = (uint16_t)(hdr[2] | ((uint16_t)hdr[3] << 8));
        uint8_t  status = hdr[0];

        if (next <= NE_PAGE_RX_START || next >= NE_PAGE_RX_END) break;
        if ((status & NE_ISR_PRX) && flen >= 14 + 4 && flen <= 1518) {
            ne_remote_read(page, 4, ne_dev.scratch, flen);
            netdev_rx_submit(ne_dev.scratch, (uint32_t)(flen - 4));
        }

        bnry = (uint8_t)(next - 1);
        if (bnry < NE_PAGE_RX_START) bnry = NE_PAGE_RX_END - 1;
        outb(ne_dev.iobase + NE_BNRY, bnry);
        page = next;
    }

    outb(ne_dev.iobase + NE_CR, NE_CR_RD2 | NE_CR_START | NE_CR_PS0);
}

static void ne_read_station_address(uint8_t mac[6]) {
    ne_remote_read(0x00, 0x00, mac, 6);
}

static int ne2000_probe(uint8_t bus, uint8_t slot, uint8_t func, uint16_t vendor, uint16_t device) {
    (void)vendor;
    if (device != 0x8029 && device != 0x0940) return 0;

    uint32_t bar0 = pci_config_read(bus, slot, func, 0x10);
    if (!(bar0 & 1)) return 0;

    memset(&ne_dev, 0, sizeof(ne_dev));
    ne_dev.iobase = (uint16_t)(bar0 & 0xFFFC);
    net_pci_enable(bus, slot, func, 0x1);

    (void)inb(ne_dev.iobase + NE_RESET);
    for (volatile int i = 0; i < 200000; i++) { }
    outb(ne_dev.iobase + NE_CR, NE_CR_RD2 | NE_CR_STOP | NE_CR_PS0);
    for (volatile int i = 0; i < 200000; i++) { }

    outb(ne_dev.iobase + NE_DCR, 0x49);

    uint8_t mac[6];
    ne_read_station_address(mac);
    netdev_set_mac(mac);

    outb(ne_dev.iobase + NE_RBCR0, 0x00);
    outb(ne_dev.iobase + NE_RBCR1, 0x00);
    outb(ne_dev.iobase + NE_RCR, 0x06);
    outb(ne_dev.iobase + NE_TCR, 0x02);
    outb(ne_dev.iobase + NE_PSTART, NE_PAGE_RX_START);
    outb(ne_dev.iobase + NE_PSTOP, NE_PAGE_RX_END);
    outb(ne_dev.iobase + NE_BNRY, NE_PAGE_RX_START);
    outb(ne_dev.iobase + NE_ISR, 0xFF);
    outb(ne_dev.iobase + NE_IMR, 0x00);

    outb(ne_dev.iobase + NE_CR, NE_CR_RD2 | NE_CR_STOP | NE_CR_PS1);
    for (int i = 0; i < 6; i++) outb(ne_dev.iobase + NE_PSTART + i, mac[i]);
    outb(ne_dev.iobase + NE_CR + 7, NE_PAGE_RX_START + 1);
    for (int i = 0; i < 8; i++) outb(ne_dev.iobase + NE_RSAR0 + i, 0x00);
    outb(ne_dev.iobase + NE_CR, NE_CR_RD2 | NE_CR_START | NE_CR_PS0);
    outb(ne_dev.iobase + NE_TCR, 0x00);
    outb(ne_dev.iobase + NE_ISR, 0xFF);

    netdev_report_link(1);
    netdev_set_name(device == 0x8029 ? "Realtek RTL8029 (NE2000)"
                                     : "NE2000-compatible");
    return 1;
}

static const net_dev_t ne2000_driver = {
    "NE2000 compatible",
    0x10EC, 0x8029,
    ne_send_frame,
    ne_poll,
    ne2000_probe
};

const net_dev_t* ne2000_driver_get(void) { return &ne2000_driver; }
