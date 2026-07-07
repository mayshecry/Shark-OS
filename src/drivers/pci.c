#include "kernel.h"

#define RTL8139_VENDOR_ID 0x10EC
#define RTL8139_DEVICE_ID 0x8139
#define PCNET_VENDOR_ID 0x1022
#define PCNET_DEVICE_ID 0x2000
#define PCNET_FAST_DEVICE_ID 0x2001

#define PCNET_RAP  0x10
#define PCNET_RDP  0x0C
#define PCNET_RESET 0x14
#define PCNET_APROM00 0x00

#define CSR0_INIT  0x0001
#define CSR0_STRT  0x0002
#define CSR0_STOP  0x0004
#define CSR0_TDMD  0x0008
#define CSR0_TXON  0x0010
#define CSR0_RXON  0x0020
#define CSR0_IENA  0x0040
#define CSR0_INTR  0x0080
#define CSR0_IDON  0x0100
#define CSR0_TINT  0x0200
#define CSR0_RINT  0x0400
#define CSR0_MERR  0x0800
#define CSR0_MISS  0x1000
#define CSR0_CERR  0x2000
#define CSR0_BABL  0x4000
#define CSR0_ERR   0x8000

#define TX_OWN   0x80000000
#define TX_STP   0x02000000
#define TX_ENP   0x01000000
#define TX_BCNT_MASK 0x0FFF

#define RX_OWN   0x80000000
#define RX_ERR   0x00004000
#define RX_BCNT_MASK 0x0FFF

#define DESC_OWN  0x80000000
#define DESC_STP  0x02000000
#define DESC_ENP  0x01000000
#define DESC_BCNT_MASK 0x0FFF
#define DESC_ERR  0x00004000

#define PCNET_NUM_TX_DESCS 8
#define PCNET_NUM_RX_DESCS 8
#define PCNET_BUF_SIZE 1536

#define ETHERTYPE_IPV4  0x0800
#define ETHERTYPE_ARP   0x0806

#define IP_PROTO_ICMP 1
#define IP_PROTO_UDP  17

#define ARP_REQUEST 1
#define ARP_REPLY   2

#define DHCP_SERVER_PORT 67
#define DHCP_CLIENT_PORT 68

#define DHCP_DISCOVER 1
#define DHCP_OFFER    2
#define DHCP_REQUEST  3
#define DHCP_ACK      5

#define ETH_HDR_LEN  14
#define IP_HDR_LEN   20
#define UDP_HDR_LEN   8

static uint8_t rtl_mac[6];
static uint32_t pcnet_io_base = 0;
static int pcnet_mmio = 0;
static uintptr_t pcnet_mmio_base = 0;
static uint8_t pcnet_mac[6];

struct pcnet_desc {
    uint32_t addr;
    uint32_t flags;
} __attribute__((packed));

struct pcnet_init_block {
    uint16_t mode;
    uint8_t phys_addr[6];
    uint32_t filter[2];
    uint32_t rx_ring;
    uint32_t tx_ring;
} __attribute__((packed));

static struct pcnet_desc* pcnet_tx_ring = NULL;
static struct pcnet_desc* pcnet_rx_ring = NULL;
static uint8_t* pcnet_tx_bufs[PCNET_NUM_TX_DESCS];
static uint8_t* pcnet_rx_bufs[PCNET_NUM_RX_DESCS];
static struct pcnet_init_block* pcnet_ib = NULL;
static uint8_t gateway_mac[6] = {0,0,0,0,0,0};
static int gateway_mac_resolved = 0;

static uint32_t dhcp_xid = 0;
static uint8_t dhcp_server_ip[4] = {0,0,0,0};
static int pcnet_rx_next = 0;
static int pcnet_rx_ready = 0;
static uint32_t pcnet_rx_len = 0;
static uint8_t pcnet_rx_buf[1536];

static void pcnet_check_rx(void);
static void pcnet_send_raw(uint8_t* data, uint32_t len);
static void pcnet_write_rap(uint16_t val);
static void pcnet_write_rdp(uint32_t val);
static uint32_t pcnet_read_rdp(void);
static void pcnet_check_rx_real(void);
static void pcnet_csr_write(int csr, uint32_t val);
static uint32_t pcnet_csr_read(int csr);

static uint16_t checksum16(uint8_t* data, uint32_t len) {
    uint32_t sum = 0;
    for (uint32_t i = 0; i < len; i += 2) {
        uint16_t word = (data[i] << 8) | ((i + 1 < len) ? data[i + 1] : 0);
        sum += word;
    }
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return ~sum & 0xFFFF;
}

static uint16_t ip_checksum(uint8_t* ip_header) {
    return checksum16(ip_header, 20);
}

static uint16_t icmp_checksum(uint8_t* icmp_start, uint32_t len) {
    return checksum16(icmp_start, len);
}

static int is_arp_packet(uint8_t* packet, uint32_t len, int opcode) {
    if (len < 42) return 0;
    if (packet[12] != 0x08 || packet[13] != 0x06) return 0;
    uint16_t pkt_opcode = (packet[20] << 8) | packet[21];
    return pkt_opcode == opcode;
}

static int is_arp_reply(uint8_t* packet, uint32_t len, uint8_t* target_ip) {
    if (!is_arp_packet(packet, len, ARP_REPLY)) return 0;
    for (int i = 0; i < 4; i++)
        if (packet[28 + i] != target_ip[i]) return 0;
    return 1;
}

static void send_arp_request(uint8_t* target_ip) {
    uint8_t packet[42];
    uint32_t plen = 0;
    for (int i = 0; i < 6; i++) packet[plen++] = 0xFF;
    for (int i = 0; i < 6; i++) packet[plen++] = pcnet_mac[i];
    packet[plen++] = 0x08; packet[plen++] = 0x06;
    packet[plen++] = 0x00; packet[plen++] = 0x01;
    packet[plen++] = 0x08; packet[plen++] = 0x00;
    packet[plen++] = 0x06; packet[plen++] = 0x04;
    packet[plen++] = 0x00; packet[plen++] = 0x01;
    for (int i = 0; i < 6; i++) packet[plen++] = pcnet_mac[i];
    for (int i = 0; i < 4; i++) packet[plen++] = ip_address[i];
    for (int i = 0; i < 6; i++) packet[plen++] = 0x00;
    for (int i = 0; i < 4; i++) packet[plen++] = target_ip[i];
    pcnet_send_raw(packet, plen);
}

