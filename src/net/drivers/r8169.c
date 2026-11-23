

#include "kernel.h"
#include "net.h"
#include "netdev.h"
#include "drivers/r8169.h"

#define R8169_NRX 64
#define R8169_NTX 32
#define R8169_BUF 2048

#define R8169_DESC_OWN 0x80000000u
#define R8169_DESC_EOR 0x40000000u
#define R8169_DESC_FS  0x20000000u
#define R8169_DESC_LS  0x10000000u

#define R8169_IDR0   0x00
#define R8169_TNPDS  0x20
#define R8169_CMD    0x37
#define R8169_TPPOLL 0x38
#define R8169_IMR    0x3C
#define R8169_ISR    0x3E
#define R8169_TCR    0x40
#define R8169_RCR    0x44
#define R8169_RDSAR  0xE4

#define R8169_CMD_RST 0x10
#define R8169_CMD_RE  0x08
#define R8169_CMD_TE  0x04

#define R8169_TPPOLL_NPQ 0x40
#define R8169_RCR_VALUE  0x0000E70F
#define R8169_TCR_VALUE  0x03000700
#define R8169_RX_BUF_BITS 0x00001FF8u

typedef struct {
    uint32_t status;
    uint32_t vlan;
    uint32_t addr_lo;
    uint32_t addr_hi;
} __attribute__((packed)) r8169_desc_t;

typedef struct {
    uintptr_t mmio;
    uint32_t  iobase;
    r8169_desc_t* rx;
    r8169_desc_t* tx;
    uint8_t* rxb[R8169_NRX];
    uint8_t* txb[R8169_NTX];
    uint32_t rx_head;
    uint32_t tx_head;
} r8169_dev_t;

static r8169_dev_t r8169_dev;

static void r8169_w8(uint32_t off, uint8_t v) {
    if (r8169_dev.mmio) *(volatile uint8_t*)(r8169_dev.mmio + off) = v;
    else outb((uint16_t)(r8169_dev.iobase + off), v);
}

static uint8_t r8169_r8(uint32_t off) {
    return r8169_dev.mmio ? *(volatile uint8_t*)(r8169_dev.mmio + off)
                          : inb((uint16_t)(r8169_dev.iobase + off));
}

static void r8169_w16(uint32_t off, uint16_t v) {
    if (r8169_dev.mmio) *(volatile uint16_t*)(r8169_dev.mmio + off) = v;
    else outw((uint16_t)(r8169_dev.iobase + off), v);
}

static void r8169_w32(uint32_t off, uint32_t v) {
    if (r8169_dev.mmio) *(volatile uint32_t*)(r8169_dev.mmio + off) = v;
    else outl((uint16_t)(r8169_dev.iobase + off), v);
}

static void r8169_send_frame(const uint8_t* data, uint32_t len) {
    r8169_dev_t* dev = &r8169_dev;
    uint32_t idx = dev->tx_head;
    volatile r8169_desc_t* d = &dev->tx[idx];

    for (int i = 0; i < 20 && (d->status & R8169_DESC_OWN); i++) delay_ms(1);
    if (d->status & R8169_DESC_OWN) return;

    if (len > R8169_BUF) len = R8169_BUF;
    memcpy(dev->txb[idx], data, len);
    if (len < 60) { memset(dev->txb[idx] + len, 0, 60 - len); len = 60; }

    d->addr_lo = (uint32_t)virt_to_phys(dev->txb[idx]);
    d->addr_hi = 0;
    d->vlan = 0;
    d->status = R8169_DESC_OWN | R8169_DESC_FS | R8169_DESC_LS | len |
                (idx == R8169_NTX - 1 ? R8169_DESC_EOR : 0);
    dev->tx_head = (idx + 1) & (R8169_NTX - 1);
    asm volatile("" ::: "memory");
    r8169_w8(R8169_TPPOLL, R8169_TPPOLL_NPQ);
}

static void r8169_poll(void) {
    r8169_dev_t* dev = &r8169_dev;
    for (int n = 0; n < R8169_NRX; n++) {
        uint32_t idx = dev->rx_head;
        volatile r8169_desc_t* d = &dev->rx[idx];
        if (d->status & R8169_DESC_OWN) break;
        uint32_t status = d->status;
        uint32_t len = status & 0x3FFF;
        if ((status & (R8169_DESC_FS | R8169_DESC_LS)) == (R8169_DESC_FS | R8169_DESC_LS) &&
            len >= 18 && len <= R8169_BUF)
            netdev_rx_submit(dev->rxb[idx], len - 4);
        d->status = R8169_DESC_OWN | R8169_RX_BUF_BITS |
                    (idx == R8169_NRX - 1 ? R8169_DESC_EOR : 0);
        dev->rx_head = (idx + 1) & (R8169_NRX - 1);
    }
}

