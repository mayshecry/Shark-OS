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
#include <stdint.h>

uint8_t net_mac[6] = {0x52,0x54,0x00,0x12,0x34,0x56};
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

static void rtl_reset(void) {
    outb(rtl_base + 0x37, 0x10);
    delay_ms(100);
    outb(rtl_base + 0x37, 0x00);
    delay_ms(100);
}

static void rtl_init_chip(void) {
    rtl_rx_buf = (uint8_t*)kmalloc(RTL_RX_SIZE + 16);
    rtl_rx_pos = 0;

    uint32_t phys = (uint32_t)virt_to_phys(rtl_rx_buf);
    outl(rtl_base + 0x30, phys);
    for (int i = 0; i < 6; i++) outb(rtl_base + i, net_mac[i]);

    /* Unlimited TX/RX DMA burst sizes, no wrap inhibit. */
    outl(rtl_base + 0x44, 0x00000000);

    /* Accept physical-match, multicast and broadcast frames. The old driver
     * wrote 0 here, which tells the chip to drop everything. Bits 5-7/8-10
     * set the RX/TX DMA burst length to unlimited. */
    outl(rtl_base + 0x40, 0x0000000F | (7 << 5) | (7 << 8));

    /* Start with a clean interrupt state and an empty ring. */
    outw(rtl_base + 0x38, 0);           /* CAPR */
    outw(rtl_base + 0x3E, 0xFFFF);      /* clear all interrupt bits */
    outw(rtl_base + 0x3C, 0x0005);      /* IMR: RX OK + TX OK */

    outb(rtl_base + 0x37, 0x0C);        /* TE | RE */
    delay_ms(20);

    /* Media Status register: bit 2 = link failure. */
    uint8_t msr = inb(rtl_base + 0x63);
    net_has_link = (msr & 0x04) ? 0 : 1;
}

static void rtl_send(const uint8_t* data, uint32_t len) {
    if (len > 1792) len = 1792;
    for (uint32_t i = 0; i < len; i++) rtl_tx_buf[i] = data[i];
    uint32_t phys = (uint32_t)virt_to_phys(rtl_tx_buf);
    outl(rtl_base + 0x20 + rtl_tx_next * 4, phys);
    outl(rtl_base + 0x10 + rtl_tx_next * 4, len);
    rtl_tx_next = (rtl_tx_next + 1) % RTL_TX_N;
}

static uint8_t rtl_ring_byte(uint32_t pos) {
    return rtl_rx_buf[pos % RTL_RX_SIZE];
}

static void rtl_poll(void) {
    uint16_t isr = inw(rtl_base + 0x3E);
    if (isr == 0) return;

    if (isr & 0x10) {                   /* RX overrun: re-arm the ring */
        outw(rtl_base + 0x38, 0);
        rtl_rx_pos = 0;
    }

    if (isr & 0x01) {                   /* RX OK */
        int guard = 0;
        while (guard++ < 64) {
            uint16_t status = (uint16_t)(rtl_ring_byte(rtl_rx_pos) |
                              (rtl_ring_byte(rtl_rx_pos + 1) << 8));
            if (!(status & 0x0001)) break;
            if (status & 0x0020) {      /* RUNT / bad frame */
                rtl_rx_pos = (rtl_rx_pos + 4) % RTL_RX_SIZE;
                continue;
            }
            uint16_t flen = (uint16_t)(rtl_ring_byte(rtl_rx_pos + 2) |
                            (rtl_ring_byte(rtl_rx_pos + 3) << 8));
            uint32_t framelen = flen - 4;   /* strip CRC */

            static uint8_t frame_tmp[1536];
            if (framelen > 14 && framelen <= 1514) {
                for (uint32_t i = 0; i < framelen; i++) {
                    frame_tmp[i] = rtl_ring_byte(rtl_rx_pos + 4 + i);
                }
                if (net_rx_cb) net_rx_cb(frame_tmp, framelen);
            }

            uint32_t adv = ((uint32_t)flen + 4 + 3) & ~3u;
            rtl_rx_pos = (rtl_rx_pos + adv) % RTL_RX_SIZE;
            outw(rtl_base + 0x38, (uint16_t)rtl_rx_pos);
        }
    }

    /* Refresh link state occasionally; reading is cheap. */
    uint8_t msr = inb(rtl_base + 0x63);
    net_has_link = (msr & 0x04) ? 0 : 1;

    outw(rtl_base + 0x3E, isr);         /* write-1-to-clear */
}

