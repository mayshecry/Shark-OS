
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

static int snprintf(char* buf, int size, const char* fmt, ...){
    char* arg_ptr = (char*)((uintptr_t)&fmt + sizeof(fmt) + sizeof(void*));
    int pos=0;
    for(int i=0;fmt[i] && pos<size-1;i++){
        if(fmt[i]=='%' && fmt[i+1]=='s'){
            const char* s = *(const char**)(arg_ptr);
            arg_ptr += sizeof(const char*);
            for(int j=0;s[j] && pos<size-1;j++) buf[pos++]=s[j];
            i++;
        } else buf[pos++]=fmt[i];
    }
    buf[pos]=0;
    return pos;
}


#define ETH_IP   0x0800
#define ETH_ARP  0x0806
#define ETH_IPV6 0x86DD
#define IP_ICMP  1
#define IP_TCP   6
#define IP_UDP   17
#define ARP_REQ   1
#define ARP_REP   2


static uint32_t rtl_base = 0;
static uint8_t rtl_irq = 0;
static uint8_t* rtl_rx_buf = NULL;
static uint32_t rtl_rx_pos = 0;
static uint32_t rtl_tx_next = 0;
static bool rtl_present = false;
#define RTL_RX_SIZE 8192
#define RTL_TX_N 4

static void rtl_reset(void){
    outb(rtl_base+0x37,0x10); delay_ms(100);
    outb(rtl_base+0x37,0x00); delay_ms(100);
}
static void rtl_init_chip(void){
    rtl_rx_buf = (uint8_t*)kmalloc(RTL_RX_SIZE+16);
    rtl_rx_pos = 0;
    uint32_t phys = (uint32_t)virt_to_phys(rtl_rx_buf);
    outl(rtl_base+0x30, phys);
    for(int i=0;i<6;i++) outb(rtl_base+i, net_mac[i]);
    outw(rtl_base+0x3C, 0x0005);
    
    outl(rtl_base+0x44, 0x000C);
    outl(rtl_base+0x40, 0x0000);
    outb(rtl_base+0x37, 0x0C);
    net_has_link = 1;
}
static void rtl_send(const uint8_t* data, uint32_t len){
    if(len > 1792) len = 1792;
    uint32_t phys = (uint32_t)virt_to_phys((void*)data);
    outl(rtl_base+0x20+rtl_tx_next*4, phys);
    outl(rtl_base+0x10+rtl_tx_next*4, len);
    rtl_tx_next = (rtl_tx_next+1) % RTL_TX_N;
}
static void rtl_poll(void){
    uint8_t isr = inb(rtl_base+0x3E);
    if(!(isr & 0x01)) return;
    
    
    volatile uint8_t* rb = (volatile uint8_t*)rtl_rx_buf;
    int guard = 0;
    while(guard++ < 64){
        uint16_t status = rb[rtl_rx_pos] | (rb[rtl_rx_pos+1]<<8);
        if(!(status & 0x0001)) break; 
        uint16_t flen = rb[rtl_rx_pos+2] | (rb[rtl_rx_pos+3]<<8);
        uint32_t framelen = flen - 4;
        if(framelen > 14 && framelen < 1518){
            if(net_rx_cb) net_rx_cb((const uint8_t*)(rb + rtl_rx_pos + 4), framelen);
        }
        uint32_t adv = ((uint32_t)flen + 4 + 3) & ~3u;
        rtl_rx_pos += adv;
        if(rtl_rx_pos >= RTL_RX_SIZE) rtl_rx_pos = 0;
        outw(rtl_base+0x38, (uint16_t)rtl_rx_pos);
    }
    outb(rtl_base+0x3E, 0x01); 
}


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
#define D_OWN  0x80000000
#define D_STP  0x02000000
#define D_ENP  0x01000000
#define D_ERR  0x00004000
#define D_BCNT 0x0FFF
#define PN_TX 8
#define PN_RX 8
#define PN_BUF 1536

static uint32_t pcnet_io = 0;
static uintptr_t pcnet_mmio = 0;
static bool pcnet_present = false;
struct pdesc { uint32_t addr; uint32_t flags; } __attribute__((packed));
struct pib { uint16_t mode; uint8_t phys[6]; uint32_t filter[2]; uint32_t rx_ring; uint32_t tx_ring; } __attribute__((packed));
static struct pdesc* pn_tx=NULL;
static struct pdesc* pn_rx=NULL;
static uint8_t* pn_txb[PN_TX];
static uint8_t* pn_rxb[PN_RX];
static struct pib* pn_ib=NULL;
static int pn_rx_next=0;

static void pn_wrap(uint16_t v){ if(pcnet_mmio)*(volatile uint16_t*)(pcnet_mmio+PCNET_RAP)=v; else outw(pcnet_io+PCNET_RAP,v); }
static void pn_wrdp(uint32_t v){ if(pcnet_mmio)*(volatile uint32_t*)(pcnet_mmio+PCNET_RDP)=v; else outl(pcnet_io+PCNET_RDP,v); }
static uint32_t pn_rrdp(void){ if(pcnet_mmio)return *(volatile uint32_t*)(pcnet_mmio+PCNET_RDP); return inl(pcnet_io+PCNET_RDP); }
static void pn_csrw(int c,uint32_t v){ pn_wrap(c); pn_wrdp(v); }
static uint32_t pn_csrr(int c){ pn_wrap(c); return pn_rrdp(); }

