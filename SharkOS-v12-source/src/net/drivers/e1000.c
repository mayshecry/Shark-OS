

#include "kernel.h"
#include "net.h"
#include "netdev.h"
#include "drivers/e1000.h"

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
#define E1000_RCTL_UPE    (1u << 3)
#define E1000_RCTL_BAM    (1u << 15)
#define E1000_RCTL_SECRC  (1u << 26)
#define E1000_TCTL_EN     (1u << 1)
#define E1000_TCTL_PSP    (1u << 3)

#define E1000_MDIC        0x0020
#define E1000_FWSM        0x5B54
#define E1000_FEXTNVM3    0x003C
#define E1000_EXTCNF_CTRL 0x0F00

#define E1000_CTRL_LANPHYPC_OVERRIDE (1u << 16)
#define E1000_CTRL_LANPHYPC_VALUE    (1u << 17)
#define E1000_EXTCNF_GATE_PHY_CFG    (1u << 7)
#define E1000_FWSM_PHY_RESET_EN      (1u << 6)

#define E1000_MDIC_PHY_ADDR   1u
#define E1000_MDIC_ADDR_SHIFT 16
#define E1000_MDIC_READY      (1u << 28)
#define E1000_MDIC_ERROR      (1u << 30)

#define E1000_MII_BMCR       0
#define E1000_MII_PHYSID1    2
#define E1000_MII_PHYSID2    3
#define E1000_MII_ADVERTISE  4
#define E1000_BMCR_RESET      0x8000
#define E1000_BMCR_POWERDOWN  0x0800
#define E1000_BMCR_ANE        0x1000
#define E1000_BMCR_RESTART_AN 0x0200
#define E1000_BMCR_SPEED_MASK 0x2040
#define E1000_ADV_10_100      0x01E1
#define E1000_ADV_PAUSE       0x0C00

#define E1000_NRX 32
#define E1000_NTX 32
#define E1000_BUF 2048

#define E1000_RXD_STAT_DD  0x01
#define E1000_RXD_STAT_EOP 0x02
#define E1000_TXD_STAT_DD  0x01
#define E1000_TXD_CMD_EOP  0x01
#define E1000_TXD_CMD_IFCS 0x02
#define E1000_TXD_CMD_RS   0x08
#define E1000_RAH_AV       0x80000000u
#define E1000_STATUS_LU    0x0002
#define E1000_EERD_START   0x01
#define E1000_EERD_DONE    0x10
#define E1000_EERD_DONE_SOFT 0x02

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

typedef struct {
    uintptr_t mmio;
    struct e1000_rxd* rx;
    struct e1000_txd* tx;
    uint8_t* rxb[E1000_NRX];
    uint8_t* txb[E1000_NTX];
    int rx_head;
    int tx_tail;
    int pch2;
    uint32_t last_link_tick;
} e1000_dev_t;

static e1000_dev_t e1000_dev;

static inline void e1000_w(uint32_t reg, uint32_t v) {
    *(volatile uint32_t*)(e1000_dev.mmio + reg) = v;
}

static inline uint32_t e1000_r(uint32_t reg) {
    return *(volatile uint32_t*)(e1000_dev.mmio + reg);
}

static void* e1000_alloc16(uint32_t size) {
    uint8_t* m = (uint8_t*)kmalloc(size + 16);
    if (!m) return NULL;
    uintptr_t a = ((uintptr_t)m + 15) & ~(uintptr_t)15;
    memset((void*)a, 0, size);
    return (void*)a;
}

static int e1000_eeprom_read(uint8_t addr, uint16_t* out) {
    e1000_w(E1000_EERD, ((uint32_t)addr << 8) | E1000_EERD_START);
    for (int i = 0; i < 100000; i++) {
        uint32_t v = e1000_r(E1000_EERD);
        if (v & E1000_EERD_DONE) { *out = (uint16_t)(v >> 16); return 1; }
    }
    e1000_w(E1000_EERD, ((uint32_t)addr << 2) | E1000_EERD_START);
    for (int i = 0; i < 100000; i++) {
        uint32_t v = e1000_r(E1000_EERD);
        if (v & E1000_EERD_DONE_SOFT) { *out = (uint16_t)(v >> 16); return 1; }
    }
    return 0;
}