/* ================================================================== PCnet */

#define PCNET_RAP 0x10
#define PCNET_RDP 0x0C
#define PCNET_RST 0x14
#define PCNET_AP  0x00
#define CSR0_INIT 0x0001
#define CSR0_STRT 0x0002
#define CSR0_STOP 0x0004
#define CSR0_TDMD 0x0008
#define CSR0_IENA 0x0040
#define CSR0_RINT 0x0400
#define CSR0_TINT 0x0200
#define CSR0_IDON 0x0100
#define D_OWN  0x80000000u
#define D_STP  0x02000000u
#define D_ENP  0x01000000u
#define D_ERR  0x00004000u
#define D_BCNT 0x00000FFFu
#define PN_TX 8
#define PN_RX 8
#define PN_BUF 1536

/* BCNT must be the two's-complement byte count in the low 12 bits. */
#define PN_BCNT(len) (((uint32_t)(-(int32_t)(len))) & D_BCNT)

static uint32_t pcnet_io = 0;
static uintptr_t pcnet_mmio = 0;
static bool pcnet_present = false;
struct pdesc { uint32_t addr; uint32_t flags; } __attribute__((packed));
struct pib { uint16_t mode; uint8_t phys[6]; uint32_t filter[2];
             uint32_t rx_ring; uint32_t tx_ring; } __attribute__((packed));
static struct pdesc* pn_tx = NULL;
static struct pdesc* pn_rx = NULL;
static uint8_t* pn_txb[PN_TX];
static uint8_t* pn_rxb[PN_RX];
static struct pib* pn_ib = NULL;
static int pn_rx_next = 0;

static void pn_wrap(uint16_t v) {
    if (pcnet_mmio) *(volatile uint16_t*)(pcnet_mmio + PCNET_RAP) = v;
    else outw(pcnet_io + PCNET_RAP, v);
}
static void pn_wrdp(uint32_t v) {
    if (pcnet_mmio) *(volatile uint32_t*)(pcnet_mmio + PCNET_RDP) = v;
    else outl(pcnet_io + PCNET_RDP, v);
}
static uint32_t pn_rrdp(void) {
    if (pcnet_mmio) return *(volatile uint32_t*)(pcnet_mmio + PCNET_RDP);
    return inl(pcnet_io + PCNET_RDP);
}
static void pn_csrw(int c, uint32_t v) { pn_wrap((uint16_t)c); pn_wrdp(v); }
static uint32_t pn_csrr(int c) { pn_wrap((uint16_t)c); return pn_rrdp(); }
static void pn_bcrw(int c, uint32_t v) { pn_wrap((uint16_t)(c | 0x40)); pn_wrdp(v); }

static void pn_init_chip(void) {
    pn_ib = (struct pib*)kmalloc(sizeof(*pn_ib));
    memset(pn_ib, 0, sizeof(*pn_ib));
    pn_tx = (struct pdesc*)kmalloc(sizeof(*pn_tx) * PN_TX);
    pn_rx = (struct pdesc*)kmalloc(sizeof(*pn_rx) * PN_RX);
    memset(pn_tx, 0, sizeof(*pn_tx) * PN_TX);
    memset(pn_rx, 0, sizeof(*pn_rx) * PN_RX);

    for (int i = 0; i < PN_TX; i++) {
        pn_txb[i] = (uint8_t*)kmalloc(PN_BUF);
        pn_tx[i].addr = (uint32_t)virt_to_phys(pn_txb[i]);
        pn_tx[i].flags = 0;
    }
    for (int i = 0; i < PN_RX; i++) {
        pn_rxb[i] = (uint8_t*)kmalloc(PN_BUF);
        pn_rx[i].addr = (uint32_t)virt_to_phys(pn_rxb[i]);
        /* Hand the NIC a real 1536-byte buffer; the old code left BCNT at 0. */
        pn_rx[i].flags = PN_BCNT(PN_BUF) | D_OWN;
    }
    pn_rx_next = 0;
    pn_ib->mode = 0;
    for (int i = 0; i < 6; i++) pn_ib->phys[i] = net_mac[i];
    /* Ring length codes: log2(8) = 3 goes in bits 13-15 of each ring addr. */
    pn_ib->rx_ring = (uint32_t)virt_to_phys(pn_rx) | (3u << 13);
    pn_ib->tx_ring = (uint32_t)virt_to_phys(pn_tx) | (3u << 13);

    /* Explicit 32-bit software style; autoselect left this to chance. */
    pn_bcrw(88, 2);

    pn_csrw(0, CSR0_STOP);
    delay_ms(10);
    uint32_t ib = (uint32_t)(uintptr_t)virt_to_phys((void*)pn_ib);
    pn_csrw(1, ib & 0xFFFF);
    pn_csrw(2, (ib >> 16) & 0xFFFF);
    pn_csrw(3, 0);
    /* APAD_XMT (bit 10): pad short frames to 64 bytes; DXMT2PD (bit 11). */
    pn_csrw(4, 0x0C00);
    pn_csrw(0, CSR0_INIT | CSR0_IENA);
    int t = 10000;
    while (t--) { if (pn_csrr(0) & CSR0_IDON) break; delay_ms(1); }
    pn_csrw(0, CSR0_IDON | CSR0_STRT | CSR0_IENA);
    delay_ms(10);
    net_has_link = 1;
}

