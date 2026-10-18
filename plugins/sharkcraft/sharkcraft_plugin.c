#include "sharkapi.h"
#include "desktop.h"
#include <stdint.h>

#define RW 320
#define RH 200
#define WX 16
#define WY 8
#define WZ 16
#define FP 1024
#define Q 256
#define NEAR_CLIP (Q/16)
#define PLAYER_RADIUS (Q*28/100)
#define PLAYER_HEIGHT (Q*18/10)
#define EYE_HEIGHT (Q*16/10)
#define WALK_SPEED (Q*42/10)
#define JUMP_SPEED (Q*8)
#define GRAVITY (Q*20)
#define MAX_FALL_SPEED (Q*35)
#define MAX_MOVE_STEP (Q/8)
#define INPUT_GRACE_MS 180
#define INPUT_FORWARD 0
#define INPUT_BACK 1
#define INPUT_LEFT 2
#define INPUT_RIGHT 3
#define INPUT_UP 4
#define INPUT_DOWN 5
#define AIR 0
#define GRASS 1
#define DIRT 2
#define STONE 3
#define WOOD 4
#define SAND 5
#define LEAVES 6

plugin_info_t sharkcraft_plugin_info = {
    .version=SHARKAPI_VERSION, .name="sharkcraft", .author="SharkOS community",
    .description="Small 3D creative voxel sandbox: fly, break/place blocks, and build.",
    .major=0, .minor=1
};

static uint8_t world[WY][WZ][WX];
static uint32_t pixels[RW*RH];
static uint32_t depth[RW*RH];
static int camera_x, camera_y, camera_z;
static int yaw, pitch, selected=GRASS;
static int focal_length=120;
static int drag_active, drag_x, drag_y;
static int yaw_drag_remainder, pitch_drag_remainder;
static int world_ready;

/* Player velocities are Q-scaled world units per second. */
static int velocity_x, velocity_y, velocity_z;
static int on_ground, flying;
static uint32_t move_deadline[6];
static uint32_t physics_last_tick;
static int physics_clock_ready;
static int position_remainder_x, position_remainder_y, position_remainder_z;