static void pn_init_chip(void){
    pn_ib=(struct pib*)kmalloc(sizeof(*pn_ib));
    memset(pn_ib,0,sizeof(*pn_ib));
    pn_tx=(struct pdesc*)kmalloc(sizeof(*pn_tx)*PN_TX);
    pn_rx=(struct pdesc*)kmalloc(sizeof(*pn_rx)*PN_RX);
    memset(pn_tx,0,sizeof(*pn_tx)*PN_TX);
    memset(pn_rx,0,sizeof(*pn_rx)*PN_RX);
    for(int i=0;i<PN_TX;i++){ pn_txb[i]=(uint8_t*)kmalloc(PN_BUF); pn_tx[i].addr=(uint32_t)virt_to_phys(pn_txb[i]); pn_tx[i].flags=0; }
    for(int i=0;i<PN_RX;i++){ pn_rxb[i]=(uint8_t*)kmalloc(PN_BUF); pn_rx[i].addr=(uint32_t)virt_to_phys(pn_rxb[i]); pn_rx[i].flags=D_OWN; }
    pn_rx_next=0;
    pn_ib->mode=0;
    for(int i=0;i<6;i++) pn_ib->phys[i]=net_mac[i];
    pn_ib->rx_ring=(uint32_t)virt_to_phys(pn_rx);
    pn_ib->tx_ring=(uint32_t)virt_to_phys(pn_tx);
    pn_csrw(0,CSR0_STOP); delay_ms(10);
    uint32_t ib=(uint32_t)(uintptr_t)pn_ib;
    pn_csrw(1, ib & 0xFFFF);
    pn_csrw(2, (ib>>16)&0xFFFF);
    pn_csrw(3, (2<<4)|(2<<12));
    pn_csrw(0, CSR0_INIT|CSR0_IENA);
    int t=10000; while(t--){ if(pn_csrr(0)&CSR0_IDON) break; delay_ms(1); }
    pn_csrw(0, CSR0_IDON|CSR0_STRT|CSR0_IENA);
    delay_ms(10);
    pn_csrw(3,0);
    net_has_link=1;
}
static void pn_send(const uint8_t* data, uint32_t len){
    int idx=-1;
    for(int i=0;i<PN_TX;i++){ if(!(pn_tx[i].flags&D_OWN)){idx=i;break;} }
    if(idx<0) return;
    for(uint32_t i=0;i<len && i<PN_BUF;i++) pn_txb[idx][i]=data[i];
    pn_tx[idx].flags=(len & D_BCNT)|D_OWN|D_STP|D_ENP;
    pn_csrw(0, pn_csrr(0)|CSR0_TDMD);
}
static void pn_poll(void){
    uint32_t csr0=pn_csrr(0);
    if(csr0 & (CSR0_RINT|CSR0_TINT)){
        for(int i=0;i<PN_RX;i++){
            int idx=(pn_rx_next+i)%PN_RX;
            uint32_t fl=pn_rx[idx].flags;
            if(fl & D_OWN) continue;
            uint32_t pl=(fl & D_BCNT)-4;
            if(!(fl&D_ERR) && pl>14 && pl<PN_BUF){
                if(net_rx_cb) net_rx_cb((const uint8_t*)pn_rxb[idx], pl);
            }
            pn_rx[idx].flags=D_OWN;
            pn_rx_next=(idx+1)%PN_RX;
        }
        pn_csrw(0, csr0|CSR0_RINT|CSR0_TINT);
    }
}


void net_set_rx_callback(net_rx_cb_t cb){ net_rx_cb = cb; }
void net_send_raw(const uint8_t* data, uint32_t len){
    if(rtl_present) rtl_send(data,len);
    else if(pcnet_present) pn_send(data,len);
}
void net_poll(void){
    if(rtl_present) rtl_poll();
    else if(pcnet_present) pn_poll();
}

void net_init(void){
    for(uint32_t bus=0;bus<256;bus++){
        for(uint32_t slot=0;slot<32;slot++){
            for(uint32_t func=0;func<8;func++){
                uint32_t d=pci_config_read(bus,slot,func,0);
                if(d==0xFFFFFFFF) continue;
                uint16_t vid=d&0xFFFF, did=(d>>16)&0xFFFF;
                if(vid==0x10EC && did==0x8139){
                    uint32_t bar0=pci_config_read(bus,slot,func,0x10);
                    rtl_base=bar0 & 0xFFFFFFFC;
                    rtl_irq=pci_config_read(bus,slot,func,0x3C)&0xFF;
                    for(int i=0;i<6;i++) net_mac[i]=inb(rtl_base+i);
                    strcpy(net_driver_name,"Realtek RTL8139");
                    rtl_present=true; rtl_reset(); rtl_init_chip(); return;
                }
                if(vid==0x1022 && (did==0x2000||did==0x2001)){
                    uint32_t bar0=pci_config_read(bus,slot,func,0x10);
                    int isio=(bar0&0x1)!=0;
                    uint32_t base=isio?(bar0&0xFFFFFFFC):(bar0&0xFFFFFFF0);
                    uint32_t cmd=pci_config_read(bus,slot,func,0x04); cmd|=0x7;
                    pci_config_write(bus,slot,func,0x04,cmd);
                    if(isio){ pcnet_io=base; pcnet_mmio=0; }
                    else { pcnet_mmio=(uintptr_t)base; pcnet_io=0; }
                    if(pcnet_mmio)*(volatile uint16_t*)(pcnet_mmio+PCNET_RST)=0;
                    else outw(pcnet_io+PCNET_RST,0);
                    delay_ms(100);
                    for(int i=0;i<6;i++){
                        if(pcnet_mmio) net_mac[i]=*(volatile uint8_t*)(pcnet_mmio+PCNET_AP+i);
                        else net_mac[i]=inb(pcnet_io+PCNET_AP+i);
                    }
                    strcpy(net_driver_name,"AMD PCnet-FAST III");
                    pcnet_present=true; pn_init_chip(); return;
                }
            }
        }
    }
    strcpy(net_driver_name,"none");
}