static int resolve_gateway(void) {
    if (gateway_mac_resolved) return 1;
    terminal_writestring("ARP: Resolving gateway 192.168.1.1...\n");
    send_arp_request(gateway);
    for (int attempt = 0; attempt < 3; attempt++) {
        delay_ms(500);
        pcnet_check_rx();
        if (pcnet_rx_ready && is_arp_reply(pcnet_rx_buf, pcnet_rx_len, gateway)) {
            for (int i = 0; i < 6; i++)
                gateway_mac[i] = pcnet_rx_buf[22 + i];
            pcnet_rx_ready = 0;
            gateway_mac_resolved = 1;
            terminal_writestring("ARP: Reply - Gateway MAC ");
            char buf[16];
            for (int i = 0; i < 6; i++) {
                hex_to_string(gateway_mac[i], buf);
                if (buf[0] == '0') terminal_writestring(&buf[2]);
                else terminal_writestring(buf);
                if (i < 5) terminal_writestring(":");
            }
            terminal_writestring("\n");
            return 1;
        }
        if (attempt < 2) send_arp_request(gateway);
    }
    terminal_writestring("ARP: Failed to resolve gateway\n");
    return 0;
}

static void rtl8139_reset(uint32_t io_base) {
    outb(io_base + 0x37, 0x10);
    delay_ms(100);
    outb(io_base + 0x37, 0x00);
    delay_ms(100);
}

static void rtl8139_set_mac(uint32_t io_base) {
    for (int i = 0; i < 6; i++) outb(io_base + i, rtl_mac[i]);
}

void rtl8139_init() {
    uint32_t io_base = 0;
    uint8_t irq = 0;
    for (uint32_t bus = 0; bus < 256; bus++) {
        for (uint32_t slot = 0; slot < 32; slot++) {
            uint32_t pci_data = pci_config_read(bus, slot, 0, 0);
            if (pci_data == 0xFFFFFFFF) continue;
            uint16_t vendor = pci_data & 0xFFFF;
            uint16_t device = (pci_data >> 16) & 0xFFFF;
            if (vendor == RTL8139_VENDOR_ID && device == RTL8139_DEVICE_ID) {
                uint32_t bar0 = pci_config_read(bus, slot, 0, 0x10);
                io_base = bar0 & 0xFFFFFFFC;
                irq = pci_config_read(bus, slot, 0, 0x3C) & 0xFF;
                for (int i = 0; i < 6; i++) rtl_mac[i] = inb(io_base + i);
                goto found;
            }
        }
    }
    terminal_writestring("RTL8139: Device not found\n");
    return;
found:
    rtl_io_base = io_base;
    rtl_irq = irq;
    terminal_writestring("RTL8139: Found at I/O base 0x");
    char buf[16]; hex_to_string(io_base, buf); terminal_writestring(buf);
    terminal_writestring(" IRQ "); int_to_string(irq, buf); terminal_writestring(buf);
    terminal_writestring("\nRTL8139: MAC ");
    for (int i = 0; i < 6; i++) {
        hex_to_string(rtl_mac[i], buf);
        if (buf[0] == '0') terminal_writestring(&buf[2]);
        else terminal_writestring(buf);
        if (i < 5) terminal_writestring(":");
    }
    terminal_writestring("\n");
    rtl8139_reset(io_base);
    rtl8139_set_mac(io_base);
    outl(io_base + 0x44, 0x000C);
    outw(io_base + 0x3C, 0x0005);
    network_initialized = true;
    terminal_writestring("RTL8139: Initialized\n");
}

void rtl8139_send_packet(void* data, uint32_t len) {
    if (!rtl_io_base) return;
    outl(rtl_io_base + 0x20 + (current_tx_buffer * 4), (uint32_t)(uintptr_t)data);
    outl(rtl_io_base + 0x10 + (current_tx_buffer * 4), len);
    current_tx_buffer = (current_tx_buffer + 1) % 4;
}

static void pcnet_send_raw(uint8_t* data, uint32_t len) {
    if ((!pcnet_io_base && !pcnet_mmio) || !pcnet_tx_ring) return;
    int idx = -1;
    for (int i = 0; i < PCNET_NUM_TX_DESCS; i++) {
        if (!(pcnet_tx_ring[i].flags & DESC_OWN)) { idx = i; break; }
    }
    if (idx < 0) {
        terminal_writestring("PCNET: TX desc all busy\n");
        return;
    }
    for (uint32_t i = 0; i < len && i < PCNET_BUF_SIZE; i++)
        pcnet_tx_bufs[idx][i] = data[i];
    pcnet_tx_ring[idx].flags = (len & DESC_BCNT_MASK) | DESC_OWN | DESC_STP | DESC_ENP;
    pcnet_csr_write(0, pcnet_csr_read(0) | CSR0_TDMD);
}

static void build_ip_header(uint8_t* hdr, uint8_t* dest_ip, uint8_t protocol, uint16_t total_len) {
    hdr[0] = 0x45;
    hdr[1] = 0x00;
    hdr[2] = (total_len >> 8) & 0xFF; hdr[3] = total_len & 0xFF;
    hdr[4] = 0x00; hdr[5] = 0x00;
    hdr[6] = 0x40; hdr[7] = 0x00;
    hdr[8] = 0x40;
    hdr[9] = protocol;
    hdr[10] = 0x00; hdr[11] = 0x00;
    for (int i = 0; i < 4; i++) hdr[12 + i] = ip_address[i];
    for (int i = 0; i < 4; i++) hdr[16 + i] = dest_ip[i];
    uint16_t csum = ip_checksum(hdr);
    hdr[10] = (csum >> 8) & 0xFF; hdr[11] = csum & 0xFF;
}

static void send_ethernet_ip(uint8_t* dest_mac, uint8_t* dest_ip, uint8_t protocol, uint8_t* payload, uint16_t payload_len) {
    uint8_t packet[1536];
    uint32_t plen = 0;
    uint16_t total = 20 + payload_len;
    for (int i = 0; i < 6; i++) packet[plen++] = dest_mac[i];
    for (int i = 0; i < 6; i++) packet[plen++] = pcnet_mac[i];
    packet[plen++] = 0x08; packet[plen++] = 0x00;
    build_ip_header(&packet[plen], dest_ip, protocol, total);
    plen += 20;
    for (uint16_t i = 0; i < payload_len; i++) packet[plen++] = payload[i];
    pcnet_send_raw(packet, plen);
}

