

#include "kernel.h"
#include "net.h"
#include "netdev.h"
#include "drivers/r8169.h"

#define R8169_NRX 64
#define R8169_NTX 32
#define R8169_BUF 2048

#define R8169_DESC_OWN 0x80000000u
#define R8169_DESC_EOR 0x40000000u
#define R8169_DESC_FS 0x20000000u
#define R8169_DESC_LS 0x10000000u

#define R8169_IDR0 0x00
#define R8169_TNPDS 0x20
#define R8169_CMD 0x37
#define R8169_TPPOLL 0x38
#define R8169_IMR 0x3C
#define R8169_ISR 0x3E
#define R8169_TCR 0x40
#define R8169_RCR 0x44
#define R8169_RDSAR 0xE4

#define R8169_CMD_RST 0x10
#define R8169_CMD_RE 0x08
#define R8169_CMD_TE 0x04

#define R8169_TPPOLL_NPQ 0x40
#define R8169_TCR_VALUE 0x03000700
#define R8169_RX_ERROR (1u << 21)

typedef struct {
    uint32_t status;
    uint32_t vlan;
    uint32_t addr_lo;
    uint32_t addr_hi;
} __attribute__((packed)) r8169_desc_t;

typedef struct {
    uintptr_t mmio;
    uint32_t iobase;
    r8169_desc_t *rx;
    r8169_desc_t *tx;
    uint8_t *rxb[R8169_NRX];
    uint8_t *txb[R8169_NTX];
    uint32_t rx_head;
    uint32_t tx_head;
} r8169_dev_t;

static r8169_dev_t r8169_dev;

static void r8169_w8(uint32_t off, uint8_t v) {
    if (r8169_dev.mmio)
        *(volatile uint8_t *)(r8169_dev.mmio + off) = v;
    else
        outb((uint16_t)(r8169_dev.iobase + off), v);
}

static uint8_t r8169_r8(uint32_t off) {
    return r8169_dev.mmio ? *(volatile uint8_t *)(r8169_dev.mmio + off)
                          : inb((uint16_t)(r8169_dev.iobase + off));
}

static void r8169_w16(uint32_t off, uint16_t v) {
    if (r8169_dev.mmio)
        *(volatile uint16_t *)(r8169_dev.mmio + off) = v;
    else
        outw((uint16_t)(r8169_dev.iobase + off), v);
}

static void r8169_w32(uint32_t off, uint32_t v) {
    if (r8169_dev.mmio)
        *(volatile uint32_t *)(r8169_dev.mmio + off) = v;
    else
        outl((uint16_t)(r8169_dev.iobase + off), v);
}

static uint32_t r8169_r32(unsigned off) {
    return r8169_dev.mmio ? *(volatile uint32_t *)(r8169_dev.mmio + off) : inl(r8169_dev.iobase + off);
}

static int busy, attached;
static uint8_t phy;
static uint32_t sampled, mac_xid;
unsigned r8169_xid(void) { return mac_xid; }
static void sample_link(void) {
    phy = r8169_r8(0x6c);
    netdev_report_link(phy != 255 && (phy & 2));
    sampled = uptime_ticks;
}
unsigned r8169_speed(void) {
    if (!attached || phy == 255 || !(phy & 2))
        return 0;
    return phy & 16 ? 1000 : phy & 8 ? 100 : phy & 4 ? 10 : 0;
}