static int my_memcmp(const void* a, const void* b, int n){
    const unsigned char* x = (const unsigned char*)a;
    const unsigned char* y = (const unsigned char*)b;
    for(int i=0;i<n;i++){
        if(x[i] != y[i]) return (int)x[i] - (int)y[i];
    }
    return 0;
}
static int my_strncmp(const char* a, const char* b, int n){
    for(int i=0;i<n;i++){
        if(a[i] != b[i]) return (int)(unsigned char)a[i] - (int)(unsigned char)b[i];
        if(a[i] == 0) break;
    }
    return 0;
}
#define memcmp my_memcmp
#define strncmp my_strncmp
static uint16_t csum16(const uint8_t* data, uint32_t len){
    uint32_t sum=0;
    for(uint32_t i=0;i<len;i+=2){
        uint16_t w=(data[i]<<8)|((i+1<len)?data[i+1]:0);
        sum+=w;
    }
    while(sum>>16) sum=(sum&0xFFFF)+(sum>>16);
    return (uint16_t)(~sum & 0xFFFF);
}


static uint32_t build_eth(uint8_t* pkt, const uint8_t* dstmac, uint16_t ethertype){
    for(int i=0;i<6;i++) pkt[i]=dstmac[i];
    for(int i=0;i<6;i++) pkt[6+i]=net_mac[i];
    pkt[12]=(ethertype>>8)&0xFF; pkt[13]=ethertype&0xFF;
    return 14;
}


static uint8_t arp_table_mac[8][6];
static uint8_t arp_table_ip[8][4];
static int arp_entries=0;

static int arp_lookup(const uint8_t* ip, uint8_t* mac){
    for(int i=0;i<arp_entries;i++){
        if(memcmp(arp_table_ip[i],ip,4)==0){ memcpy(mac,arp_table_mac[i],6); return 1; }
    }
    return 0;
}
static void arp_add(const uint8_t* ip, const uint8_t* mac){
    for(int i=0;i<arp_entries;i++){
        if(memcmp(arp_table_ip[i],ip,4)==0){ memcpy(arp_table_mac[i],mac,6); return; }
    }
    if(arp_entries<8){
        memcpy(arp_table_ip[arp_entries],ip,4);
        memcpy(arp_table_mac[arp_entries],mac,6);
        arp_entries++;
    }
}

