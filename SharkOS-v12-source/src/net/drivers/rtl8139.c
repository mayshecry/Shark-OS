

#include "kernel.h"
#include "net.h"
#include "netdev.h"
#include "drivers/rtl8139.h"

#define RTL_RX_SIZE 8192
#define RTL_TX_N    4
#define RTL_TX_MAX  1792

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
#define RTL_CR_BUFE 0x01

#define RTL_ISR_ROK  0x0001
#define RTL_ISR_RER  0x0002
#define RTL_ISR_TOK  0x0004
#define RTL_ISR_TER  0x0008
#define RTL_ISR_RXOVW 0x0010
#define RTL_ISR_FOVW  0x0040

#define RTL_TSD_OWN 0x2000

#define RTL_RCR_VALUE (0x0000000Fu | (1u << 7) | (7u << 8) | (7u << 13))
#define RTL_TCR_VALUE ((7u << 8) | (3u << 24))

typedef struct {
    uint16_t iobase;
    uint8_t* rx_ring;
    uint32_t rx_pos;
    uint32_t tx_next;
    uint8_t tx_buf[RTL_TX_MAX + 8] __attribute__((aligned(16)));
    uint8_t mac[6];
} rtl8139_dev_t;

static rtl8139_dev_t rtl_dev;

static void rtl_reset(rtl8139_dev_t* dev) {
    outb(dev->iobase + RTL_CONFIG1, 0x00);
    outb(dev->iobase + RTL_CR, RTL_CR_RST);
    for (int i = 0; i < 1000; i++) {
        if ((inb(dev->iobase + RTL_CR) & RTL_CR_RST) == 0) break;
        delay_ms(1);
    }
}

static void rtl_read_link(rtl8139_dev_t* dev) {
    uint8_t msr = inb(dev->iobase + RTL_MSR);
    netdev_report_link((msr & 0x04) ? 0 : 1);
}

static int rtl_init_chip(rtl8139_dev_t* dev) {
    dev->rx_ring = (uint8_t*)kmalloc(RTL_RX_SIZE + 16 + 1536);
    if (!dev->rx_ring) return 0;
    memset(dev->rx_ring, 0, RTL_RX_SIZE + 16 + 1536);
    dev->rx_pos = 0;
    dev->tx_next = 0;

    for (int i = 0; i < 6; i++) dev->mac[i] = inb(dev->iobase + RTL_IDR0 + i);
    netdev_set_mac(dev->mac);

    outl(dev->iobase + RTL_RBSTART, (uint32_t)virt_to_phys(dev->rx_ring));
    outb(dev->iobase + RTL_CR, RTL_CR_RE | RTL_CR_TE);
    outl(dev->iobase + RTL_RCR, RTL_RCR_VALUE);
    outl(dev->iobase + RTL_TCR, RTL_TCR_VALUE);

    outw(dev->iobase + RTL_ISR, 0xFFFF);
    outw(dev->iobase + RTL_IMR, 0);
    outw(dev->iobase + RTL_CAPR, (uint16_t)(0 - 16));

    rtl_read_link(dev);
    return 1;
}

static uint16_t rtl_tsd(rtl8139_dev_t* dev) {
    return inl(dev->iobase + RTL_TSD0 + dev->tx_next * 4) & 0xFFFF;
}

static void rtl_send_frame(const uint8_t* data, uint32_t len) {
    rtl8139_dev_t* dev = &rtl_dev;
    if (len > RTL_TX_MAX) len = RTL_TX_MAX;
    for (int i = 0; i < 20 && !(rtl_tsd(dev) & RTL_TSD_OWN); i++) delay_ms(1);

    memcpy(dev->tx_buf, data, len);
    while (len < 60) dev->tx_buf[len++] = 0;

    outl(dev->iobase + RTL_TSAD0 + dev->tx_next * 4, (uint32_t)virt_to_phys(dev->tx_buf));
    outl(dev->iobase + RTL_TSD0 + dev->tx_next * 4, len & 0x1FFF);
    dev->tx_next = (dev->tx_next + 1) % RTL_TX_N;
}

static void rtl_rx_reset(rtl8139_dev_t* dev) {
    outb(dev->iobase + RTL_CR, RTL_CR_TE);
    outb(dev->iobase + RTL_CR, RTL_CR_RE | RTL_CR_TE);
    outl(dev->iobase + RTL_RCR, RTL_RCR_VALUE);
    dev->rx_pos = 0;
    outw(dev->iobase + RTL_CAPR, (uint16_t)(0 - 16));
    outw(dev->iobase + RTL_ISR, 0xFFFF);
}

static void rtl_poll(void) {
    rtl8139_dev_t* dev = &rtl_dev;
    uint16_t isr = inw(dev->iobase + RTL_ISR);
    if (isr) outw(dev->iobase + RTL_ISR, isr);
    if (isr & (RTL_ISR_RXOVW | RTL_ISR_FOVW)) {
        rtl_rx_reset(dev);
        return;
    }

    int guard = 0;
    while (!(inb(dev->iobase + RTL_CR) & RTL_CR_BUFE) && guard++ < 64) {
        const uint8_t* hdr = dev->rx_ring + dev->rx_pos;
        uint16_t status = (uint16_t)(hdr[0] | (hdr[1] << 8));
        uint16_t flen   = (uint16_t)(hdr[2] | (hdr[3] << 8));

        if (flen == 0xFFF0) break;
        if (flen < 4 || flen > 4 + 1514) {
            rtl_rx_reset(dev);
            return;
        }
        if ((status & RTL_ISR_ROK) && flen >= 4 + 14)
            netdev_rx_submit(hdr + 4, (uint32_t)flen - 4);

        dev->rx_pos = (dev->rx_pos + 4 + flen + 3) & ~3u;
        if (dev->rx_pos >= RTL_RX_SIZE) dev->rx_pos -= RTL_RX_SIZE;
        outw(dev->iobase + RTL_CAPR, (uint16_t)(dev->rx_pos - 16));
    }

    rtl_read_link(dev);
}

static int rtl8139_probe(uint8_t bus, uint8_t slot, uint8_t func, uint16_t vendor, uint16_t device) {
    if (vendor != 0x10EC || device != 0x8139) return 0;

    rtl8139_dev_t* dev = &rtl_dev;
    memset(dev, 0, sizeof(*dev));
    dev->iobase = (uint16_t)(pci_config_read(bus, slot, func, 0x10) & 0xFFFFFFFC);
    net_pci_enable(bus, slot, func, 0x5);
    rtl_reset(dev);
    return rtl_init_chip(dev);
}

static const net_dev_t rtl8139_driver = {
    "Realtek RTL8139",
    0x10EC, 0x8139,
    rtl_send_frame,
    rtl_poll,
    rtl8139_probe
};

const net_dev_t* rtl8139_driver_get(void) { return &rtl8139_driver; }
