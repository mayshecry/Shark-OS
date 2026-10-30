#include "kernel.h"
#include "net.h"
#include "tls.h"
#include "ssh.h"
#include "httpd.h"
#include "server_cli.h"
#include "sftp.h"

#define SSH_CLIENTS 8
#define SSH_MAX_PACKET 65536
#define SSH_STREAM_SIZE 16384
#define SSH_TEXT_SIZE 512
#define SSH_VERSION "SSH-2.0-SharkOS_0.1"

typedef struct {
    aes128_t aes;
    uint8_t counter[16];
    uint8_t stream[16];
    uint8_t used;
} ssh_ctr_t;

typedef struct {
    const uint8_t* p;
    uint32_t len;
    uint32_t pos;
    int bad;
} ssh_reader_t;

typedef struct {
    uint8_t* p;
    uint32_t len;
    uint32_t cap;
    int bad;
} ssh_writer_t;

typedef struct {
    int handle;
    uint8_t active;
    uint8_t state;
    uint8_t stream[SSH_STREAM_SIZE];
    uint32_t stream_len;
    uint32_t stream_rd;
    uint8_t wire[SSH_MAX_PACKET + 64];
    uint32_t wire_len;
    uint32_t packet_need;
    uint32_t packet_len;
    uint8_t length_decrypted;
    uint8_t payload[SSH_MAX_PACKET];
    uint32_t payload_len;
    uint8_t version[256];
    uint32_t version_len;
    uint8_t client_version[256];
    uint32_t client_version_len;
    uint8_t client_kexinit[2048];
    uint32_t client_kexinit_len;
    uint8_t server_kexinit[512];
    uint32_t server_kexinit_len;
    uint8_t key_private[32];
    uint8_t key_public[32];
    uint8_t shared[32];
    uint8_t exchange_hash[32];
    uint8_t session_id[32];
    uint8_t host_blob[64];
    uint32_t host_blob_len;
    uint8_t in_encrypted;
    uint8_t out_encrypted;
    uint8_t in_ctr_ready;
    uint8_t out_ctr_ready;
    ssh_ctr_t in_ctr;
    ssh_ctr_t out_ctr;
    uint8_t in_mac_key[32];
    uint8_t out_mac_key[32];
    uint32_t in_seq;
    uint32_t out_seq;
    uint32_t channel_client_id;
    uint32_t channel_window;
    uint32_t channel_max_packet;
    uint8_t channel_open;
    uint8_t channel_shell;
    uint8_t channel_sftp;
    uint8_t channel_pty;
    uint8_t channel_htop;
    uint32_t channel_htop_last_draw;
    sftp_session_t sftp;
    uint8_t channel_eof;
    uint8_t auth_failures;
    uint8_t discard_next;
    uint8_t suppress_lf;
    struct fs_node* working_directory;
    char line[SSH_TEXT_SIZE];
    uint32_t line_len;
} ssh_session_t;

static ssh_session_t ssh_sessions[SSH_CLIENTS];
static uint8_t ssh_host_seed[32];
static uint8_t ssh_host_public[32];
static char ssh_username[32];
static char ssh_password[64];
static uint8_t ssh_host_ready;
static uint8_t ssh_password_ready;
static uint8_t ssh_active;
static uint16_t ssh_port;
static char ssh_htop_output[4096];