static void send_icmp_echo(uint8_t* dest_mac, uint8_t* dest_ip, uint16_t id, uint16_t seq) {
    uint8_t payload[60];
    payload[0] = 0x08; payload[1] = 0x00;
    payload[2] = 0x00; payload[3] = 0x00;
    payload[4] = (id >> 8) & 0xFF; payload[5] = id & 0xFF;
    payload[6] = (seq >> 8) & 0xFF; payload[7] = seq & 0xFF;
    for (int i = 0; i < 32; i++) payload[8 + i] = 'A' + (i % 26);
    uint16_t icmp_csum = icmp_checksum(payload, 40);
    payload[2] = (icmp_csum >> 8) & 0xFF; payload[3] = icmp_csum & 0xFF;
    send_ethernet_ip(dest_mac, dest_ip, IP_PROTO_ICMP, payload, 40);
}

enum dhcp_state { DHCP_INIT, DHCP_SELECTING, DHCP_REQUESTING, DHCP_BOUND };
static enum dhcp_state dhcp_state = DHCP_INIT;

static int is_dhcp_packet(uint8_t* packet, uint32_t len, int expected_type) {
    if (len < ETH_HDR_LEN + IP_HDR_LEN + UDP_HDR_LEN + 240) return 0;
    if (packet[12] != 0x08 || packet[13] != 0x00) return 0;
    uint8_t ip_ihl = packet[ETH_HDR_LEN] & 0x0F;
    uint32_t ip_hdr_len_val = ip_ihl * 4;
    if (ip_hdr_len_val < 20 || ip_hdr_len_val > 60) return 0;
    if (packet[ETH_HDR_LEN + 9] != IP_PROTO_UDP) return 0;
    uint32_t udp_start = ETH_HDR_LEN + ip_hdr_len_val;
    if (udp_start + 4 > len) return 0;
    uint16_t src_port = (packet[udp_start] << 8) | packet[udp_start + 1];
    uint16_t dst_port = (packet[udp_start + 2] << 8) | packet[udp_start + 3];
    if (src_port != DHCP_SERVER_PORT || dst_port != DHCP_CLIENT_PORT) return 0;
    uint32_t dhcp_start = udp_start + UDP_HDR_LEN;
    if (dhcp_start + 240 > len) return 0;
    if (packet[dhcp_start + 236] != 0x63 || packet[dhcp_start + 237] != 0x82 ||
        packet[dhcp_start + 238] != 0x53 || packet[dhcp_start + 239] != 0x63) return 0;
    uint32_t pkt_xid = (packet[dhcp_start + 4] << 24) | (packet[dhcp_start + 5] << 16) |
                       (packet[dhcp_start + 6] << 8) | packet[dhcp_start + 7];
    if (pkt_xid != dhcp_xid) return 0;
    int opt_ptr = dhcp_start + 240;
    while (opt_ptr + 2 < (int)len && packet[opt_ptr] != 0xFF) {
        uint8_t opt = packet[opt_ptr];
        if (opt == 0x00) { opt_ptr++; continue; }
        if (opt_ptr + 2 > (int)len) return 0;
        uint8_t opt_len = packet[opt_ptr + 1];
        if (opt_ptr + 2 + opt_len > (int)len) return 0;
        if (opt == 0x35 && opt_len == 1) {
            return packet[opt_ptr + 2] == expected_type;
        }
        opt_ptr += 2 + opt_len;
    }
    return 0;
}

static void parse_dhcp_options(uint8_t* packet, uint32_t len) {
    if (len < ETH_HDR_LEN + IP_HDR_LEN + UDP_HDR_LEN + 240) return;
    uint8_t ip_ihl = packet[ETH_HDR_LEN] & 0x0F;
    uint32_t ip_hdr_len_val = ip_ihl * 4;
    uint32_t dhcp_start = ETH_HDR_LEN + ip_hdr_len_val + UDP_HDR_LEN;
    int opt_ptr = dhcp_start + 240;
    while (opt_ptr < (int)len && packet[opt_ptr] != 0xFF) {
        uint8_t opt = packet[opt_ptr];
        if (opt == 0x00) { opt_ptr++; continue; }
        uint8_t opt_len = packet[opt_ptr + 1];
        if (opt == 0x01 && opt_len == 4) {
            subnet_mask[0] = packet[opt_ptr + 2]; subnet_mask[1] = packet[opt_ptr + 3];
            subnet_mask[2] = packet[opt_ptr + 4]; subnet_mask[3] = packet[opt_ptr + 5];
            terminal_writestring("DHCP: Learned subnet mask\n");
        }
        if (opt == 0x03 && opt_len >= 4) {
            gateway[0] = packet[opt_ptr + 2]; gateway[1] = packet[opt_ptr + 3];
            gateway[2] = packet[opt_ptr + 4]; gateway[3] = packet[opt_ptr + 5];
            char buf[16];
            terminal_writestring("DHCP: Learned gateway ");
            int_to_string(gateway[0], buf); terminal_writestring(buf); terminal_writestring(".");
            int_to_string(gateway[1], buf); terminal_writestring(buf); terminal_writestring(".");
            int_to_string(gateway[2], buf); terminal_writestring(buf); terminal_writestring(".");
            int_to_string(gateway[3], buf); terminal_writestring(buf);
            terminal_writestring("\n");
        }
        if (opt == 0x06 && opt_len >= 4) {
            dns_server[0] = packet[opt_ptr + 2]; dns_server[1] = packet[opt_ptr + 3];
            dns_server[2] = packet[opt_ptr + 4]; dns_server[3] = packet[opt_ptr + 5];
            terminal_writestring("DHCP: Learned DNS server\n");
        }
        if (opt == 0x36 && opt_len == 4) {
            dhcp_server_ip[0] = packet[opt_ptr + 2]; dhcp_server_ip[1] = packet[opt_ptr + 3];
            dhcp_server_ip[2] = packet[opt_ptr + 4]; dhcp_server_ip[3] = packet[opt_ptr + 5];
        }
        opt_ptr += 2 + opt_len;
    }
}

