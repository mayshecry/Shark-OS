

struct virtq_desc  { uint64_t addr; uint32_t len; uint16_t flags; uint16_t next; };
struct virtq_avail { uint16_t flags; uint16_t idx; uint16_t ring[VIRTQ_N]; };
struct virtq_used  { uint16_t flags; uint16_t idx;
                     struct { uint32_t id; uint32_t len; } ring[VIRTQ_N]; };

static uint32_t virtio_io = 0;
static bool virtio_ready = false;
static struct virtq_desc*  virtq_desc[2];
static struct virtq_avail* virtq_avail[2];
static struct virtq_used*  virtq_used[2];
static uint8_t* virtq_rxbuf[VIRTQ_N];
static uint8_t* virtq_txbuf = NULL;
static uint16_t virtq_rx_seen = 0;
static uint16_t virtq_tx_seen = 0;
static bool virtq_tx_inflight = false;

static int virtio_queue_alloc(int q) {
    uint32_t desc_sz  = 16 * VIRTQ_N;
    uint32_t avail_sz = 4 + 2 * VIRTQ_N;
    uint32_t used_sz  = 4 + 8 * VIRTQ_N;
    uint8_t* blk = (uint8_t*)kmalloc(desc_sz + avail_sz + used_sz + 0x1000);
    if (!blk) return 0;
    uintptr_t a = ((uintptr_t)blk + 0xFFF) & ~(uintptr_t)0xFFF;
    memset((void*)a, 0, desc_sz + avail_sz + used_sz);
    virtq_desc[q]  = (struct virtq_desc*)(a);
    virtq_avail[q] = (struct virtq_avail*)(a + desc_sz);
    virtq_used[q]  = (struct virtq_used*)(a + desc_sz + avail_sz);
    return 1;
}

static void virtio_send(const uint8_t* data, uint32_t len) {
    if (!virtio_ready || !len) return;
    if (len > VIRTQ_BUF) len = VIRTQ_BUF;

    if (virtq_tx_inflight) {
        for (int i = 0; i < 50 && virtq_used[1]->idx == virtq_tx_seen; i++) delay_ms(1);
        if (virtq_used[1]->idx != virtq_tx_seen) {
            virtq_tx_seen = virtq_used[1]->idx;
            virtq_tx_inflight = false;
        }
    }

    memcpy(virtq_txbuf, data, len);
    if (len < 60) { memset(virtq_txbuf + len, 0, 60 - len); len = 60; }
    virtq_desc[1][0].len = len;
    virtq_avail[1]->ring[virtq_avail[1]->idx % VIRTQ_N] = 0;
    asm volatile("" ::: "memory");
    virtq_avail[1]->idx++;
    virtq_tx_inflight = true;
    outw(virtio_io + VIRTIO_QUEUE_NOTIFY, 1);
}

static void virtio_poll(void) {
    if (!virtio_ready) return;
    (void)inb(virtio_io + VIRTIO_ISR_STATUS);

    if (virtq_used[1]->idx != virtq_tx_seen) {
        virtq_tx_seen = virtq_used[1]->idx;
        virtq_tx_inflight = false;
    }

    while (virtq_used[0]->idx != virtq_rx_seen) {
        uint32_t id  = virtq_used[0]->ring[virtq_rx_seen % VIRTQ_N].id;
        uint32_t len = virtq_used[0]->ring[virtq_rx_seen % VIRTQ_N].len;
        virtq_rx_seen++;
        if (id < VIRTQ_N && len >= 14 && len <= 1518) {
            net_rx_frame(virtq_rxbuf[id], len);
        }

        virtq_avail[0]->ring[virtq_avail[0]->idx % VIRTQ_N] = (uint16_t)id;
        asm volatile("" ::: "memory");
        virtq_avail[0]->idx++;
    }
    outw(virtio_io + VIRTIO_QUEUE_NOTIFY, 0);
}