static void ssh_writer_init(ssh_writer_t* w,uint8_t* p,uint32_t cap){w->p=p;w->len=0;w->cap=cap;w->bad=0;}
static void ssh_put_bytes(ssh_writer_t* w,const uint8_t* p,uint32_t n){if(w->bad||n>w->cap-w->len){w->bad=1;return;}if(n)memcpy(w->p+w->len,p,n);w->len+=n;}
static void ssh_put_u8(ssh_writer_t* w,uint8_t v){ssh_put_bytes(w,&v,1);}
static void ssh_put_u32(ssh_writer_t* w,uint32_t v){uint8_t b[4]={(uint8_t)(v>>24),(uint8_t)(v>>16),(uint8_t)(v>>8),(uint8_t)v};ssh_put_bytes(w,b,4);}
static void ssh_put_string(ssh_writer_t* w,const uint8_t* p,uint32_t n){ssh_put_u32(w,n);ssh_put_bytes(w,p,n);}
static void ssh_put_text(ssh_writer_t* w,const char* s){ssh_put_string(w,(const uint8_t*)s,(uint32_t)strlen(s));}
static void ssh_reader_init(ssh_reader_t* r,const uint8_t* p,uint32_t len){r->p=p;r->len=len;r->pos=0;r->bad=0;}
static uint8_t ssh_get_u8(ssh_reader_t* r){if(r->pos>=r->len){r->bad=1;return 0;}return r->p[r->pos++];}
static uint32_t ssh_get_u32(ssh_reader_t* r){if(r->len-r->pos<4){r->bad=1;return 0;}uint32_t v=((uint32_t)r->p[r->pos]<<24)|((uint32_t)r->p[r->pos+1]<<16)|((uint32_t)r->p[r->pos+2]<<8)|r->p[r->pos+3];r->pos+=4;return v;}
static const uint8_t* ssh_get_string(ssh_reader_t* r,uint32_t* n){uint32_t len=ssh_get_u32(r);if(r->bad||len>r->len-r->pos){r->bad=1;*n=0;return NULL;}const uint8_t* p=r->p+r->pos;r->pos+=len;*n=len;return p;}
static int ssh_bytes_equal(const uint8_t* a,const uint8_t* b,uint32_t n){uint8_t d=0;for(uint32_t i=0;i<n;i++)d|=a[i]^b[i];return d==0;}
static int ssh_string_is(const uint8_t* p,uint32_t n,const char* text){return p&&n==strlen(text)&&ssh_bytes_equal(p,(const uint8_t*)text,n);}
static int ssh_name_list_has(const uint8_t* p,uint32_t n,const char* name){uint32_t start=0;uint32_t want=(uint32_t)strlen(name);while(start<n){uint32_t end=start;while(end<n&&p[end]!=',')end++;if(end-start==want&&ssh_bytes_equal(p+start,(const uint8_t*)name,want))return 1;start=end+1;}return 0;}
static int ssh_first_name_is(const uint8_t* p,uint32_t n,const char* name){uint32_t end=0;while(end<n&&p[end]!=',')end++;return end==strlen(name)&&ssh_bytes_equal(p,(const uint8_t*)name,end);}
static int ssh_equal_secret(const char* a,const uint8_t* b,uint32_t n){uint32_t al=(uint32_t)strlen(a);uint32_t diff=al^n;uint32_t max=al>n?al:n;for(uint32_t i=0;i<max;i++){uint8_t x=i<al?(uint8_t)a[i]:0;uint8_t y=i<n?b[i]:0;diff|=x^y;}return diff==0;}
static void ssh_ctr_init(ssh_ctr_t* c,const uint8_t key[16],const uint8_t iv[16]){aes128_init(&c->aes,key);memcpy(c->counter,iv,16);memset(c->stream,0,16);c->used=16;}
static void ssh_ctr_xor(ssh_ctr_t* c,uint8_t* data,uint32_t len){for(uint32_t i=0;i<len;i++){if(c->used==16){aes128_encrypt_block(&c->aes,c->counter,c->stream);for(int j=15;j>=0;j--){c->counter[j]++;if(c->counter[j])break;}c->used=0;}data[i]^=c->stream[c->used++];}}
static int ssh_send_raw(ssh_session_t* s,const uint8_t* data,uint32_t len){return net_tcp_server_send(s->handle,data,len)==(int)len?0:-1;}

static int ssh_send_packet(ssh_session_t* s,const uint8_t* payload,uint32_t payload_len){
    uint8_t packet[SSH_MAX_PACKET+64];uint8_t mac_input[SSH_MAX_PACKET+8];
    if(payload_len>SSH_MAX_PACKET-64)return -1;
    uint32_t block=s->out_encrypted?16:8;
    uint32_t pad=block-((4+1+payload_len)%block);if(pad<4)pad+=block;
    uint32_t packet_len=1+payload_len+pad;uint32_t total=4+packet_len;
    if(total+(s->out_encrypted?32:0)>sizeof(packet))return -1;
    packet[0]=(uint8_t)(packet_len>>24);packet[1]=(uint8_t)(packet_len>>16);packet[2]=(uint8_t)(packet_len>>8);packet[3]=(uint8_t)packet_len;
    packet[4]=(uint8_t)pad;if(payload_len)memcpy(packet+5,payload,payload_len);
    if(pad&&crypto_random_bytes(packet+5+payload_len,pad)!=0)return -1;
    if(s->out_encrypted){mac_input[0]=(uint8_t)(s->out_seq>>24);mac_input[1]=(uint8_t)(s->out_seq>>16);mac_input[2]=(uint8_t)(s->out_seq>>8);mac_input[3]=(uint8_t)s->out_seq;memcpy(mac_input+4,packet,total);uint8_t mac[32];hmac_sha256(s->out_mac_key,32,mac_input,total+4,mac);ssh_ctr_xor(&s->out_ctr,packet,total);memcpy(packet+total,mac,32);total+=32;}
    if(ssh_send_raw(s,packet,total)!=0)return -1;
    s->out_seq++;
    return 0;
}

static int ssh_stream_fill(ssh_session_t* s){
    if(s->stream_rd&&s->stream_rd==s->stream_len)s->stream_rd=s->stream_len=0;
    if(s->stream_rd&&s->stream_len==sizeof(s->stream)){uint32_t n=s->stream_len-s->stream_rd;memcpy(s->stream,s->stream+s->stream_rd,n);s->stream_len=n;s->stream_rd=0;}
    if(s->stream_len==sizeof(s->stream))return 0;
    int n=net_tcp_server_recv(s->handle,s->stream+s->stream_len,sizeof(s->stream)-s->stream_len,0);
    if(n>0){s->stream_len+=(uint32_t)n;return n;}
    if(n<0)return -1;
    return 0;
}

