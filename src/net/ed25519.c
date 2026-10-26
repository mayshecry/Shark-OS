#include "tls.h"
#include "kernel.h"

#define FE_MASK ((((uint64_t)1) << 51) - 1)

typedef struct { uint64_t v[5]; } ed_fe;
typedef struct { ed_fe x, y, z, t; } ed_point;
typedef struct { uint64_t h[8]; uint8_t buf[128]; uint32_t used; uint64_t bytes_hi, bytes_lo; } ed_sha512;

static const uint64_t ed_2d[5] = { 0x69b9426b2f159ULL, 0x35050762add7aULL, 0x3cf44c0038052ULL, 0x6738cc7407977ULL, 0x2406d9dc56dffULL };
static const uint64_t ed_bx[5] = { 0x62d608f25d51aULL, 0x412a4b4f6592aULL, 0x75b7171a4b31dULL, 0x1ff60527118feULL, 0x216936d3cd6e5ULL };
static const uint64_t ed_by[5] = { 0x6666666666658ULL, 0x4ccccccccccccULL, 0x1999999999999ULL, 0x3333333333333ULL, 0x6666666666666ULL };

static uint64_t ed_rotr64(uint64_t x, unsigned n) { return (x >> n) | (x << (64 - n)); }

static const uint64_t ed_k512[80] = {
    0x428a2f98d728ae22ULL,0x7137449123ef65cdULL,0xb5c0fbcfec4d3b2fULL,0xe9b5dba58189dbbcULL,
    0x3956c25bf348b538ULL,0x59f111f1b605d019ULL,0x923f82a4af194f9bULL,0xab1c5ed5da6d8118ULL,
    0xd807aa98a3030242ULL,0x12835b0145706fbeULL,0x243185be4ee4b28cULL,0x550c7dc3d5ffb4e2ULL,
    0x72be5d74f27b896fULL,0x80deb1fe3b1696b1ULL,0x9bdc06a725c71235ULL,0xc19bf174cf692694ULL,
    0xe49b69c19ef14ad2ULL,0xefbe4786384f25e3ULL,0x0fc19dc68b8cd5b5ULL,0x240ca1cc77ac9c65ULL,
    0x2de92c6f592b0275ULL,0x4a7484aa6ea6e483ULL,0x5cb0a9dcbd41fbd4ULL,0x76f988da831153b5ULL,
    0x983e5152ee66dfabULL,0xa831c66d2db43210ULL,0xb00327c898fb213fULL,0xbf597fc7beef0ee4ULL,
    0xc6e00bf33da88fc2ULL,0xd5a79147930aa725ULL,0x06ca6351e003826fULL,0x142929670a0e6e70ULL,
    0x27b70a8546d22ffcULL,0x2e1b21385c26c926ULL,0x4d2c6dfc5ac42aedULL,0x53380d139d95b3dfULL,
    0x650a73548baf63deULL,0x766a0abb3c77b2a8ULL,0x81c2c92e47edaee6ULL,0x92722c851482353bULL,
    0xa2bfe8a14cf10364ULL,0xa81a664bbc423001ULL,0xc24b8b70d0f89791ULL,0xc76c51a30654be30ULL,
    0xd192e819d6ef5218ULL,0xd69906245565a910ULL,0xf40e35855771202aULL,0x106aa07032bbd1b8ULL,
    0x19a4c116b8d2d0c8ULL,0x1e376c085141ab53ULL,0x2748774cdf8eeb99ULL,0x34b0bcb5e19b48a8ULL,
    0x391c0cb3c5c95a63ULL,0x4ed8aa4ae3418acbULL,0x5b9cca4f7763e373ULL,0x682e6ff3d6b2b8a3ULL,
    0x748f82ee5defb2fcULL,0x78a5636f43172f60ULL,0x84c87814a1f0ab72ULL,0x8cc702081a6439ecULL,
    0x90befffa23631e28ULL,0xa4506cebde82bde9ULL,0xbef9a3f7b2c67915ULL,0xc67178f2e372532bULL,
    0xca273eceea26619cULL,0xd186b8c721c0c207ULL,0xeada7dd6cde0eb1eULL,0xf57d4f7fee6ed178ULL,
    0x06f067aa72176fbaULL,0x0a637dc5a2c898a6ULL,0x113f9804bef90daeULL,0x1b710b35131c471bULL,
    0x28db77f523047d84ULL,0x32caab7b40c72493ULL,0x3c9ebe0a15c9bebcULL,0x431d67c49c100d4cULL,
    0x4cc5d4becb3e42b6ULL,0x597f299cfc657e2aULL,0x5fcb6fab3ad6faecULL,0x6c44198c4a475817ULL
};