int virtio_net_probe(uint8_t bus, uint8_t slot, uint8_t func, uint16_t did) {
    if (did != 0x1000) return 0;

    uint32_t bar0 = pci_config_read(bus, slot, func, 0x10);
    if (!(bar0 & 0x1)) return 0;
    virtio_io = bar0 & 0xFFFFFFFC;
    net_pci_enable(bus, slot, func, 0x1);

    outb(virtio_io + VIRTIO_DEV_STATUS, 0);
    (void)inb(virtio_io + VIRTIO_ISR_STATUS);
    outb(virtio_io + VIRTIO_DEV_STATUS, VIRTIO_ST_ACK);
    outb(virtio_io + VIRTIO_DEV_STATUS, VIRTIO_ST_ACK | VIRTIO_ST_DRIVER);

    (void)inl(virtio_io + VIRTIO_DEV_FEATURES);
    outl(virtio_io + VIRTIO_GUEST_FEATURES, 0);

    outw(virtio_io + VIRTIO_QUEUE_SEL, 0);
    uint16_t q0 = inw(virtio_io + VIRTIO_QUEUE_SIZE);
    outw(virtio_io + VIRTIO_QUEUE_SEL, 1);
    uint16_t q1 = inw(virtio_io + VIRTIO_QUEUE_SIZE);
    if (q0 < VIRTQ_N || q1 < VIRTQ_N) return 0;

    outb(virtio_io + VIRTIO_DEV_STATUS,
         VIRTIO_ST_ACK | VIRTIO_ST_DRIVER | VIRTIO_ST_FEAT_OK);

    if (!virtio_queue_alloc(0) || !virtio_queue_alloc(1)) return 0;
    for (int i = 0; i < VIRTQ_N; i++) {
        virtq_rxbuf[i] = (uint8_t*)kmalloc(VIRTQ_BUF);
        if (!virtq_rxbuf[i]) return 0;
    }
    virtq_txbuf = (uint8_t*)kmalloc(VIRTQ_BUF);
    if (!virtq_txbuf) return 0;

    outw(virtio_io + VIRTIO_QUEUE_SEL, 0);
    outl(virtio_io + VIRTIO_QUEUE_PFN, (uint32_t)(virt_to_phys(virtq_desc[0]) >> 12));
    outw(virtio_io + VIRTIO_QUEUE_SEL, 1);
    outl(virtio_io + VIRTIO_QUEUE_PFN, (uint32_t)(virt_to_phys(virtq_desc[1]) >> 12));

    for (int i = 0; i < VIRTQ_N; i++) {
        virtq_desc[0][i].addr  = virt_to_phys(virtq_rxbuf[i]);
        virtq_desc[0][i].len   = VIRTQ_BUF;
        virtq_desc[0][i].flags = VIRTQ_DESC_F_WRITE;
        virtq_desc[0][i].next  = 0;
        virtq_avail[0]->ring[i] = (uint16_t)i;
    }
    virtq_avail[0]->idx = VIRTQ_N;

    virtq_desc[1][0].addr  = virt_to_phys(virtq_txbuf);
    virtq_desc[1][0].len   = 0;
    virtq_desc[1][0].flags = 0;
    virtq_desc[1][0].next  = 0;
    virtq_rx_seen = virtq_tx_seen = 0;
    virtq_tx_inflight = false;

    outb(virtio_io + VIRTIO_DEV_STATUS,
         VIRTIO_ST_ACK | VIRTIO_ST_DRIVER | VIRTIO_ST_FEAT_OK | VIRTIO_ST_DRIVER_OK);

    for (int i = 0; i < 6; i++) net_mac[i] = inb(virtio_io + VIRTIO_CONFIG_OFF + i);

    virtio_ready = true;
    net_has_link = 1;
    outw(virtio_io + VIRTIO_QUEUE_NOTIFY, 0);
    net_driver_register("virtio-net", virtio_send, virtio_poll);
    return 1;
}

struct r8169_desc {
    uint32_t status;
    uint32_t vlan;
    uint32_t addr_lo;
    uint32_t addr_hi;
} __attribute__((packed));

static uintptr_t r8169_mmio = 0;
static uint32_t  r8169_io = 0;
static struct r8169_desc* r8169_rx = NULL;
static struct r8169_desc* r8169_tx = NULL;
static uint8_t* r8169_rxb[R8169_NRX];
static uint8_t* r8169_txb[R8169_NTX];
static uint32_t r8169_rx_head = 0;
static uint32_t r8169_tx_head = 0;

