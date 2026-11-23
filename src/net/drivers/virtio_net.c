

#include "kernel.h"
#include "net.h"
#include "netdev.h"
#include "drivers/virtio_net.h"

#define VIRTQ_N 32
#define VIRTQ_BUF 2048

#define VIRTIO_DEV_FEATURES 0x00
#define VIRTIO_GUEST_FEATURES 0x04
#define VIRTIO_QUEUE_PFN    0x08
#define VIRTIO_QUEUE_SIZE   0x0C
#define VIRTIO_QUEUE_SEL    0x0E
#define VIRTIO_QUEUE_NOTIFY 0x10
#define VIRTIO_DEV_STATUS   0x12
#define VIRTIO_ISR_STATUS   0x13
#define VIRTIO_CONFIG_OFF   0x14

#define VIRTIO_ST_ACK       0x01
#define VIRTIO_ST_DRIVER    0x02
#define VIRTIO_ST_DRIVER_OK 0x04
#define VIRTIO_ST_FEAT_OK   0x08

#define VIRTQ_DESC_F_WRITE 0x02

struct virtq_desc {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
} __attribute__((packed));

struct virtq_avail {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[VIRTQ_N];
} __attribute__((packed));

struct virtq_used {
    uint16_t flags;
    uint16_t idx;
    struct { uint32_t id; uint32_t len; } ring[VIRTQ_N];
} __attribute__((packed));

typedef struct {
    uint16_t iobase;
    struct virtq_desc*  desc;
    struct virtq_avail* avail;
    struct virtq_used*  used;
    uint8_t* rxbuf[VIRTQ_N];
    uint8_t* txbuf;
    uint16_t rx_seen;
    uint16_t tx_seen;
    int      tx_inflight;
} virtio_net_dev_t;

static virtio_net_dev_t vt_dev;

static void* virtq_alloc_pages(uint32_t bytes) {
    uint8_t* blk = (uint8_t*)kmalloc(bytes + 0x1000);
    if (!blk) return NULL;
    uintptr_t a = ((uintptr_t)blk + 0xFFF) & ~(uintptr_t)0xFFF;
    memset((void*)a, 0, bytes);
    return (void*)a;
}