int net_arp_resolve(const uint8_t* ip, uint8_t* mac){
    if(arp_lookup(ip,mac)) return 1;
    if(!net_has_link) return 0;
    uint8_t bc[6]={0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
    uint8_t pkt[42];
    uint32_t off=build_eth(pkt,bc,ETH_ARP);
    pkt[off+0]=0; pkt[off+1]=1; 
    pkt[off+2]=0x08; pkt[off+3]=0x00; 
    pkt[off+4]=6; pkt[off+5]=4; 
    pkt[off+6]=0; pkt[off+7]=ARP_REQ;
    for(int i=0;i<6;i++) pkt[off+8+i]=net_mac[i];
    for(int i=0;i<4;i++) pkt[off+14+i]=net_ip[i];
    for(int i=0;i<6;i++) pkt[off+18+i]=0;
    for(int i=0;i<4;i++) pkt[off+24+i]=ip[i];
    net_send_raw(pkt,42);
    for(int attempt=0;attempt<4;attempt++){
        delay_ms(300);
        net_poll();
        if(arp_lookup(ip,mac)) return 1;
        net_send_raw(pkt,42);
    }
    return 0;
}


static void (*ip_recv_cb)(const uint8_t* ip_payload, uint32_t len, const uint8_t* src_ip) = NULL;
static void (*arp_recv_tmp)(const uint8_t* pkt, uint32_t len) = NULL;
#define arp_recv_cb arp_recv_tmp

static void net_on_frame(const uint8_t* frame, uint32_t len){
    if(len < 14) return;
    uint16_t et = (frame[12]<<8)|frame[13];
    if(et==ETH_ARP){
        
        if(len < 28+20) return;
        uint16_t op=(frame[20]<<8)|frame[21];
        if(op==ARP_REP){
            
            uint8_t* smac=(uint8_t*)&frame[22];
            uint8_t* sip=(uint8_t*)&frame[28];
            arp_add(sip,smac);
            if(arp_recv_cb) arp_recv_cb(frame,len);
        }
    } else if(et==ETH_IP){
        if(len < 14+20) return;
        uint8_t ihl = (frame[14]&0x0F)*4;
        if(ihl<20) return;
        uint8_t proto=frame[14+9];
        const uint8_t* sip=&frame[14+12];
        const uint8_t* payload=&frame[14+ihl];
        uint16_t total=(frame[14+2]<<8)|frame[14+3];
        uint32_t plen = (total>ihl)?(total-ihl):0;
        if(plen > len-14-ihl) plen = (len>14+ihl)?(len-14-ihl):0;
        if(proto==IP_ICMP){
            
            if(payload[0]==0 && ip_recv_cb) ip_recv_cb(payload, plen, sip);
        } else if(proto==IP_TCP){
            if(ip_recv_cb) ip_recv_cb(payload, plen, sip);
        } else if(proto==IP_UDP){
            
            if(ip_recv_cb) ip_recv_cb(payload, plen, sip);
        }
    }
}


static uint8_t icmp_expect_ip[4];
static uint16_t icmp_id=0x1234;
static volatile int icmp_got_reply=0;
static void icmp_recv(const uint8_t* payload, uint32_t len, const uint8_t* src_ip){
    if(len<8) return;
    if(payload[0]!=0) return; 
    if(memcmp(src_ip, icmp_expect_ip, 4)!=0) return;
    icmp_got_reply=1;
}
int net_ping(const char* ip_str, int count){
    uint8_t tip[4];
    int oct=0,val=0; const char* p=ip_str;
    while(*p && oct<4){
        if(*p>='0'&&*p<='9'){ val=val*10+(*p-'0'); }
        else if(*p=='.'){ tip[oct++]=val; val=0; }
        p++;
    }
    if(oct==3) tip[3]=val; else return -1;
    if(!net_configured){ terminal_writestring("net: not configured (run ifconfig dhcp)\n"); return -1; }
    uint8_t gwmac[6];
    if(!net_arp_resolve(net_gw, gwmac)){ terminal_writestring("net: cannot reach gateway\n"); return -1; }
    memcpy(icmp_expect_ip, tip, 4);
    int received=0;
    for(int seq=0; seq<count; seq++){
        uint8_t pkt[14+20+40];
        uint32_t off=build_eth(pkt,gwmac,ETH_IP);
        
        pkt[off+0]=0x45; pkt[off+1]=0;
        uint16_t tlen=20+40;
        pkt[off+2]=(tlen>>8)&0xFF; pkt[off+3]=tlen&0xFF;
        pkt[off+4]=0; pkt[off+5]=0;
        pkt[off+6]=0x40; pkt[off+7]=0; 
        pkt[off+8]=0x40; pkt[off+9]=IP_ICMP;
        pkt[off+10]=0; pkt[off+11]=0;
        for(int i=0;i<4;i++) pkt[off+12+i]=net_ip[i];
        for(int i=0;i<4;i++) pkt[off+16+i]=tip[i];
        uint16_t ic=csum16(&pkt[off],20);
        pkt[off+10]=(ic>>8)&0xFF; pkt[off+11]=ic&0xFF;
        
        uint8_t* icmp=&pkt[off+20];
        icmp[0]=8; icmp[1]=0; icmp[2]=0; icmp[3]=0;
        icmp[4]=(icmp_id>>8)&0xFF; icmp[5]=icmp_id&0xFF;
        icmp[6]=(seq>>8)&0xFF; icmp[7]=seq&0xFF;
        for(int i=0;i<32;i++) icmp[8+i]='A'+(i%26);
        uint16_t cc=csum16(icmp,40);
        icmp[2]=(cc>>8)&0xFF; icmp[3]=cc&0xFF;
        icmp_got_reply=0;
        ip_recv_cb = icmp_recv;
        net_send_raw(pkt,sizeof(pkt));
        for(int w=0;w<20;w++){
            delay_ms(50);
            net_poll();
            if(icmp_got_reply) break;
        }
        if(icmp_got_reply){ received++; terminal_writestring("PING: reply seq="); }
        else terminal_writestring("PING: timeout seq=");
        char b[16]; int_to_string(seq+1,b); terminal_writestring(b);
        terminal_writestring("\n");
    }
    char b[16]; terminal_writestring("PING: ");
    int_to_string(received,b); terminal_writestring(b);
    terminal_writestring("/"); int_to_string(count,b); terminal_writestring(b);
    terminal_writestring(" received\n");
    ip_recv_cb=NULL;
    return received;
}


static uint8_t dns_resp_ip[4];
static volatile int dns_got=0;
static uint16_t dns_txid=0xABCD;

static void dns_recv(const uint8_t* payload, uint32_t len, const uint8_t* src_ip){
    if(len < 12) return;
    uint16_t tid=(payload[0]<<8)|payload[1];
    if(tid!=dns_txid) return;
    uint16_t anc=(payload[6]<<8)|payload[7];
    if(anc==0) return;
    
    uint32_t pos=12;
    while(pos<len && payload[pos]!=0) pos+=payload[pos]+1;
    pos++; 
    pos+=4; 
    for(int i=0;i<anc && pos+12<=len;i++){
        pos+=2; 
        uint16_t type=(payload[pos]<<8)|payload[pos+1]; pos+=2;
        pos+=2; 
        pos+=4; 
        uint16_t rdlen=(payload[pos]<<8)|payload[pos+1]; pos+=2;
        if(type==1 && rdlen==4){
            for(int j=0;j<4;j++) dns_resp_ip[j]=payload[pos+j];
            dns_got=1;
        }
        pos+=rdlen;
    }
}

static void dns_encode_name(char* out, const char* name){
    int o=0, seg=0;
    out[o++]=0;
    for(int i=0;name[i];i++){
        if(name[i]=='.'){ out[seg]=(uint8_t)(o-seg-1); seg=o; out[o++]=0; }
        else out[o++]=name[i];
    }
    out[seg]=(uint8_t)(o-seg-1);
    out[o++]=0;
}

int net_dns_lookup(const char* hostname, uint8_t* ip_out){
    if(!net_configured){ terminal_writestring("net: DNS not configured\n"); return -1; }
    if(net_dns[0]==0){ terminal_writestring("net: no DNS server\n"); return -1; }
    uint8_t gwmac[6];
    if(!net_arp_resolve(net_gw,gwmac)){ terminal_writestring("net: gateway unreachable\n"); return -1; }
    uint8_t pkt[14+20+8+256];
    uint32_t off=build_eth(pkt,gwmac,ETH_IP);
    
    uint16_t sport=0x3039; 
    uint16_t dport=53;
    
    uint8_t* dns=&pkt[off+20+8];
    dns[0]=(dns_txid>>8)&0xFF; dns[1]=dns_txid&0xFF;
    dns[2]=0x01; dns[3]=0; 
    dns[4]=0; dns[5]=1; 
    dns[6]=0; dns[7]=0; dns[8]=0; dns[9]=0; dns[10]=0; dns[11]=0;
    uint32_t np=12;
    char namebuf[128];
    int i; for(i=0;hostname[i]&& i<127;i++) namebuf[i]=hostname[i];
    namebuf[i]=0;
    dns_encode_name((char*)&dns[np], namebuf);
    uint32_t qlen=strlen((char*)&dns[np])+1;
    np+=qlen;
    dns[np++]=(0<<8)|1; 
    dns[np++]=(0<<8)|1; 
    uint16_t udplen=8+np-12;
    
    uint8_t* udp=&pkt[off+20];
    udp[0]=(sport>>8)&0xFF; udp[1]=sport&0xFF;
    udp[2]=(dport>>8)&0xFF; udp[3]=dport&0xFF;
    udp[4]=(udplen>>8)&0xFF; udp[5]=udplen&0xFF;
    udp[6]=0; udp[7]=0;
    
    pkt[off+0]=0x45; pkt[off+1]=0;
    uint16_t tlen=20+udplen;
    pkt[off+2]=(tlen>>8)&0xFF; pkt[off+3]=tlen&0xFF;
    pkt[off+4]=0; pkt[off+5]=0;
    pkt[off+6]=0x40; pkt[off+7]=0;
    pkt[off+8]=0x40; pkt[off+9]=IP_UDP;
    pkt[off+10]=0; pkt[off+11]=0;
    for(int i=0;i<4;i++) pkt[off+12+i]=net_ip[i];
    for(int i=0;i<4;i++) pkt[off+16+i]=net_dns[i];
    uint16_t ic=csum16(&pkt[off],20);
    pkt[off+10]=(ic>>8)&0xFF; pkt[off+11]=ic&0xFF;
    
    dns_got=0;
    ip_recv_cb = dns_recv;
    net_send_raw(pkt, off+20+udplen);
    for(int w=0;w<20;w++){ delay_ms(100); net_poll(); if(dns_got) break; }
    ip_recv_cb=NULL;
    if(dns_got){ memcpy(ip_out,dns_resp_ip,4); return 1; }
    return 0;
}



static uint8_t tcp_dst_mac[6];
static uint8_t tcp_dst_ip[4];
static uint16_t tcp_dst_port;
static uint32_t tcp_seq=0x1000;
static uint32_t tcp_ack=0;
static uint16_t tcp_src_port=0x4000;
static volatile int tcp_connected=0;
static uint8_t tcp_rx_buf[4096];
static volatile uint32_t tcp_rx_len=0;
static volatile int tcp_rx_ready=0;

static uint16_t tcp_csum(const uint8_t* ih, const uint8_t* tcp, uint32_t tcplen){
    
    uint32_t sum=0;
    uint32_t src=*(uint32_t*)&ih[12];
    uint32_t dst=*(uint32_t*)&ih[16];
    sum += (src>>16)+(src&0xFFFF)+(dst>>16)+(dst&0xFFFF);
    sum += 6; 
    sum += tcplen;
    for(uint32_t i=0;i<tcplen;i+=2){
        uint16_t w=(tcp[i]<<8)|((i+1<tcplen)?tcp[i+1]:0);
        sum+=w;
    }
    while(sum>>16) sum=(sum&0xFFFF)+(sum>>16);
    return (uint16_t)(~sum & 0xFFFF);
}

static void tcp_send_flags(uint8_t flags, const uint8_t* data, uint32_t dlen){
    uint8_t pkt[14+20+40];
    uint32_t off=build_eth(pkt,tcp_dst_mac,ETH_IP);
    uint8_t* ip=&pkt[off];
    ip[0]=0x45; ip[1]=0;
    uint16_t tlen=20+20+dlen;
    ip[2]=(tlen>>8)&0xFF; ip[3]=tlen&0xFF;
    ip[4]=0; ip[5]=0; ip[6]=0x40; ip[7]=0;
    ip[8]=0x40; ip[9]=IP_TCP; ip[10]=0; ip[11]=0;
    for(int i=0;i<4;i++) ip[12+i]=net_ip[i];
    for(int i=0;i<4;i++) ip[16+i]=tcp_dst_ip[i];
    uint16_t ic=csum16(ip,20); ip[10]=(ic>>8)&0xFF; ip[11]=ic&0xFF;
    uint8_t* tcp=&pkt[off+20];
    tcp[0]=(tcp_src_port>>8)&0xFF; tcp[1]=tcp_src_port&0xFF;
    tcp[2]=(tcp_dst_port>>8)&0xFF; tcp[3]=tcp_dst_port&0xFF;
    tcp[4]=(tcp_seq>>24)&0xFF; tcp[5]=(tcp_seq>>16)&0xFF; tcp[6]=(tcp_seq>>8)&0xFF; tcp[7]=tcp_seq&0xFF;
    tcp[8]=(tcp_ack>>24)&0xFF; tcp[9]=(tcp_ack>>16)&0xFF; tcp[10]=(tcp_ack>>8)&0xFF; tcp[11]=tcp_ack&0xFF;
    tcp[12]=0x50; 
    tcp[13]=flags;
    tcp[14]=0x72; tcp[15]=0x10; 
    tcp[16]=0; tcp[17]=0; 
    
    for(uint32_t i=0;i<dlen;i++) tcp[20+i]=data[i];
    uint16_t cs=tcp_csum(ip,tcp,20+dlen);
    tcp[16]=(cs>>8)&0xFF; tcp[17]=cs&0xFF;
    net_send_raw(pkt, off+20+20+dlen);
}

static void tcp_recv(const uint8_t* payload, uint32_t len, const uint8_t* src_ip){
    if(len < 20) return;
    uint16_t sport=(payload[0]<<8)|payload[1];
    uint16_t dport=(payload[2]<<8)|payload[3];
    if(dport!=tcp_src_port) return;
    uint32_t seq=((uint32_t)payload[4]<<24)|((uint32_t)payload[5]<<16)|((uint32_t)payload[6]<<8)|payload[7];
    uint8_t data_off=((payload[12]>>4)&0xF)*4;
    uint8_t flags=payload[13];
    if(flags & 0x02){ 
        tcp_ack = seq+1;
        tcp_send_flags(0x12, NULL, 0); 
        tcp_seq += 1;
        tcp_connected=1;
        return;
    }
    if(flags & 0x01){ 
        tcp_ack = seq + (len-data_off) + 1;
        tcp_send_flags(0x11, NULL, 0); 
        return;
    }
    if(len > data_off){
        uint32_t dlen = len - data_off;
        if(tcp_rx_len + dlen < 4096){
            for(uint32_t i=0;i<dlen;i++) tcp_rx_buf[tcp_rx_len+i]=payload[data_off+i];
            tcp_rx_len += dlen;
            tcp_rx_ready=1;
        }
        tcp_ack = seq + dlen;
        tcp_send_flags(0x10, NULL, 0); 
    }
}

int net_tcp_connect(const uint8_t* ip, uint16_t port,
                    const uint8_t* send_data, uint32_t send_len,
                    uint8_t* resp_buf, uint32_t resp_max){
    if(!net_configured){ terminal_writestring("net: not configured\n"); return -1; }
    if(!net_arp_resolve(net_gw, tcp_dst_mac)){ terminal_writestring("net: gateway unreachable\n"); return -1; }
    memcpy(tcp_dst_ip,ip,4); tcp_dst_port=port;
    tcp_seq=0x1000; tcp_ack=0; tcp_connected=0;
    tcp_rx_len=0; tcp_rx_ready=0;
    tcp_src_port=0x4000;
    ip_recv_cb=tcp_recv;
    
    tcp_send_flags(0x02, NULL, 0);
    int connected=0;
    for(int w=0;w<40;w++){ delay_ms(50); net_poll(); if(tcp_connected){connected=1;break;} }
    if(!connected){ ip_recv_cb=NULL; terminal_writestring("net: connection failed\n"); return -1; }
    
    if(send_data && send_len>0){
        tcp_seq += 1; 
        tcp_send_flags(0x18, send_data, send_len); 
        tcp_seq += send_len;
    }
    
    int total=0;
    for(int w=0;w<100;w++){
        delay_ms(50); net_poll();
        if(tcp_rx_len>0){ total=tcp_rx_len; if(total>=resp_max) break; }
    }
    if(total>resp_max) total=resp_max;
    if(total>0) memcpy(resp_buf,tcp_rx_buf,total);
    
    tcp_send_flags(0x11, NULL, 0); 
    ip_recv_cb=NULL;
    return total>0?total:-1;
}


int net_http_get(const char* url, uint8_t* out_buf, uint32_t out_max){
    
    const char* p=url;
    if(strncmp(p,"http://",7)==0) p+=7;
    char host[128]; int hi=0;
    while(*p && *p!=':' && *p!='/'){ if(hi<127) host[hi++]=*p; p++; }
    host[hi]=0;
    uint16_t port=80;
    if(*p==':'){ port=0; p++; while(*p>='0'&&*p<='9'){ port=port*10+(*p-'0'); p++; } }
    char path[256]; int pi=0;
    if(*p!='/') path[pi++]='/';
    while(*p && pi<255){ path[pi++]=*p; p++; }
    path[pi]=0;
    uint8_t ip[4];
    if(!net_dns_lookup(host, ip)){ terminal_writestring("net: DNS lookup failed for "); terminal_writestring(host); terminal_writestring("\n"); return -1; }
    char req[512];
    int rl=snprintf(req,sizeof(req),"GET %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n", path, host);
    uint8_t resp[8192];
    int n=net_tcp_connect(ip, port, (uint8_t*)req, rl, resp, sizeof(resp));
    if(n<=0) return -1;
    
    int header_end=-1;
    for(int i=0;i+4<n;i++){ if(resp[i]=='\r'&&resp[i+1]=='\n'&&resp[i+2]=='\r'&&resp[i+3]=='\n'){ header_end=i+4; break; } }
    int body_start=(header_end>=0)?header_end:0;
    int body_len=n-body_start;
    if(body_len>out_max) body_len=out_max;
    memcpy(out_buf, resp+body_start, body_len);
    return body_len;
}


void net_stack_init(void){
    net_set_rx_callback(net_on_frame);
}


void net_cmd_ifconfig(void){
    char b[16];
    terminal_writestring("NIC: "); terminal_writestring(net_driver_name); terminal_writestring("\n");
    terminal_writestring("MAC: ");
    for(int i=0;i<6;i++){ hex_to_string(net_mac[i],b); if(b[0]=='0') terminal_writestring(&b[2]); else terminal_writestring(b); if(i<5) terminal_writestring(":"); }
    terminal_writestring("\n");
    terminal_writestring("Link: "); terminal_writestring(net_has_link?"up\n":"down\n");
    terminal_writestring("IP: ");
    int_to_string(net_ip[0],b); terminal_writestring(b); terminal_writestring(".");
    int_to_string(net_ip[1],b); terminal_writestring(b); terminal_writestring(".");
    int_to_string(net_ip[2],b); terminal_writestring(b); terminal_writestring(".");
    int_to_string(net_ip[3],b); terminal_writestring(b); terminal_writestring("\n");
    terminal_writestring("Mask: ");
    int_to_string(net_mask[0],b); terminal_writestring(b); terminal_writestring(".");
    int_to_string(net_mask[1],b); terminal_writestring(b); terminal_writestring(".");
    int_to_string(net_mask[2],b); terminal_writestring(b); terminal_writestring(".");
    int_to_string(net_mask[3],b); terminal_writestring(b); terminal_writestring("\n");
    terminal_writestring("Gateway: ");
    int_to_string(net_gw[0],b); terminal_writestring(b); terminal_writestring(".");
    int_to_string(net_gw[1],b); terminal_writestring(b); terminal_writestring(".");
    int_to_string(net_gw[2],b); terminal_writestring(b); terminal_writestring(".");
    int_to_string(net_gw[3],b); terminal_writestring(b); terminal_writestring("\n");
    terminal_writestring("DNS: ");
    int_to_string(net_dns[0],b); terminal_writestring(b); terminal_writestring(".");
    int_to_string(net_dns[1],b); terminal_writestring(b); terminal_writestring(".");
    int_to_string(net_dns[2],b); terminal_writestring(b); terminal_writestring(".");
    int_to_string(net_dns[3],b); terminal_writestring(b); terminal_writestring("\n");
}

void net_cmd_ping(const char* target){ net_ping(target,4); }
void net_cmd_dns(const char* host){
    uint8_t ip[4];
    if(net_dns_lookup(host,ip)){
        char b[16]; terminal_writestring(host); terminal_writestring(" -> ");
        int_to_string(ip[0],b); terminal_writestring(b); terminal_writestring(".");
        int_to_string(ip[1],b); terminal_writestring(b); terminal_writestring(".");
        int_to_string(ip[2],b); terminal_writestring(b); terminal_writestring(".");
        int_to_string(ip[3],b); terminal_writestring(b); terminal_writestring("\n");
    } else terminal_writestring("DNS lookup failed\n");
}
void net_cmd_wget(const char* url){
    uint8_t out[8192];
    int n=net_http_get(url,out,sizeof(out));
    if(n>0){
        char b[16]; terminal_writestring("Received "); int_to_string(n,b); terminal_writestring(b); terminal_writestring(" bytes\n");
        for(int i=0;i<n;i++){ if(out[i]>=32 && out[i]<127) terminal_putchar(out[i]); else if(out[i]=='\n') terminal_putchar('\n'); }
        terminal_writestring("\n");
    } else terminal_writestring("HTTP GET failed\n");
}
void net_cmd_netstat(void){
    net_cmd_ifconfig();
}


static uint8_t dhcp_srv[4];
static uint32_t dhcp_xid=0xDEADBEEF;
static volatile int dhcp_got=0;

static void dhcp_recv(const uint8_t* payload, uint32_t len, const uint8_t* src_ip){
    
    if(len < 240) return;
    uint32_t off=236;
    if(payload[off]!=0x63||payload[off+1]!=0x82||payload[off+2]!=0x53||payload[off+3]!=0x63) return;
    uint32_t msgtype=0;
    uint32_t p=off+4;
    while(p+2<=len){
        uint8_t opt=payload[p]; if(opt==0xFF) break;
        uint8_t ol=(p<len)?payload[p+1]:0;
        if(opt==0x35 && ol==1){ msgtype=payload[p+2]; }
        else if(opt==0x01 && ol==4){ for(int i=0;i<4;i++) net_mask[i]=payload[p+2+i]; }
        else if(opt==0x03 && ol>=4){ for(int i=0;i<4;i++) net_gw[i]=payload[p+2+i]; }
        else if(opt==0x06 && ol>=4){ for(int i=0;i<4;i++) net_dns[i]=payload[p+2+i]; }
        else if(opt==0x36 && ol==4){ for(int i=0;i<4;i++) dhcp_srv[i]=payload[p+2+i]; }
        p += 2 + (ol?ol:1);
    }
    if(msgtype==5){ 
        
        for(int i=0;i<4;i++) net_ip[i]=payload[16+i];
        dhcp_got=1;
    }
}

static void dhcp_send(int type, const uint8_t* req_ip, const uint8_t* srv){
    uint8_t bc[6]={0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
    uint8_t pkt[14+20+8+300];
    uint32_t off=build_eth(pkt,bc,ETH_IP);
    uint16_t sport=68, dport=67;
    uint8_t* d=(uint8_t*)&pkt[off+20+8];
    memset(d,0,300);
    d[0]=1; d[1]=1; d[2]=6; d[3]=0;
    d[4]=(dhcp_xid>>24)&0xFF; d[5]=(dhcp_xid>>16)&0xFF; d[6]=(dhcp_xid>>8)&0xFF; d[7]=dhcp_xid&0xFF;
    for(int i=0;i<6;i++) d[28+i]=net_mac[i];
    d[236]=0x63; d[237]=0x82; d[238]=0x53; d[239]=0x63;
    int o=240;
    d[o++]=0x35; d[o++]=1; d[o++]=type;
    if(req_ip && type==3){ d[o++]=0x32; d[o++]=4; for(int i=0;i<4;i++) d[o++]=req_ip[i]; }
    if(srv && type==3){ d[o++]=0x36; d[o++]=4; for(int i=0;i<4;i++) d[o++]=srv[i]; }
    d[o++]=0x37; d[o++]=4; d[o++]=1; d[o++]=3; d[o++]=6; d[o++]=0x36;
    d[o++]=0xFF;
    uint16_t udplen=8+o;
    uint8_t* udp=&pkt[off+20];
    udp[0]=(sport>>8)&0xFF; udp[1]=sport&0xFF;
    udp[2]=(dport>>8)&0xFF; udp[3]=dport&0xFF;
    udp[4]=(udplen>>8)&0xFF; udp[5]=udplen&0xFF; udp[6]=0; udp[7]=0;
    pkt[off+0]=0x45; pkt[off+1]=0;
    uint16_t tlen=20+udplen;
    pkt[off+2]=(tlen>>8)&0xFF; pkt[off+3]=tlen&0xFF;
    pkt[off+4]=0; pkt[off+5]=0; pkt[off+6]=0x40; pkt[off+7]=0;
    pkt[off+8]=0x40; pkt[off+9]=IP_UDP; pkt[off+10]=0; pkt[off+11]=0;
    for(int i=0;i<4;i++) pkt[off+12+i]=0;
    for(int i=0;i<4;i++) pkt[off+16+i]=0xFF;
    uint16_t ic=csum16(&pkt[off],20); pkt[off+10]=(ic>>8)&0xFF; pkt[off+11]=ic&0xFF;
    net_send_raw(pkt, off+20+udplen);
}

int net_dhcp(void){
    if(!net_has_link){ terminal_writestring("net: no link\n"); return -1; }
    dhcp_xid = 0xDEADBEEF;
    dhcp_got=0;
    ip_recv_cb = dhcp_recv;
    dhcp_send(1, NULL, NULL); 
    terminal_writestring("DHCP: discovering...\n");
    for(int a=0;a<8;a++){
        delay_ms(400); net_poll();
        if(dhcp_got) break;
        dhcp_send(1, NULL, NULL);
    }
    if(dhcp_got){
        
        dhcp_send(3, net_ip, dhcp_srv);
        for(int a=0;a<8;a++){
            delay_ms(400); net_poll();
            if(dhcp_got && net_ip[0]!=0) break;
        }
    }
    ip_recv_cb=NULL;
    if(net_ip[0]!=0||net_ip[1]!=0||net_ip[2]!=0||net_ip[3]!=0){
        net_configured=1;
        char b[16];
        terminal_writestring("DHCP: configured IP ");
        int_to_string(net_ip[0],b); terminal_writestring(b); terminal_writestring(".");
        int_to_string(net_ip[1],b); terminal_writestring(b); terminal_writestring(".");
        int_to_string(net_ip[2],b); terminal_writestring(b); terminal_writestring(".");
        int_to_string(net_ip[3],b); terminal_writestring(b); terminal_writestring("\n");
        return 1;
    }
    
    net_ip[0]=169; net_ip[1]=254; net_ip[2]=net_mac[4]; net_ip[3]=net_mac[5];
    net_mask[0]=255; net_mask[1]=255; net_mask[2]=0; net_mask[3]=0;
    net_configured=1;
    terminal_writestring("DHCP: no server, using link-local\n");
    return 0;
}

void net_cmd_dhcp(void){
    net_dhcp();
}