typedef struct { int x,y,z,u,v,iz,uoz,voz; } vertex_t;
typedef struct { int x,y,z,u,v; } camera_vertex_t;
static const int cs[256]={
    1024, 1024, 1023, 1021, 1019, 1016, 1013, 1009, 1004, 999, 993, 987, 980, 972, 964, 955,
    946, 936, 926, 915, 903, 891, 878, 865, 851, 837, 822, 807, 792, 775, 759, 742,
    724, 706, 688, 669, 650, 630, 610, 590, 569, 548, 526, 505, 483, 460, 438, 415,
    392, 369, 345, 321, 297, 273, 249, 224, 200, 175, 150, 125, 100, 75, 50, 25,
    0, -25, -50, -75, -100, -125, -150, -175, -200, -224, -249, -273, -297, -321, -345, -369,
    -392, -415, -438, -460, -483, -505, -526, -548, -569, -590, -610, -630, -650, -669, -688, -706,
    -724, -742, -759, -775, -792, -807, -822, -837, -851, -865, -878, -891, -903, -915, -926, -936,
    -946, -955, -964, -972, -980, -987, -993, -999, -1004, -1009, -1013, -1016, -1019, -1021, -1023, -1024,
    -1024, -1024, -1023, -1021, -1019, -1016, -1013, -1009, -1004, -999, -993, -987, -980, -972, -964, -955,
    -946, -936, -926, -915, -903, -891, -878, -865, -851, -837, -822, -807, -792, -775, -759, -742,
    -724, -706, -688, -669, -650, -630, -610, -590, -569, -548, -526, -505, -483, -460, -438, -415,
    -392, -369, -345, -321, -297, -273, -249, -224, -200, -175, -150, -125, -100, -75, -50, -25,
    0, 25, 50, 75, 100, 125, 150, 175, 200, 224, 249, 273, 297, 321, 345, 369,
    392, 415, 438, 460, 483, 505, 526, 548, 569, 590, 610, 630, 650, 669, 688, 706,
    724, 742, 759, 775, 792, 807, 822, 837, 851, 865, 878, 891, 903, 915, 926, 936,
    946, 955, 964, 972, 980, 987, 993, 999, 1004, 1009, 1013, 1016, 1019, 1021, 1023, 1024,
};
static const int sn[256]={
    0, 25, 50, 75, 100, 125, 150, 175, 200, 224, 249, 273, 297, 321, 345, 369,
    392, 415, 438, 460, 483, 505, 526, 548, 569, 590, 610, 630, 650, 669, 688, 706,
    724, 742, 759, 775, 792, 807, 822, 837, 851, 865, 878, 891, 903, 915, 926, 936,
    946, 955, 964, 972, 980, 987, 993, 999, 1004, 1009, 1013, 1016, 1019, 1021, 1023, 1024,
    1024, 1024, 1023, 1021, 1019, 1016, 1013, 1009, 1004, 999, 993, 987, 980, 972, 964, 955,
    946, 936, 926, 915, 903, 891, 878, 865, 851, 837, 822, 807, 792, 775, 759, 742,
    724, 706, 688, 669, 650, 630, 610, 590, 569, 548, 526, 505, 483, 460, 438, 415,
    392, 369, 345, 321, 297, 273, 249, 224, 200, 175, 150, 125, 100, 75, 50, 25,
    0, -25, -50, -75, -100, -125, -150, -175, -200, -224, -249, -273, -297, -321, -345, -369,
    -392, -415, -438, -460, -483, -505, -526, -548, -569, -590, -610, -630, -650, -669, -688, -706,
    -724, -742, -759, -775, -792, -807, -822, -837, -851, -865, -878, -891, -903, -915, -926, -936,
    -946, -955, -964, -972, -980, -987, -993, -999, -1004, -1009, -1013, -1016, -1019, -1021, -1023, -1024,
    -1024, -1024, -1023, -1021, -1019, -1016, -1013, -1009, -1004, -999, -993, -987, -980, -972, -964, -955,
    -946, -936, -926, -915, -903, -891, -878, -865, -851, -837, -822, -807, -792, -775, -759, -742,
    -724, -706, -688, -669, -650, -630, -610, -590, -569, -548, -526, -505, -483, -460, -438, -415,
    -392, -369, -345, -321, -297, -273, -249, -224, -200, -175, -150, -125, -100, -75, -50, -25,
};