static uint32_t ssh_stream_take(ssh_session_t* s,uint8_t* out,uint32_t max){uint32_t n=s->stream_len-s->stream_rd;if(n>max)n=max;if(n){memcpy(out,s->stream+s->stream_rd,n);s->stream_rd+=n;if(s->stream_rd==s->stream_len)s->stream_rd=s->stream_len=0;}return n;}

static int ssh_packet_next(ssh_session_t* s){
    uint32_t mac_len=s->in_encrypted?32:0;
    if(s->wire_len<4){uint32_t n=ssh_stream_take(s,s->wire+s->wire_len,4-s->wire_len);s->wire_len+=n;if(s->wire_len<4)return 0;}
    if(!s->length_decrypted){if(s->in_encrypted)ssh_ctr_xor(&s->in_ctr,s->wire,4);s->packet_len=((uint32_t)s->wire[0]<<24)|((uint32_t)s->wire[1]<<16)|((uint32_t)s->wire[2]<<8)|s->wire[3];if(s->packet_len<5||s->packet_len>SSH_MAX_PACKET-64){
    return -1;}if(((s->packet_len+4)&(s->in_encrypted?15:7))!=0)return -1;s->packet_need=4+s->packet_len+mac_len;s->length_decrypted=1;}
    if(s->wire_len<s->packet_need){uint32_t n=ssh_stream_take(s,s->wire+s->wire_len,s->packet_need-s->wire_len);s->wire_len+=n;if(s->wire_len<s->packet_need)return 0;}
    if(s->in_encrypted){ssh_ctr_xor(&s->in_ctr,s->wire+4,s->packet_len);uint8_t mac_input[SSH_MAX_PACKET+8],mac[32];mac_input[0]=(uint8_t)(s->in_seq>>24);mac_input[1]=(uint8_t)(s->in_seq>>16);mac_input[2]=(uint8_t)(s->in_seq>>8);mac_input[3]=(uint8_t)s->in_seq;memcpy(mac_input+4,s->wire,4+s->packet_len);hmac_sha256(s->in_mac_key,32,mac_input,8+s->packet_len,mac);uint8_t diff=0;for(int i=0;i<32;i++)diff|=mac[i]^s->wire[4+s->packet_len+i];if(diff){
    return -1;}}
    uint32_t padding=s->wire[4];if(padding<4||padding+1>s->packet_len)return -1;s->payload_len=s->packet_len-padding-1;if(s->payload_len>sizeof(s->payload))return -1;if(s->payload_len)memcpy(s->payload,s->wire+5,s->payload_len);s->in_seq++;s->wire_len=0;s->packet_need=0;s->packet_len=0;s->length_decrypted=0;return 1;
}

static void ssh_put_mpint_from_shared(ssh_writer_t* w,const uint8_t little[32]){
    uint8_t be[33];be[0]=0;for(int i=0;i<32;i++)be[i+1]=little[i];uint32_t start=1;while(start<33&&be[start]==0)start++;if(start==33){ssh_put_u32(w,0);return;}if(be[start]&0x80)start--;ssh_put_string(w,be+start,33-start);
}

static int ssh_derive_key(const uint8_t* k_mpint,uint32_t k_len,const uint8_t h[32],uint8_t letter,const uint8_t sid[32],uint8_t* out,uint32_t out_len){
    uint32_t done=0;uint8_t previous[32];uint32_t prev_len=0;
    while(done<out_len){sha256_ctx_t c;uint8_t digest[32];sha256_init(&c);sha256_update(&c,k_mpint,k_len);sha256_update(&c,h,32);if(!prev_len){sha256_update(&c,&letter,1);sha256_update(&c,sid,32);}else sha256_update(&c,previous,prev_len);sha256_final(&c,digest);uint32_t n=out_len-done;if(n>32)n=32;memcpy(out+done,digest,n);done+=n;memcpy(previous,digest,32);prev_len=32;}
    return 0;
}

static int ssh_make_kexinit(ssh_session_t* s){
    ssh_writer_t w;ssh_writer_init(&w,s->server_kexinit,sizeof(s->server_kexinit));ssh_put_u8(&w,20);uint8_t cookie[16];if(crypto_random_bytes(cookie,sizeof(cookie))!=0)return -1;ssh_put_bytes(&w,cookie,sizeof(cookie));
    ssh_put_text(&w,"curve25519-sha256,curve25519-sha256@libssh.org");ssh_put_text(&w,"ssh-ed25519");ssh_put_text(&w,"aes128-ctr");ssh_put_text(&w,"aes128-ctr");ssh_put_text(&w,"hmac-sha2-256");ssh_put_text(&w,"hmac-sha2-256");ssh_put_text(&w,"none");ssh_put_text(&w,"none");ssh_put_text(&w,"");ssh_put_text(&w,"");ssh_put_u8(&w,0);ssh_put_u32(&w,0);if(w.bad)return -1;s->server_kexinit_len=w.len;return 0;
}