static void send_dhcp_request(void) {
    uint8_t broadcast_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    uint8_t broadcast_ip[4] = {255, 255, 255, 255};
    uint8_t dhcp_msg[280];
    memset(dhcp_msg, 0, 280);
    dhcp_msg[0] = 0x01;
    dhcp_msg[1] = 0x01;
    dhcp_msg[2] = 0x06;
    dhcp_msg[3] = 0x00;
    dhcp_msg[4] = (dhcp_xid >> 24) & 0xFF;
    dhcp_msg[5] = (dhcp_xid >> 16) & 0xFF;
    dhcp_msg[6] = (dhcp_xid >> 8) & 0xFF;
    dhcp_msg[7] = dhcp_xid & 0xFF;
    dhcp_msg[8] = 0x00; dhcp_msg[9] = 0x00;
    dhcp_msg[10] = 0x00; dhcp_msg[11] = 0x00;
    dhcp_msg[12] = 0x00; dhcp_msg[13] = 0x00;
    dhcp_msg[14] = 0x00; dhcp_msg[15] = 0x00;
    for (int i = 0; i < 6; i++) dhcp_msg[28 + i] = pcnet_mac[i];
    dhcp_msg[236] = 0x63; dhcp_msg[237] = 0x82; dhcp_msg[238] = 0x53; dhcp_msg[239] = 0x63;
    int opt = 240;
    dhcp_msg[opt++] = 0x35; dhcp_msg[opt++] = 0x01; dhcp_msg[opt++] = DHCP_REQUEST;
    dhcp_msg[opt++] = 0x32; dhcp_msg[opt++] = 0x04;
    dhcp_msg[opt++] = ip_address[0]; dhcp_msg[opt++] = ip_address[1];
    dhcp_msg[opt++] = ip_address[2]; dhcp_msg[opt++] = ip_address[3];
    dhcp_msg[opt++] = 0x36; dhcp_msg[opt++] = 0x04;
    dhcp_msg[opt++] = dhcp_server_ip[0]; dhcp_msg[opt++] = dhcp_server_ip[1];
    dhcp_msg[opt++] = dhcp_server_ip[2]; dhcp_msg[opt++] = dhcp_server_ip[3];
    dhcp_msg[opt++] = 0xFF;
    uint8_t packet[1536];
    uint32_t plen = 0;
    for (int i = 0; i < 6; i++) packet[plen++] = broadcast_mac[i];
    for (int i = 0; i < 6; i++) packet[plen++] = pcnet_mac[i];
    packet[plen++] = 0x08; packet[plen++] = 0x00;
    build_ip_header(&packet[plen], broadcast_ip, IP_PROTO_UDP, 20 + 8 + opt);
    plen += 20;
    packet[plen++] = 68 >> 8; packet[plen++] = 68 & 0xFF;
    packet[plen++] = 67 >> 8; packet[plen++] = 67 & 0xFF;
    packet[plen++] = (opt + 8) >> 8; packet[plen++] = (opt + 8) & 0xFF;
    packet[plen++] = 0x00; packet[plen++] = 0x00;
    for (int i = 0; i < opt; i++) packet[plen++] = dhcp_msg[i];
    pcnet_send_raw(packet, plen);
    terminal_writestring("DHCP: Request sent\n");
}

static void send_dhcp_discover_raw(void) {
    uint8_t broadcast_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    uint8_t broadcast_ip[4] = {255, 255, 255, 255};
    dhcp_xid = uptime_ticks ^ 0xDEADBEEF;
    dhcp_state = DHCP_SELECTING;
    uint8_t dhcp_msg[280];
    memset(dhcp_msg, 0, 280);
    dhcp_msg[0] = 0x01;
    dhcp_msg[1] = 0x01;
    dhcp_msg[2] = 0x06;
    dhcp_msg[3] = 0x00;
    dhcp_msg[4] = (dhcp_xid >> 24) & 0xFF;
    dhcp_msg[5] = (dhcp_xid >> 16) & 0xFF;
    dhcp_msg[6] = (dhcp_xid >> 8) & 0xFF;
    dhcp_msg[7] = dhcp_xid & 0xFF;
    for (int i = 0; i < 6; i++) dhcp_msg[28 + i] = pcnet_mac[i];
    dhcp_msg[12] = 0x00; dhcp_msg[13] = 0x00; dhcp_msg[14] = 0x00; dhcp_msg[15] = 0x00;
    dhcp_msg[236] = 0x63; dhcp_msg[237] = 0x82; dhcp_msg[238] = 0x53; dhcp_msg[239] = 0x63;
    int opt = 240;
    dhcp_msg[opt++] = 0x35; dhcp_msg[opt++] = 0x01; dhcp_msg[opt++] = DHCP_DISCOVER;
    dhcp_msg[opt++] = 0x37; dhcp_msg[opt++] = 0x04;
    dhcp_msg[opt++] = 0x01; dhcp_msg[opt++] = 0x03; dhcp_msg[opt++] = 0x06; dhcp_msg[opt++] = 0x36;
    dhcp_msg[opt++] = 0xFF;
    uint8_t packet[1536];
    uint32_t plen = 0;
    for (int i = 0; i < 6; i++) packet[plen++] = broadcast_mac[i];
    for (int i = 0; i < 6; i++) packet[plen++] = pcnet_mac[i];
    packet[plen++] = 0x08; packet[plen++] = 0x00;
    build_ip_header(&packet[plen], broadcast_ip, IP_PROTO_UDP, 20 + 8 + opt);
    plen += 20;
    packet[plen++] = 68 >> 8; packet[plen++] = 68 & 0xFF;
    packet[plen++] = 67 >> 8; packet[plen++] = 67 & 0xFF;
    packet[plen++] = (opt + 8) >> 8; packet[plen++] = (opt + 8) & 0xFF;
    packet[plen++] = 0x00; packet[plen++] = 0x00;
    for (int i = 0; i < opt; i++) packet[plen++] = dhcp_msg[i];
    pcnet_send_raw(packet, plen);
    terminal_writestring("DHCP: Discover sent\n");
}

static int is_icmp_reply(uint8_t* packet, uint32_t len) {
    if (len < 42) return 0;
    if (packet[12] != 0x08 || packet[13] != 0x00) return 0;
    if (packet[23] != IP_PROTO_ICMP) return 0;
    if (packet[34] != 0x00) return 0;
    return 1;
}