static int solid(int x,int y,int z) {
    return x>=0&&x<WX&&y>=0&&y<WY&&z>=0&&z<WZ ? world[y][z][x]!=AIR : 0;
}
static int floor_block(int value) {
    if(value>=0) return value/Q;
    return -((-value+Q-1)/Q);
}
static int player_collides_at(int px,int py,int pz) {
    int min_x=floor_block(px-PLAYER_RADIUS);
    int max_x=floor_block(px+PLAYER_RADIUS-1);
    int min_y=floor_block(py-EYE_HEIGHT);
    int max_y=floor_block(py+(PLAYER_HEIGHT-EYE_HEIGHT)-1);
    int min_z=floor_block(pz-PLAYER_RADIUS);
    int max_z=floor_block(pz+PLAYER_RADIUS-1);
    for(int y=min_y;y<=max_y;y++)
        for(int z=min_z;z<=max_z;z++)
            for(int x=min_x;x<=max_x;x++)
                if(solid(x,y,z)) return 1;
    return 0;
}
static int world_surface_height(int x,int z) {
    for(int y=WY-1;y>=0;y--) if(world[y][z][x]!=AIR) return (y+1)*Q;
    return 0;
}
static uint32_t texel(int block,int face,int u,int v) {
    u&=7; v&=7;
    unsigned n=(unsigned)(u*17+v*31+block*53+face*19);
    n=(n^(n>>3))*1103515245u+12345u;
    int variation=(int)((n>>28)&7)-3;
    int r=0,g=0,b=0;
    if(block==GRASS) {
        if(face==2) { r=75+variation*3; g=158+variation*5; b=55+variation*2; }
        else if(v==0) { r=70+variation*3; g=145+variation*4; b=49+variation*2; }
        else { r=113+variation*4; g=78+variation*3; b=48+variation*2; }
    } else if(block==DIRT) { r=117+variation*4; g=78+variation*3; b=48+variation*2; }
    else if(block==STONE) { r=112+variation*6; g=119+variation*6; b=124+variation*6; }
    else if(block==WOOD) {
        if(face==2||face==3) { int ring=((u-3)*(u-3)+(v-3)*(v-3))&3; r=154-ring*9;g=104-ring*6;b=54-ring*3; }
        else { int stripe=(u+v/2)&3; r=135+stripe*7;g=86+stripe*5;b=43+stripe*3; }
    } else if(block==SAND) { r=211+variation*4;g=190+variation*4;b=119+variation*3; }
    else { r=49+variation*3;g=133+variation*5;b=58+variation*3; }
    if(face==0||face==1) { r=r*82/100; g=g*82/100; b=b*82/100; }
    if(face==3) { r=r*70/100; g=g*70/100; b=b*70/100; }
    if(u<=1||u>=6||v<=1||v>=6) { r=r*58/100; g=g*58/100; b=b*58/100; }
    return 0xFF000000u|((uint32_t)r<<16)|((uint32_t)g<<8)|(uint32_t)b;
}

static void world_generate(void) {
    for(int y=0;y<WY;y++) for(int z=0;z<WZ;z++) for(int x=0;x<WX;x++) world[y][z][x]=AIR;
    for(int z=0;z<WZ;z++) for(int x=0;x<WX;x++) {
        int h=2+((x*7+z*11+(x*z)%9)%3);
        for(int y=0;y<=h;y++) world[y][z][x]=(y==h)?GRASS:(y>=h-2?DIRT:STONE);
        if((x*3+z*5)%29==0 && h+3<WY) {
            world[h+1][z][x]=WOOD; world[h+2][z][x]=WOOD;
            for(int dz=-1;dz<=1;dz++) for(int dx=-1;dx<=1;dx++)
                if(x+dx>=0&&x+dx<WX&&z+dz>=0&&z+dz<WZ&&h+3<WY) world[h+3][z+dz][x+dx]=LEAVES;
        }
        if((x+z*2)%23==0) world[h][z][x]=SAND;
    }
    world_ready=1;
}

static void camera_reset(void) {
    camera_x=8*Q+Q/2;
    camera_z=8*Q+Q/2;
    camera_y=world_surface_height(8,8)+EYE_HEIGHT;
    yaw=0; pitch=-10; focal_length=105; selected=GRASS; drag_active=0;
    yaw_drag_remainder=0; pitch_drag_remainder=0;
    velocity_x=velocity_y=velocity_z=0;
    on_ground=1; flying=0;
    for(int i=0;i<6;i++) move_deadline[i]=0;
    position_remainder_x=position_remainder_y=position_remainder_z=0;
    physics_clock_ready=0;
}
void sharkcraft_game_reset(void) { world_generate(); camera_reset(); }