static int ssh_validate_kexinit(ssh_session_t* s){
    ssh_reader_t r;ssh_reader_init(&r,s->payload,s->payload_len);if(ssh_get_u8(&r)!=20||r.len-r.pos<16)return -1;r.pos+=16;const uint8_t* lists[10];uint32_t lens[10];for(int i=0;i<10;i++)lists[i]=ssh_get_string(&r,&lens[i]);uint8_t follows=ssh_get_u8(&r);ssh_get_u32(&r);if(r.bad||r.pos!=r.len)return -1;
    if(!ssh_name_list_has(lists[0],lens[0],"curve25519-sha256")&&!ssh_name_list_has(lists[0],lens[0],"curve25519-sha256@libssh.org"))return -1;
    if(!ssh_name_list_has(lists[1],lens[1],"ssh-ed25519"))return -1;
    if(!ssh_name_list_has(lists[2],lens[2],"aes128-ctr")||!ssh_name_list_has(lists[3],lens[3],"aes128-ctr"))return -1;
    if(!ssh_name_list_has(lists[4],lens[4],"hmac-sha2-256")||!ssh_name_list_has(lists[5],lens[5],"hmac-sha2-256"))return -1;
    if(!ssh_name_list_has(lists[6],lens[6],"none")||!ssh_name_list_has(lists[7],lens[7],"none"))return -1;
    if(follows&&(!ssh_first_name_is(lists[0],lens[0],"curve25519-sha256")&&!ssh_first_name_is(lists[0],lens[0],"curve25519-sha256@libssh.org")))s->discard_next=1;
    if(follows&&!ssh_first_name_is(lists[1],lens[1],"ssh-ed25519"))s->discard_next=1;
    return 0;
}

static int ssh_kex_reply(ssh_session_t* s){
    ssh_reader_t r;ssh_reader_init(&r,s->payload,s->payload_len);if(ssh_get_u8(&r)!=30)return -1;uint32_t qlen;const uint8_t* qc=ssh_get_string(&r,&qlen);if(r.bad||r.pos!=r.len||qlen!=32)return -1;
    if(crypto_random_bytes(s->key_private,32)!=0)return -1;
    x25519_base(s->key_public,s->key_private);
    x25519(s->shared,s->key_private,qc);
    uint8_t nz=0;for(int i=0;i<32;i++)nz|=s->shared[i];if(!nz)return -1;
    uint8_t host_blob[64];ssh_writer_t hw;ssh_writer_init(&hw,host_blob,sizeof(host_blob));ssh_put_text(&hw,"ssh-ed25519");ssh_put_string(&hw,ssh_host_public,32);if(hw.bad)return -1;s->host_blob_len=hw.len;memcpy(s->host_blob,host_blob,hw.len);
    uint8_t kenc[40];ssh_writer_t kw;ssh_writer_init(&kw,kenc,sizeof(kenc));ssh_put_mpint_from_shared(&kw,s->shared);if(kw.bad)return -1;
    uint8_t exchange[4096];ssh_writer_t ew;ssh_writer_init(&ew,exchange,sizeof(exchange));ssh_put_string(&ew,s->client_version,s->client_version_len);ssh_put_text(&ew,SSH_VERSION);ssh_put_string(&ew,s->client_kexinit,s->client_kexinit_len);ssh_put_string(&ew,s->server_kexinit,s->server_kexinit_len);ssh_put_string(&ew,s->host_blob,s->host_blob_len);ssh_put_string(&ew,qc,qlen);ssh_put_string(&ew,s->key_public,32);ssh_put_bytes(&ew,kenc,kw.len);if(ew.bad)return -1;sha256(exchange,ew.len,s->exchange_hash);memcpy(s->session_id,s->exchange_hash,32);
    uint8_t signature[64];if(ed25519_sign(ssh_host_seed,s->exchange_hash,32,signature)!=0)return -1;
    uint8_t sig_blob[128];ssh_writer_t sw;ssh_writer_init(&sw,sig_blob,sizeof(sig_blob));ssh_put_text(&sw,"ssh-ed25519");ssh_put_string(&sw,signature,sizeof(signature));if(sw.bad)return -1;
    uint8_t reply[512];ssh_writer_t rw;ssh_writer_init(&rw,reply,sizeof(reply));ssh_put_u8(&rw,31);ssh_put_string(&rw,s->host_blob,s->host_blob_len);ssh_put_string(&rw,s->key_public,32);ssh_put_string(&rw,sig_blob,sw.len);if(rw.bad||ssh_send_packet(s,reply,rw.len)!=0)return -1;
    uint8_t iv_c2s[16],iv_s2c[16],key_c2s[16],key_s2c[16],mac_c2s[32],mac_s2c[32];ssh_derive_key(kenc,kw.len,s->exchange_hash,'A',s->session_id,iv_c2s,16);ssh_derive_key(kenc,kw.len,s->exchange_hash,'B',s->session_id,iv_s2c,16);ssh_derive_key(kenc,kw.len,s->exchange_hash,'C',s->session_id,key_c2s,16);ssh_derive_key(kenc,kw.len,s->exchange_hash,'D',s->session_id,key_s2c,16);ssh_derive_key(kenc,kw.len,s->exchange_hash,'E',s->session_id,mac_c2s,32);ssh_derive_key(kenc,kw.len,s->exchange_hash,'F',s->session_id,mac_s2c,32);
    ssh_ctr_init(&s->in_ctr,key_c2s,iv_c2s);ssh_ctr_init(&s->out_ctr,key_s2c,iv_s2c);memcpy(s->in_mac_key,mac_c2s,32);memcpy(s->out_mac_key,mac_s2c,32);s->in_ctr_ready=1;s->out_ctr_ready=1;
    uint8_t newkeys=21;if(ssh_send_packet(s,&newkeys,1)!=0)return -1;s->out_encrypted=1;s->state=4;return 0;
}