static void e1000_read_link(void) {
    netdev_report_link((e1000_r(E1000_STATUS) & E1000_STATUS_LU) ? 1 : 0);
}

static int e1000_mdic_read(uint8_t reg, uint16_t* out) {
    e1000_w(E1000_MDIC, (uint32_t)reg |
            ((uint32_t)E1000_MDIC_PHY_ADDR << E1000_MDIC_ADDR_SHIFT) | (2u << 26));
    for (int i = 0; i < 5000; i++) {
        uint32_t v = e1000_r(E1000_MDIC);
        if (v & E1000_MDIC_ERROR) return 0;
        if (v & E1000_MDIC_READY) { *out = (uint16_t)(v & 0xFFFFu); return 1; }
    }
    return 0;
}

static int e1000_mdic_write(uint8_t reg, uint16_t val) {
    e1000_w(E1000_MDIC, (uint32_t)val | (uint32_t)reg |
            ((uint32_t)E1000_MDIC_PHY_ADDR << E1000_MDIC_ADDR_SHIFT) | (1u << 26));
    for (int i = 0; i < 5000; i++) {
        uint32_t v = e1000_r(E1000_MDIC);
        if (v & E1000_MDIC_ERROR) return 0;
        if (v & E1000_MDIC_READY) return 1;
    }
    return 0;
}

static int e1000_phy_is_accessible(void) {
    uint16_t id = 0;
    for (int retry = 0; retry < 2; retry++) {
        if (!e1000_mdic_read(E1000_MII_PHYSID1, &id) || id == 0xFFFF) continue;
        if (!e1000_mdic_read(E1000_MII_PHYSID2, &id) || id == 0xFFFF) continue;
        return 1;
    }
    return 0;
}

static void e1000_toggle_lanphypc(void) {
    uint32_t v = e1000_r(E1000_FEXTNVM3);
    v = (v & ~0x0C000000u) | 0x08000000u;
    e1000_w(E1000_FEXTNVM3, v);

    v = e1000_r(E1000_CTRL);
    v |=  E1000_CTRL_LANPHYPC_OVERRIDE;
    v &= ~E1000_CTRL_LANPHYPC_VALUE;
    e1000_w(E1000_CTRL, v);
    (void)e1000_r(E1000_CTRL);
    for (volatile int i = 0; i < 2000; i++) { }
    v &= ~E1000_CTRL_LANPHYPC_OVERRIDE;
    e1000_w(E1000_CTRL, v);
    (void)e1000_r(E1000_CTRL);
    delay_ms(50);
}

static void e1000_pch2_phy_wake(void) {
    e1000_w(E1000_EXTCNF_CTRL,
            e1000_r(E1000_EXTCNF_CTRL) | E1000_EXTCNF_GATE_PHY_CFG);

    for (int i = 0; i < 30; i++) {
        if (e1000_r(E1000_FWSM) & E1000_FWSM_PHY_RESET_EN) break;
        delay_ms(10);
    }

    if (!e1000_phy_is_accessible()) e1000_toggle_lanphypc();

    uint16_t bmcr = 0;
    if (e1000_mdic_read(E1000_MII_BMCR, &bmcr)) {
        bmcr &= (uint16_t)~(E1000_BMCR_POWERDOWN | E1000_BMCR_SPEED_MASK);
        e1000_mdic_write(E1000_MII_BMCR, (uint16_t)(bmcr | E1000_BMCR_RESET));
        delay_ms(1);
        for (int i = 0; i < 100; i++) {
            uint16_t r = 0;
            if (!e1000_mdic_read(E1000_MII_BMCR, &r)) break;
            if (!(r & E1000_BMCR_RESET)) break;
            delay_ms(1);
        }
    }

    e1000_mdic_write(E1000_MII_ADVERTISE,
                     (uint16_t)(E1000_ADV_10_100 | E1000_ADV_PAUSE));
    if (e1000_mdic_read(E1000_MII_BMCR, &bmcr)) {
        bmcr &= (uint16_t)~(E1000_BMCR_POWERDOWN | E1000_BMCR_RESET);
        e1000_mdic_write(E1000_MII_BMCR,
                         (uint16_t)(bmcr | E1000_BMCR_ANE | E1000_BMCR_RESTART_AN));
    }

    delay_ms(10);
    e1000_w(E1000_EXTCNF_CTRL,
            e1000_r(E1000_EXTCNF_CTRL) & ~E1000_EXTCNF_GATE_PHY_CFG);
}