static void pn_send(const uint8_t* data, uint32_t len) {
    int idx = -1;
    for (int i = 0; i < PN_TX; i++) {
        if (!(pn_tx[i].flags & D_OWN)) { idx = i; break; }
    }
    if (idx < 0) return;
    if (len > PN_BUF) len = PN_BUF;
    for (uint32_t i = 0; i < len; i++) pn_txb[idx][i] = data[i];
    pn_tx[idx].flags = PN_BCNT(len) | D_OWN | D_STP | D_ENP;
    pn_csrw(0, CSR0_TDMD);
}

static void pn_poll(void) {
    uint32_t csr0 = pn_csrr(0);
    if (csr0 & (CSR0_RINT | CSR0_TINT)) {
        for (int i = 0; i < PN_RX; i++) {
            int idx = (pn_rx_next + i) % PN_RX;
            uint32_t fl = pn_rx[idx].flags;
            if (fl & D_OWN) continue;
            uint32_t mcnt = (fl >> 16) & 0x0FFF;     /* received byte count */
            uint32_t pl = (mcnt > 4) ? mcnt - 4 : 0;
            if (!(fl & D_ERR) && pl > 14 && pl < PN_BUF) {
                if (net_rx_cb) net_rx_cb((const uint8_t*)pn_rxb[idx], pl);
            }
            pn_rx[idx].flags = PN_BCNT(PN_BUF) | D_OWN;
            pn_rx_next = (idx + 1) % PN_RX;
        }
        pn_csrw(0, CSR0_RINT | CSR0_TINT);
    }
}

/* ============================================================== dispatch */

void net_set_rx_callback(net_rx_cb_t cb) { net_rx_cb = cb; }

void net_send_raw(const uint8_t* data, uint32_t len) {
    if (rtl_present) rtl_send(data, len);
    else if (pcnet_present) pn_send(data, len);
}

void net_poll(void) {
    if (rtl_present) rtl_poll();
    else if (pcnet_present) pn_poll();
}