static void ssh_send_auth_failure(ssh_session_t* s){uint8_t p[32];ssh_writer_t w;ssh_writer_init(&w,p,sizeof(p));ssh_put_u8(&w,51);ssh_put_text(&w,"password");ssh_put_u8(&w,0);ssh_send_packet(s,p,w.len);}
static int ssh_send_service_accept(ssh_session_t* s,const uint8_t* name,uint32_t len){uint8_t p[64];ssh_writer_t w;ssh_writer_init(&w,p,sizeof(p));ssh_put_u8(&w,6);ssh_put_string(&w,name,len);return w.bad?-1:ssh_send_packet(s,p,w.len);}
static int ssh_send_channel_status(ssh_session_t* s,uint8_t type){uint8_t p[5];ssh_writer_t w;ssh_writer_init(&w,p,sizeof(p));ssh_put_u8(&w,type);ssh_put_u32(&w,s->channel_client_id);return ssh_send_packet(s,p,w.len);}
static int ssh_send_exit_status(ssh_session_t* s,uint32_t status){uint8_t p[32];ssh_writer_t w;ssh_writer_init(&w,p,sizeof(p));ssh_put_u8(&w,98);ssh_put_u32(&w,s->channel_client_id);ssh_put_text(&w,"exit-status");ssh_put_u8(&w,0);ssh_put_u32(&w,status);return w.bad?-1:ssh_send_packet(s,p,w.len);}
static int ssh_send_channel_data(ssh_session_t* s,const uint8_t* data,uint32_t len){if(!s->channel_open||!len)return len? -1:0;uint32_t max=s->channel_max_packet;if(!max||max>1024)max=1024;if(max>len)max=len;if(max>s->channel_window)max=s->channel_window;if(!max)return -1;uint8_t p[1040];ssh_writer_t w;ssh_writer_init(&w,p,sizeof(p));ssh_put_u8(&w,94);ssh_put_u32(&w,s->channel_client_id);ssh_put_string(&w,data,max);if(w.bad||ssh_send_packet(s,p,w.len)!=0)return -1;s->channel_window-=max;return (int)max;}
static int ssh_send_text(ssh_session_t* s,const char* text){uint32_t len=(uint32_t)strlen(text),done=0;while(done<len){int n=ssh_send_channel_data(s,(const uint8_t*)text+done,len-done);if(n<=0)return -1;done+=(uint32_t)n;}return 0;}

static void ssh_text_append(char* out,uint32_t cap,uint32_t* used,const char* text){while(*text&&*used+1<cap)out[(*used)++]=*text++;out[*used]=0;}
static void ssh_text_num(char* out,uint32_t cap,uint32_t* used,uint32_t value){char n[16];int_to_string(value,n);ssh_text_append(out,cap,used,n);}
static int ssh_send_bytes(ssh_session_t* s,const uint8_t* data,uint32_t len){uint32_t done=0;while(done<len){int n=ssh_send_channel_data(s,data+done,len-done);if(n<=0)return -1;done+=(uint32_t)n;}return 0;}

static int ssh_sftp_send(void* context,const uint8_t* data,uint32_t length){return ssh_send_bytes((ssh_session_t*)context,data,length);}


static int ssh_send_prompt(ssh_session_t* s){
    char path[256];
    char prompt[300];
    server_format_path(s->working_directory?s->working_directory:root,path,sizeof(path));
    uint32_t used=0;
    const char* prefix="root@shark:";
    while(*prefix&&used+1<sizeof(prompt))prompt[used++]=*prefix++;
    for(uint32_t i=0;path[i]&&used+1<sizeof(prompt);i++)prompt[used++]=path[i];
    const char* suffix="# ";
    while(*suffix&&used+1<sizeof(prompt))prompt[used++]=*suffix++;
    prompt[used]=0;
    return ssh_send_text(s,prompt);
}

