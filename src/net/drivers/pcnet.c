

#include "kernel.h"
#include "net.h"
#include "netdev.h"
#include "drivers/pcnet.h"

#define PN_RDP       0x10
#define PN_RAP       0x14
#define PN_RESET     0x18
#define PN_BDP       0x1C
#define PN_WIO_RESET 0x14

#define CSR0_INIT 0x0001
#define CSR0_STRT 0x0002
#define CSR0_STOP 0x0004
#define CSR0_TDMD 0x0008
#define CSR0_IENA 0x0040
#define CSR0_IDON 0x0100
#define CSR0_TINT 0x0200
#define CSR0_RINT 0x0400
#define CSR0_MISS 0x1000

#define PD_OWN  0x80000000u
#define PD_ERR  0x40000000u
#define PD_STP  0x02000000u
#define PD_ENP  0x01000000u
#define PD_ONES 0x0000F000u
#define PD_BCNT(n) (((uint32_t)(-(int32_t)(n))) & 0x0FFFu)

#define PN_TX      8
#define PN_RX      16
#define PN_BUF     1536
#define PN_LOG2_TX 3
#define PN_LOG2_RX 4

struct pn_desc {
    uint32_t addr;
    uint32_t status;
    uint32_t misc;
    uint32_t user;
} __attribute__((packed));

struct pn_initblk {
    uint16_t mode;
    uint8_t  rlen;
    uint8_t  tlen;
    uint8_t  padr[6];
    uint16_t reserved;
    uint8_t  ladrf[8];
    uint32_t rdra;
    uint32_t tdra;
} __attribute__((packed));

typedef struct {
    uint32_t         iobase;
    uintptr_t        mmio;
    uint8_t          mac[6];
    struct pn_desc*  tx;
    struct pn_desc*  rx;
    uint8_t*         txb[PN_TX];
    uint8_t*         rxb[PN_RX];
    struct pn_initblk* ib;
    int              rx_next;
    int              tx_next;
    uint32_t         last_link_tick;
} pcnet_dev_t;

static pcnet_dev_t pcnet_dev;

static void pn_w32(pcnet_dev_t* dev, uint32_t off, uint32_t v) {
    if (dev->mmio) *(volatile uint32_t*)(dev->mmio + off) = v;
    else outl(dev->iobase + off, v);
}

static uint32_t pn_r32(pcnet_dev_t* dev, uint32_t off) {
    if (dev->mmio) return *(volatile uint32_t*)(dev->mmio + off);
    return inl(dev->iobase + off);
}

static void pn_csrw(pcnet_dev_t* dev, int c, uint32_t v) {
    pn_w32(dev, PN_RAP, (uint32_t)c);
    pn_w32(dev, PN_RDP, v);
}

static uint32_t pn_csrr(pcnet_dev_t* dev, int c) {
    pn_w32(dev, PN_RAP, (uint32_t)c);
    return pn_r32(dev, PN_RDP) & 0xFFFF;
}

static void pn_bcrw(pcnet_dev_t* dev, int c, uint32_t v) {
    pn_w32(dev, PN_RAP, (uint32_t)c);
    pn_w32(dev, PN_BDP, v);
}

static uint32_t pn_bcrr(pcnet_dev_t* dev, int c) {
    pn_w32(dev, PN_RAP, (uint32_t)c);
    return pn_r32(dev, PN_BDP) & 0xFFFF;
}

static void* pn_alloc16(uint32_t size) {
    uint8_t* m = (uint8_t*)kmalloc(size + 16);
    if (!m) return NULL;
    uintptr_t a = ((uintptr_t)m + 15) & ~(uintptr_t)15;
    memset((void*)a, 0, size);
    return (void*)a;
}

static void pn_read_link(pcnet_dev_t* dev) {
    uint32_t bcr4 = pn_bcrr(dev, 4);
    netdev_report_link((bcr4 & 0x0040) ? ((bcr4 & 0x8000) ? 1 : 0) : 1);
}