static camera_vertex_t to_camera(int wx,int wy,int wz,int u,int v) {
    int dx=wx*Q-camera_x, dy=wy*Q-camera_y, dz=wz*Q-camera_z;
    int r=yaw&255;
    int x=(dx*cs[r]+dz*sn[r])/FP;
    int z=(-dx*sn[r]+dz*cs[r])/FP;
    int p=pitch&255;
    int y=(dy*cs[p]-z*sn[p])/FP;
    z=(dy*sn[p]+z*cs[p])/FP;
    return (camera_vertex_t){x,y,z,u,v};
}
static int clip_near(camera_vertex_t* in,int count,camera_vertex_t* out) {
    int n=0;
    for(int i=0;i<count;i++) {
        camera_vertex_t a=in[i], b=in[(i+1)%count];
        int ain=a.z>=NEAR_CLIP, bin=b.z>=NEAR_CLIP;
        if(ain && bin) out[n++]=b;
        else if(ain && !bin) {
            int den=b.z-a.z, num=NEAR_CLIP-a.z;
            out[n++]=(camera_vertex_t){
                a.x+(int)((int64_t)(b.x-a.x)*num/den),
                a.y+(int)((int64_t)(b.y-a.y)*num/den),NEAR_CLIP,
                a.u+(int)((int64_t)(b.u-a.u)*num/den),
                a.v+(int)((int64_t)(b.v-a.v)*num/den)};
        } else if(!ain && bin) {
            int den=b.z-a.z, num=NEAR_CLIP-a.z;
            out[n++]=(camera_vertex_t){
                a.x+(int)((int64_t)(b.x-a.x)*num/den),
                a.y+(int)((int64_t)(b.y-a.y)*num/den),NEAR_CLIP,
                a.u+(int)((int64_t)(b.u-a.u)*num/den),
                a.v+(int)((int64_t)(b.v-a.v)*num/den)};
            out[n++]=b;
        }
    }
    return n;
}
static void project_camera(vertex_t* p,camera_vertex_t v) {
    /* x and z are both Q-scaled, so their ratio already cancels Q. */
    p->x=RW/2+(int)((int64_t)v.x*focal_length/v.z);
    p->y=RH/2-(int)((int64_t)v.y*focal_length/v.z);
    p->iz=(int)((int64_t)65535*Q/v.z);
    p->u=v.u; p->v=v.v;
    p->uoz=p->u*p->iz; p->voz=p->v*p->iz;
}
static int64_t edge(vertex_t a,vertex_t b,int x,int y) {
    return (int64_t)(x-a.x)*(b.y-a.y)-(int64_t)(y-a.y)*(b.x-a.x);
}
static void triangle(vertex_t a,vertex_t b,vertex_t c,int block,int face) {
    int64_t area=edge(a,b,c.x,c.y); if(!area) return;
    int minx=a.x,maxx=a.x,miny=a.y,maxy=a.y;
    if(b.x<minx)minx=b.x;
    if(c.x<minx)minx=c.x;
    if(b.x>maxx)maxx=b.x;
    if(c.x>maxx)maxx=c.x;
    if(b.y<miny)miny=b.y;
    if(c.y<miny)miny=c.y;
    if(b.y>maxy)maxy=b.y;
    if(c.y>maxy)maxy=c.y;
    if(minx<0)minx=0;
    if(miny<0)miny=0;
    if(maxx>=RW)maxx=RW-1;
    if(maxy>=RH)maxy=RH-1;
    for(int y=miny;y<=maxy;y++) for(int x=minx;x<=maxx;x++) {
        int64_t wa=edge(b,c,x,y),wb=edge(c,a,x,y),wc=edge(a,b,x,y);
        if((area>0&&wa>=0&&wb>=0&&wc>=0)||(area<0&&wa<=0&&wb<=0&&wc<=0)) {
            int iz=(int)((wa*a.iz+wb*b.iz+wc*c.iz)/area), i=y*RW+x;
            if(iz>0&&(uint32_t)iz>depth[i]) {
                int uoz=(int)((wa*a.uoz+wb*b.uoz+wc*c.uoz)/area);
                int voz=(int)((wa*a.voz+wb*b.voz+wc*c.voz)/area);
                int u=uoz/iz, v=voz/iz;
                depth[i]=(uint32_t)iz; pixels[i]=texel(block,face,u,v);
            }
        }
    }
}