void run_dhcp(void) {
    if (!pcnet_io_base && !pcnet_mmio) {
        terminal_writestring("DHCP: No network adapter\n");
        return;
    }
    dhcp_state = DHCP_INIT;
    send_dhcp_discover_raw();
    terminal_writestring("DHCP: Waiting for OFFER...\n");
    for (int attempt = 0; attempt < 5; attempt++) {
        terminal_writestring("DHCP: Poll attempt ");
        char abuf[16]; int_to_string(attempt + 1, abuf); terminal_writestring(abuf);
        terminal_writestring("\n");
        delay_ms(500);
        pcnet_rx_ready = 0;
        pcnet_check_rx();
        if (pcnet_rx_ready) {
            terminal_writestring("DHCP: RX pkt len=");
            char lbuf[16]; int_to_string(pcnet_rx_len, lbuf); terminal_writestring(lbuf);
            terminal_writestring(" ethtype=0x");
            if (pcnet_rx_len >= 14) {
                hex_to_string((pcnet_rx_buf[12] << 8) | pcnet_rx_buf[13], lbuf);
                terminal_writestring(lbuf);
            } else {
                terminal_writestring("??");
            }
            terminal_writestring("\n");
        }
        if (pcnet_rx_ready && is_dhcp_packet(pcnet_rx_buf, pcnet_rx_len, DHCP_OFFER)) {
            uint8_t ip_ihl = pcnet_rx_buf[ETH_HDR_LEN] & 0x0F;
            uint32_t ip_hdr_len = ip_ihl * 4;
            uint32_t dhcp_start = ETH_HDR_LEN + ip_hdr_len + UDP_HDR_LEN;
            uint8_t* dhcp = &pcnet_rx_buf[dhcp_start];
            uint8_t offered_ip[4] = {dhcp[16], dhcp[17], dhcp[18], dhcp[19]};
            dhcp_server_ip[0] = dhcp[20]; dhcp_server_ip[1] = dhcp[21];
            dhcp_server_ip[2] = dhcp[22]; dhcp_server_ip[3] = dhcp[23];
            char buf[16];
            terminal_writestring("DHCP: OFFER received - IP ");
            int_to_string(offered_ip[0], buf); terminal_writestring(buf); terminal_writestring(".");
            int_to_string(offered_ip[1], buf); terminal_writestring(buf); terminal_writestring(".");
            int_to_string(offered_ip[2], buf); terminal_writestring(buf); terminal_writestring(".");
            int_to_string(offered_ip[3], buf); terminal_writestring(buf);
            terminal_writestring(" from server ");
            int_to_string(dhcp_server_ip[0], buf); terminal_writestring(buf); terminal_writestring(".");
            int_to_string(dhcp_server_ip[1], buf); terminal_writestring(buf); terminal_writestring(".");
            int_to_string(dhcp_server_ip[2], buf); terminal_writestring(buf); terminal_writestring(".");
            int_to_string(dhcp_server_ip[3], buf); terminal_writestring(buf);
            terminal_writestring("\n");
            ip_address[0] = offered_ip[0]; ip_address[1] = offered_ip[1];
            ip_address[2] = offered_ip[2]; ip_address[3] = offered_ip[3];
            parse_dhcp_options(pcnet_rx_buf, pcnet_rx_len);
            dhcp_state = DHCP_SELECTING;
            pcnet_rx_ready = 0;
            send_dhcp_request();
            terminal_writestring("DHCP: Waiting for ACK...\n");
            for (int retry = 0; retry < 5; retry++) {
                delay_ms(500);
                pcnet_rx_ready = 0;
                pcnet_check_rx();
                if (pcnet_rx_ready && is_dhcp_packet(pcnet_rx_buf, pcnet_rx_len, DHCP_ACK)) {
                    terminal_writestring("DHCP: ACK received! IP assigned\n");
                    dhcp_state = DHCP_BOUND;
                    dhcp_enabled = true;
                    pcnet_rx_ready = 0;
                    char dbuf[16];
                    terminal_writestring("  IP: ");
                    int_to_string(ip_address[0], dbuf); terminal_writestring(dbuf); terminal_writestring(".");
                    int_to_string(ip_address[1], dbuf); terminal_writestring(dbuf); terminal_writestring(".");
                    int_to_string(ip_address[2], dbuf); terminal_writestring(dbuf); terminal_writestring(".");
                    int_to_string(ip_address[3], dbuf); terminal_writestring(dbuf);
                    terminal_writestring("\n  Netmask: ");
                    int_to_string(subnet_mask[0], dbuf); terminal_writestring(dbuf); terminal_writestring(".");
                    int_to_string(subnet_mask[1], dbuf); terminal_writestring(dbuf); terminal_writestring(".");
                    int_to_string(subnet_mask[2], dbuf); terminal_writestring(dbuf); terminal_writestring(".");
                    int_to_string(subnet_mask[3], dbuf); terminal_writestring(dbuf);
                    terminal_writestring("\n  Gateway: ");
                    int_to_string(gateway[0], dbuf); terminal_writestring(dbuf); terminal_writestring(".");
                    int_to_string(gateway[1], dbuf); terminal_writestring(dbuf); terminal_writestring(".");
                    int_to_string(gateway[2], dbuf); terminal_writestring(dbuf); terminal_writestring(".");
                    int_to_string(gateway[3], dbuf); terminal_writestring(dbuf);
                    terminal_writestring("\n");
                    gateway_mac_resolved = 0;
                    return;
                }
            }
            terminal_writestring("DHCP: ACK timeout\n");
            dhcp_state = DHCP_INIT;
            return;
        }
        pcnet_rx_ready = 0;
    }
    terminal_writestring("DHCP: No OFFER received\n");

     ip_address[0] = 169; ip_address[1] = 254;
     ip_address[2] = pcnet_mac[4];
     ip_address[3] = pcnet_mac[5];
     subnet_mask[0] = 255; subnet_mask[1] = 255; subnet_mask[2] = 0; subnet_mask[3] = 0;
     gateway[0] = gateway[1] = gateway[2] = gateway[3] = 0;
     dhcp_enabled = false; 
     terminal_writestring("DHCP: Falling back to link-local IP ");
     char ipbuf[16];
     int_to_string(ip_address[0], ipbuf); terminal_writestring(ipbuf); terminal_writestring(".");
     int_to_string(ip_address[1], ipbuf); terminal_writestring(ipbuf); terminal_writestring(".");
     int_to_string(ip_address[2], ipbuf); terminal_writestring(ipbuf); terminal_writestring(".");
     int_to_string(ip_address[3], ipbuf); terminal_writestring(ipbuf); terminal_writestring("\n");
}

void send_dhcp_discover(void) { run_dhcp(); }

static void pcnet_check_rx(void) {
    uint32_t display_base = pcnet_mmio ? (uint32_t)pcnet_mmio_base : pcnet_io_base;
    terminal_writestring("PCNET: check_rx called, base=0x");
    char dbuf[16]; hex_to_string(display_base, dbuf); terminal_writestring(dbuf);
    terminal_writestring("\n");
    if (!pcnet_io_base && !pcnet_mmio) {
        terminal_writestring("PCNET: skip - no base configured\n");
        return;
    }
    terminal_writestring("PCNET: calling check_rx_real\n");
    pcnet_check_rx_real();
}