static void     r8169_w8(uint32_t o, uint8_t v)  { if (r8169_mmio) *(volatile uint8_t*)(r8169_mmio+o)=v; else outb(r8169_io+o, v); }
static uint8_t  r8169_r8(uint32_t o)             { return r8169_mmio ? *(volatile uint8_t*)(r8169_mmio+o) : inb(r8169_io+o); }
static void     r8169_w16(uint32_t o, uint16_t v){ if (r8169_mmio) *(volatile uint16_t*)(r8169_mmio+o)=v; else outw(r8169_io+o, v); }
static void     r8169_w32(uint32_t o, uint32_t v){ if (r8169_mmio) *(volatile uint32_t*)(r8169_mmio+o)=v; else outl(r8169_io+o, v); }

static void* r8169_alloc_ring(uint32_t bytes) {
    uint8_t* m = (uint8_t*)kmalloc(bytes + 256);
    if (!m) return NULL;
    uintptr_t a = ((uintptr_t)m + 255) & ~(uintptr_t)255;
    memset((void*)a, 0, bytes);
    return (void*)a;
}

static void r8169_send(const uint8_t* data, uint32_t len) {
    uint32_t idx = r8169_tx_head;
    struct r8169_desc* d = &r8169_tx[idx];
    for (int i = 0; i < 20 && (d->status & R8169_DESC_OWN); i++) delay_ms(1);
    if (d->status & R8169_DESC_OWN) return;
    if (len > R8169_BUF) len = R8169_BUF;
    memcpy(r8169_txb[idx], data, len);
    if (len < 60) { memset(r8169_txb[idx] + len, 0, 60 - len); len = 60; }
    d->addr_lo = (uint32_t)virt_to_phys(r8169_txb[idx]);
    d->addr_hi = 0;
    d->vlan = 0;
    d->status = R8169_DESC_OWN | R8169_DESC_FS | R8169_DESC_LS | len |
                (idx == R8169_NTX - 1 ? R8169_DESC_EOR : 0);
    r8169_tx_head = (idx + 1) & (R8169_NTX - 1);
    r8169_w8(R8169_TPPOLL, 0x40);
}

static void r8169_poll(void) {
    for (int n = 0; n < R8169_NRX; n++) {
        struct r8169_desc* d = &r8169_rx[r8169_rx_head];
        if (d->status & R8169_DESC_OWN) break;
        uint32_t st  = d->status;
        uint32_t len = st & 0x3FFF;
        if ((st & (R8169_DESC_FS | R8169_DESC_LS)) == (R8169_DESC_FS | R8169_DESC_LS) &&
            len >= 14 + 4 && len <= 0x1FFF) {
            net_rx_frame(r8169_rxb[r8169_rx_head], len - 4);
        }
        uint32_t idx = r8169_rx_head;
        d->status = R8169_DESC_OWN | 0x1FF8u |
                    (idx == R8169_NRX - 1 ? R8169_DESC_EOR : 0);
        r8169_rx_head = (idx + 1) & (R8169_NRX - 1);
    }
}