static void ssh_shell_command(ssh_session_t* s,const char* line){
    if(strcmp(line,"htop")==0&&s->channel_shell){s->channel_htop=1;s->channel_htop_last_draw=uptime_ticks;uint32_t n=server_htop_render_remote(ssh_htop_output,sizeof(ssh_htop_output));if(n)ssh_send_bytes(s,(const uint8_t*)ssh_htop_output,n);return;}
    if(strcmp(line,"exit")==0){ssh_send_text(s,"logout\r\n");s->channel_eof=1;ssh_send_exit_status(s,0);uint8_t p[16];ssh_writer_t w;ssh_writer_init(&w,p,sizeof(p));ssh_put_u8(&w,96);ssh_put_u32(&w,s->channel_client_id);ssh_send_packet(s,p,w.len);ssh_send_channel_status(s,97);s->state=8;return;}
    char output[16384];
    uint32_t length=server_execute_remote(line,output,sizeof(output),&s->working_directory);
    if(length)ssh_send_bytes(s,(const uint8_t*)output,length);
}

static int ssh_shell_exec(ssh_session_t* s,const uint8_t* cmd,uint32_t len){char line[SSH_TEXT_SIZE];if(len>=sizeof(line))return -1;memcpy(line,cmd,len);line[len]=0;ssh_shell_command(s,line);if(s->state==8)return 0;ssh_send_exit_status(s,0);uint8_t eof[5];ssh_writer_t w;ssh_writer_init(&w,eof,sizeof(eof));ssh_put_u8(&w,96);ssh_put_u32(&w,s->channel_client_id);ssh_send_packet(s,eof,w.len);ssh_send_channel_status(s,97);s->state=8;return 0;}

static void ssh_process_shell_data(ssh_session_t* s,const uint8_t* data,uint32_t len){
    for(uint32_t i=0;i<len;i++){
        char c=(char)data[i];
        if(s->suppress_lf&&c=='\n'){s->suppress_lf=0;continue;}s->suppress_lf=0;
        if(s->channel_htop){if(c=='q'||c=='Q'||c=='z'||c=='Z'){s->channel_htop=0;ssh_send_text(s,"\033[2J\033[Hhtop closed.\r\n");ssh_send_prompt(s);}continue;}
        if(c=='\r'||c=='\n'){
            s->suppress_lf=(c=='\r');if(s->channel_pty)ssh_send_text(s,"\r\n");s->line[s->line_len]=0;ssh_shell_command(s,s->line);s->line_len=0;s->line[0]=0;if(!s->channel_eof&&!s->channel_htop)ssh_send_prompt(s);continue;
        }
        if(c==8||c==127){if(s->line_len){s->line[--s->line_len]=0;if(s->channel_pty)ssh_send_text(s,"\b \b");}continue;}
        if((uint8_t)c<32||s->line_len+1>=sizeof(s->line))continue;
        s->line[s->line_len++]=c;s->line[s->line_len]=0;if(s->channel_pty)ssh_send_channel_data(s,(const uint8_t*)&c,1);
    }
}