static void pcnet_check_rx_real(void) {
    if (!pcnet_io_base && !pcnet_mmio) {
        terminal_writestring("PCNET: RX skip - no base configured\n");
        return;
    }
    terminal_writestring("PCNET: RX check start\n");
    uint32_t csr0 = pcnet_csr_read(0);
    terminal_writestring("PCNET: Check RX CSR0=0x");
    char dbuf[16]; hex_to_string(csr0, dbuf); terminal_writestring(dbuf);
    terminal_writestring("\n");
    int start_idx = pcnet_rx_next;
    int packets_found = 0;
    for (int i = 0; i < PCNET_NUM_RX_DESCS; i++) {
        int idx = (start_idx + i) % PCNET_NUM_RX_DESCS;
        uint32_t flags = pcnet_rx_ring[idx].flags;
        terminal_writestring("PCNET: RX desc ");
        int_to_string(idx, dbuf); terminal_writestring(dbuf);
        terminal_writestring(" flags=0x");
        hex_to_string(flags, dbuf); terminal_writestring(dbuf);
        terminal_writestring("\n");
        terminal_writestring("PCNET: desc addr=0x");
        hex_to_string(pcnet_rx_ring[idx].addr, dbuf); terminal_writestring(dbuf); terminal_writestring("\n");

        if (flags & DESC_OWN) {
            terminal_writestring("PCNET: desc ");
            int_to_string(idx, dbuf); terminal_writestring(dbuf);
            terminal_writestring(" OWN set - buffer preview: ");
            for (int b = 0; b < 64; b++) {
                hex_to_string(pcnet_rx_bufs[idx][b], dbuf);
                terminal_writestring(&dbuf[2]);
                if (b < 63) terminal_writestring(" ");
            }
            terminal_writestring("\n");
            continue;
        }

        uint32_t pkt_len = flags & DESC_BCNT_MASK;
        terminal_writestring("PCNET: RX pkt len=");
        int_to_string(pkt_len, dbuf); terminal_writestring(dbuf);
        terminal_writestring("\n");
        if (!(flags & DESC_ERR) && pkt_len > 0 && pkt_len < PCNET_BUF_SIZE) {
            for (uint32_t j = 0; j < pkt_len; j++)
                pcnet_rx_buf[j] = pcnet_rx_bufs[idx][j];
            pcnet_rx_len = pkt_len;
            pcnet_rx_ready = 1;
            terminal_writestring("PCNET: RX packet captured!\n");
            terminal_writestring("PCNET: RX data (first 16 bytes): ");
            uint32_t to_dump = (pcnet_rx_len < 16) ? pcnet_rx_len : 16;
            for (uint32_t b = 0; b < to_dump; b++) {
                hex_to_string(pcnet_rx_buf[b], dbuf);
                terminal_writestring(&dbuf[2]);
                if (b + 1 < to_dump) terminal_writestring(" ");
            }
            terminal_writestring("\n");
            packets_found++;
        }

        pcnet_rx_ring[idx].flags = DESC_OWN;
        pcnet_rx_next = (idx + 1) % PCNET_NUM_RX_DESCS;
    }
    if (packets_found > 0) {
        terminal_writestring("PCNET: Found ");
        int_to_string(packets_found, dbuf); terminal_writestring(dbuf);
        terminal_writestring(" packet(s)\n");
    }
    pcnet_csr_write(0, csr0 | CSR0_RINT);
}

void send_icmp_ping(const char* target) {
    if (pcnet_io_base || pcnet_mmio) {
        uint8_t target_ip[4];
        int octet = 0;
        int val = 0;
        const char* p = target;
        while (*p && octet < 4) {
            if (*p >= '0' && *p <= '9') { val = val * 10 + (*p - '0'); }
            else if (*p == '.') { target_ip[octet++] = val; val = 0; }
            p++;
        }
        if (octet == 3) target_ip[3] = val;
        if (ip_address[0] == 0 && ip_address[1] == 0) {
            terminal_writestring("Error: No IP. Run 'ifconfig dhcp' first.\n");
            return;
        }
        terminal_writestring("PING: Resolving gateway...\n");
        if (!resolve_gateway()) {
            terminal_writestring("PING: Cannot reach gateway\n");
            return;
        }
        char buf[16];
        terminal_writestring("PING: Sending to "); terminal_writestring(target);
        terminal_writestring(" via gateway\n");
        int received = 0;
        for (int seq = 0; seq < 4; seq++) {
            send_icmp_echo(gateway_mac, target_ip, 0x1234, seq + 1);
            terminal_writestring("PING: Sent seq=");
            int_to_string(seq + 1, buf); terminal_writestring(buf);
            terminal_writestring("\n");
            delay_ms(200);
            pcnet_check_rx();
            if (pcnet_rx_ready && is_icmp_reply(pcnet_rx_buf, pcnet_rx_len)) {
                terminal_writestring("PING: Reply! bytes=32 time<1ms TTL=64\n");
                received++;
                pcnet_rx_ready = 0;
            }
        }
        terminal_writestring("PING: ");
        int_to_string(received, buf); terminal_writestring(buf);
        terminal_writestring("/4 received\n");
    } else if (rtl_io_base) {
        terminal_writestring("RTL8139 ping not implemented\n");
    } else {
        terminal_writestring("No network adapter\n");
    }
}

uint32_t pci_config_read(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t address = (uint32_t)((bus << 16) | (slot << 11) | (func << 8) | (offset & 0xfc) | ((uint32_t)0x80000000));
    outl(0xCF8, address);
    return inl(0xCFC);
}

void pci_config_write(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t value) {
    uint32_t address = (uint32_t)((bus << 16) | (slot << 11) | (func << 8) | (offset & 0xfc) | ((uint32_t)0x80000000));
    outl(0xCF8, address);
    outl(0xCFC, value);
}

static void pcnet_write_rap(uint16_t val) {
    if (pcnet_mmio) {
        volatile uint16_t* reg = (volatile uint16_t*)(pcnet_mmio_base + PCNET_RAP);
        *reg = val;
    } else {
        outw(pcnet_io_base + PCNET_RAP, val);
    }
}
static void pcnet_write_rdp(uint32_t val) {
    if (pcnet_mmio) {
        volatile uint32_t* reg = (volatile uint32_t*)(pcnet_mmio_base + PCNET_RDP);
        *reg = val;
    } else {
        outl(pcnet_io_base + PCNET_RDP, val);
    }
}
static uint32_t pcnet_read_rdp(void) {
    if (pcnet_mmio) {
        volatile uint32_t* reg = (volatile uint32_t*)(pcnet_mmio_base + PCNET_RDP);
        return *reg;
    } else {
        return inl(pcnet_io_base + PCNET_RDP);
    }
}

static void pcnet_csr_write(int csr, uint32_t val) {
    pcnet_write_rap(csr);
    pcnet_write_rdp(val);
}

static uint32_t pcnet_csr_read(int csr) {
    pcnet_write_rap(csr);
    return pcnet_read_rdp();
}