static int e1000_init_chip(void) {
    e1000_dev_t* dev = &e1000_dev;

    e1000_w(E1000_IMC, 0xFFFFFFFFu);
    e1000_w(E1000_CTRL, e1000_r(E1000_CTRL) | E1000_CTRL_RST);
    delay_ms(10);
    int rst_wait = dev->pch2 ? 100 : 1000;
    for (int i = 0; i < rst_wait && (e1000_r(E1000_CTRL) & E1000_CTRL_RST); i++) delay_ms(1);
    e1000_w(E1000_IMC, 0xFFFFFFFFu);
    (void)e1000_r(E1000_ICR);

    if (dev->pch2) e1000_pch2_phy_wake();

    uint32_t ctrl = e1000_r(E1000_CTRL);
    ctrl |= E1000_CTRL_SLU | E1000_CTRL_ASDE;
    ctrl &= ~(E1000_CTRL_LRST | E1000_CTRL_ILOS | E1000_CTRL_PHYRST);
    e1000_w(E1000_CTRL, ctrl);

    uint32_t ral = e1000_r(E1000_RAL0), rah = e1000_r(E1000_RAH0);
    if (ral == 0 && (rah & 0xFFFF) == 0) {
        uint16_t w0, w1, w2;
        if (e1000_eeprom_read(0, &w0) && e1000_eeprom_read(1, &w1) &&
            e1000_eeprom_read(2, &w2)) {
            ral = (uint32_t)w0 | ((uint32_t)w1 << 16);
            rah = w2;
        }
    }
    uint8_t mac[6];
    mac[0] = ral & 0xFF;         mac[1] = (ral >> 8) & 0xFF;
    mac[2] = (ral >> 16) & 0xFF; mac[3] = (ral >> 24) & 0xFF;
    mac[4] = rah & 0xFF;         mac[5] = (rah >> 8) & 0xFF;
    netdev_set_mac(mac);
    e1000_w(E1000_RAL0, ral);
    e1000_w(E1000_RAH0, (rah & 0xFFFF) | E1000_RAH_AV);
    for (int i = 0; i < 128; i++) e1000_w(E1000_MTA + i * 4, 0);

    dev->rx = (struct e1000_rxd*)e1000_alloc16(sizeof(struct e1000_rxd) * E1000_NRX);
    dev->tx = (struct e1000_txd*)e1000_alloc16(sizeof(struct e1000_txd) * E1000_NTX);
    if (!dev->rx || !dev->tx) return 0;

    for (int i = 0; i < E1000_NRX; i++) {
        dev->rxb[i] = (uint8_t*)e1000_alloc16(E1000_BUF);
        if (!dev->rxb[i]) return 0;
        dev->rx[i].addr_lo = (uint32_t)virt_to_phys(dev->rxb[i]);
        dev->rx[i].addr_hi = 0;
        dev->rx[i].status = 0;
    }
    e1000_w(E1000_RDBAL, (uint32_t)virt_to_phys(dev->rx));
    e1000_w(E1000_RDBAH, 0);
    e1000_w(E1000_RDLEN, sizeof(struct e1000_rxd) * E1000_NRX);
    e1000_w(E1000_RDH, 0);
    e1000_w(E1000_RDT, E1000_NRX - 1);
    dev->rx_head = 0;

    e1000_w(E1000_RCTL, E1000_RCTL_EN | E1000_RCTL_UPE | E1000_RCTL_BAM | E1000_RCTL_SECRC);

    for (int i = 0; i < E1000_NTX; i++) {
        dev->txb[i] = (uint8_t*)e1000_alloc16(E1000_BUF);
        if (!dev->txb[i]) return 0;
        dev->tx[i].addr_lo = (uint32_t)virt_to_phys(dev->txb[i]);
        dev->tx[i].addr_hi = 0;
        dev->tx[i].cmd = 0;
        dev->tx[i].status = E1000_TXD_STAT_DD;
    }
    e1000_w(E1000_TDBAL, (uint32_t)virt_to_phys(dev->tx));
    e1000_w(E1000_TDBAH, 0);
    e1000_w(E1000_TDLEN, sizeof(struct e1000_txd) * E1000_NTX);
    e1000_w(E1000_TDH, 0);
    e1000_w(E1000_TDT, 0);
    dev->tx_tail = 0;

    e1000_w(E1000_TCTL, E1000_TCTL_EN | E1000_TCTL_PSP | (15u << 4) | (64u << 12));
    e1000_w(E1000_TIPG, 10 | (8u << 10) | (6u << 20));

    e1000_read_link();
    return 1;
}