static int r8169_init(uint8_t bus, uint8_t slot, uint8_t func, uint16_t device) {
    r8169_dev_t* dev = &r8169_dev;
    uint32_t bar0 = pci_config_read(bus, slot, func, 0x10);
    uint32_t bar1 = pci_config_read(bus, slot, func, 0x14);
    net_pci_enable(bus, slot, func, 0x7);

    dev->mmio = 0;
    dev->iobase = 0;
    if (bar0 & 1) {
        dev->iobase = bar0 & 0xFFFFFFFC;
    } else {
        uint64_t address = bar0 & 0xFFFFFFF0;
        if ((bar0 & 6) == 4) address |= (uint64_t)bar1 << 32;
        if (!address || address >= 137438953472ULL) return 0;
#ifdef SHARKOS_32BIT
        if (address > 0xFFFFFFFFULL) return 0;
#endif
        dev->mmio = (uintptr_t)address;
    }

    r8169_w8(R8169_CMD, R8169_CMD_RST);
    for (int i = 0; i < 100 && (r8169_r8(R8169_CMD) & R8169_CMD_RST); i++) delay_ms(1);
    if (r8169_r8(R8169_CMD) & R8169_CMD_RST) return 0;

    uint8_t mac[6];
    for (int i = 0; i < 6; i++) mac[i] = r8169_r8(R8169_IDR0 + (uint32_t)i);
    netdev_set_mac(mac);

    dev->rx = (r8169_desc_t*)kmalloc(sizeof(r8169_desc_t) * R8169_NRX + 256);
    dev->tx = (r8169_desc_t*)kmalloc(sizeof(r8169_desc_t) * R8169_NTX + 256);
    if (!dev->rx || !dev->tx) return 0;
    dev->rx = (r8169_desc_t*)(((uintptr_t)dev->rx + 255) & ~(uintptr_t)255);
    dev->tx = (r8169_desc_t*)(((uintptr_t)dev->tx + 255) & ~(uintptr_t)255);
    memset(dev->rx, 0, sizeof(r8169_desc_t) * R8169_NRX);
    memset(dev->tx, 0, sizeof(r8169_desc_t) * R8169_NTX);
    if (virt_to_phys(dev->rx) > 0xFFFFFFFFu || virt_to_phys(dev->tx) > 0xFFFFFFFFu) return 0;

    for (int i = 0; i < R8169_NRX; i++) {
        dev->rxb[i] = (uint8_t*)kmalloc(R8169_BUF);
        if (!dev->rxb[i] || virt_to_phys(dev->rxb[i]) > 0xFFFFFFFFu) return 0;
        dev->rx[i].addr_lo = (uint32_t)virt_to_phys(dev->rxb[i]);
        dev->rx[i].status = R8169_DESC_OWN | R8169_RX_BUF_BITS |
                            (i == R8169_NRX - 1 ? R8169_DESC_EOR : 0);
    }
    for (int i = 0; i < R8169_NTX; i++) {
        dev->txb[i] = (uint8_t*)kmalloc(R8169_BUF);
        if (!dev->txb[i] || virt_to_phys(dev->txb[i]) > 0xFFFFFFFFu) return 0;
        dev->tx[i].addr_lo = (uint32_t)virt_to_phys(dev->txb[i]);
    }
    dev->rx_head = 0;
    dev->tx_head = 0;

    r8169_w32(R8169_RDSAR, (uint32_t)virt_to_phys(dev->rx));
    r8169_w32(R8169_RDSAR + 4, 0);
    r8169_w32(R8169_TNPDS, (uint32_t)virt_to_phys(dev->tx));
    r8169_w32(R8169_TNPDS + 4, 0);
    r8169_w16(R8169_IMR, 0);
    r8169_w16(R8169_ISR, 0xFFFF);
    r8169_w32(R8169_RCR, R8169_RCR_VALUE);
    r8169_w32(R8169_TCR, R8169_TCR_VALUE);
    r8169_w8(R8169_CMD, R8169_CMD_RE | R8169_CMD_TE);

    netdev_report_link(1);
    netdev_set_name(device == 0x8169 ? "Realtek RTL8169"
                                     : "Realtek RTL8111/8168/8211/8411");
    return 1;
}

static int r8169_probe(uint8_t bus, uint8_t slot, uint8_t func, uint16_t vendor, uint16_t device) {
    if (vendor != 0x10EC || (device != 0x8168 && device != 0x8169)) return 0;
    memset(&r8169_dev, 0, sizeof(r8169_dev));
    return r8169_init(bus, slot, func, device);
}

static const net_dev_t r8169_driver = {
    "Realtek RTL8169/8111",
    0x10EC, 0x8168,
    r8169_send_frame,
    r8169_poll,
    r8169_probe
};

const net_dev_t* r8169_driver_get(void) { return &r8169_driver; }