static void pcnet_init_chip(void) {
    pcnet_ib = (struct pcnet_init_block*)kmalloc(sizeof(struct pcnet_init_block));
    memset(pcnet_ib, 0, sizeof(struct pcnet_init_block));
    pcnet_tx_ring = (struct pcnet_desc*)kmalloc(sizeof(struct pcnet_desc) * PCNET_NUM_TX_DESCS);
    pcnet_rx_ring = (struct pcnet_desc*)kmalloc(sizeof(struct pcnet_desc) * PCNET_NUM_RX_DESCS);
    memset(pcnet_tx_ring, 0, sizeof(struct pcnet_desc) * PCNET_NUM_TX_DESCS);
    memset(pcnet_rx_ring, 0, sizeof(struct pcnet_desc) * PCNET_NUM_RX_DESCS);
    for (int i = 0; i < PCNET_NUM_TX_DESCS; i++) {
        pcnet_tx_bufs[i] = (uint8_t*)kmalloc(PCNET_BUF_SIZE);
        memset(pcnet_tx_bufs[i], 0, PCNET_BUF_SIZE);
        pcnet_tx_ring[i].addr = (uint32_t)virt_to_phys(pcnet_tx_bufs[i]);
        pcnet_tx_ring[i].flags = 0;
    }
    for (int i = 0; i < PCNET_NUM_RX_DESCS; i++) {
        pcnet_rx_bufs[i] = (uint8_t*)kmalloc(PCNET_BUF_SIZE);
        memset(pcnet_rx_bufs[i], 0, PCNET_BUF_SIZE);
        pcnet_rx_ring[i].addr = (uint32_t)virt_to_phys(pcnet_rx_bufs[i]);
        pcnet_rx_ring[i].flags = DESC_OWN;
    }
    pcnet_rx_next = 0;
    char dbuf[16];
    pcnet_ib->mode = 0x0000;
    for (int i = 0; i < 6; i++) pcnet_ib->phys_addr[i] = pcnet_mac[i];
    pcnet_ib->filter[0] = 0x00000000;
    pcnet_ib->filter[1] = 0x00000000;
    pcnet_ib->rx_ring = (uint32_t)virt_to_phys(pcnet_rx_ring);
    pcnet_ib->tx_ring = (uint32_t)virt_to_phys(pcnet_tx_ring);
    terminal_writestring("PCnet: RX ring phys=0x");
    hex_to_string((uint32_t)(uintptr_t)pcnet_rx_ring, dbuf);
    terminal_writestring(" TX ring phys=0x");
    hex_to_string((uint32_t)(uintptr_t)pcnet_tx_ring, dbuf);
    terminal_writestring("\n");
    pcnet_csr_write(0, CSR0_STOP);
    delay_ms(10);
    uint32_t csr0_check = pcnet_csr_read(0);
    terminal_writestring("PCNET: After STOP CSR0=0x");
    hex_to_string(csr0_check, dbuf);
    terminal_writestring("\n");
    uint32_t ib_addr = (uint32_t)(uintptr_t)pcnet_ib;
    terminal_writestring("PCNET: Init block at phys 0x");
    hex_to_string(ib_addr, dbuf);
    terminal_writestring("\n");
    pcnet_csr_write(1, ib_addr & 0xFFFF);
    pcnet_csr_write(2, (ib_addr >> 16) & 0xFFFF);
    pcnet_csr_write(3, (2 << 4) | (2 << 12));
    pcnet_csr_write(0, CSR0_INIT | CSR0_IENA);
    terminal_writestring("PCNET: INIT command sent, waiting for IDON...\n");
    int timeout = 10000;
    while (timeout--) {
        csr0_check = pcnet_csr_read(0);
        if (csr0_check & CSR0_IDON) {
            terminal_writestring("PCNET: IDON received after ");
            int_to_string(10000 - timeout, dbuf); terminal_writestring(dbuf);
            terminal_writestring("ms\n");
            break;
        }
        delay_ms(1);
    }
    if (timeout <= 0) {
        terminal_writestring("PCNET: INIT timeout! CSR0=0x");
        hex_to_string(pcnet_csr_read(0), dbuf); terminal_writestring(dbuf);
        terminal_writestring("\n");
    }
    pcnet_csr_write(0, CSR0_IDON | CSR0_STRT | CSR0_IENA);
    delay_ms(10);
    pcnet_csr_write(3, 0x0000);
    csr0_check = pcnet_csr_read(0);
    terminal_writestring("PCNET: After START CSR0=0x");
    hex_to_string(csr0_check, dbuf); terminal_writestring(dbuf);
    terminal_writestring("\n");
    if (!(csr0_check & CSR0_RXON)) {
        terminal_writestring("PCNET: RX failed to start!\n");
    }
    if (!(csr0_check & CSR0_TXON)) {
        terminal_writestring("PCNET: TX failed to start!\n");
    }
    terminal_writestring("PCnet-FAST: Descriptor rings ready\n");
}

static void pcnet_init(uint32_t base, uint8_t irq, int is_mmio) {
    pcnet_mmio = is_mmio;
    if (pcnet_mmio) {
        pcnet_mmio_base = (uintptr_t)base;
        pcnet_io_base = 0;
    } else {
        pcnet_io_base = base;
        pcnet_mmio_base = 0;
    }
    uint32_t display_base = pcnet_mmio ? (uint32_t)pcnet_mmio_base : pcnet_io_base;
    terminal_writestring("PCnet-FAST: Found at base 0x");
    char buf[16]; hex_to_string(display_base, buf); terminal_writestring(buf);
    terminal_writestring(" IRQ "); int_to_string(irq, buf); terminal_writestring(buf);
    terminal_writestring("\n");
    if (pcnet_mmio) {
        volatile uint16_t* reset_reg = (volatile uint16_t*)(pcnet_mmio_base + PCNET_RESET);
        *reset_reg = 0x0000;
    } else {
        outw(pcnet_io_base + PCNET_RESET, 0x0000);
    }
    delay_ms(100);
    uint32_t csr0 = pcnet_csr_read(0);
    terminal_writestring("PCNET: After reset CSR0=0x");
    char dbuf[16]; hex_to_string(csr0, dbuf); terminal_writestring(dbuf);
    terminal_writestring("\n");
    if (pcnet_mmio) {
        volatile uint8_t* prom = (volatile uint8_t*)(pcnet_mmio_base + PCNET_APROM00);
        for (int i = 0; i < 6; i++) pcnet_mac[i] = prom[i];
    } else {
        for (int i = 0; i < 6; i++) pcnet_mac[i] = inb(pcnet_io_base + PCNET_APROM00 + i);
    }
    terminal_writestring("PCnet-FAST: MAC ");
    for (int i = 0; i < 6; i++) {
        hex_to_string(pcnet_mac[i], buf);
        if (buf[0] == '0') terminal_writestring(&buf[2]);
        else terminal_writestring(buf);
        if (i < 5) terminal_writestring(":");
    }
    terminal_writestring("\n");
    pcnet_init_chip();
    network_initialized = true;
    terminal_writestring("PCnet-FAST: Ready\n");
}