static int ssh_handle_packet(ssh_session_t* s){
    ssh_reader_t r;ssh_reader_init(&r,s->payload,s->payload_len);uint8_t type=ssh_get_u8(&r);
    if(s->discard_next){s->discard_next=0;return 0;}
    if(type==1){s->state=8;return 0;}
    if(type==2||type==4||type==7||type==82)return 0;
    if(s->state==1){
        if(type!=20||s->payload_len>sizeof(s->client_kexinit))return -1;
        memcpy(s->client_kexinit,s->payload,s->payload_len);s->client_kexinit_len=s->payload_len;if(ssh_validate_kexinit(s)!=0||ssh_make_kexinit(s)!=0)return -1;if(ssh_send_packet(s,s->server_kexinit,s->server_kexinit_len)!=0)return -1;s->state=2;return 0;
    }
    if(s->state==2){if(type!=30)return -1;if(ssh_kex_reply(s)!=0)return -1;return 0;}
    if(s->state==4){if(type!=21||s->payload_len!=1)return -1;s->in_encrypted=1;s->state=5;return 0;}
    if(s->state==5){
        if(type==5){uint32_t n;const uint8_t* service=ssh_get_string(&r,&n);if(r.bad||r.pos!=r.len||!ssh_string_is(service,n,"ssh-userauth"))return -1;if(ssh_send_service_accept(s,service,n)!=0)return -1;s->state=6;return 0;}
        return 0;
    }
    if(s->state==6){
        if(type!=50)return 0;
        uint32_t un,sv,mn,pn;const uint8_t* user=ssh_get_string(&r,&un);const uint8_t* service=ssh_get_string(&r,&sv);const uint8_t* method=ssh_get_string(&r,&mn);int ok=0;
        if(!r.bad&&ssh_string_is(service,sv,"ssh-connection")&&ssh_string_is(method,mn,"password")){uint8_t change=ssh_get_u8(&r);const uint8_t* pass=ssh_get_string(&r,&pn);if(!r.bad&&r.pos==r.len&&!change&&ssh_equal_secret(ssh_username,user,un)&&ssh_equal_secret(ssh_password,pass,pn))ok=1;}
        if(ok){uint8_t p=52;if(ssh_send_packet(s,&p,1)!=0)return -1;s->state=7;s->auth_failures=0;return 0;}
        s->auth_failures++;ssh_send_auth_failure(s);if(s->auth_failures>=6)s->state=8;return 0;
    }
    if(s->state==7){
        if(type==90){uint32_t n;const uint8_t* chan=ssh_get_string(&r,&n);uint32_t sender=ssh_get_u32(&r);uint32_t win=ssh_get_u32(&r);uint32_t max=ssh_get_u32(&r);if(r.bad||r.pos!=r.len||!ssh_string_is(chan,n,"session")||s->channel_open)return -1;s->channel_client_id=sender;s->channel_window=win;s->channel_max_packet=max?max:32768;s->channel_open=1;uint8_t p[17];ssh_writer_t w;ssh_writer_init(&w,p,sizeof(p));ssh_put_u8(&w,91);ssh_put_u32(&w,sender);ssh_put_u32(&w,0);ssh_put_u32(&w,65536);ssh_put_u32(&w,32768);if(ssh_send_packet(s,p,w.len)!=0)return -1;s->state=10;return 0;}
        if(type==80){uint32_t n;const uint8_t* name=ssh_get_string(&r,&n);uint8_t want=ssh_get_u8(&r);if(want){uint8_t p=ssh_string_is(name,n,"keepalive@openssh.com")?81:82;ssh_send_packet(s,&p,1);}return 0;}
        return 0;
    }
    if(s->state==8){if(type==97){s->state=9;}return 0;}
    if(s->state==9){s->state=8;return 0;}
    if(s->state==10){
        if(type==98){uint32_t recipient,n;const uint8_t* req;recipient=ssh_get_u32(&r);req=ssh_get_string(&r,&n);uint8_t want=ssh_get_u8(&r);if(r.bad||recipient!=0)return -1;
            if(ssh_string_is(req,n,"pty-req")){uint32_t termn;ssh_get_string(&r,&termn);ssh_get_u32(&r);ssh_get_u32(&r);ssh_get_u32(&r);ssh_get_u32(&r);uint32_t modes;ssh_get_string(&r,&modes);s->channel_pty=1;if(want)ssh_send_channel_status(s,99);return 0;}
            if(ssh_string_is(req,n,"subsystem")){uint32_t subsystem_len;const uint8_t* subsystem=ssh_get_string(&r,&subsystem_len);if(r.bad||r.pos!=r.len||!ssh_string_is(subsystem,subsystem_len,"sftp")||!server_webroot_node()){if(want)ssh_send_channel_status(s,100);return 0;}s->channel_sftp=1;sftp_session_init(&s->sftp,server_webroot_node());if(want)ssh_send_channel_status(s,99);return 0;}
            if(ssh_string_is(req,n,"shell")){s->channel_shell=1;if(want)ssh_send_channel_status(s,99);ssh_send_text(s,"SharkOS remote shell\r\n");ssh_send_prompt(s);return 0;}
            if(ssh_string_is(req,n,"exec")){uint32_t cmdn;const uint8_t* cmd=ssh_get_string(&r,&cmdn);if(want)ssh_send_channel_status(s,99);return ssh_shell_exec(s,cmd,cmdn);}
            if(ssh_string_is(req,n,"window-change"))return 0;
            if(want)ssh_send_channel_status(s,100);
            return 0;
        }
        if(type==94){uint32_t recipient,n;recipient=ssh_get_u32(&r);const uint8_t* data=ssh_get_string(&r,&n);if(r.bad||r.pos!=r.len||recipient!=0)return -1;if(s->channel_sftp){if(sftp_session_feed(&s->sftp,data,n,ssh_sftp_send,s)!=0)return -1;}else if(s->channel_shell)ssh_process_shell_data(s,data,n);uint8_t p[9];ssh_writer_t w;ssh_writer_init(&w,p,sizeof(p));ssh_put_u8(&w,93);ssh_put_u32(&w,s->channel_client_id);ssh_put_u32(&w,n);ssh_send_packet(s,p,w.len);return 0;}
        if(type==93){ssh_get_u32(&r);uint32_t amount=ssh_get_u32(&r);s->channel_window+=amount;return 0;}
        if(type==96){ssh_send_channel_status(s,97);s->state=8;return 0;}
        if(type==97){s->state=8;return 0;}
        if(type==80){uint32_t n;ssh_get_string(&r,&n);uint8_t want=ssh_get_u8(&r);if(want){uint8_t p=82;ssh_send_packet(s,&p,1);}return 0;}
        return 0;
    }
    return 0;
}