static const int face_neighbor[6][3]={{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
static const int face_corner[6][4][3]={
 {{1,0,0},{1,1,0},{1,1,1},{1,0,1}}, {{0,0,1},{0,1,1},{0,1,0},{0,0,0}},
 {{0,1,1},{1,1,1},{1,1,0},{0,1,0}}, {{0,0,0},{1,0,0},{1,0,1},{0,0,1}},
 {{1,0,1},{1,1,1},{0,1,1},{0,0,1}}, {{0,0,0},{0,1,0},{1,1,0},{1,0,0}}
};
static void draw_scene(int ox,int oy,int w,int h) {
    for(int i=0;i<RW*RH;i++) { int yy=i/RW; pixels[i]=yy<RH*53/100?0xFF86B9E8:0xFF34443A; depth[i]=0; }
    for(int z=0;z<WZ;z++) for(int y=0;y<WY;y++) for(int x=0;x<WX;x++) {
        int b=world[y][z][x]; if(!b) continue;
        for(int f=0;f<6;f++) {
            if(solid(x+face_neighbor[f][0],y+face_neighbor[f][1],z+face_neighbor[f][2])) continue;
            int face_cx=x*Q+Q/2+face_neighbor[f][0]*Q/2;
            int face_cy=y*Q+Q/2+face_neighbor[f][1]*Q/2;
            int face_cz=z*Q+Q/2+face_neighbor[f][2]*Q/2;
            int64_t facing=(int64_t)face_neighbor[f][0]*(camera_x-face_cx)+
                           (int64_t)face_neighbor[f][1]*(camera_y-face_cy)+
                           (int64_t)face_neighbor[f][2]*(camera_z-face_cz);
            if(facing<=0) continue;
            camera_vertex_t input[4], clipped[8];
            for(int k=0;k<4;k++) {
                const int *c=face_corner[f][k];
                input[k]=to_camera(x+c[0],y+c[1],z+c[2],
                                   (k==1||k==2)?7:0,(k>=2)?7:0);
            }
            int count=clip_near(input,4,clipped);
            if(count>=3) {
                vertex_t first,previous,current;
                project_camera(&first,clipped[0]);
                project_camera(&previous,clipped[1]);
                for(int k=2;k<count;k++) {
                    project_camera(&current,clipped[k]);
                    triangle(first,previous,current,b,f);
                    previous=current;
                }
            }
        }
    }

    for(int d=-4;d<=4;d++) { pixels[(RH/2)*RW+RW/2+d]=0xFFFFFFFF; pixels[(RH/2+d)*RW+RW/2]=0xFFFFFFFF; }

    for(int n=0;n<6;n++) {
        int x0=RW/2-39+n*13, y0=RH-15;
        for(int yy=y0;yy<RH-3;yy++) for(int xx=x0;xx<x0+12;xx++)
            pixels[yy*RW+xx]=(n+1==selected)?0xFFFFFFFF:0xFF282828;
        uint32_t col=texel(n+1,2,3,3);
        for(int yy=y0+3;yy<y0+9;yy++) for(int xx=x0+3;xx<x0+9;xx++) pixels[yy*RW+xx]=col;
    }
    if(w<=0||h<=0) return;
    int view_w=w, view_h=h;
    if((int64_t)w*RH>(int64_t)h*RW) view_w=(int)((int64_t)h*RW/RH);
    else view_h=(int)((int64_t)w*RH/RW);
    int view_x=ox+(w-view_w)/2, view_y=oy+(h-view_h)/2;
    sharkapi_draw_rect((rect_t){0xFF000000u,ox,oy,w,h},0xFF000000u);
    for(int y=0;y<RH;y++) {
        int x=0,sy=view_y+y*view_h/RH,ey=view_y+(y+1)*view_h/RH;
        while(x<RW) { uint32_t c=pixels[y*RW+x]; int e=x+1; while(e<RW&&pixels[y*RW+e]==c)e++;
            int sx=view_x+x*view_w/RW,ex=view_x+e*view_w/RW;
            if(ex>sx&&ey>sy) sharkapi_draw_rect((rect_t){c,sx,sy,ex-sx,ey-sy},c);
            x=e;
        }
    }
}

static int ray_target(int *tx,int *ty,int *tz,int *px,int *py,int *pz) {
    int r=yaw&255,p=pitch&255;
    int dx=sn[r]*cs[p]/FP, dz=cs[r]*cs[p]/FP, dy=sn[p];
    int lastx=-1,lasty=-1,lastz=-1;
    for(int t=0;t<16*Q;t+=24) {
        int x=(camera_x+dx*t/FP)/Q, y=(camera_y+dy*t/FP)/Q, z=(camera_z+dz*t/FP)/Q;
        if(x<0||x>=WX||y<0||y>=WY||z<0||z>=WZ) continue;
        if(solid(x,y,z)) { *tx=x;*ty=y;*tz=z;*px=lastx;*py=lasty;*pz=lastz;return 1; }
        lastx=x;lasty=y;lastz=z;
    }
    return 0;
}
static void interact(int place) {
    int x,y,z,px,py,pz;
    if(!ray_target(&x,&y,&z,&px,&py,&pz)) return;
    if(place) {
        if(px>=0&&px<WX&&py>=0&&py<WY&&pz>=0&&pz<WZ&&!solid(px,py,pz)) {
            world[py][pz][px]=(uint8_t)selected;
            if(!flying&&player_collides_at(camera_x,camera_y,camera_z)) world[py][pz][px]=AIR;
        }
    } else world[y][z][x]=AIR;
}
static void clamp_camera(void) {
    if(camera_x < -4*Q) camera_x=-4*Q;
    if(camera_x > (WX+4)*Q) camera_x=(WX+4)*Q;
    if(camera_z < -8*Q) camera_z=-8*Q;
    if(camera_z > (WZ+4)*Q) camera_z=(WZ+4)*Q;
}
static int input_active(int index,uint32_t now) {
    return (int32_t)(move_deadline[index]-now)>0;
}
static int abs_value(int value) { return value<0?-value:value; }
static int approach_value(int value,int target,int amount) {
    if(value<target) return value+((target-value<amount)?target-value:amount);
    if(value>target) return value-((value-target<amount)?value-target:amount);
    return value;
}
static int integrate_velocity(int velocity,int dt,int *remainder) {
    int64_t total=(int64_t)velocity*dt+*remainder;
    int delta=(int)(total/1000);
    *remainder=(int)(total-(int64_t)delta*1000);
    return delta;
}
static void set_axis_position(int axis,int value) {
    if(axis==0) camera_x=value;
    else if(axis==1) camera_y=value;
    else camera_z=value;
}
static void move_axis(int axis,int delta) {
    if(!delta) return;
    int steps=(abs_value(delta)+MAX_MOVE_STEP-1)/MAX_MOVE_STEP;
    int base=delta/steps, remainder=delta%steps;
    int extra=abs_value(remainder), direction=delta<0?-1:1;
    for(int i=0;i<steps;i++) {
        int step=base+(i<extra?direction:0);
        if(!step) continue;
        int nx=camera_x,ny=camera_y,nz=camera_z;
        if(axis==0) nx+=step;
        else if(axis==1) ny+=step;
        else nz+=step;
        if(player_collides_at(nx,ny,nz)) {
            int safe=axis==0?camera_x:(axis==1?camera_y:camera_z);
            int blocked=axis==0?nx:(axis==1?ny:nz);
            while(abs_value(blocked-safe)>1) {
                int mid=safe+(blocked-safe)/2;
                int tx=camera_x,ty=camera_y,tz=camera_z;
                if(axis==0) tx=mid;
                else if(axis==1) ty=mid;
                else tz=mid;
                if(player_collides_at(tx,ty,tz)) blocked=mid;
                else safe=mid;
            }
            set_axis_position(axis,safe);
            if(axis==0) { velocity_x=0; position_remainder_x=0; }
            else if(axis==1) {
                if(delta<0) on_ground=1;
                velocity_y=0; position_remainder_y=0;
            } else { velocity_z=0; position_remainder_z=0; }
            return;
        }
        camera_x=nx; camera_y=ny; camera_z=nz;
    }
}
static void physics_update(void) {
    uint32_t now=sharkapi_get_ticks();
    if(!physics_clock_ready) {
        physics_last_tick=now;
        physics_clock_ready=1;
        return;
    }
    uint32_t elapsed=now-physics_last_tick;
    physics_last_tick=now;
    if(elapsed>50) elapsed=50;
    if(!elapsed) return;
    int dt=(int)elapsed;
    int grounded_at_start=on_ground;
    int forward=input_active(INPUT_FORWARD,now)-input_active(INPUT_BACK,now);
    int strafe=input_active(INPUT_RIGHT,now)-input_active(INPUT_LEFT,now);
    int direction_x=sn[yaw&255]*forward+cs[yaw&255]*strafe;
    int direction_z=cs[yaw&255]*forward+sn[yaw&255]*strafe;
    if(forward&&strafe) {
        direction_x=direction_x*724/FP;
        direction_z=direction_z*724/FP;
    }
    int target_x=direction_x*WALK_SPEED/FP;
    int target_z=direction_z*WALK_SPEED/FP;
    int accel_x=direction_x?24*Q:(grounded_at_start?18*Q:2*Q);
    int accel_z=direction_z?24*Q:(grounded_at_start?18*Q:2*Q);
    int change_x=(int)((int64_t)accel_x*dt/1000);
    int change_z=(int)((int64_t)accel_z*dt/1000);
    if(change_x<1) change_x=1;
    if(change_z<1) change_z=1;
    velocity_x=approach_value(velocity_x,target_x,change_x);
    velocity_z=approach_value(velocity_z,target_z,change_z);

    if(flying) {
        int up=input_active(INPUT_UP,now), down=input_active(INPUT_DOWN,now);
        int target_y=(up-down)*WALK_SPEED;
        int change_y=(int)((int64_t)24*Q*dt/1000);
        if(change_y<1) change_y=1;
        velocity_y=approach_value(velocity_y,target_y,change_y);
        camera_x+=integrate_velocity(velocity_x,dt,&position_remainder_x);
        camera_y+=integrate_velocity(velocity_y,dt,&position_remainder_y);
        camera_z+=integrate_velocity(velocity_z,dt,&position_remainder_z);
        on_ground=0;
        clamp_camera();
        if(camera_y < -4*Q) camera_y=-4*Q;
        if(camera_y > (WY+12)*Q) camera_y=(WY+12)*Q;
        return;
    }

    on_ground=0;
    move_axis(0,integrate_velocity(velocity_x,dt,&position_remainder_x));
    move_axis(2,integrate_velocity(velocity_z,dt,&position_remainder_z));
    if(grounded_at_start&&velocity_y<=0&&player_collides_at(camera_x,camera_y-1,camera_z)) {
        on_ground=1;
        velocity_y=0;
        position_remainder_y=0;
    }
    if(on_ground) velocity_y=0;
    else {
        velocity_y-=(int)((int64_t)GRAVITY*dt/1000);
        if(velocity_y < -MAX_FALL_SPEED) velocity_y=-MAX_FALL_SPEED;
    }
    move_axis(1,integrate_velocity(velocity_y,dt,&position_remainder_y));
    clamp_camera();
    if(camera_y < -5*Q) camera_reset();
}
static int move_key(char key) {
    if(key>='A'&&key<='Z') key=(char)(key-'A'+'a');
    if(key>='1'&&key<='6'){selected=key-'0';return 1;}
    if(key=='-'){if(focal_length>80)focal_length-=10;return 1;}
    if(key=='+'){if(focal_length<200)focal_length+=10;return 1;}
    if(key=='f') {
        flying=!flying;
        velocity_x=velocity_y=velocity_z=0;
        position_remainder_x=position_remainder_y=position_remainder_z=0;
        for(int i=0;i<6;i++) move_deadline[i]=0;
        if(!flying) {
            for(int i=0;i<8*Q&&player_collides_at(camera_x,camera_y,camera_z);i+=Q/8)
                camera_y+=Q/8;
            on_ground=player_collides_at(camera_x,camera_y-1,camera_z);
        } else on_ground=0;
        return 1;
    }
    uint32_t deadline=sharkapi_get_ticks()+INPUT_GRACE_MS;
    if(key=='w'){move_deadline[INPUT_FORWARD]=deadline;return 1;}
    if(key=='s'){move_deadline[INPUT_BACK]=deadline;return 1;}
    if(key=='a'){move_deadline[INPUT_LEFT]=deadline;return 1;}
    if(key=='d'){move_deadline[INPUT_RIGHT]=deadline;return 1;}
    if(key==' ') {
        if(flying) move_deadline[INPUT_UP]=deadline;
        else if(on_ground) {
            velocity_y=JUMP_SPEED;
            position_remainder_y=0;
            on_ground=0;
        }
        return 1;
    }
    if(key=='c') { if(flying) move_deadline[INPUT_DOWN]=deadline; return 1; }
    if(key=='q'){interact(0);return 1;}
    if(key=='e'){interact(1);return 1;}
    return 0;
}

void sharkcraft_draw_window(int x,int y,int w,int h) {
    if(!world_ready) sharkcraft_game_reset();
    physics_update();
    if(w>0&&h>0) draw_scene(x,y,w,h);
}
int sharkcraft_handle_key(char key) { return move_key(key); }
int sharkcraft_handle_mouse(int x,int y,int buttons) {
    if(!(buttons&1)){drag_active=0;return 0;}
    if(!drag_active){drag_active=1;drag_x=x;drag_y=y;return 0;}
    int dx=x-drag_x,dy=y-drag_y;drag_x=x;drag_y=y;
    yaw_drag_remainder+=dx;
    pitch_drag_remainder+=dy;
    int yaw_step=yaw_drag_remainder/4;
    int pitch_step=pitch_drag_remainder/4;
    yaw_drag_remainder-=yaw_step*4;
    pitch_drag_remainder-=pitch_step*4;
    yaw=(yaw+yaw_step)&255;
    pitch+=pitch_step;
    if(pitch>48){pitch=48;pitch_drag_remainder=0;}
    if(pitch<-48){pitch=-48;pitch_drag_remainder=0;}
    return dx||dy;
}

int sharkcraft_plugin_init(void){if(!world_ready)sharkcraft_game_reset();return 0;}
void sharkcraft_plugin_cleanup(void){}
int sharkcraft_plugin_command(int argc,char**argv) {
    (void)argc;(void)argv;
    sharkcraft_game_reset();
    int w=640,h=480;
    sharkapi_get_screen_size(&w,&h);
    sharkapi_clear_screen();
    draw_scene(0,0,w,h);
    uint32_t last=sharkapi_get_ticks();
    for(;;) {
        int changed=0; char c; int n=0;
        while(n++<8 && (c=sharkapi_getchar())!=0) {
            if(c==27) { sharkapi_clear_screen(); return 0; }
            changed |= move_key(c);
        }
        physics_update();
        uint32_t now=sharkapi_get_ticks();
        if(changed || now-last>=16) { draw_scene(0,0,w,h); last=now; }
        sharkapi_yield();
    }
}
plugin_info_t *sharkcraft_plugin_get_info(void){return &sharkcraft_plugin_info;}
#ifndef SHARKCRAFT_BUILTIN
int plugin_init_entry(void){return sharkcraft_plugin_init();}
void plugin_cleanup_entry(void){sharkcraft_plugin_cleanup();}
int plugin_command_entry(int argc,char**argv){return sharkcraft_plugin_command(argc,argv);}
plugin_info_t *plugin_get_info(void){return sharkcraft_plugin_get_info();}
#endif