void pci_list_devices() {
    char buffer[11];
    for (uint32_t bus = 0; bus < 256; bus++) {
        for (uint32_t slot = 0; slot < 32; slot++) {
            for (uint32_t func = 0; func < 8; func++) {
                uint32_t pci_data = pci_config_read(bus, slot, func, 0);
                if (pci_data == 0xFFFFFFFF) continue;
                uint16_t vendor = pci_data & 0xFFFF;
                uint16_t device = (pci_data >> 16) & 0xFFFF;
                if (vendor == RTL8139_VENDOR_ID && device == RTL8139_DEVICE_ID) {
                    terminal_writestring("Found: Realtek RTL8139\n");
                    uint32_t bar0 = pci_config_read(bus, slot, func, 0x10);
                    uint32_t io_base = bar0 & 0xFFFFFFFC;
                    uint8_t irq = pci_config_read(bus, slot, func, 0x3C) & 0xFF;
                    for (int i = 0; i < 6; i++) rtl_mac[i] = inb(io_base + i);
                    rtl_io_base = io_base; rtl_irq = irq;
                    rtl8139_reset(io_base); rtl8139_set_mac(io_base);
                    outl(io_base + 0x44, 0x000C); outw(io_base + 0x3C, 0x0005);
                    network_initialized = true;
                    terminal_writestring("RTL8139: Initialized\n");
                    return;
                }
                if (vendor == PCNET_VENDOR_ID && (device == PCNET_DEVICE_ID || device == PCNET_FAST_DEVICE_ID)) {
                    terminal_writestring("Found: AMD PCnet-FAST III\n");
                    uint32_t bar0 = pci_config_read(bus, slot, func, 0x10);
                    terminal_writestring("PCNET: BAR0 raw=0x");
                    char dbuf[16]; hex_to_string(bar0, dbuf); terminal_writestring(dbuf);
                    terminal_writestring("\n");
                    int is_io = (bar0 & 0x1) != 0;
                    uint32_t base = is_io ? (bar0 & 0xFFFFFFFC) : (bar0 & 0xFFFFFFF0);
                    terminal_writestring(is_io ? "PCNET: I/O base=0x" : "PCNET: MMIO base=0x");
                    hex_to_string(base, dbuf); terminal_writestring(dbuf);
                    terminal_writestring("\n");
                    uint32_t cmd = pci_config_read(bus, slot, func, 0x04);
                    terminal_writestring("PCNET: PCI cmd before=0x");
                    hex_to_string(cmd, dbuf); terminal_writestring(dbuf);
                    terminal_writestring("\n");

                    cmd |= 0x7;
                    pci_config_write(bus, slot, func, 0x04, cmd);
                    cmd = pci_config_read(bus, slot, func, 0x04);
                    terminal_writestring("PCNET: PCI cmd after=0x");
                    hex_to_string(cmd, dbuf); terminal_writestring(dbuf);
                    terminal_writestring("\n");
                    uint8_t irq = pci_config_read(bus, slot, func, 0x3C) & 0xFF;
                    pcnet_init(base, irq, is_io ? 0 : 1);
                    return;
                }
            }
        }
    }
    terminal_writestring("No supported network adapter found\n");
}

void detect_network_cards() {
    terminal_writestring("Detecting network adapters...\n");
    for (uint32_t bus = 0; bus < 256; bus++) {
        for (uint32_t slot = 0; slot < 32; slot++) {
            for (uint32_t func = 0; func < 8; func++) {
                uint32_t pci_data = pci_config_read(bus, slot, func, 0);
                if (pci_data == 0xFFFFFFFF) continue;
                uint16_t vendor = pci_data & 0xFFFF;
                uint16_t device = (pci_data >> 16) & 0xFFFF;
                if (vendor == RTL8139_VENDOR_ID && device == RTL8139_DEVICE_ID) {
                    terminal_writestring("Found: Realtek RTL8139\n");
                    uint32_t bar0 = pci_config_read(bus, slot, func, 0x10);
                    uint32_t io_base = bar0 & 0xFFFFFFFC;
                    uint8_t irq = pci_config_read(bus, slot, func, 0x3C) & 0xFF;
                    for (int i = 0; i < 6; i++) rtl_mac[i] = inb(io_base + i);
                    rtl_io_base = io_base; rtl_irq = irq;
                    rtl8139_reset(io_base); rtl8139_set_mac(io_base);
                    outl(io_base + 0x44, 0x000C); outw(io_base + 0x3C, 0x0005);
                    network_initialized = true;
                    terminal_writestring("RTL8139: Initialized\n");
                    return;
                }
                if (vendor == PCNET_VENDOR_ID && (device == PCNET_DEVICE_ID || device == PCNET_FAST_DEVICE_ID)) {
                    terminal_writestring("Found: AMD PCnet-FAST III\n");
                    uint32_t bar0 = pci_config_read(bus, slot, func, 0x10);
                    terminal_writestring("PCNET: BAR0 raw=0x");
                    char dbuf[16]; hex_to_string(bar0, dbuf); terminal_writestring(dbuf);
                    terminal_writestring("\n");
                    int is_io = (bar0 & 0x1) != 0;
                    uint32_t base = is_io ? (bar0 & 0xFFFFFFFC) : (bar0 & 0xFFFFFFF0);
                    terminal_writestring(is_io ? "PCNET: I/O base=0x" : "PCNET: MMIO base=0x");
                    hex_to_string(base, dbuf); terminal_writestring(dbuf);
                    terminal_writestring("\n");
                    uint32_t cmd = pci_config_read(bus, slot, func, 0x04);
                    terminal_writestring("PCNET: PCI cmd before=0x");
                    hex_to_string(cmd, dbuf); terminal_writestring(dbuf);
                    terminal_writestring("\n");
                    cmd |= 0x7;
                    pci_config_write(bus, slot, func, 0x04, cmd);
                    cmd = pci_config_read(bus, slot, func, 0x04);
                    terminal_writestring("PCNET: PCI cmd after=0x");
                    hex_to_string(cmd, dbuf); terminal_writestring(dbuf);
                    terminal_writestring("\n");
                    uint8_t irq = pci_config_read(bus, slot, func, 0x3C) & 0xFF;
                    pcnet_init(base, irq, is_io ? 0 : 1);
                    return;
                }
            }
        }
    }
    terminal_writestring("No supported network adapter found\n");
}

