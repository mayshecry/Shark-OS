#include <assert.h>
#include <stdio.h>
#include "../src/net/drivers/r8169.c"

static uint8_t regs[4096] __attribute__((aligned(4096)));
static uint8_t heap[sizeof(struct dma_area) + 255] __attribute__((aligned(256)));
static uint32_t config[64];
static int writes, allocs, stuck, link, frames;
static uint16_t control = 0x0800, advertise;
volatile uint32_t uptime_ticks;
uint32_t pci_config_read(uint8_t b, uint8_t s, uint8_t f, uint8_t o) {
    (void)b;
    (void)s;
    (void)f;
    return config[o / 4];
}
void pci_config_write(uint8_t b, uint8_t s, uint8_t f, uint8_t o, uint32_t v) {
    (void)b;
    (void)s;
    (void)f;
    if (o == 0x44) {
        config[o / 4] = v;
        assert(!(v & 0x8000));
        return;
    }
    assert(o == 4 && !(v >> 16));
    config[1] = v;
    writes++;
}
void *kmalloc(size_t n) {
    assert(n <= sizeof heap);
    allocs++;
    return heap;
}
uintptr_t virt_to_phys(void *p) { return (uintptr_t)p; }
void delay_ms(uint32_t ms) {
    uptime_ticks += ms;
    if (!stuck)
        regs[R8169_CMD] &= ~R8169_CMD_RST;
    uint32_t *mdio = (void *)(regs + 0xb8), val = *mdio;
    uint32_t addr = (val >> 15) & 0xfffe;
    if (addr == 0xa400 || addr == 0xa412) {
        uint16_t *word = addr == 0xa400 ? &control : &advertise;
        if (val >> 31) {
            *word = val;
            *mdio = val & 0x7fffffffu;
        } else
            *mdio = val | 0x80000000u | *word;
    }
}
void serial_puts(const char *s) { (void)s; }
void serial_puthex32(uint32_t x) { (void)x; }
void netdev_set_mac(const uint8_t mac[6]) { assert(mac[0] == 2 && mac[5] == 9); }
void netdev_set_name(const char *s) { assert(s && *s); }
void netdev_report_link(int up) { link = !!up; }
void netdev_rx_submit(const uint8_t *frame, uint32_t len) {
    assert(frame[0] == 7 && len == 100);
    frames++;
    r8169_poll();
}
int main(void) {
    const net_dev_t *drv = r8169_driver_get();
    assert(!drv->probe(0, 0, 0, 0x10ec, 0xc821) && !writes && !allocs);
    assert(!drv->probe(0, 0, 0, 0x10ec, 0x8168) && !writes);
    config[4] = 0xc001;
    config[6] = (uintptr_t)regs | 4u;
    config[1] = 0xabcd0000u;
    regs[0] = 2;
    regs[5] = 9;
    *(uint32_t *)(regs + R8169_TCR) = 0x54100000u;
    config[1] = 0x100000u;
    config[0x34 / 4] = 0x40;
    config[0x40 / 4] = 1;
    config[0x44 / 4] = 0x8003;
    stuck = 1;
    assert(!drv->probe(0, 0, 0, 0x10ec, 0x8168) && !allocs && config[1] == 0);
    assert((config[0x44 / 4] & 3) == 3);
    config[1] |= 0x100000u;
    stuck = 0;
    assert(drv->probe(0, 0, 0, 0x10ec, 0x8168) && allocs == 1 && config[1] == 6 && !link);
    assert(!(config[0x44 / 4] & 3) && r8169_xid() == 0x541);
    assert((control & 0x1e00) == 0x1200 && (advertise & 0x200));
    assert(r8169_dev.mmio == (uintptr_t)regs && *(uint16_t *)(regs + 0xda) == R8169_BUF);
    assert(*(uint32_t *)(regs + R8169_RDSAR) == (uintptr_t)dma->rx);
    assert(*(uint32_t *)(regs + R8169_TNPDS) == (uintptr_t)dma->tx);
    for (unsigned i = 0; i < R8169_NRX; i++)
        assert((dma->rx[i].status & 0x3fff) == sizeof dma->receive[i]);
    regs[0x6c] = 0x13;
    uptime_ticks += 100;
    drv->poll();
    assert(link && r8169_speed() == 1000);
    uint8_t data[64] = {1, 2, 3};
    drv->send(data, 3);
    assert(dma->send[0][2] == 3 && dma->send[0][59] == 0);
    assert((dma->tx[0].status & 0x3fff) == 60 && (dma->tx[0].status & R8169_DESC_OWN));
    for (unsigned i = 1; i < R8169_NTX; i++)
        drv->send(data, sizeof data);
    assert(dma->tx[R8169_NTX - 1].status & R8169_DESC_EOR);
    drv->send(data, 64);
    assert((dma->tx[0].status & 0x3fff) == 60);
    dma->rx[0].status = R8169_DESC_FS | R8169_DESC_LS | 104;
    dma->receive[0][0] = 7;
    dma->rx[1].status = dma->rx[0].status | R8169_RX_ERROR;
    dma->rx[2].status = R8169_DESC_FS | 104;
    dma->rx[3].status = R8169_DESC_FS | R8169_DESC_LS | 3000;
    drv->poll();
    assert(frames == 1 && r8169_dev.rx_head == 4 && dma->rx[3].status == (R8169_DESC_OWN | R8169_BUF));
    regs[0x6c] = 0;
    uptime_ticks += 100;
    drv->poll();
    assert(!link && !r8169_speed());
    puts("Realtek register/ring model passed: match, BAR, reset, PHY, DMA, link, RX and TX");
    return 0;
}