static void r8169_send_frame(const uint8_t *data, uint32_t len) {
    if (!attached || !data || !len || len > R8169_BUF)
        return;
    unsigned i = r8169_dev.tx_head;
    volatile r8169_desc_t *d = r8169_dev.tx + i;
    for (int t = 0; t < 20 && (d->status & R8169_DESC_OWN); t++)
        delay_ms(1);
    if (d->status & R8169_DESC_OWN)
        return;
    memcpy(r8169_dev.txb[i], data, len);
    if (len < 60) {
        memset(r8169_dev.txb[i] + len, 0, 60 - len);
        len = 60;
    }
    d->vlan = 0;
    __atomic_thread_fence(__ATOMIC_RELEASE);
    d->status =
        R8169_DESC_OWN | R8169_DESC_FS | R8169_DESC_LS | len | (i + 1 == R8169_NTX ? R8169_DESC_EOR : 0);
    __atomic_thread_fence(__ATOMIC_RELEASE);
    r8169_dev.tx_head = (i + 1) % R8169_NTX;
    r8169_w8(R8169_TPPOLL, R8169_TPPOLL_NPQ);
}

static void r8169_poll(void) {
    if (!attached || busy)
        return;
    busy = 1;
    if (uptime_ticks - sampled >= TICKS_PER_SEC / 10)
        sample_link();
    for (unsigned n = 0; n < R8169_NRX; n++) {
        unsigned i = r8169_dev.rx_head;
        volatile r8169_desc_t *d = r8169_dev.rx + i;
        uint32_t flags = d->status, len = flags & 0x3fff;
        if (flags & R8169_DESC_OWN)
            break;
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        if ((flags & (R8169_DESC_FS | R8169_DESC_LS | R8169_RX_ERROR)) == (R8169_DESC_FS | R8169_DESC_LS) &&
            len >= 18 && len <= R8169_BUF)
            netdev_rx_submit(r8169_dev.rxb[i], len - 4);
        d->vlan = 0;
        __atomic_thread_fence(__ATOMIC_RELEASE);
        d->status = R8169_DESC_OWN | R8169_BUF | (i + 1 == R8169_NRX ? R8169_DESC_EOR : 0);
        r8169_dev.rx_head = (i + 1) % R8169_NRX;
    }
    busy = 0;
}

static int find_bar(uint8_t bus, uint8_t slot, uint8_t func) {
    uintptr_t io = 0;
    for (unsigned i = 0; i < 6; i++) {
        uint32_t v = pci_config_read(bus, slot, func, 0x10 + i * 4);
        if (!v || v == 0xffffffffu)
            continue;
        if (v & 1) {
            uint32_t base = v & ~3u;
            if (i == 0 && base && base <= 0xff00)
                io = base;
            continue;
        }
        uint64_t base = v & ~15u;
        if ((v & 6) == 4) {
            if (i == 5)
                break;
            base |= (uint64_t)pci_config_read(bus, slot, func, 0x10 + 4 * (++i)) << 32;
        } else if ((v & 6) != 0)
            continue;
        if (!base)
            continue;
        uint64_t limit = sizeof(uintptr_t) == 4 ? (1ull << 32) : (1ull << 37);
        if (base <= limit - 256) {
            r8169_dev.mmio = (uintptr_t)base;
            return 1;
        }
        break;
    }
    r8169_dev.iobase = io;
    return io != 0;
}

struct dma_area {
    r8169_desc_t rx[R8169_NRX];
    r8169_desc_t tx[R8169_NTX];
    uint8_t receive[R8169_NRX][R8169_BUF], send[R8169_NTX][R8169_BUF];
};
static struct dma_area *dma;
static int setup_rings(void) {
    if (!dma) {
        void *p = kmalloc(sizeof(*dma) + 255);
        if (!p)
            return 0;
        uintptr_t base = ((uintptr_t)p + 255) & ~(uintptr_t)255;
        uintptr_t phys = virt_to_phys((void *)base);
        if ((phys & 255) || phys > 0xffffffffu - (sizeof(*dma) - 1))
            return 0;
        dma = (struct dma_area *)base;
    }
    r8169_dev.rx = dma->rx;
    r8169_dev.tx = dma->tx;
    memset(dma->rx, 0, sizeof dma->rx);
    memset(dma->tx, 0, sizeof dma->tx);
    for (unsigned i = 0; i < R8169_NRX; i++) {
        r8169_dev.rxb[i] = dma->receive[i];
        dma->rx[i].addr_lo = virt_to_phys(dma->receive[i]);
        dma->rx[i].status = R8169_DESC_OWN | R8169_BUF | (i + 1 == R8169_NRX ? R8169_DESC_EOR : 0);
    }
    for (unsigned i = 0; i < R8169_NTX; i++) {
        r8169_dev.txb[i] = dma->send[i];
        dma->tx[i].addr_lo = virt_to_phys(dma->send[i]);
        dma->tx[i].status = (i + 1 == R8169_NTX ? R8169_DESC_EOR : 0);
    }
    return 1;
}