static void ssh_close_session(ssh_session_t* s){if(s->active)net_tcp_server_close(s->handle);memset(s,0,sizeof(*s));}
static int ssh_start_session(ssh_session_t* s,int handle){memset(s,0,sizeof(*s));s->handle=handle;s->active=1;s->state=0;const char* banner=SSH_VERSION "\r\n";return ssh_send_raw(s,(const uint8_t*)banner,(uint32_t)strlen(banner));}

int ssh_server_init(void){memset(ssh_host_seed,0,sizeof(ssh_host_seed));memset(ssh_host_public,0,sizeof(ssh_host_public));ssh_host_ready=0;if(crypto_random_bytes(ssh_host_seed,sizeof(ssh_host_seed))!=0)return -1;if(ed25519_public_from_seed(ssh_host_seed,ssh_host_public)!=0){memset(ssh_host_seed,0,sizeof(ssh_host_seed));return -1;}ssh_host_ready=1;return 0;}
int ssh_server_set_password(const char* username,const char* password){if(ssh_active||!username||!password)return -1;uint32_t un=(uint32_t)strlen(username),pn=(uint32_t)strlen(password);if(!un||un>=sizeof(ssh_username)||pn<7||pn>=sizeof(ssh_password))return -1;memset(ssh_username,0,sizeof(ssh_username));memset(ssh_password,0,sizeof(ssh_password));memcpy(ssh_username,username,un);memcpy(ssh_password,password,pn);ssh_password_ready=1;return 0;}
int ssh_server_start(uint16_t port){if(ssh_active||!ssh_host_ready||!ssh_password_ready||!port||!net_has_nic())return -1;if(net_tcp_listen(port)!=0)return -1;ssh_port=port;ssh_active=1;return 0;}
void ssh_server_stop(void){if(!ssh_active)return;net_tcp_server_stop(ssh_port);for(int i=0;i<SSH_CLIENTS;i++)if(ssh_sessions[i].active)ssh_close_session(&ssh_sessions[i]);ssh_active=0;ssh_port=0;}
int ssh_server_is_active(void){return ssh_active;}
int ssh_server_has_password(void){return ssh_password_ready;}
int ssh_server_has_host_key(void){return ssh_host_ready;}
uint16_t ssh_server_listen_port(void){return ssh_port;}

void ssh_server_poll(void){
    if(!ssh_active)return;
    for(;;){int handle=net_tcp_server_accept(ssh_port);if(handle<0)break;int stored=0;for(int i=0;i<SSH_CLIENTS;i++)if(!ssh_sessions[i].active){if(ssh_start_session(&ssh_sessions[i],handle)==0)stored=1;break;}if(!stored)net_tcp_server_close(handle);}
    for(int i=0;i<SSH_CLIENTS;i++){
        ssh_session_t* s=&ssh_sessions[i];if(!s->active)continue;
        int got=ssh_stream_fill(s);if(got<0){ssh_close_session(s);continue;}
        int failed=0;
        if(s->state==0){while(s->stream_rd<s->stream_len){uint8_t c=s->stream[s->stream_rd++];if(c=='\n'){if(s->version_len&&s->version[s->version_len-1]=='\r')s->version_len--;s->version[s->version_len]=0;if(s->version_len<8||!ssh_bytes_equal(s->version,(const uint8_t*)"SSH-2.0-",8)){failed=1;break;}memcpy(s->client_version,s->version,s->version_len);s->client_version_len=s->version_len;s->state=1;break;}if(s->version_len+1>=sizeof(s->version)){failed=1;break;}s->version[s->version_len++]=c;}if(s->stream_rd){uint32_t remain=s->stream_len-s->stream_rd;memcpy(s->stream,s->stream+s->stream_rd,remain);s->stream_len=remain;s->stream_rd=0;}}
        if(failed){ssh_close_session(s);continue;}
        if(s->state==0)continue;
        for(int step=0;step<8&&s->active;step++){
            int n=ssh_packet_next(s);if(n<0){failed=1;break;}if(!n)break;
            if(s->state==1&&s->payload_len<=sizeof(s->client_kexinit)){memcpy(s->client_kexinit,s->payload,s->payload_len);s->client_kexinit_len=s->payload_len;}
            if(ssh_handle_packet(s)!=0){failed=1;break;}
            if(s->state==8||s->state==9){ssh_close_session(s);break;}
        }
        if(failed&&s->active)ssh_close_session(s);
        if(s->active&&s->channel_htop&&uptime_ticks-s->channel_htop_last_draw>=1000){
            s->channel_htop_last_draw=uptime_ticks;
            uint32_t length=server_htop_render_remote(ssh_htop_output,sizeof(ssh_htop_output));
            if(length&&ssh_send_bytes(s,(const uint8_t*)ssh_htop_output,length)!=0)ssh_close_session(s);
        }
    }
}
