#include "sharkapi.h"
#include <stdint.h>

plugin_info_t sharkcube_plugin_info = {
    .version = SHARKAPI_VERSION,
    .name = "sharkcube",
    .author = "SharkOS community",
    .description = "Tiny first-person wireframe cube walkabout. WASD move, A/D turn, ESC exit.",
    .major = 1, .minor = 0
};

                                                                           
                                                                           
                                       
#define RW 480
#define RH 300
#define FP 1024
static int running;
static int cam_x, cam_z, turn, cube_yaw, cube_pitch;
static int mouse_dragging, mouse_last_x, mouse_last_y;
static uint16_t zbuf[RW * RH];
static uint32_t pixels[RW * RH];

typedef struct { int x,y,z; } vec3;
typedef struct { int x,y,z; } point2;
static const int cs[256] = {
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
static const int sn[256] = {
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

static int edge(point2 a, point2 b, int x, int y) { return (x-a.x)*(b.y-a.y)-(y-a.y)*(b.x-a.x); }
static void tri(point2 a, point2 b, point2 c, uint32_t color) {
    int area=edge(a,b,c.x,c.y); if (!area) return;
    int minx=a.x, maxx=a.x, miny=a.y, maxy=a.y;
    if (b.x < minx) minx = b.x;
    if (c.x < minx) minx = c.x;
    if (b.x > maxx) maxx = b.x;
    if (c.x > maxx) maxx = c.x;
    if (b.y < miny) miny = b.y;
    if (c.y < miny) miny = c.y;
    if (b.y > maxy) maxy = b.y;
    if (c.y > maxy) maxy = c.y;
    if (minx < 0) minx = 0;
    if (miny < 0) miny = 0;
    if (maxx >= RW) maxx = RW - 1;
    if (maxy >= RH) maxy = RH - 1;
    for(int y=miny;y<=maxy;y++) for(int x=minx;x<=maxx;x++) {
        int wa=edge(b,c,x,y), wb=edge(c,a,x,y), wc=edge(a,b,x,y);
        if ((area>0 && wa>=0 && wb>=0 && wc>=0)||(area<0 && wa<=0 && wb<=0 && wc<=0)) {
            int iz=(wa*a.z+wb*b.z+wc*c.z)/area;
            int idx=y*RW+x;
            if(iz>zbuf[idx]) { zbuf[idx]=(uint16_t)iz; pixels[idx]=color; }
        }
    }
}
static point2 project(vec3 v, int w, int h) {
    point2 p;
    int focal=(RH*9)/10;
    p.z=65535/v.z;
    p.x=RW/2 + (v.x*focal)/v.z;
    p.y=RH/2 - (v.y*focal)/v.z;
    (void)w; (void)h;
    return p;
}
static vec3 transform(int x,int y,int z) {
                                                                          
                                                                               
    int cy=cube_yaw&255, cp=cube_pitch&255;
    int rx=(x*cs[cy]+z*sn[cy])/FP;
    int rz=(-x*sn[cy]+z*cs[cy])/FP;
    int ry=(y*cs[cp]-rz*sn[cp])/FP;
    rz=(y*sn[cp]+rz*cs[cp])/FP;
    int wx=rx-cam_x, wz=(310+rz)-cam_z;
    int r=turn&255;
    vec3 v={ (wx*cs[r]+wz*sn[r])/FP, ry,
             (-wx*sn[r]+wz*cs[r])/FP + 450 };
    return v;
}
static void raster_line(int x0,int y0,int x1,int y1,uint32_t color) {
    int dx=x1>x0?x1-x0:x0-x1, sx=x0<x1?1:-1;
    int dy=y1>y0?y0-y1:y1-y0, sy=y0<y1?1:-1;
    int err=dx+dy;
    for(;;) {
        if(x0>=0 && x0<RW && y0>=0 && y0<RH) pixels[y0*RW+x0]=color;
        if(x0==x1 && y0==y1) break;
        int e2=2*err;
        if(e2>=dy){err+=dy;x0+=sx;}
        if(e2<=dx){err+=dx;y0+=sy;}
    }
}
static void frame(int ox,int oy,int w,int h) {
    const int horizon=(RH*53)/100;
    for(int y=0;y<RH;y++) for(int x=0;x<RW;x++) {
        if(y<horizon) pixels[y*RW+x]=0xFF17253F;
        else pixels[y*RW+x]=0xFF252A2B;
        zbuf[y*RW+x]=0;
    }
                                                                       
    for(int i=-7;i<=7;i++) {
        int bx=RW/2 + i*(RW/14);
        raster_line(RW/2,horizon,bx,RH-1,0xFF46525A);
    }
    for(int k=1;k<=8;k++) {
        int gy=horizon+(RH-horizon)*k*k/64;
        raster_line(0,gy,RW-1,gy,0xFF46525A);
    }
                                                                            
                                                                                
    vec3 v[8];
    int xx[8]={-150,150,150,-150,-150,150,150,-150};
    int yy[8]={-150,-150,150,150,-150,-150,150,150};
    int zz[8]={-90,-90,-90,-90,90,90,90,90};
    point2 p[8];
    for(int i=0;i<8;i++){ v[i]=transform(xx[i],yy[i],zz[i]); if(v[i].z<20)v[i].z=20; p[i]=project(v[i],w,h); }
    const int faces[6][4]={{0,1,2,3},{5,4,7,6},{4,0,3,7},{1,5,6,2},{3,2,6,7},{4,5,1,0}};
    const uint32_t shades[6]={0xFF00E5D4,0xFF00A99C,0xFF20C8B7,0xFF55FFE9,0xFF168DFF,0xFF65C8FF};
    for(int f=0;f<6;f++){ int a=faces[f][0],b=faces[f][1],c=faces[f][2],d=faces[f][3]; tri(p[a],p[b],p[c],shades[f]); tri(p[a],p[c],p[d],shades[f]); }
                                                                             
    for(int y=0;y<RH;y++) {
        int x=0, sy=oy+y*h/RH, ey=oy+(y+1)*h/RH;
        while(x<RW) {
            uint32_t c=pixels[y*RW+x];
            int end=x+1;
            while(end<RW && pixels[y*RW+end]==c) end++;
            int sx=ox+x*w/RW, ex=ox+end*w/RW;
            if(ex>sx && ey>sy) sharkapi_draw_rect((rect_t){c,sx,sy,ex-sx,ey-sy},c);
            x=end;
        }
    }
}

void sharkcube_game_reset(void) {
    cam_x=0; cam_z=0; turn=0; cube_yaw=0; cube_pitch=0; mouse_dragging=0;
}

int sharkcube_handle_key(char key) {
    int r=turn&255;
    if (key=='w'||key=='W') { cam_x-=sn[r]*12/FP; cam_z+=cs[r]*12/FP; return 1; }
    if (key=='s'||key=='S') { cam_x+=sn[r]*12/FP; cam_z-=cs[r]*12/FP; return 1; }
    if (key=='a'||key=='A') { turn=(turn+16)&255; return 1; }
    if (key=='d'||key=='D') { turn=(turn+240)&255; return 1; }
    return 0;
}

int sharkcube_handle_mouse(int x,int y,int buttons) {
    if (!(buttons&1)) { mouse_dragging=0; return 0; }
    if (!mouse_dragging) { mouse_dragging=1; mouse_last_x=x; mouse_last_y=y; return 0; }
    int dx=x-mouse_last_x, dy=y-mouse_last_y;
    mouse_last_x=x; mouse_last_y=y;
    if (!dx && !dy) return 0;
                                                                                  
    cube_yaw=(cube_yaw+dx)&255;
    cube_pitch=(cube_pitch+dy)&255;
    return 1;
}

void sharkcube_draw_window(int x, int y, int width, int height) {
    if(width>0 && height>0) frame(x,y,width,height);
}

int sharkcube_plugin_init(void) { return 0; }
void sharkcube_plugin_cleanup(void) { running=0; }

int sharkcube_plugin_command(int argc, char **argv) {
    (void)argc; (void)argv;
    int w=640,h=480;
    sharkapi_get_screen_size(&w,&h);
    if (w < 160 || h < 120) return 1;
    sharkcube_game_reset(); running=1;
                                                                             
    for(int i=0;i<32 && sharkapi_getchar()!=0;i++) { }
    sharkapi_clear_screen();
    frame(0,0,w,h);
    uint32_t last_frame=sharkapi_get_ticks();
    while (running) {
        char k; int read=0;
        while(read++<8 && (k=sharkapi_getchar())!=0) {
            if (k==27) running=0;
            else sharkcube_handle_key(k);
        }
        uint32_t now=sharkapi_get_ticks();
        if(now-last_frame>=16) { frame(0,0,w,h); last_frame=now; }
        sharkapi_yield();
    }
    sharkapi_clear_screen();
    return 0;
}
plugin_info_t *sharkcube_plugin_get_info(void) { return &sharkcube_plugin_info; }

                                                                          
                                                                                 
#ifndef SHARKCUBE_BUILTIN
int plugin_init_entry(void) { return sharkcube_plugin_init(); }
void plugin_cleanup_entry(void) { sharkcube_plugin_cleanup(); }
int plugin_command_entry(int argc, char **argv) { return sharkcube_plugin_command(argc, argv); }
plugin_info_t *plugin_get_info(void) { return sharkcube_plugin_get_info(); }
#endif