void net_init(void) {
    for (uint32_t bus = 0; bus < 256; bus++) {
        for (uint32_t slot = 0; slot < 32; slot++) {
            for (uint32_t func = 0; func < 8; func++) {
                uint32_t d = pci_config_read(bus, slot, func, 0);
                if (d == 0xFFFFFFFF) continue;
                uint16_t vid = d & 0xFFFF, did = (d >> 16) & 0xFFFF;
                if (vid == 0x10EC && did == 0x8139) {
                    uint32_t bar0 = pci_config_read(bus, slot, func, 0x10);
                    rtl_base = bar0 & 0xFFFFFFFC;
                    uint32_t cmd = pci_config_read(bus, slot, func, 0x04);
                    cmd |= 0x5;   /* IO space + bus master */
                    pci_config_write(bus, slot, func, 0x04, cmd);
                    for (int i = 0; i < 6; i++) net_mac[i] = inb(rtl_base + i);
                    strcpy(net_driver_name, "Realtek RTL8139");
                    rtl_present = true;
                    rtl_reset();
                    rtl_init_chip();
                    return;
                }
                if (vid == 0x1022 && (did == 0x2000 || did == 0x2001)) {
                    uint32_t bar0 = pci_config_read(bus, slot, func, 0x10);
                    int isio = (bar0 & 0x1) != 0;
                    uint32_t base = isio ? (bar0 & 0xFFFFFFFC)
                                         : (bar0 & 0xFFFFFFF0);
                    uint32_t cmd = pci_config_read(bus, slot, func, 0x04);
                    cmd |= 0x7;   /* IO + memory + bus master */
                    pci_config_write(bus, slot, func, 0x04, cmd);
                    if (isio) { pcnet_io = base; pcnet_mmio = 0; }
                    else { pcnet_mmio = (uintptr_t)base; pcnet_io = 0; }
                    if (pcnet_mmio) *(volatile uint16_t*)(pcnet_mmio + PCNET_RST) = 0;
                    else outw(pcnet_io + PCNET_RST, 0);
                    delay_ms(100);
                    for (int i = 0; i < 6; i++) {
                        if (pcnet_mmio) {
                            net_mac[i] = *(volatile uint8_t*)(pcnet_mmio + PCNET_AP + i);
                        } else {
                            net_mac[i] = inb(pcnet_io + PCNET_AP + i);
                        }
                    }
                    strcpy(net_driver_name, "AMD PCnet-FAST III");
                    pcnet_present = true;
                    pn_init_chip();
                    return;
                }
                if (func == 0 && (d & 0x00800000) == 0) break;  /* not multi-fn */
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

static void (*ip_recv_cb)(const uint8_t* ip_payload, uint32_t len,
                          const uint8_t* src_ip) = NULL;

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
        uint32_t ihl = (uint32_t)((frame[14] & 0x0F) * 4);
        if (ihl < 20) return;
        if (len < 14 + ihl) return;
        uint8_t proto = frame[14 + 9];
        const uint8_t* sip = &frame[14 + 12];
        const uint8_t* payload = &frame[14 + ihl];
        uint16_t total = (uint16_t)((frame[14 + 2] << 8) | frame[14 + 3]);
        uint32_t plen = (total > ihl) ? (total - ihl) : 0;
        if (plen > len - 14 - ihl) plen = len - 14 - ihl;
        if (proto == IP_ICMP) {
            if (payload[0] == 0 && ip_recv_cb) ip_recv_cb(payload, plen, sip);
        } else if (proto == IP_TCP) {
            if (ip_recv_cb) ip_recv_cb(payload, plen, sip);
        } else if (proto == IP_UDP) {
            if (ip_recv_cb) ip_recv_cb(payload, plen, sip);
        }
    }
}

/* ------------------------------------------------------------------ ICMP */

static uint8_t icmp_expect_ip[4];
static volatile int icmp_got_reply = 0;

static void icmp_recv(const uint8_t* payload, uint32_t len, const uint8_t* src_ip) {
    if (len < 8) return;
    if (payload[0] != 0) return;
    if (my_memcmp(src_ip, icmp_expect_ip, 4) != 0) return;
    icmp_got_reply = 1;
}

int net_ping(const char* ip_str, int count) {
    uint8_t tip[4];
    int oct = 0, val = 0;
    const char* p = ip_str;
    while (*p && oct < 4) {
        if (*p >= '0' && *p <= '9') { val = val * 10 + (*p - '0'); }
        else if (*p == '.') { tip[oct++] = (uint8_t)val; val = 0; }
        p++;
    }
    if (oct == 3) tip[3] = (uint8_t)val; else return -1;
    if (!net_configured) { terminal_writestring("net: not configured (run dhcp)\n"); return -1; }

    uint8_t gwmac[6];
    if (!net_route(tip, gwmac)) { terminal_writestring("net: cannot reach target\n"); return -1; }

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
        ip_recv_cb = icmp_recv;
        net_send_raw(pkt, sizeof(pkt));
        for (int w = 0; w < 20; w++) {
            delay_ms(50);
            net_poll();
            if (icmp_got_reply) break;
        }
        char b[16];
        if (icmp_got_reply) { received++; terminal_writestring("PING: reply seq="); }
        else terminal_writestring("PING: timeout seq=");
        int_to_string((uint32_t)(seq + 1), b);
        terminal_writestring(b);
        terminal_writestring("\n");
    }
    char b[16];
    terminal_writestring("PING: ");
    int_to_string((uint32_t)received, b); terminal_writestring(b);
    terminal_writestring("/");
    int_to_string((uint32_t)count, b); terminal_writestring(b);
    terminal_writestring(" received\n");
    ip_recv_cb = NULL;
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
    ip_recv_cb = dns_recv;
    net_send_raw(pkt, off + 20 + udplen);
    for (int w = 0; w < 20; w++) { delay_ms(100); net_poll(); if (dns_got) break; }
    ip_recv_cb = NULL;
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
static uint8_t tcp_rx_buf[4096];
static volatile uint32_t tcp_rx_len = 0;

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

    if (flags & 0x02) {
        /* SYN|ACK: complete the handshake with a plain ACK. The old code
         * answered with SYN|ACK (0x12), so servers reset the connection. */
        tcp_ack = seq + 1;
        tcp_send_flags(0x10, NULL, 0);
        tcp_connected = 1;
        return;
    }
    if (flags & 0x01) {
        tcp_ack = seq + ((len > data_off) ? (len - data_off) : 0) + 1;
        tcp_send_flags(0x11, NULL, 0);
        return;
    }
    if (len > data_off) {
        uint32_t dlen = len - data_off;
        if (tcp_rx_len + dlen < sizeof(tcp_rx_buf)) {
            for (uint32_t i = 0; i < dlen; i++) {
                tcp_rx_buf[tcp_rx_len + i] = payload[data_off + i];
            }
            tcp_rx_len += dlen;
        }
        tcp_ack = seq + dlen;
        tcp_send_flags(0x10, NULL, 0);
    } else if (flags & 0x10) {
        /* Pure ACK (e.g. of our request); nothing to do. */
    }
}

int net_tcp_connect(const uint8_t* ip, uint16_t port,
                    const uint8_t* send_data, uint32_t send_len,
                    uint8_t* resp_buf, uint32_t resp_max) {
    if (!net_configured) { terminal_writestring("net: not configured\n"); return -1; }
    if (!net_route(ip, tcp_dst_mac)) { terminal_writestring("net: host unreachable\n"); return -1; }
    for (int i = 0; i < 4; i++) tcp_dst_ip[i] = ip[i];
    tcp_dst_port = port;
    tcp_seq = 0x1000; tcp_ack = 0; tcp_connected = 0;
    tcp_rx_len = 0;
    tcp_src_port = (uint16_t)(0x4000 + (uptime_ticks & 0x3FF));
    ip_recv_cb = tcp_recv;

    tcp_send_flags(0x02, NULL, 0);
    int connected = 0;
    for (int w = 0; w < 40; w++) {
        delay_ms(50); net_poll();
        if (tcp_connected) { connected = 1; break; }
        if ((w & 7) == 7) tcp_send_flags(0x02, NULL, 0);   /* SYN retry */
    }
    if (!connected) { ip_recv_cb = NULL; terminal_writestring("net: connection failed\n"); return -1; }

    if (send_data && send_len > 0) {
        tcp_send_flags(0x18, send_data, send_len);
        tcp_seq += send_len;
    }

    uint32_t total = 0;
    for (int w = 0; w < 100; w++) {
        delay_ms(50); net_poll();
        if (tcp_rx_len > 0) {
            total = tcp_rx_len;
            if (total >= resp_max) break;
        }
    }
    if (total > resp_max) total = resp_max;
    if (total > 0) { for (uint32_t i = 0; i < total; i++) resp_buf[i] = tcp_rx_buf[i]; }

    tcp_send_flags(0x11, NULL, 0);
    ip_recv_cb = NULL;
    return total > 0 ? (int)total : -1;
}

/* ------------------------------------------------------------------ HTTP */

/* Static: the old code stacked 8 KB here and another 8 KB in net_cmd_wget,
 * blowing the 8 KB kernel stack. */
static uint8_t http_resp[8192];

int net_http_get(const char* url, uint8_t* out_buf, uint32_t out_max) {
    const char* p = url;
    if (my_strncmp_prefix(p, "http://")) p += 7;
    char host[128];
    int hi = 0;
    while (*p && *p != ':' && *p != '/') { if (hi < 127) host[hi++] = *p; p++; }
    host[hi] = 0;
    uint16_t port = 80;
    if (*p == ':') {
        port = 0; p++;
        while (*p >= '0' && *p <= '9') { port = (uint16_t)(port * 10 + (*p - '0')); p++; }
    }
    char path[256];
    int pi = 0;
    if (*p != '/') path[pi++] = '/';
    while (*p && pi < 255) { path[pi++] = *p; p++; }
    path[pi] = 0;

    uint8_t ip[4];
    if (!net_dns_lookup(host, ip)) {
        terminal_writestring("net: DNS lookup failed for ");
        terminal_writestring(host);
        terminal_writestring("\n");
        return -1;
    }

    char req[512];
    int rl = net_snprintf(req, sizeof(req),
                          "GET %s HTTP/1.1\r\nHost: %s\r\n"
                          "User-Agent: SharkOS98\r\n"
                          "Connection: close\r\n\r\n", path, host);

    int n = net_tcp_connect(ip, port, (uint8_t*)req, (uint32_t)rl,
                            http_resp, sizeof(http_resp));
    if (n <= 0) return -1;

    int header_end = -1;
    for (int i = 0; i + 4 < n; i++) {
        if (http_resp[i] == '\r' && http_resp[i + 1] == '\n' &&
            http_resp[i + 2] == '\r' && http_resp[i + 3] == '\n') {
            header_end = i + 4;
            break;
        }
    }
    int body_start = (header_end >= 0) ? header_end : 0;
    int body_len = n - body_start;
    if (body_len > (int)out_max) body_len = (int)out_max;
    for (int i = 0; i < body_len; i++) out_buf[i] = http_resp[body_start + i];
    return body_len;
}

/* ------------------------------------------------------------- init/plumb */

void net_stack_init(void) {
    net_set_rx_callback(net_on_frame);
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

void net_cmd_ifconfig(void) {
    char b[16];
    terminal_writestring("NIC: "); terminal_writestring(net_driver_name); terminal_writestring("\n");
    terminal_writestring("MAC: ");
    for (int i = 0; i < 6; i++) {
        hex_to_string(net_mac[i], b);
        if (b[0] == '0') terminal_writestring(&b[2]); else terminal_writestring(b);
        if (i < 5) terminal_writestring(":");
    }
    terminal_writestring("\n");
    terminal_writestring("Link: "); terminal_writestring(net_has_link ? "up\n" : "down\n");
    print_ip4("IP: ", net_ip);
    print_ip4("Mask: ", net_mask);
    print_ip4("Gateway: ", net_gw);
    print_ip4("DNS: ", net_dns);
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

static uint8_t dhcp_srv[4];
static uint32_t dhcp_xid = 0xDEADBEEF;
static volatile int dhcp_state = 0;   /* 1 = offer seen, 2 = ack */
static uint8_t dhcp_offered[4];

static void dhcp_recv(const uint8_t* payload, uint32_t len, const uint8_t* src_ip) {
    (void)src_ip;
    if (len < 240) return;
    uint32_t xid = ((uint32_t)payload[4] << 24) | ((uint32_t)payload[5] << 16) |
                   ((uint32_t)payload[6] << 8) | payload[7];
    if (xid != dhcp_xid) return;
    if (payload[0] != 2) return;                     /* BOOTREPLY only */
    uint32_t off = 236;
    if (payload[off] != 0x63 || payload[off + 1] != 0x82 ||
        payload[off + 2] != 0x53 || payload[off + 3] != 0x63) return;

    uint32_t msgtype = 0;
    uint32_t p = off + 4;
    while (p + 1 < len) {
        uint8_t opt = payload[p];
        if (opt == 0xFF) break;
        if (opt == 0x00) { p++; continue; }          /* pad: no length byte */
        uint8_t ol = payload[p + 1];
        if (p + 2 + ol > len) break;
        if (opt == 0x35 && ol == 1) msgtype = payload[p + 2];
        else if (opt == 0x01 && ol >= 4) { for (int i = 0; i < 4; i++) net_mask[i] = payload[p + 2 + i]; }
        else if (opt == 0x03 && ol >= 4) { for (int i = 0; i < 4; i++) net_gw[i] = payload[p + 2 + i]; }
        else if (opt == 0x06 && ol >= 4) { for (int i = 0; i < 4; i++) net_dns[i] = payload[p + 2 + i]; }
        else if (opt == 0x36 && ol >= 4) { for (int i = 0; i < 4; i++) dhcp_srv[i] = payload[p + 2 + i]; }
        p += 2 + ol;
    }

    if (msgtype == 2) {                              /* OFFER */
        for (int i = 0; i < 4; i++) dhcp_offered[i] = payload[16 + i];
        dhcp_state = 1;
    } else if (msgtype == 5) {                       /* ACK */
        for (int i = 0; i < 4; i++) net_ip[i] = payload[16 + i];
        dhcp_state = 2;
    }
}

static void dhcp_send(int type, const uint8_t* req_ip, const uint8_t* srv) {
    uint8_t bc[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
    uint8_t pkt[14 + 20 + 8 + 300];
    uint32_t off = build_eth(pkt, bc, ETH_IP);
    uint16_t sport = 68, dport = 67;
    uint8_t* d = &pkt[off + 20 + 8];
    for (int i = 0; i < 300; i++) d[i] = 0;
    d[0] = 1; d[1] = 1; d[2] = 6; d[3] = 0;
    d[4] = (dhcp_xid >> 24) & 0xFF; d[5] = (dhcp_xid >> 16) & 0xFF;
    d[6] = (dhcp_xid >> 8) & 0xFF; d[7] = dhcp_xid & 0xFF;
    for (int i = 0; i < 6; i++) d[28 + i] = net_mac[i];
    d[236] = 0x63; d[237] = 0x82; d[238] = 0x53; d[239] = 0x63;
    int o = 240;
    d[o++] = 0x35; d[o++] = 1; d[o++] = (uint8_t)type;
    if (req_ip && type == 3) { d[o++] = 0x32; d[o++] = 4; for (int i = 0; i < 4; i++) d[o++] = req_ip[i]; }
    if (srv && type == 3) { d[o++] = 0x36; d[o++] = 4; for (int i = 0; i < 4; i++) d[o++] = srv[i]; }
    d[o++] = 0x37; d[o++] = 4; d[o++] = 1; d[o++] = 3; d[o++] = 6; d[o++] = 0x36;
    d[o++] = 0xFF;
    uint16_t udplen = (uint16_t)(8 + o);

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
    for (int i = 0; i < 4; i++) pkt[off + 12 + i] = 0;
    for (int i = 0; i < 4; i++) pkt[off + 16 + i] = 0xFF;
    uint16_t ic = csum16(&pkt[off], 20);
    pkt[off + 10] = (ic >> 8) & 0xFF; pkt[off + 11] = ic & 0xFF;
    net_send_raw(pkt, off + 20 + udplen);
}

int net_dhcp(void) {
    if (!net_has_link) { terminal_writestring("net: no link\n"); return -1; }
    dhcp_xid = 0xDEADBEEF;
    dhcp_state = 0;
    ip_recv_cb = dhcp_recv;

    terminal_writestring("DHCP: discovering...\n");
    dhcp_send(1, NULL, NULL);
    for (int a = 0; a < 8 && dhcp_state < 1; a++) {
        delay_ms(400); net_poll();
        if (dhcp_state < 1) dhcp_send(1, NULL, NULL);
    }

    if (dhcp_state >= 1) {
        terminal_writestring("DHCP: offer received, requesting...\n");
        dhcp_state = 1;
        dhcp_send(3, dhcp_offered, dhcp_srv);
        for (int a = 0; a < 8 && dhcp_state < 2; a++) {
            delay_ms(400); net_poll();
            if (dhcp_state < 2 && (a & 1)) dhcp_send(3, dhcp_offered, dhcp_srv);
        }
    }
    ip_recv_cb = NULL;

    if (net_ip[0] || net_ip[1] || net_ip[2] || net_ip[3]) {
        net_configured = 1;
        char b[16];
        terminal_writestring("DHCP: configured IP ");
        int_to_string(net_ip[0], b); terminal_writestring(b); terminal_writestring(".");
        int_to_string(net_ip[1], b); terminal_writestring(b); terminal_writestring(".");
        int_to_string(net_ip[2], b); terminal_writestring(b); terminal_writestring(".");
        int_to_string(net_ip[3], b); terminal_writestring(b); terminal_writestring("\n");
        return 1;
    }

    net_ip[0] = 169; net_ip[1] = 254; net_ip[2] = net_mac[4]; net_ip[3] = net_mac[5];
    net_mask[0] = 255; net_mask[1] = 255; net_mask[2] = 0; net_mask[3] = 0;
    net_configured = 1;
    terminal_writestring("DHCP: no server, using link-local\n");
    return 0;
}

void net_cmd_dhcp(void) {
    net_dhcp();
}