static uint64_t ed_be64(const uint8_t* p) {
    uint64_t v = 0;
    for (int i = 0; i < 8; i++) v = (v << 8) | p[i];
    return v;
}

static void ed_sha512_block(ed_sha512* c, const uint8_t* p) {
    uint64_t w[80];
    for (int i = 0; i < 16; i++) w[i] = ed_be64(p + 8 * i);
    for (int i = 16; i < 80; i++) {
        uint64_t s0 = ed_rotr64(w[i - 15], 1) ^ ed_rotr64(w[i - 15], 8) ^ (w[i - 15] >> 7);
        uint64_t s1 = ed_rotr64(w[i - 2], 19) ^ ed_rotr64(w[i - 2], 61) ^ (w[i - 2] >> 6);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint64_t a=c->h[0],b=c->h[1],d=c->h[3],e=c->h[4],f=c->h[5],g=c->h[6],h=c->h[7],cc=c->h[2];
    for (int i = 0; i < 80; i++) {
        uint64_t s1=ed_rotr64(e,14)^ed_rotr64(e,18)^ed_rotr64(e,41);
        uint64_t ch=(e&f)^(~e&g);
        uint64_t t1=h+s1+ch+ed_k512[i]+w[i];
        uint64_t s0=ed_rotr64(a,28)^ed_rotr64(a,34)^ed_rotr64(a,39);
        uint64_t maj=(a&b)^(a&cc)^(b&cc);
        uint64_t t2=s0+maj;
        h=g;g=f;f=e;e=d+t1;d=cc;cc=b;b=a;a=t1+t2;
    }
    c->h[0]+=a;c->h[1]+=b;c->h[2]+=cc;c->h[3]+=d;c->h[4]+=e;c->h[5]+=f;c->h[6]+=g;c->h[7]+=h;
}

static void ed_sha512_init(ed_sha512* c) {
    static const uint64_t initial[8] = { 0x6a09e667f3bcc908ULL,0xbb67ae8584caa73bULL,0x3c6ef372fe94f82bULL,0xa54ff53a5f1d36f1ULL,0x510e527fade682d1ULL,0x9b05688c2b3e6c1fULL,0x1f83d9abfb41bd6bULL,0x5be0cd19137e2179ULL };
    memcpy(c->h, initial, sizeof(initial));
    c->used=0;c->bytes_hi=0;c->bytes_lo=0;
}

static void ed_sha512_update(ed_sha512* c, const uint8_t* data, size_t len) {
    uint64_t old=c->bytes_lo;
    c->bytes_lo+=(uint64_t)len;
    if(c->bytes_lo<old)c->bytes_hi++;
    while(len){
        uint32_t n=128-c->used;if(n>len)n=(uint32_t)len;
        memcpy(c->buf+c->used,data,n);c->used+=n;data+=n;len-=n;
        if(c->used==128){ed_sha512_block(c,c->buf);c->used=0;}
    }
}

static void ed_sha512_final(ed_sha512* c, uint8_t out[64]) {
    uint64_t bit_hi=(c->bytes_hi<<3)|(c->bytes_lo>>61),bit_lo=c->bytes_lo<<3;
    c->buf[c->used++]=0x80;
    if(c->used>112){while(c->used<128)c->buf[c->used++]=0;ed_sha512_block(c,c->buf);c->used=0;}
    while(c->used<112)c->buf[c->used++]=0;
    for(int i=0;i<8;i++)c->buf[112+i]=(uint8_t)(bit_hi>>(56-8*i));
    for(int i=0;i<8;i++)c->buf[120+i]=(uint8_t)(bit_lo>>(56-8*i));
    ed_sha512_block(c,c->buf);
    for(int i=0;i<8;i++)for(int j=0;j<8;j++)out[i*8+j]=(uint8_t)(c->h[i]>>(56-8*j));
}

static void ed_sha512_hash(const uint8_t* data,size_t len,uint8_t out[64]) { ed_sha512 c;ed_sha512_init(&c);ed_sha512_update(&c,data,len);ed_sha512_final(&c,out); }

static void fe_copy(ed_fe* o,const ed_fe* a){memcpy(o,a,sizeof(*o));}
static void fe_zero(ed_fe* o){memset(o,0,sizeof(*o));}
static void fe_one(ed_fe* o){fe_zero(o);o->v[0]=1;}

static void fe_carry(ed_fe* a) {
    uint64_t c;
    for(int r=0;r<2;r++){
        c=a->v[0]>>51;a->v[0]&=FE_MASK;a->v[1]+=c;
        c=a->v[1]>>51;a->v[1]&=FE_MASK;a->v[2]+=c;
        c=a->v[2]>>51;a->v[2]&=FE_MASK;a->v[3]+=c;
        c=a->v[3]>>51;a->v[3]&=FE_MASK;a->v[4]+=c;
        c=a->v[4]>>51;a->v[4]&=FE_MASK;a->v[0]+=c*19;
    }
    c=a->v[0]>>51;a->v[0]&=FE_MASK;a->v[1]+=c;
}

static void fe_add(ed_fe* o,const ed_fe* a,const ed_fe* b){for(int i=0;i<5;i++)o->v[i]=a->v[i]+b->v[i];fe_carry(o);}
static void fe_sub(ed_fe* o,const ed_fe* a,const ed_fe* b){
    o->v[0]=a->v[0]+2*(FE_MASK-18)-b->v[0];
    for(int i=1;i<5;i++)o->v[i]=a->v[i]+2*FE_MASK-b->v[i];
    fe_carry(o);
}

static void fe_mul(ed_fe* o,const ed_fe* a,const ed_fe* b){
    __uint128_t t[5];
    t[0]=(__uint128_t)a->v[0]*b->v[0]+19*((__uint128_t)a->v[1]*b->v[4]+(__uint128_t)a->v[2]*b->v[3]+(__uint128_t)a->v[3]*b->v[2]+(__uint128_t)a->v[4]*b->v[1]);
    t[1]=(__uint128_t)a->v[0]*b->v[1]+(__uint128_t)a->v[1]*b->v[0]+19*((__uint128_t)a->v[2]*b->v[4]+(__uint128_t)a->v[3]*b->v[3]+(__uint128_t)a->v[4]*b->v[2]);
    t[2]=(__uint128_t)a->v[0]*b->v[2]+(__uint128_t)a->v[1]*b->v[1]+(__uint128_t)a->v[2]*b->v[0]+19*((__uint128_t)a->v[3]*b->v[4]+(__uint128_t)a->v[4]*b->v[3]);
    t[3]=(__uint128_t)a->v[0]*b->v[3]+(__uint128_t)a->v[1]*b->v[2]+(__uint128_t)a->v[2]*b->v[1]+(__uint128_t)a->v[3]*b->v[0]+19*(__uint128_t)a->v[4]*b->v[4];
    t[4]=(__uint128_t)a->v[0]*b->v[4]+(__uint128_t)a->v[1]*b->v[3]+(__uint128_t)a->v[2]*b->v[2]+(__uint128_t)a->v[3]*b->v[1]+(__uint128_t)a->v[4]*b->v[0];
    for(int i=0;i<4;i++){t[i+1]+=t[i]>>51;o->v[i]=(uint64_t)t[i]&FE_MASK;}
    t[0]=(t[4]>>51)*19+o->v[0];o->v[4]=(uint64_t)t[4]&FE_MASK;
    uint64_t c=(uint64_t)(t[0]>>51);o->v[0]=(uint64_t)t[0]&FE_MASK;o->v[1]+=c;
    fe_carry(o);
}

static void fe_sq(ed_fe* o,const ed_fe* a){fe_mul(o,a,a);}

static void fe_inv(ed_fe* o,const ed_fe* a){
    static const uint8_t exp[32]={0xeb,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0x7f};
    ed_fe r;fe_one(&r);
    for(int i=254;i>=0;i--){fe_sq(&r,&r);if((exp[i>>3]>>(i&7))&1)fe_mul(&r,&r,a);}
    fe_copy(o,&r);
}

static void fe_tobytes(uint8_t out[32],const ed_fe* in){
    ed_fe h;fe_copy(&h,in);fe_carry(&h);
    int ge=1;
    for(int i=4;i>=0;i--){uint64_t p=i==0?FE_MASK-18:FE_MASK;if(h.v[i]>p){ge=1;break;}if(h.v[i]<p){ge=0;break;}}
    if(ge){uint64_t borrow=0;for(int i=0;i<5;i++){uint64_t p=i==0?FE_MASK-18:FE_MASK;uint64_t sub=p+borrow;if(h.v[i]<sub){h.v[i]=h.v[i]+((uint64_t)1<<51)-sub;borrow=1;}else{h.v[i]-=sub;borrow=0;}}}
    memset(out,0,32);
    for(int i=0;i<255;i++)if((h.v[i/51]>>(i%51))&1)out[i>>3]|=(uint8_t)(1u<<(i&7));
}

static int fe_isnegative(const ed_fe* a){uint8_t b[32];fe_tobytes(b,a);return b[0]&1;}

static void point_identity(ed_point* p){fe_zero(&p->x);fe_one(&p->y);fe_one(&p->z);fe_zero(&p->t);}
static void point_base(ed_point* p){memcpy(p->x.v,ed_bx,sizeof(ed_bx));memcpy(p->y.v,ed_by,sizeof(ed_by));fe_one(&p->z);fe_mul(&p->t,&p->x,&p->y);}

static void point_add(ed_point* o,const ed_point* p,const ed_point* q){
    ed_fe a,b,c,d,e,f,g,h,t1,t2;
    fe_sub(&t1,&p->y,&p->x);fe_sub(&t2,&q->y,&q->x);fe_mul(&a,&t1,&t2);
    fe_add(&t1,&p->y,&p->x);fe_add(&t2,&q->y,&q->x);fe_mul(&b,&t1,&t2);
    fe_mul(&c,&p->t,&q->t);ed_fe dd;memcpy(dd.v,ed_2d,sizeof(ed_2d));fe_mul(&c,&c,&dd);
    fe_mul(&d,&p->z,&q->z);fe_add(&d,&d,&d);
    fe_sub(&e,&b,&a);fe_sub(&f,&d,&c);fe_add(&g,&d,&c);fe_add(&h,&b,&a);
    fe_mul(&o->x,&e,&f);fe_mul(&o->y,&g,&h);fe_mul(&o->t,&e,&h);fe_mul(&o->z,&f,&g);
}

static void point_select(ed_point* o,const ed_point* a,const ed_point* b,uint32_t bit){
    uint64_t mask=(uint64_t)0-(uint64_t)bit;
    for(int i=0;i<5;i++){o->x.v[i]=(a->x.v[i]&~mask)|(b->x.v[i]&mask);o->y.v[i]=(a->y.v[i]&~mask)|(b->y.v[i]&mask);o->z.v[i]=(a->z.v[i]&~mask)|(b->z.v[i]&mask);o->t.v[i]=(a->t.v[i]&~mask)|(b->t.v[i]&mask);}
}

static void point_scalarmult_base(ed_point* o,const uint8_t scalar[32]){
    ed_point r,b,twice,sum;point_identity(&r);point_base(&b);
    for(int i=255;i>=0;i--){point_add(&twice,&r,&r);point_add(&sum,&twice,&b);point_select(&r,&twice,&sum,(scalar[i>>3]>>(i&7))&1);}
    memcpy(o,&r,sizeof(r));
}

static void point_encode(uint8_t out[32],const ed_point* p){
    ed_fe zi,x,y;fe_inv(&zi,&p->z);fe_mul(&x,&p->x,&zi);fe_mul(&y,&p->y,&zi);fe_tobytes(out,&y);out[31]|=(uint8_t)(fe_isnegative(&x)<<7);
}

static const uint32_t ed_l[8]={0x5cf5d3ed,0x5812631a,0xa2f79cd6,0x14def9de,0,0,0,0x10000000};

static void scalar_reduce(const uint8_t in[64],uint8_t out[32]){
    uint32_t r[8]={0};
    for(int bit=511;bit>=0;bit--){
        uint32_t carry=(in[bit>>3]>>(bit&7))&1;
        for(int i=0;i<8;i++){uint64_t v=((uint64_t)r[i]<<1)|carry;r[i]=(uint32_t)v;carry=(uint32_t)(v>>32);}
        uint32_t diff[8];uint64_t borrow=0;
        for(int i=0;i<8;i++){uint64_t sub=(uint64_t)ed_l[i]+borrow;diff[i]=(uint32_t)((uint64_t)r[i]-sub);borrow=(uint64_t)r[i]<sub;}
        uint32_t mask=0u-(uint32_t)(borrow^1u);
        for(int i=0;i<8;i++)r[i]=(diff[i]&mask)|(r[i]&~mask);
    }
    for(int i=0;i<8;i++)for(int j=0;j<4;j++)out[i*4+j]=(uint8_t)(r[i]>>(8*j));
}

static void scalar_muladd(const uint8_t a[32],const uint8_t b[32],const uint8_t c[32],uint8_t out[32]){
    uint32_t aa[8],bb[8],cc[8],prod[16]={0};
    for(int i=0;i<8;i++){aa[i]=(uint32_t)a[4*i]|((uint32_t)a[4*i+1]<<8)|((uint32_t)a[4*i+2]<<16)|((uint32_t)a[4*i+3]<<24);bb[i]=(uint32_t)b[4*i]|((uint32_t)b[4*i+1]<<8)|((uint32_t)b[4*i+2]<<16)|((uint32_t)b[4*i+3]<<24);cc[i]=(uint32_t)c[4*i]|((uint32_t)c[4*i+1]<<8)|((uint32_t)c[4*i+2]<<16)|((uint32_t)c[4*i+3]<<24);}
    for(int i=0;i<8;i++){uint64_t carry=0;for(int j=0;j<8;j++){uint64_t v=(uint64_t)aa[i]*bb[j]+prod[i+j]+carry;prod[i+j]=(uint32_t)v;carry=v>>32;}for(int k=i+8;k<16;k++){uint64_t v=(uint64_t)prod[k]+carry;prod[k]=(uint32_t)v;carry=v>>32;}}
    uint64_t carry=0;for(int i=0;i<8;i++){uint64_t v=(uint64_t)prod[i]+cc[i]+carry;prod[i]=(uint32_t)v;carry=v>>32;}for(int i=8;i<16;i++){uint64_t v=(uint64_t)prod[i]+carry;prod[i]=(uint32_t)v;carry=v>>32;}
    uint8_t wide[64];for(int i=0;i<16;i++)for(int j=0;j<4;j++)wide[4*i+j]=(uint8_t)(prod[i]>>(8*j));scalar_reduce(wide,out);
}

int ed25519_public_from_seed(const uint8_t seed[32],uint8_t public_key[32]){
    uint8_t h[64],a[32];ed_point p;ed_sha512_hash(seed,32,h);memcpy(a,h,32);a[0]&=248;a[31]&=63;a[31]|=64;point_scalarmult_base(&p,a);point_encode(public_key,&p);memset(h,0,sizeof(h));memset(a,0,sizeof(a));return 0;
}

int ed25519_sign(const uint8_t seed[32],const uint8_t* message,size_t message_len,uint8_t signature[64]){
    uint8_t h[64],a[32],rwide[64],r[32],kwide[64],k[32],pub[32],buf[96];ed_point rp;
    ed_sha512_hash(seed,32,h);memcpy(a,h,32);a[0]&=248;a[31]&=63;a[31]|=64;
    ed_sha512 c;ed_sha512_init(&c);ed_sha512_update(&c,h+32,32);ed_sha512_update(&c,message,message_len);ed_sha512_final(&c,rwide);scalar_reduce(rwide,r);
    point_scalarmult_base(&rp,r);point_encode(signature,&rp);ed25519_public_from_seed(seed,pub);
    memcpy(buf,signature,32);memcpy(buf+32,pub,32);ed_sha512_init(&c);ed_sha512_update(&c,buf,64);ed_sha512_update(&c,message,message_len);ed_sha512_final(&c,kwide);scalar_reduce(kwide,k);
    scalar_muladd(k,a,r,signature+32);
    memset(h,0,sizeof(h));memset(a,0,sizeof(a));memset(rwide,0,sizeof(rwide));memset(r,0,sizeof(r));memset(kwide,0,sizeof(kwide));memset(k,0,sizeof(k));memset(buf,0,sizeof(buf));
    return 0;
}