static int virtio_net_probe(uint8_t bus, uint8_t slot, uint8_t func, uint16_t vendor, uint16_t device) {
    if (vendor != 0x1AF4 || device != 0x1000) return 0;

    uint32_t bar0 = pci_config_read(bus, slot, func, 0x10);
    if (!(bar0 & 0x1)) return 0;

    virtio_net_dev_t* dev = &vt_dev;
    memset(dev, 0, sizeof(*dev));
    dev->iobase = (uint16_t)(bar0 & 0xFFFFFFFC);
    net_pci_enable(bus, slot, func, 0x1);

    outb(dev->iobase + VIRTIO_DEV_STATUS, 0);
    (void)inb(dev->iobase + VIRTIO_ISR_STATUS);
    outb(dev->iobase + VIRTIO_DEV_STATUS, VIRTIO_ST_ACK);
    outb(dev->iobase + VIRTIO_DEV_STATUS, VIRTIO_ST_ACK | VIRTIO_ST_DRIVER);

    (void)inl(dev->iobase + VIRTIO_DEV_FEATURES);
    outl(dev->iobase + VIRTIO_GUEST_FEATURES, 0);

    outw(dev->iobase + VIRTIO_QUEUE_SEL, 0);
    uint16_t rx_qsize = inw(dev->iobase + VIRTIO_QUEUE_SIZE);
    outw(dev->iobase + VIRTIO_QUEUE_SEL, 1);
    uint16_t tx_qsize = inw(dev->iobase + VIRTIO_QUEUE_SIZE);
    if (rx_qsize < VIRTQ_N || tx_qsize < VIRTQ_N) return 0;

    outb(dev->iobase + VIRTIO_DEV_STATUS, VIRTIO_ST_ACK | VIRTIO_ST_DRIVER | VIRTIO_ST_FEAT_OK);

    uint32_t desc_sz  = (uint32_t)sizeof(struct virtq_desc) * VIRTQ_N;
    uint32_t avail_sz = (uint32_t)sizeof(struct virtq_avail);
    uint32_t used_sz  = (uint32_t)sizeof(struct virtq_used);
    uint32_t queue_sz = desc_sz + avail_sz + used_sz;

    uint8_t* rx_queue = (uint8_t*)virtq_alloc_pages(queue_sz);
    uint8_t* tx_queue = (uint8_t*)virtq_alloc_pages(queue_sz);
    if (!rx_queue || !tx_queue) return 0;

    for (int i = 0; i < VIRTQ_N; i++) {
        dev->rxbuf[i] = (uint8_t*)kmalloc(VIRTQ_BUF);
        if (!dev->rxbuf[i]) return 0;
    }
    dev->txbuf = (uint8_t*)kmalloc(VIRTQ_BUF);
    if (!dev->txbuf) return 0;

    struct virtq_desc*  rx_desc  = (struct virtq_desc*)rx_queue;
    struct virtq_avail* rx_avail = (struct virtq_avail*)(rx_queue + desc_sz);
    dev->used                    = (struct virtq_used*)(rx_queue + desc_sz + avail_sz);

    struct virtq_desc*  tx_desc  = (struct virtq_desc*)tx_queue;
    struct virtq_avail* tx_avail = (struct virtq_avail*)(tx_queue + desc_sz);
    struct virtq_used*  tx_used  = (struct virtq_used*)(tx_queue + desc_sz + avail_sz);

    for (int i = 0; i < VIRTQ_N; i++) {
        rx_desc[i].addr  = virt_to_phys(dev->rxbuf[i]);
        rx_desc[i].len   = VIRTQ_BUF;
        rx_desc[i].flags = VIRTQ_DESC_F_WRITE;
        rx_desc[i].next  = 0;
        rx_avail->ring[i] = (uint16_t)i;
    }
    rx_avail->idx = VIRTQ_N;

    tx_desc[0].addr  = virt_to_phys(dev->txbuf);
    tx_desc[0].len   = 0;
    tx_desc[0].flags = 0;
    tx_desc[0].next  = 0;

    outw(dev->iobase + VIRTIO_QUEUE_SEL, 0);
    outl(dev->iobase + VIRTIO_QUEUE_PFN, (uint32_t)(virt_to_phys(rx_queue) >> 12));
    outw(dev->iobase + VIRTIO_QUEUE_SEL, 1);
    outl(dev->iobase + VIRTIO_QUEUE_PFN, (uint32_t)(virt_to_phys(tx_queue) >> 12));

    dev->desc  = rx_desc;
    dev->avail = rx_avail;
    dev->tx_seen = 0;
    dev->rx_seen = 0;
    dev->tx_inflight = 0;
    (void)tx_used;
    (void)tx_desc;
    (void)tx_avail;

    outb(dev->iobase + VIRTIO_DEV_STATUS,
         VIRTIO_ST_ACK | VIRTIO_ST_DRIVER | VIRTIO_ST_FEAT_OK | VIRTIO_ST_DRIVER_OK);

    uint8_t mac[6];
    for (int i = 0; i < 6; i++) mac[i] = inb(dev->iobase + VIRTIO_CONFIG_OFF + i);
    netdev_set_mac(mac);

    outw(dev->iobase + VIRTIO_QUEUE_NOTIFY, 0);
    netdev_report_link(1);
    netdev_set_name("virtio-net");
    return 1;
}

static void virtio_net_send(const uint8_t* data, uint32_t len) {
    virtio_net_dev_t* dev = &vt_dev;
    if (!len || !dev->desc) return;
    if (len > VIRTQ_BUF) len = VIRTQ_BUF;
    memcpy(dev->txbuf, data, len);
    if (len < 60) { memset(dev->txbuf + len, 0, 60 - len); len = 60; }
    dev->desc[0].len = len;
    dev->avail->ring[dev->avail->idx % VIRTQ_N] = 0;
    asm volatile("" ::: "memory");
    dev->avail->idx++;
    outw(dev->iobase + VIRTIO_QUEUE_NOTIFY, 1);
}

static void virtio_net_poll(void) {
    virtio_net_dev_t* dev = &vt_dev;
    if (!dev->desc) return;
    (void)inb(dev->iobase + VIRTIO_ISR_STATUS);

    while (dev->used->idx != dev->rx_seen) {
        uint32_t id  = dev->used->ring[dev->rx_seen % VIRTQ_N].id;
        uint32_t len = dev->used->ring[dev->rx_seen % VIRTQ_N].len;
        dev->rx_seen++;
        if (id < VIRTQ_N && len >= 14 && len <= 1518)
            netdev_rx_submit(dev->rxbuf[id], len);
        dev->avail->ring[dev->avail->idx % VIRTQ_N] = (uint16_t)id;
        asm volatile("" ::: "memory");
        dev->avail->idx++;
    }
    outw(dev->iobase + VIRTIO_QUEUE_NOTIFY, 0);
}

static const net_dev_t virtio_net_driver = {
    "virtio-net",
    0x1AF4, 0x1000,
    virtio_net_send,
    virtio_net_poll,
    virtio_net_probe
};

const net_dev_t* virtio_net_driver_get(void) { return &virtio_net_driver; }