static unsigned rx_config(unsigned xid) {
    unsigned family = xid & 0x7cf;
    unsigned mode = 0xe000;
    switch (family) {
    case 0x4c0:
    case 0x509:
    case 0x5c8:
    case 0x541:
    case 0x6c0:
    case 0x502:
        mode = 0xc800;
        break;
    default:
        if ((family & 0x7c0) == 0x3c0 || (family & 0x7c0) == 0x480 || (family & 0x7c8) == 0x2c8)
            mode = 0xc000;
        else if ((family & 0x7c0) == 0x280 || (family & 0x7c8) == 0x2c0)
            mode = 0x8000;
    }
    return mode | 0x070f;
}

static int phy_access(unsigned ocp, unsigned reg, int write, unsigned value) {
    unsigned off = ocp ? 0xb8 : 0x60;
    unsigned addr = ocp ? ((0xa400u + 2u * reg) << 15) : reg << 16;
    r8169_w32(off, addr | (write ? 0x80000000u | (value & 0xffffu) : 0));
    for (unsigned i = 0; i < 20; i++) {
        delay_ms(1);
        uint32_t v = r8169_r32(off);
        if (v == 0xffffffffu)
            return -1;
        if ((v >> 31) != (unsigned)write)
            return v & 0xffff;
    }
    return -1;
}

static int tune_phy(unsigned xid) {
    unsigned id = xid & 0x7cf;
    unsigned ocp = (rx_config(xid) & 0x0800) != 0;
    if (!ocp && ((id & 0x7c0) != 0x380 && (id & 0x7c0) != 0x3c0 && (id & 0x7c8) != 0x280 &&
                 (id & 0x7c0) != 0x2c0 && (id & 0x7c0) != 0x480))
        return 0;
    if (!ocp && phy_access(0, 31, 1, 0) < 0)
        return -1;
    int ctrl = phy_access(ocp, 0, 0, 0), adv = phy_access(ocp, 9, 0, 0);
    if (ctrl < 0 || adv < 0 || ctrl == 0xffff || adv == 0xffff || (ctrl & 0x8000))
        return -1;
    if (phy_access(ocp, 9, 1, adv | 0x200) < 0)
        return -1;
    return phy_access(ocp, 0, 1, (ctrl & ~0x0c00) | 0x1200);
}

static unsigned power_reg(uint8_t b, uint8_t s, uint8_t f) {
    if (!(pci_config_read(b, s, f, 4) & 0x100000u))
        return 0;
    unsigned ptr = pci_config_read(b, s, f, 0x34) & 0xfcu;
    for (unsigned i = 0; i < 48 && ptr >= 0x40; i++) {
        uint32_t hdr = pci_config_read(b, s, f, ptr);
        if ((hdr & 255) == 1 && ptr <= 0xf8)
            return ptr + 4;
        ptr = (hdr >> 8) & 0xfc;
    }
    return 0;
}