static int pn_init_chip(pcnet_dev_t* dev) {
    (void)pn_r32(dev, PN_RESET);
    if (dev->mmio) (void)*(volatile uint16_t*)(dev->mmio + PN_WIO_RESET);
    else (void)inw(dev->iobase + PN_WIO_RESET);
    delay_ms(2);

    pn_w32(dev, PN_RDP, 0);

    for (int i = 0; i < 6; i++) {
        if (dev->mmio) dev->mac[i] = *(volatile uint8_t*)(dev->mmio + i);
        else dev->mac[i] = inb(dev->iobase + i);
    }
    netdev_set_mac(dev->mac);

    pn_bcrw(dev, 20, 0x0002);

    dev->ib = (struct pn_initblk*)pn_alloc16(sizeof(*dev->ib));
    dev->tx = (struct pn_desc*)pn_alloc16(sizeof(struct pn_desc) * PN_TX);
    dev->rx = (struct pn_desc*)pn_alloc16(sizeof(struct pn_desc) * PN_RX);
    if (!dev->ib || !dev->tx || !dev->rx) return 0;

    for (int i = 0; i < PN_TX; i++) {
        dev->txb[i] = (uint8_t*)pn_alloc16(PN_BUF);
        if (!dev->txb[i]) return 0;
        dev->tx[i].addr = (uint32_t)virt_to_phys(dev->txb[i]);
        dev->tx[i].status = PD_ONES;
        dev->tx[i].misc = 0;
    }
    for (int i = 0; i < PN_RX; i++) {
        dev->rxb[i] = (uint8_t*)pn_alloc16(PN_BUF);
        if (!dev->rxb[i]) return 0;
        dev->rx[i].addr = (uint32_t)virt_to_phys(dev->rxb[i]);
        dev->rx[i].status = PD_OWN | PD_ONES | PD_BCNT(PN_BUF);
        dev->rx[i].misc = 0;
    }
    dev->rx_next = 0;
    dev->tx_next = 0;

    dev->ib->mode = 0x8000;
    dev->ib->rlen = (uint8_t)(PN_LOG2_RX << 4);
    dev->ib->tlen = (uint8_t)(PN_LOG2_TX << 4);
    for (int i = 0; i < 6; i++) dev->ib->padr[i] = dev->mac[i];
    for (int i = 0; i < 8; i++) dev->ib->ladrf[i] = 0;
    dev->ib->rdra = (uint32_t)virt_to_phys(dev->rx);
    dev->ib->tdra = (uint32_t)virt_to_phys(dev->tx);

    uint32_t ib = (uint32_t)virt_to_phys(dev->ib);
    pn_csrw(dev, 1, ib & 0xFFFF);
    pn_csrw(dev, 2, ib >> 16);
    pn_csrw(dev, 3, 0x5F00);
    pn_csrw(dev, 4, 0x0915);

    pn_csrw(dev, 0, CSR0_INIT);
    int ok = 0;
    for (int t = 0; t < 1000; t++) {
        if (pn_csrr(dev, 0) & CSR0_IDON) { ok = 1; break; }
        delay_ms(1);
    }
    if (!ok) return 0;
    pn_csrw(dev, 0, CSR0_IDON | CSR0_STRT);
    pn_read_link(dev);
    return 1;
}

static void pn_send_frame(const uint8_t* data, uint32_t len) {
    pcnet_dev_t* dev = &pcnet_dev;
    struct pn_desc* d = &dev->tx[dev->tx_next];

    for (int i = 0; i < 20 && (d->status & PD_OWN); i++) delay_ms(1);
    if (d->status & PD_OWN) return;
    if (len > PN_BUF) len = PN_BUF;
    memcpy(dev->txb[dev->tx_next], data, len);
    if (len < 60) { memset(dev->txb[dev->tx_next] + len, 0, 60 - len); len = 60; }
    d->misc = 0;
    d->status = PD_OWN | PD_STP | PD_ENP | PD_ONES | PD_BCNT(len);
    pn_csrw(dev, 0, CSR0_TDMD);
    dev->tx_next = (dev->tx_next + 1) % PN_TX;
}

static void pn_poll(void) {
    pcnet_dev_t* dev = &pcnet_dev;

    uint32_t csr0 = pn_csrr(dev, 0);
    if (csr0 & 0x7F00) pn_csrw(dev, 0, csr0 & 0x7F00);

    for (int n = 0; n < PN_RX; n++) {
        struct pn_desc* d = &dev->rx[dev->rx_next];
        if (d->status & PD_OWN) break;
        uint32_t st = d->status;
        uint32_t mcnt = d->misc & 0x0FFF;
        if (!(st & PD_ERR) && (st & PD_STP) && (st & PD_ENP) &&
            mcnt > 18 && mcnt <= PN_BUF)
            netdev_rx_submit(dev->rxb[dev->rx_next], mcnt - 4);
        d->misc = 0;
        d->status = PD_OWN | PD_ONES | PD_BCNT(PN_BUF);
        dev->rx_next = (dev->rx_next + 1) % PN_RX;
    }

    if (uptime_ticks - dev->last_link_tick >= 500) {
        dev->last_link_tick = uptime_ticks;
        pn_read_link(dev);
    }
}

static int pcnet_probe(uint8_t bus, uint8_t slot, uint8_t func, uint16_t vendor, uint16_t device) {
    if (vendor != 0x1022 || (device != 0x2000 && device != 0x2001)) return 0;

    pcnet_dev_t* dev = &pcnet_dev;
    memset(dev, 0, sizeof(*dev));
    uint32_t bar0 = pci_config_read(bus, slot, func, 0x10);
    net_pci_enable(bus, slot, func, 0x7);
    if (bar0 & 0x1) {
        dev->iobase = bar0 & 0xFFFFFFFC;
        dev->mmio = 0;
    } else {
        dev->mmio = (uintptr_t)(bar0 & 0xFFFFFFF0);
        dev->iobase = 0;
    }
    return pn_init_chip(dev);
}

static const net_dev_t pcnet_driver = {
    "AMD PCnet-PCI",
    0x1022, 0x2000,
    pn_send_frame,
    pn_poll,
    pcnet_probe
};

const net_dev_t* pcnet_driver_get(void) { return &pcnet_driver; }