int r8169_probe(uint8_t bus, uint8_t slot, uint8_t func, uint16_t did) {
    if (did != 0x8169 && did != 0x8168) return 0;

    uint32_t bar0 = pci_config_read(bus, slot, func, 0x10);
    uint32_t bar1 = pci_config_read(bus, slot, func, 0x14);
    net_pci_enable(bus, slot, func, 0x7);

    r8169_mmio = 0; r8169_io = 0;
    if (!(bar1 & 0x1) && (bar1 & 0xFFFFFFF0))      r8169_mmio = bar1 & 0xFFFFFFF0;
    else if (!(bar0 & 0x1) && (bar0 & 0xFFFFFFF0)) r8169_mmio = bar0 & 0xFFFFFFF0;
    else r8169_io = bar0 & 0xFFFFFFFC;

    r8169_w8(R8169_CMD, R8169_CMD_RST);
    for (int i = 0; i < 100 && (r8169_r8(R8169_CMD) & R8169_CMD_RST); i++) delay_ms(1);

    for (int i = 0; i < 6; i++) net_mac[i] = r8169_r8(0x00 + i);

    r8169_rx = (struct r8169_desc*)r8169_alloc_ring(sizeof(*r8169_rx) * R8169_NRX);
    r8169_tx = (struct r8169_desc*)r8169_alloc_ring(sizeof(*r8169_tx) * R8169_NTX);
    if (!r8169_rx || !r8169_tx) return 0;

    for (int i = 0; i < R8169_NRX; i++) {
        r8169_rxb[i] = (uint8_t*)kmalloc(R8169_BUF);
        if (!r8169_rxb[i]) return 0;
        r8169_rx[i].addr_lo = (uint32_t)virt_to_phys(r8169_rxb[i]);
        r8169_rx[i].addr_hi = 0;
        r8169_rx[i].status = R8169_DESC_OWN | 0x1FF8u |
                             (i == R8169_NRX - 1 ? R8169_DESC_EOR : 0);
    }
    for (int i = 0; i < R8169_NTX; i++) {
        r8169_txb[i] = (uint8_t*)kmalloc(R8169_BUF);
        if (!r8169_txb[i]) return 0;
        r8169_tx[i].addr_lo = (uint32_t)virt_to_phys(r8169_txb[i]);
        r8169_tx[i].addr_hi = 0;
        r8169_tx[i].status = 0;
    }
    r8169_rx_head = r8169_tx_head = 0;

    r8169_w32(R8169_RDSAR, (uint32_t)virt_to_phys(r8169_rx));
    r8169_w32(R8169_RDSAR + 4, 0);
    r8169_w32(R8169_TNPDS, (uint32_t)virt_to_phys(r8169_tx));
    r8169_w32(R8169_TNPDS + 4, 0);

    r8169_w16(R8169_IMR, 0x0000);
    r8169_w16(R8169_ISR, 0xFFFF);
    r8169_w32(R8169_RCR, 0x0000E70Fu);
    r8169_w32(R8169_TCR, 0x03000700u);
    r8169_w8(R8169_CMD, R8169_CMD_RE | R8169_CMD_TE);

    net_has_link = 1;
    net_driver_register(did == 0x8169 ? "Realtek RTL8169"
                                      : "Realtek RTL8168/8111",
                        r8169_send, r8169_poll);
    return 1;
}

static uint16_t ne_io = 0;
static uint8_t  ne_scratch[2048];

static void ne_remote_read(uint8_t page, uint16_t off, uint8_t* dst, uint16_t len) {
    uint16_t addr = (uint16_t)(((uint16_t)page << 8) | off);
    outb(ne_io + NE_CR, 0x21);
    outb(ne_io + NE_RBCR0, len & 0xFF);
    outb(ne_io + NE_RBCR1, len >> 8);
    outb(ne_io + NE_RSAR0, addr & 0xFF);
    outb(ne_io + NE_RSAR1, addr >> 8);
    outb(ne_io + NE_CR, 0x0A);
    for (uint16_t i = 0; i < len; i += 2) {
        uint16_t w = inw(ne_io + NE_DATA);
        dst[i] = (uint8_t)(w & 0xFF);
        if (i + 1 < len) dst[i + 1] = (uint8_t)(w >> 8);
    }
    outb(ne_io + NE_CR, 0x22);
}

static void ne_remote_write(uint8_t page, uint16_t off, const uint8_t* src, uint16_t len) {
    uint16_t addr = (uint16_t)(((uint16_t)page << 8) | off);
    outb(ne_io + NE_CR, 0x21);
    outb(ne_io + NE_RBCR0, len & 0xFF);
    outb(ne_io + NE_RBCR1, len >> 8);
    outb(ne_io + NE_RSAR0, addr & 0xFF);
    outb(ne_io + NE_RSAR1, addr >> 8);
    outb(ne_io + NE_CR, 0x12);
    for (uint16_t i = 0; i < len; i += 2) {
        uint16_t w = src[i] | ((i + 1 < len) ? ((uint16_t)src[i + 1] << 8) : 0);
        outw(ne_io + NE_DATA, w);
    }
    outb(ne_io + NE_CR, 0x22);
}