static void e1000_send_frame(const uint8_t* data, uint32_t len) {
    e1000_dev_t* dev = &e1000_dev;
    struct e1000_txd* d = &dev->tx[dev->tx_tail];
    for (int i = 0; i < 20 && !(d->status & E1000_TXD_STAT_DD); i++) delay_ms(1);
    if (!(d->status & E1000_TXD_STAT_DD)) return;
    if (len > E1000_BUF) len = E1000_BUF;
    memcpy(dev->txb[dev->tx_tail], data, len);
    if (len < 60) { memset(dev->txb[dev->tx_tail] + len, 0, 60 - len); len = 60; }
    d->length = (uint16_t)len;
    d->cso = 0;
    d->css = 0;
    d->special = 0;
    d->status = 0;
    d->cmd = E1000_TXD_CMD_EOP | E1000_TXD_CMD_IFCS | E1000_TXD_CMD_RS;
    dev->tx_tail = (dev->tx_tail + 1) % E1000_NTX;
    e1000_w(E1000_TDT, (uint32_t)dev->tx_tail);
}

static void e1000_poll(void) {
    e1000_dev_t* dev = &e1000_dev;
    (void)e1000_r(E1000_ICR);
    for (int n = 0; n < E1000_NRX; n++) {
        struct e1000_rxd* d = &dev->rx[dev->rx_head];
        if (!(d->status & E1000_RXD_STAT_DD)) break;
        uint32_t len = d->length;
        if ((d->status & E1000_RXD_STAT_EOP) && !d->errors && len >= 14 && len <= 1514)
            netdev_rx_submit(dev->rxb[dev->rx_head], len);
        d->status = 0;
        e1000_w(E1000_RDT, (uint32_t)dev->rx_head);
        dev->rx_head = (dev->rx_head + 1) % E1000_NRX;
    }
    if (uptime_ticks - dev->last_link_tick >= 250) {
        dev->last_link_tick = uptime_ticks;
        e1000_read_link();
    }
}

static int e1000_is_supported(uint16_t did) {
    static const uint16_t ids[] = {
        0x100E, 0x100F, 0x1010, 0x1011, 0x1012, 0x1013, 0x1015, 0x1016, 0x1017,
        0x1018, 0x1019, 0x101A, 0x101D, 0x101E, 0x1026, 0x1027, 0x1028, 0x1075,
        0x1076, 0x1077, 0x1078, 0x1079, 0x107A, 0x107B, 0x107C, 0x108A, 0x1099,
        0x10B5, 0x10D3,
        0x1502, 0x1503,
    };
    for (unsigned i = 0; i < sizeof(ids) / sizeof(ids[0]); i++)
        if (ids[i] == did) return 1;
    return 0;
}

static int e1000_probe(uint8_t bus, uint8_t slot, uint8_t func, uint16_t vendor, uint16_t device) {
    if (vendor != 0x8086 || !e1000_is_supported(device)) return 0;

    uint32_t bar0 = pci_config_read(bus, slot, func, 0x10);
    if (bar0 & 1) return 0;

    e1000_dev_t* dev = &e1000_dev;
    memset(dev, 0, sizeof(*dev));
    net_pci_enable(bus, slot, func, 0x6);
    dev->mmio = (uintptr_t)(bar0 & 0xFFFFFFF0);
    dev->pch2 = (device == 0x1502 || device == 0x1503);
    return e1000_init_chip();
}

static const net_dev_t e1000_driver = {
    "Intel 8254x (e1000)",
    0x8086, 0x100E,
    e1000_send_frame,
    e1000_poll,
    e1000_probe
};

const net_dev_t* e1000_driver_get(void) { return &e1000_driver; }