static int r8169_probe(uint8_t bus, uint8_t slot, uint8_t func, uint16_t vendor, uint16_t device) {
    if (attached || vendor != 0x10ec || (device != 0x8168 && device != 0x8169))
        return 0;
    memset(&r8169_dev, 0, sizeof r8169_dev);
    unsigned pm = power_reg(bus, slot, func);
    uint32_t pmcsr = pm ? pci_config_read(bus, slot, func, pm) : 0;
    if (pmcsr & 3) {
        pci_config_write(bus, slot, func, pm, pmcsr & ~0x8003u);
        delay_ms(10);
        if (pci_config_read(bus, slot, func, pm) & 3)
            return 0;
    }
    if (!find_bar(bus, slot, func))
        goto restore_power;
    uint16_t saved = pci_config_read(bus, slot, func, 4), decode = r8169_dev.mmio ? 2 : 1;
    pci_config_write(bus, slot, func, 4, (saved | decode) & ~4u);
    uint32_t tcr = r8169_r32(R8169_TCR);
    if (tcr == 0xffffffffu)
        goto fail;
    unsigned xid = (tcr >> 20) & 0xfcf;
    serial_puts("[r8169] XID ");
    serial_puthex32(xid);
    serial_puts("\n");
    r8169_w16(R8169_IMR, 0);
    r8169_w8(R8169_CMD, R8169_CMD_RST);
    for (unsigned t = 0; t < 100 && (r8169_r8(R8169_CMD) & R8169_CMD_RST); t++)
        delay_ms(1);
    if (r8169_r8(R8169_CMD) & R8169_CMD_RST)
        goto fail;
    uint8_t mac[6], any = 0;
    for (unsigned i = 0; i < 6; i++) {
        mac[i] = r8169_r8(i);
        any |= mac[i];
    }
    if (!any || (mac[0] & 1) || !setup_rings())
        goto fail;
    r8169_w16(R8169_ISR, 0xffff);
    r8169_w8(0x50, 0xc0);
    uint16_t cp =
        r8169_dev.mmio ? *(volatile uint16_t *)(r8169_dev.mmio + 0xe0) : inw(r8169_dev.iobase + 0xe0);
    r8169_w16(0xe0, cp & ~(1u << 6));
    unsigned cfg = rx_config(xid);
    r8169_w8(0xec, (device == 0x8168 && ((cfg & 0x0800) || (xid & 0x7c0) == 0x480 || (xid & 0x7c8) == 0x2c8))
                       ? 0x27
                       : 0x3f);
    if (device == 0x8168 && (cfg & 0x0800))
        r8169_w32(0xf0, r8169_r32(0xf0) & ~(1u << 19));
    r8169_w16(0xe2, 0);
    r8169_w16(0xda, R8169_BUF);
    __atomic_thread_fence(__ATOMIC_RELEASE);
    r8169_w32(R8169_TNPDS + 4, 0);
    r8169_w32(R8169_TNPDS, virt_to_phys(dma->tx));
    r8169_w32(R8169_RDSAR + 4, 0);
    r8169_w32(R8169_RDSAR, virt_to_phys(dma->rx));
    r8169_w8(0x50, 0);
    pci_config_write(bus, slot, func, 4, saved | decode | 4);
    r8169_w8(R8169_CMD, R8169_CMD_RE | R8169_CMD_TE);
    r8169_w32(R8169_RCR, rx_config(xid));
    r8169_w32(R8169_TCR, R8169_TCR_VALUE);
    if (device == 0x8168 && !(r8169_r8(0x6c) & 2) && tune_phy(xid) < 0)
        serial_puts("[r8169] PHY negotiation unavailable\n");
    netdev_set_mac(mac);
    netdev_set_name(device == 0x8168 ? "Realtek RTL8111/8168 family" : "Realtek RTL8169");
    mac_xid = xid;
    attached = 1;
    sample_link();
    return 1;
fail:
    pci_config_write(bus, slot, func, 4, saved);
restore_power:
    if (pmcsr & 3)
        pci_config_write(bus, slot, func, pm, pmcsr & ~0x8000u);
    serial_puts("[r8169] not attached\n");
    return 0;
}

static const net_dev_t r8169_driver = {"Realtek RTL8169/8111", 0x10EC,     0x8168,
                                       r8169_send_frame,       r8169_poll, r8169_probe};

const net_dev_t *r8169_driver_get(void) { return &r8169_driver; }