static void ne_send(const uint8_t* data, uint32_t len) {
    if (len > 1500) len = 1500;
    uint32_t plen = len < 60 ? 60 : len;
    if (plen & 1) plen++;
    memcpy(ne_scratch, data, len);
    memset(ne_scratch + len, 0, plen - len);

    ne_remote_write(NE_PAGE_TX, 0, ne_scratch, (uint16_t)plen);
    outb(ne_io + NE_TPSR, NE_PAGE_TX);
    outb(ne_io + NE_TBCR0, plen & 0xFF);
    outb(ne_io + NE_TBCR1, plen >> 8);
    outb(ne_io + NE_CR, 0x26);

    for (int i = 0; i < 100000 && !(inb(ne_io + NE_ISR) & 0x08); i++) { }
    outb(ne_io + NE_ISR, 0x08);
}

static void ne_poll(void) {
    outb(ne_io + NE_CR, 0x61);
    uint8_t curr = inb(ne_io + NE_ISR);
    outb(ne_io + NE_CR, 0x21);
    uint8_t bnry = inb(ne_io + NE_BNRY);

    uint8_t page = bnry + 1;
    if (page >= NE_PAGE_RX_END) page = NE_PAGE_RX_START;

    int guard = 0;
    while (page != curr && guard++ < 64) {
        uint8_t hdr[4];
        ne_remote_read(page, 0, hdr, 4);
        uint8_t  next = hdr[1];
        uint16_t len  = (uint16_t)(hdr[2] | ((uint16_t)hdr[3] << 8));

        if ((hdr[0] & 0x01) && len >= 14 + 4 && len <= 1518) {
            ne_remote_read(page, 4, ne_scratch, len);
            net_rx_frame(ne_scratch, len - 4);
        }

        bnry = next - 1;
        if (bnry < NE_PAGE_RX_START) bnry = NE_PAGE_RX_END - 1;
        outb(ne_io + NE_BNRY, bnry);

        page = next;
        if (page >= NE_PAGE_RX_END) page = NE_PAGE_RX_START;
    }
    outb(ne_io + NE_CR, 0x22);
}

int ne2000_probe(uint8_t bus, uint8_t slot, uint8_t func, uint16_t did) {
    if (did != 0x8029 && did != 0x0940) return 0;

    uint32_t bar0 = pci_config_read(bus, slot, func, 0x10);
    ne_io = (uint16_t)(bar0 & 0xFFFC);
    net_pci_enable(bus, slot, func, 0x1);

    (void)inb(ne_io + NE_RESET);
    for (volatile int i = 0; i < 200000; i++) { }
    outb(ne_io + NE_CR, 0x21);
    for (volatile int i = 0; i < 200000; i++) { }

    outb(ne_io + NE_DCR, 0x49);

    ne_remote_read(0x00, 0x00, net_mac, 6);

    outb(ne_io + NE_RBCR0, 0x00);
    outb(ne_io + NE_RBCR1, 0x00);
    outb(ne_io + NE_RCR, 0x06);
    outb(ne_io + NE_TCR, 0x02);
    outb(ne_io + NE_PSTART, NE_PAGE_RX_START);
    outb(ne_io + NE_PSTOP, NE_PAGE_RX_END);
    outb(ne_io + NE_BNRY, NE_PAGE_RX_START);
    outb(ne_io + NE_ISR, 0xFF);
    outb(ne_io + NE_IMR, 0x00);

    outb(ne_io + NE_CR, 0x61);
    for (int i = 0; i < 6; i++) outb(ne_io + NE_PSTART + i, net_mac[i]);
    outb(ne_io + 0x07, NE_PAGE_RX_START + 1);
    for (int i = 0; i < 8; i++) outb(ne_io + 0x08 + i, 0x00);
    outb(ne_io + NE_CR, 0x22);
    outb(ne_io + NE_TCR, 0x00);
    outb(ne_io + NE_ISR, 0xFF);

    net_has_link = 1;
    net_driver_register(did == 0x8029 ? "Realtek RTL8029 (NE2000)"
                                      : "NE2000-compatible",
                        ne_send, ne_poll);
    return 1;
}
