#include "kernel.h"
#include "fnaf.h"
#include "desktop.h"
#include "fnaf_assets.h"

uint8_t fnaf_screen[FNAF_SCREEN_H][FNAF_SCREEN_W];
uint32_t fnaf_palette[256];

static int window_x = 0, window_y = 0, window_w = 640, window_h = 480;

#define C_BLACK        0
#define C_WALL         64
#define C_FLOOR        65
#define C_DESK         66
#define C_DOOR         67
#define C_DOORFRAME    68
#define C_LIGHTON      69
#define C_LIGHTOFF     70
#define C_HALL_BG      71
#define C_HALL_FLOOR   72
#define C_BONNIE      73
#define C_BONNIE2     74
#define C_CHICA       75
#define C_CHICA_BEAK  76
#define C_FREDDY      77
#define C_MUZZLE      78
#define C_FOXY        79
#define C_WHITE       80
#define C_RED         81
#define C_GREEN       82
#define C_YELLOW      83
#define C_GREY        84
#define C_DGREY       85
#define C_LGREY       86
#define C_ORANGE      87
#define C_BLUE        88
#define C_PURPLE      89
#define C_BROWN       90
#define C_STATIC1     91
#define C_STATIC2     92
#define C_TOOLBAR     93
#define C_POWERGREEN  94
#define C_POWERYELLOW 95
#define C_POWERRED    96
#define C_BLACK2      97

#define ST_MENU       0
#define ST_PLAYING    1
#define ST_JUMPSCARE  2
#define ST_GAMEOVER   3
#define ST_WIN        4
#define ST_POWEROUT   5

static int fnaf_state = ST_MENU;
static int fnaf_night = 1;
static int fnaf_hour = 0;
static int hour_progress = 0;
static int power = 10000;
static int left_door = 0;
static int right_door = 0;
static int left_light = 0;
static int right_light = 0;
static int camera_up = 0;
static int current_cam = 0;
static int power_out = 0;
static int running = 1;

static int bonnie_pos = 0;
static int chica_pos = 0;
static int freddy_pos = 0;
static int foxy_stage = 0;
static int foxy_timer = 0;
static int foxy_sprint = 0;
static int foxy_sprint_timer = 0;

static int ai_bonnie = 1, ai_chica=1, ai_freddy=1, ai_foxy=1;

static int left_door_block_timer = 0;
static int right_door_block_timer = 0;
static int jumpscare_type = 0;
static uint32_t jumpscare_start = 0;
static uint32_t menu_time = 0;
static uint32_t gameover_time = 0;
static uint32_t win_time = 0;

static uint32_t last_power_tick = 0;
static uint32_t last_hour_tick = 0;
static uint32_t last_ai_tick = 0;
static uint32_t last_foxy_tick = 0;
static uint32_t last_static_tick = 0;
static int static_seed = 12345;
static uint32_t rng_state = 0x12345678;

static int flash = 0;

static void set_px(int x, int y, uint8_t c){
    if (x>=0 && x<320 && y>=0 && y<200) fnaf_screen[y][x]=c;
}
static void fill_rect(int x, int y, int w, int h, uint8_t c){
    if (w<=0||h<=0) return;
    if (x<0){ w+=x; x=0; }
    if (y<0){ h+=y; y=0; }
    if (x+w>320) w=320-x;
    if (y+h>200) h=200-y;
    if (w<=0||h<=0) return;
    for(int j=0;j<h;j++) for(int i=0;i<w;i++) fnaf_screen[y+j][x+i]=c;
}
static void draw_hline(int x,int y,int w,uint8_t c){ fill_rect(x,y,w,1,c); }
static void draw_vline(int x,int y,int h,uint8_t c){ fill_rect(x,y,1,h,c); }
static void draw_rect_outline(int x,int y,int w,int h,uint8_t c){
    draw_hline(x,y,w,c); draw_hline(x,y+h-1,w,c); draw_vline(x,y,h,c); draw_vline(x+w-1,y,h,c);
}
static void draw_font_char(int x,int y,char c,uint8_t fg){
    extern uint8_t font8x8[96][8];
    unsigned char uc=(unsigned char)c;
    if(uc<32||uc>=128) return;
    int idx=uc-32;
    for(int r=0;r<8;r++){
        uint8_t bits=font8x8[idx][r];
        for(int b=0;b<8;b++) if(bits & (0x80>>b)) set_px(x+b,y+r,fg);
    }
}
static void draw_text(int x,int y,const char* s,uint8_t fg){
    for(int i=0;s[i];i++){ draw_font_char(x+i*8,y,s[i],fg); }
}
static void draw_text_center(int y,const char* s,uint8_t fg){
    int len=0; while(s[len]) len++;
    int x=160 - len*4;
    draw_text(x,y,s,fg);
}
static void int_to_str(int v,char* out){
    if(v==0){ out[0]='0'; out[1]=0; return; }
    char tmp[12]; int n=0; int neg=0;
    if(v<0){ neg=1; v=-v; }
    while(v>0 && n<11){ tmp[n++]='0'+(v%10); v/=10; }
    if(neg) tmp[n++]='-';
    int p=0; for(int i=n-1;i>=0;i--) out[p++]=tmp[i]; out[p]=0;
}
static unsigned int rng_next(void){
    rng_state = rng_state*1103515245 + 12345;
    return (rng_state>>16)&0x7FFF;
}
static int rng_range(int n){ if(n<=0) return 0; return (int)(rng_next()% (unsigned)n); }

static void build_pal(void){
    for(int i=0;i<256;i++) fnaf_palette[i]=0;
    // Copy authentic texture palette (0..63)
    for(int i=0;i<64;i++) fnaf_palette[i]=fnaf_tex_palette[i];
    fnaf_palette[C_BLACK]=fnaf_palette[0];
    // UI / overlay colors beyond texture palette (use high indices to avoid clash)
    fnaf_palette[C_WALL]=0x2E2E3A;
    fnaf_palette[C_FLOOR]=0x1A1A1E;
    fnaf_palette[C_DESK]=0x3D2E1E;
    fnaf_palette[C_DOOR]=0x4A4A4A;
    fnaf_palette[C_DOORFRAME]=0x2B2B2B;
    fnaf_palette[C_LIGHTON]=0xFFF8B0;
    fnaf_palette[C_LIGHTOFF]=0x0A0A0A;
    fnaf_palette[C_HALL_BG]=0x1B1B1B;
    fnaf_palette[C_HALL_FLOOR]=0x2F2F2F;
    fnaf_palette[C_BONNIE]=0x8A2BE2;
    fnaf_palette[C_BONNIE2]=0x5B1AA3;
    fnaf_palette[C_CHICA]=0xFFDB58;
    fnaf_palette[C_CHICA_BEAK]=0xFF7F11;
    fnaf_palette[C_FREDDY]=0x8B4513;
    fnaf_palette[C_MUZZLE]=0xE8C9A0;
    fnaf_palette[C_FOXY]=0xA52A2A;
    fnaf_palette[C_WHITE]=0xFFFFFF;
    fnaf_palette[C_RED]=0xFF2020;
    fnaf_palette[C_GREEN]=0x20FF20;
    fnaf_palette[C_YELLOW]=0xFFFF00;
    fnaf_palette[C_GREY]=0x777777;
    fnaf_palette[C_DGREY]=0x333333;
    fnaf_palette[C_LGREY]=0xAAAAAA;
    fnaf_palette[C_ORANGE]=0xFF8C00;
    fnaf_palette[C_BLUE]=0x4080FF;
    fnaf_palette[C_PURPLE]=0xAA00FF;
    fnaf_palette[C_BROWN]=0x6B4423;
    fnaf_palette[C_STATIC1]=0x555555;
    fnaf_palette[C_STATIC2]=0x999999;
    fnaf_palette[C_TOOLBAR]=0x101018;
    fnaf_palette[C_POWERGREEN]=0x00C853;
    fnaf_palette[C_POWERYELLOW]=0xFFD600;
    fnaf_palette[C_POWERRED]=0xD50000;
    fnaf_palette[C_BLACK2]=0x0A0A0F;
    for(int i=98;i<256;i++) if(fnaf_palette[i]==0) fnaf_palette[i]=0x101010;
}

static int office_pan = 40; // 0..80 for 400px wide office viewed at 320

static inline void fnaf_blit_tex(int tex_id, int dst_x, int dst_y, int src_x0, int src_y0, int src_w, int src_h){
    if(tex_id <0 || tex_id >= FNAF_TEX_COUNT) return;
    const fnaf_tex_info_t *ti = &fnaf_tex_info[tex_id];
    const uint8_t *src = fnaf_tex_rle_data + ti->offset;
    int tex_w = ti->w;
    int tex_h = ti->h;
    // Quick clip: if src region outside texture
    if(src_x0<0) { src_w+=src_x0; dst_x-=src_x0; src_x0=0; }
    if(src_y0<0) { src_h+=src_y0; dst_y-=src_y0; src_y0=0; }
    if(src_x0+src_w > tex_w) src_w = tex_w - src_x0;
    if(src_y0+src_h > tex_h) src_h = tex_h - src_y0;
    if(src_w<=0||src_h<=0) return;
    // Decode RLE and blit only needed region
    int pos = 0; // decoded pixel index 0..tex_w*tex_h-1
    int cur_run = 0;
    int run_rem = 0;
    uint8_t run_val = 0;
    int src_idx = 0;
    int total = tex_w * tex_h;
    // Iterate through RLE to generate pixels in order
    // We need to track y/x via pos
    int rle_p = 0;
    // Preload first run
    if(ti->rle_size>=2){
        run_rem = src[0];
        run_val = src[1];
        rle_p = 2;
    }
    for(int py=0; py<tex_h; py++){
        for(int px=0; px<tex_w; px++){
            // advance to current pos's value
            // run_rem already holds remaining of current run
            while(run_rem==0 && rle_p < (int)ti->rle_size){
                run_rem = src[rle_p];
                run_val = src[rle_p+1];
                rle_p+=2;
            }
            if(run_rem==0) break;
            // check if this pixel is within requested src region
            if(px >= src_x0 && px < src_x0+src_w && py >= src_y0 && py < src_y0+src_h){
                int dx = dst_x + (px - src_x0);
                int dy = dst_y + (py - src_y0);
                if(dx>=0 && dx<320 && dy>=0 && dy<200){
                    fnaf_screen[dy][dx] = run_val;
                }
            }
            run_rem--;
            pos++;
        }
    }
}
static inline void fnaf_blit_tex_scaled(int tex_id, int dst_x, int dst_y, int dst_w, int dst_h){
    // nearest-neighbor scale texture to dst rect (simple decode into temp then scale blit)
    // For small textures we can decode into stack temp (max 400*120=48000)
    // Use static buffer to avoid stack overflow
    static uint8_t tmp[57600];
    if(tex_id <0 || tex_id >= FNAF_TEX_COUNT) return;
    const fnaf_tex_info_t *ti = &fnaf_tex_info[tex_id];
    // quick decode full texture into tmp
    const uint8_t *src = fnaf_tex_rle_data + ti->offset;
    uint32_t out=0;
    for(uint32_t i=0;i<ti->rle_size;i+=2){
        uint8_t cnt=src[i]; uint8_t val=src[i+1];
        for(int k=0;k<cnt;k++) tmp[out++]=val;
    }
    // scale
    for(int dy=0; dy<dst_h; dy++){
        int sy = (dy * ti->h) / dst_h;
        if(sy<0) sy=0; if(sy>=ti->h) sy=ti->h-1;
        for(int dx=0; dx<dst_w; dx++){
            int sx = (dx * ti->w) / dst_w;
            if(sx<0) sx=0; if(sx>=ti->w) sx=ti->w-1;
            uint8_t v = tmp[sy*ti->w + sx];
            int fx = dst_x+dx;
            int fy = dst_y+dy;
            if(fx>=0 && fx<320 && fy>=0 && fy<200) fnaf_screen[fy][fx]=v;
        }
    }
}



static void blit(void){
    uint32_t stride = (uint32_t)(screen_pitch/4);
    int scale=2;
    if(window_w<320) scale=1;
    if(window_h<200) scale=1;
    for(int ly=0; ly<200; ly++){
        for(int sy=0; sy<scale; sy++){
            int fy = window_y + ly*scale + sy;
            if(fy <0 || fy >= (int)screen_height) continue;
            if(fy < window_y || fy >= window_y+window_h) continue;
            for(int lx=0; lx<320; lx++){
                uint32_t col = fnaf_palette[fnaf_screen[ly][lx]];
                for(int sx=0; sx<scale; sx++){
                    int fx = window_x + lx*scale + sx;
                    if(fx <0 || fx >= (int)screen_width) continue;
                    if(fx < window_x || fx >= window_x+window_w) continue;
                    lfbptr[fy*stride + fx]=col;
                }
            }
        }
    }
    if(scale==2 && (window_w!=640 || window_h!=400)){
        for(int wy=0; wy<window_h; wy++){
            int sy = wy * 200 / window_h;
            if(sy<0) sy=0; if(sy>=200) sy=199;
            int fy = window_y + wy;
            if(fy<0||fy>= (int)screen_height) continue;
            for(int wx=0; wx<window_w; wx++){
                int sx = wx * 320 / window_w;
                if(sx<0) sx=0; if(sx>=320) sx=319;
                lfbptr[fy*stride + (window_x+wx)] = fnaf_palette[fnaf_screen[sy][sx]];
            }
        }
    }
}

// 1:1 FNAF1 cameras - 11 feeds (Kitchen has audio only)
#define CAM_1A 0
#define CAM_1B 1
#define CAM_1C 2
#define CAM_2A 3
#define CAM_2B 4
#define CAM_3  5
#define CAM_4A 6
#define CAM_4B 7
#define CAM_5  8
#define CAM_6  9
#define CAM_7  10
static const char* cam_names[11]={
    "SHOW STAGE - CAM 1A",
    "DINING AREA - CAM 1B",
    "PIRATE COVE - CAM 1C",
    "WEST HALL - CAM 2A",
    "W. HALL CORNER - CAM 2B",
    "SUPPLY CLOSET - CAM 3",
    "EAST HALL - CAM 4A",
    "E. HALL CORNER - CAM 4B",
    "BACKSTAGE - CAM 5",
    "KITCHEN - CAM 6",
    "RESTROOMS - CAM 7"
};
#define CAM_COUNT 11
// Real FNAF map positions for animatronics
// Bonnie: 0:1A 1:1B 2:5 3:3 4:2A 5:2B 6:DOOR 7:INSIDE
// Chica : 0:1A 1:1B 2:7 3:6 4:4A 5:4B 6:DOOR 7:INSIDE
// Freddy: 0:1A 1:1B 2:7 3:6 4:4A 5:4B 6:DOOR 7:INSIDE (same as Chica but deterministic)
// Foxy : stage 0-3 at 1C then sprint


static int base_ai_bonnie, base_ai_chica, base_ai_freddy, base_ai_foxy;
static void set_ai_for_night(void){
    // Accurate FNAF1 AI levels per CeriW / technicalFNaF
    // Night 1..6 (7=custom 1:1 as 20/20/20/20 for bonus)
    int table[7][4]={
        {0,0,0,0},      // Night 1: 0/0/0/0 -> 0/3/2/2 by 4AM
        {3,1,0,1},      // Night 2: 0/3/1/1
        {0,5,1,2},      // Night 3: 1/0/5/2 (Freddy 1)
        {2,4,1,6},      // Night 4: 1-2/2/4/6 (random 1or2 for Freddy, we pick 1)
        {5,7,3,5},      // Night 5: 3/5/7/5
        {10,12,4,16},   // Night 6: 4/10/12/16 (Freddy 4, Foxy 16 on night6 per doc)
        {20,20,20,20}   // Night 7 custom night max
    };
    // Special: Night4 Freddy random 1 or 2 -> use 1 (or pick randomly at start)
    int n=fnaf_night-1;
    if(n<0) n=0; if(n>6) n=6;
    base_ai_bonnie=table[n][0];
    base_ai_chica=table[n][1];
    base_ai_freddy=table[n][2];
    base_ai_foxy=table[n][3];
    if(fnaf_night==4){
        // 50% chance 2 as in real game
        if(rng_range(2)==0) base_ai_freddy=1; else base_ai_freddy=2;
    }
    ai_bonnie=base_ai_bonnie;
    ai_chica=base_ai_chica;
    ai_freddy=base_ai_freddy;
    ai_foxy=base_ai_foxy;
}
static void update_ai_boosts(void){
    // 2AM Bonnie +1, 3AM Bonnie/Chica/Foxy +1, 4AM Bonnie/Chica/Foxy +1 (Freddy never boosts)
    int target_b = base_ai_bonnie;
    int target_c = base_ai_chica;
    int target_f = base_ai_foxy;
    if(fnaf_hour >= 2) target_b += 1;
    if(fnaf_hour >= 3){ target_b += 1; target_c += 1; target_f += 1; }
    if(fnaf_hour >= 4){ target_b += 1; target_c += 1; target_f += 1; }
    if(target_b>20) target_b=20;
    if(target_c>20) target_c=20;
    if(target_f>20) target_f=20;
    ai_bonnie = target_b;
    ai_chica = target_c;
    ai_foxy = target_f;
    // Freddy stays base
    ai_freddy = base_ai_freddy;
}

// 1:1 state
static int bonnie_inside=0, chica_inside=0, freddy_inside=0;
static uint32_t bonnie_door_time=0, chica_door_time=0;
static int left_door_jammed=0, right_door_jammed=0;
static int left_light_jammed=0, right_light_jammed=0;
static uint32_t office_entry_time=0;
static uint32_t last_bonnie_tick=0, last_chica_tick=0, last_freddy_tick=0, last_foxy_tick2=0;
static int freddy_countdown=0; // frames (60fps equivalent ticks) until move, 0 means ready
static int foxy_stall=0; // ticks remaining where Foxy cannot move after cam down
static int power_out_phase=0;
static uint32_t power_out_phase_start=0;
static int foxy_attack_pending=0;
static void reset_positions(void){
    bonnie_pos=0; chica_pos=0; freddy_pos=0;
    bonnie_inside=0; chica_inside=0; freddy_inside=0;
    bonnie_door_time=0; chica_door_time=0;
    left_door_jammed=0; right_door_jammed=0; left_light_jammed=0; right_light_jammed=0;
    foxy_stage=0; foxy_timer=0; foxy_sprint=0; foxy_sprint_timer=0; foxy_stall=0; foxy_attack_pending=0;
    freddy_countdown=0;
    left_door=0; right_door=0; left_light=0; right_light=0; camera_up=0; current_cam=0;
    power=9990; // 99.9% as in real (99%)
    left_door_block_timer=0; right_door_block_timer=0;
    fnaf_hour=0; hour_progress=0; power_out=0; power_out_phase=0;
    office_entry_time=0;
    last_power_tick=uptime_ticks;
    last_hour_tick=uptime_ticks;
    last_ai_tick=uptime_ticks;
    last_foxy_tick=uptime_ticks;
    last_static_tick=uptime_ticks;
    last_bonnie_tick=uptime_ticks;
    last_chica_tick=uptime_ticks;
    last_freddy_tick=uptime_ticks;
    last_foxy_tick2=uptime_ticks;
    set_ai_for_night();
}

static void trigger_jumpscare(int type){
    fnaf_state=ST_JUMPSCARE;
    jumpscare_type=type;
    jumpscare_start=uptime_ticks;
    camera_up=0;
}

static void draw_office_walls(void){
    // Authentic office texture with panning (400x120 -> 320 viewport, pan 0..80)
    // Update pan based on mouse? kept in fnaf_handle_mouse via office_pan static
    fnaf_blit_tex(TEX_OFFICE, 0, 16, office_pan, 0, 320, 120);
    // Darken hallways when light off (overlay translucent? just darken door rect)
    if(!left_light || power_out){
        // dim left doorway area with semi-dark overlay using checker
        for(int y=38;y<116;y++) for(int x=28;x<73;x++) if(((x+y)&1)==0) fnaf_screen[y][x] = (fnaf_screen[y][x] & 0x3F) ? fnaf_screen[y][x] : fnaf_screen[y][x];
    }
    if(!right_light || power_out){
        for(int y=38;y<116;y++) for(int x=248;x<293;x++) if(((x+y)&1)==0) {}
    }
    // Overlay authentic hall views when light on
    if(left_light && !power_out && !left_door){
        // West hall lit view (use TEX_WEST_HALL scaled into doorway)
        fnaf_blit_tex_scaled(TEX_WEST_HALL, 28, 38, 45, 74);
        // If Bonnie at door, overlay hint via brighten? actual jumpscare handled separately, but add silhouette
        if(bonnie_pos==4 || bonnie_pos==5){
            // add eyes effect
            fill_rect(38,60,6,4,C_WHITE);
            fill_rect(52,60,6,4,C_WHITE);
        }
    }
    if(right_light && !power_out && !right_door){
        fnaf_blit_tex_scaled(TEX_EAST_HALL_CHICA, 248, 38, 45, 74);
        if(chica_pos==6 || chica_pos==7 || freddy_pos==6 || freddy_pos==7){
            fill_rect(258,62,6,4,C_WHITE);
            fill_rect(276,62,6,4,C_WHITE);
        }
    }
    // Doors closed overlay (metal shutter - procedural but tinted)
    if(left_door){
        fill_rect(28,38,45,84,C_DOOR);
        for(int y=38;y<120;y+=6) draw_hline(28,y,45,C_DGREY);
        fill_rect(30,40,6,6,C_RED);
        draw_rect_outline(28,38,45,84,C_BLACK);
    }
    if(right_door){
        fill_rect(248,38,45,84,C_DOOR);
        for(int y=38;y<120;y+=6) draw_hline(248,y,45,C_DGREY);
        fill_rect(280,40,6,6,C_RED);
        draw_rect_outline(248,38,45,84,C_BLACK);
    }
    // Buttons (keep UI)
    uint8_t btn_col = left_door? C_RED: C_DGREY;
    fill_rect(8,88,12,16,btn_col);
    draw_rect_outline(8,88,12,16,C_BLACK);
    draw_text(10,92,"D", C_WHITE);
    btn_col = left_light? C_YELLOW: C_DGREY;
    fill_rect(8,108,12,12,btn_col);
    draw_rect_outline(8,108,12,12,C_BLACK);
    draw_text(10,111,"L", C_BLACK);
    btn_col = right_door? C_RED: C_DGREY;
    fill_rect(300,88,12,16,btn_col);
    draw_rect_outline(300,88,12,16,C_BLACK);
    draw_text(302,92,"D", C_WHITE);
    btn_col = right_light? C_YELLOW: C_DGREY;
    fill_rect(300,108,12,12,btn_col);
    draw_rect_outline(300,108,12,12,C_BLACK);
    draw_text(302,111,"L", C_BLACK);
    if(left_door) { fill_rect(40,122,8,4,C_RED); }
    else fill_rect(40,122,8,4,C_GREEN);
    if(right_door) fill_rect(272,122,8,4,C_RED);
    else fill_rect(272,122,8,4,C_GREEN);
}

static void draw_animatronic_at_door(void){
    if(bonnie_pos==4 || bonnie_pos==5){
        if(left_light && !left_door){
            fill_rect(34,50,6,10, C_BONNIE);
            fill_rect(60,50,6,10, C_BONNIE);
            fill_rect(36,58,28,20, C_BONNIE);
            fill_rect(48,62,6,4, C_WHITE);
            fill_rect(38,78,24,14, C_BONNIE2);
            fill_rect(36,52,2,4, C_BONNIE2);
            fill_rect(62,52,2,4, C_BONNIE2);
            fill_rect(46,68,8,6, C_RED);
            fill_rect(42,64,6,4, C_BLACK);
            fill_rect(52,64,6,4, C_BLACK);
        } else if(bonnie_pos==5 && left_light){
            fill_rect(30,46,42,30, C_BONNIE);
            fill_rect(44,55,6,6, C_WHITE);
            fill_rect(56,55,6,6, C_WHITE);
        }
    }
    if(chica_pos==6 || chica_pos==7){
        if(right_light && !right_door){
            fill_rect(254,50,6,10, C_CHICA);
            fill_rect(280,50,6,10, C_CHICA);
            fill_rect(256,58,28,20, C_CHICA);
            fill_rect(266,66,8,6, C_CHICA_BEAK);
            fill_rect(258,62,6,4, C_BLACK);
            fill_rect(276,62,6,4, C_BLACK);
            fill_rect(262,78,16,10, C_WHITE);
            fill_rect(266,80,8,4, C_YELLOW);
        }
    }
    if(freddy_pos==6 || freddy_pos==7){
        if(right_light && !right_door){
            fill_rect(254,48,38,24, C_FREDDY);
            fill_rect(262,60,22,12, C_MUZZLE);
            fill_rect(266,64,4,4, C_BLACK);
            fill_rect(276,64,4,4, C_BLACK);
            fill_rect(270,70,6,4, C_BLACK);
            fill_rect(264,44,16,6, C_BLACK);
        }
    }
    if(foxy_sprint){
        fill_rect(30,52,40,32, C_FOXY);
        fill_rect(42,60,16,10, C_MUZZLE);
        fill_rect(50,66,8,6, C_BLACK);
        fill_rect(28,70,6,10, C_LGREY);
        fill_rect(38,58,6,6, C_BLACK);
    }
}

static void draw_camera_feed(void){
    // Full-screen authentic camera (320x200) - office hidden
    fill_rect(0,0,320,200,C_BLACK);
    int cam_x=4, cam_y=4, cam_w=312, cam_h=160;
    // white border will be drawn by HUD, but draw faint inner

    int tex = TEX_STAGE;
    // 1:1 mapping: 0=1A 1=1B 2=1C 3=2A 4=2B 5=3 6=4A 7=4B 8=5 9=6 10=7
    switch(current_cam){
        case CAM_1A: tex = TEX_STAGE; break;
        case CAM_1B: tex = TEX_DINING; break;
        case CAM_1C: tex = TEX_PIRATE; break;
        case CAM_2A: // West Hall
            if(foxy_sprint) tex = TEX_WEST_CHICA;
            else if(bonnie_pos==4) tex = TEX_WEST_CHICA;
            else tex = TEX_WEST_HALL;
            break;
        case CAM_2B: tex = TEX_WEST_CHICA; break;
        case CAM_3: tex = TEX_BACKSTAGE; break; // Supply Closet reuses Backstage dark
        case CAM_4A: // East Hall
            if(chica_pos==4 || freddy_pos==4) tex = TEX_EAST_HALL_CHICA;
            else if(rng_range(100) < 2) tex = TEX_HALL_ITS_ME;
            else tex = TEX_EAST_HALL;
            break;
        case CAM_4B: tex = TEX_EAST_HALL_CHICA; break;
        case CAM_5: tex = TEX_BACKSTAGE; break;
        case CAM_6: // Kitchen - audio only, black with text
            tex = TEX_DINING; break; // placeholder, will overlay kitchen text
        case CAM_7: tex = TEX_DINING; break; // Restrooms
        default: tex = TEX_STAGE; break;
    }
    // Kitchen CAM 6 has no video - audio only (pots/pans)
    if(current_cam==CAM_6){
        fnaf_blit_tex_scaled(tex, cam_x+2, cam_y+2, cam_w-4, cam_h-4);
        fill_rect(cam_x+2,cam_y+2,cam_w-4,cam_h-4,C_BLACK);
        draw_text(cam_x+cam_w/2-40,cam_y+40,"- AUDIO ONLY -",C_GREY);
        if(chica_pos==3 || freddy_pos==3){
            draw_text(cam_x+cam_w/2-50,cam_y+60,"* CLANK * POTS PAN *",C_YELLOW);
            if(rng_range(30)==0) draw_text(cam_x+cam_w/2-30,cam_y+70,"movement...",C_WHITE);
        } else {
            draw_text(cam_x+cam_w/2-30,cam_y+60,"(silence)",C_DGREY);
        }
        for(int y=cam_y+2; y<cam_y+cam_h-2; y+=6) draw_hline(cam_x+2, y, cam_w-4, C_DGREY);
    } else if(current_cam==2 && foxy_stage>0){
        fnaf_blit_tex_scaled(tex, cam_x+2, cam_y+2, cam_w-4, cam_h-4);
        if(foxy_stage==1) draw_text(cam_x+cam_w/2-20,cam_y+80,"PEEKING",C_YELLOW);
        else if(foxy_stage==2) draw_text(cam_x+cam_w/2-16,cam_y+80,"STANDS",C_YELLOW);
        else if(foxy_stage==3) draw_text(cam_x+cam_w/2-24,cam_y+80,"!! RUN !!",C_RED);
        // add static lines
        for(int y=cam_y+2; y<cam_y+cam_h-2; y+=6) draw_hline(cam_x+2, y, cam_w-4, C_DGREY);
    } else {
        // blit authentic texture scaled to camera viewport
        fnaf_blit_tex_scaled(tex, cam_x+2, cam_y+2, cam_w-4, cam_h-4);
        // subtle scanlines for CRT effect
        for(int y=cam_y+2; y<cam_y+cam_h-2; y+=4) draw_hline(cam_x+2, y, cam_w-4, C_DGREY);
        // light flicker when animatronic in hall
        if((bonnie_pos==4 && current_cam==4) || (chica_pos==6 && current_cam==6)){
            if(rng_range(50)==0) fill_rect(cam_x+2,cam_y+2,cam_w-4,cam_h-4,C_STATIC1);
        }
    }
    // static speckles
    for(int y=0;y<6;y++){
        if(rng_range(12)==0){
            int x = cam_x+2 + rng_range(cam_w-4);
            int yy = cam_y+2 + rng_range(cam_h-4);
            set_px(x, yy, C_STATIC1);
            set_px(x+1, yy, C_STATIC2);
        }
    }
    // REC dot is drawn in HUD, not here
    // Location name already drawn in HUD centered, remove duplicate bottom label
}

static void draw_camera_map(void){
    // Authentic wireframe map bottom right, transparent over feed
    // Draw white outlines for each room (as in original reference)
    int mx=180, my=120, mw=130, mh=58;
    // semi-transparent dark backing for map
    fill_rect(mx,my,mw,mh,C_BLACK);
    draw_rect_outline(mx,my,mw,mh,C_WHITE);

    // Map layout approx matching FNAF1 building
    int posx[11]={46,46,12,12,8,18,68,68,18,68,42};
    int posy[11]={4,12,12,20,26,20,20,26,8,16,12};
    for(int i=0;i<CAM_COUNT;i++){
        int px=mx+4+posx[i], py=my+12+posy[i];
        uint8_t col = (i==current_cam)? C_YELLOW: C_GREY;
        fill_rect(px,py,5,5,col);
        if(i==CAM_1C && foxy_stage>0) fill_rect(px,py,5,5,C_RED);
        // Bonnie positions to camera mapping
        if(bonnie_pos==0 && i==CAM_1A) fill_rect(px+1,py+1,3,3,C_BONNIE);
        if(bonnie_pos==1 && i==CAM_1B) fill_rect(px+1,py+1,3,3,C_BONNIE);
        if(bonnie_pos==2 && i==CAM_5) fill_rect(px+1,py+1,3,3,C_BONNIE);
        if(bonnie_pos==3 && i==CAM_3) fill_rect(px+1,py+1,3,3,C_BONNIE);
        if(bonnie_pos==4 && i==CAM_2A) fill_rect(px+1,py+1,3,3,C_BONNIE);
        if(bonnie_pos==5 && i==CAM_2B) fill_rect(px+1,py+1,3,3,C_BONNIE);
        if(chica_pos==0 && i==CAM_1A) fill_rect(px+1,py+1,3,3,C_CHICA);
        if(chica_pos==1 && i==CAM_1B) fill_rect(px+1,py+1,3,3,C_CHICA);
        if(chica_pos==2 && i==CAM_7) fill_rect(px+1,py+1,3,3,C_CHICA);
        if(chica_pos==3 && i==CAM_6) fill_rect(px+1,py+1,3,3,C_CHICA);
        if(chica_pos==4 && i==CAM_4A) fill_rect(px+1,py+1,3,3,C_CHICA);
        if(chica_pos==5 && i==CAM_4B) fill_rect(px+1,py+1,3,3,C_CHICA);
        if(freddy_pos==0 && i==CAM_1A) fill_rect(px+1,py+1,3,3,C_FREDDY);
        if(freddy_pos==1 && i==CAM_1B) fill_rect(px+1,py+1,3,3,C_FREDDY);
        if(freddy_pos==2 && i==CAM_7) fill_rect(px+1,py+1,3,3,C_FREDDY);
        if(freddy_pos==3 && i==CAM_6) fill_rect(px+1,py+1,3,3,C_FREDDY);
        if(freddy_pos==4 && i==CAM_4A) fill_rect(px+1,py+1,3,3,C_FREDDY);
        if(freddy_pos==5 && i==CAM_4B) fill_rect(px+1,py+1,3,3,C_FREDDY);
    }
}

static void draw_power_usage(int x,int y){
    char buf[32];
    int p_int = power/100;
    if(p_int<0) p_int=0; if(p_int>99) p_int=99;
    char num[8]; int_to_str(p_int,num);
    char line[24]="Power left: ";
    int n=0; for(int k=0;line[k];k++) {} // placeholder
    // draw "Power left: XX%"
    draw_text(x,y,"Power left:",C_WHITE);
    draw_text(x+96,y,num,C_WHITE);
    draw_text(x+96+ (p_int>=10?16:8),y,"%",C_WHITE);
    // Usage: green bars 1-5
    draw_text(x,y+10,"Usage:",C_WHITE);
    int usage = 1 + left_door + right_door + left_light + right_light + camera_up;
    if(power_out) usage=1;
    if(usage>5) usage=5;
    for(int i=0;i<usage;i++){
        fill_rect(x+48+i*10, y+10, 8, 8, C_GREEN);
        draw_rect_outline(x+48+i*10, y+10, 8, 8, C_BLACK);
    }
    for(int i=usage;i<5;i++){
        draw_rect_outline(x+48+i*10, y+10, 8, 8, C_GREY);
    }
}
static void draw_time_top_right(void){
    const char* hour_names[7]={"12 AM","1 AM","2 AM","3 AM","4 AM","5 AM","6 AM"};
    const char* t = hour_names[fnaf_hour];
    int len=0; while(t[len]) len++;
    draw_text(320 - len*8 - 8, 6, t, C_WHITE);
    if(fnaf_state==ST_PLAYING && !power_out){
        char night_s[16]="Night ";
        night_s[6]='0'+fnaf_night; night_s[7]=0;
        int nlen=0; while(night_s[nlen]) nlen++;
        draw_text(320 - nlen*8 - 8, 16, night_s, C_GREY);
    }
}
static void draw_topbar(void){
    // No longer used as separate bar - HUD is overlayed
}
static void draw_controls(void){
    // Auth controls handled inside office/camera HUD
}
static void draw_office_hud(void){
    draw_power_usage(10, 180);
    draw_time_top_right();
    // Subtle vignette on office
    // Door buttons already drawn in draw_office_walls
}
static void draw_camera_hud(void){
    // Authentic FNAF HUD: white outer border, REC top left, time top right
    draw_rect_outline(4,4,312,192,C_WHITE);
    // Inner feed border already
    // Red REC dot top left (solid, blinks)
    if((uptime_ticks/600)%2==0){
        fill_rect(14,10,12,12,C_RED);
        draw_rect_outline(14,10,12,12,C_BLACK);
        draw_text(30,12,"REC",C_WHITE);
    }
    // Location name centered slightly above map (like real: middle of feed)
    const char* loc_names[11]={"Show Stage","Dining Area","Pirate Cove","West Hall","W. Hall Corner","Supply Closet","East Hall","E. Hall Corner","Backstage","Kitchen","Restrooms"};
    const char* loc = loc_names[current_cam];
    int l=0; while(loc[l]) l++;
    // Slight offset from center to avoid scanlines
    draw_text(160 - l*4, 92, loc, C_WHITE);
    draw_power_usage(10, 180);
    draw_time_top_right();
    // Scanlines overlay subtle
    for(int y=4;y<196;y+=6) draw_hline(5,y,310,C_DGREY);
}


static void draw_menu_screen(void){
    fill_rect(0,0,320,200,C_BLACK2);
    draw_text_center(16,"FIVE NIGHTS",C_WHITE);
    draw_text_center(26,"AT SHARKY'S",C_YELLOW);
    draw_text_center(36,"By SharkOS",C_GREY);
    fill_rect(130,50,60,50,C_FREDDY);
    fill_rect(140,65,40,30,C_MUZZLE);
    fill_rect(138,64,8,8,C_WHITE); fill_rect(174,64,8,8,C_WHITE);
    fill_rect(140,66,4,4,C_BLACK); fill_rect(176,66,4,4,C_BLACK);
    fill_rect(148,80,24,6,C_BLACK);
    fill_rect(130,46,60,8,C_BLACK);
    fill_rect(140,46,40,4,C_RED);
    draw_text_center(110,"SELECT NIGHT",C_LGREY);
    for(int n=1;n<=7;n++){
        int by=122 + (n-1)*10;
        char line[24]="> Night X";
        line[2]='0'+n;
        uint8_t col = (n==fnaf_night)? C_YELLOW: C_GREY;
        if(n==fnaf_night){
            fill_rect(80,by-1,160,10,C_DGREY);
            line[0]='>'; line[1]=' ';
        } else {
            line[0]=' '; line[1]=' ';
        }
        int tx=100; draw_text(tx,by,line,col);
        char ai[16]="*";
        int diff=n;
        for(int i=0;i<diff && i<5;i++) ai[i]='*'; ai[diff]=0;
        draw_text(160,by,ai,C_RED);
    }
    draw_text_center(188,"ENTER: Start  Q/E: Choose  ESC: Quit",C_GREY);
    if((uptime_ticks/500)%2==0){
        draw_text_center(116+60,"PRESS ENTER TO BEGIN",C_GREEN);
    }
}

static void draw_jumpscare(void){
    uint32_t elapsed = uptime_ticks - jumpscare_start;
    if(elapsed<1400){
        int tex = TEX_FREDDY_JUMP;
        if(jumpscare_type==1) tex = TEX_CHICA_JUMP; // Bonnie uses Chica office jump as placeholder (both in doorway)
        else if(jumpscare_type==2) tex = TEX_CHICA_JUMP;
        else if(jumpscare_type==3) tex = TEX_FREDDY_JUMP;
        else if(jumpscare_type==4) tex = TEX_FREDDY_JUMP; // Foxy uses Freddy hall run
        // flicker between texture and black
        if((elapsed/70)%2==0){
            fnaf_blit_tex_scaled(tex, 0, 0, 320, 200);
        } else {
            fill_rect(0,0,320,200,C_BLACK);
        }
        if((elapsed/40)%2==0) for(int y=0;y<200;y+=3) draw_hline(0,y,320,C_STATIC1);
    }
}

static void draw_gameover(void){
    fill_rect(0,0,320,200,C_BLACK);
    if(jumpscare_type) draw_text_center(60,"JUMPSCARE!",C_RED);
    draw_text_center(90,"GAME OVER",C_WHITE);
    draw_text_center(110,"You didn't survive the night",C_GREY);
    char line[32]="Night X";
    line[6]='0'+fnaf_night;
    draw_text_center(130,line,C_YELLOW);
    if((uptime_ticks/400)%2==0) draw_text_center(160,"Press ENTER to retry  ESC: Menu",C_GREEN);
}

static void draw_win(void){
    fill_rect(0,0,320,200,C_BLACK);
    draw_text_center(50,"6 AM",C_YELLOW);
    draw_text_center(70,"YOU SURVIVED!",C_WHITE);
    char line[24]="Night X Complete";
    line[6]='0'+fnaf_night;
    draw_text_center(90,line,C_GREEN);
    if(fnaf_night<7){
        draw_text_center(120,"Get ready for next shift...",C_GREY);
    } else {
        draw_text_center(120,"YOU BEAT THE GAME!",C_YELLOW);
        draw_text_center(132,"All nights cleared",C_WHITE);
    }
    if((uptime_ticks/400)%2==0) draw_text_center(170,"Press ENTER to continue",C_GREEN);
}

static void update_power(void){
    if(fnaf_state!=ST_PLAYING) return;
    if(power<=0){
        if(!power_out){
            power_out=1;
            power_out_phase=0;
            power_out_phase_start=uptime_ticks;
            left_door=0; right_door=0; left_light=0; right_light=0; camera_up=0;
            // doors forced open, lights dead
            left_door_jammed=1; right_door_jammed=1; left_light_jammed=1; right_light_jammed=1;
        }
        return;
    }
    // Real drain: every 100ms: (usage + nightly)/10
    // usage = count of (left door, right door, cam, left light, right light) capped 4, min 1 when none
    if(uptime_ticks - last_power_tick >= 100){
        last_power_tick = uptime_ticks;
        int active = left_door + right_door + left_light + right_light + camera_up;
        if(active>4) active=4;
        if(active==0) active=1; // idle still drains 1
        // nightly debuff: 0.1 / divisor, divisor per night: 1:9.6 2:6 3:5 4:4 5+:3
        int divisor;
        if(fnaf_night==1) divisor=96; // 9.6*10 to keep integer math
        else if(fnaf_night==2) divisor=60;
        else if(fnaf_night==3) divisor=50;
        else if(fnaf_night==4) divisor=40;
        else divisor=30;
        // power is 0..10000 (99.9% = 9990)
        // per 100ms drain = (active*100 + (100/divisor))/10  -> to keep integer
        // Simpler: active*10 + (10/divisor*10) ??? Let's compute float then convert: nightly per 100ms = (0.1/divisor)/10*10000 ??? 
        // Research formula: total % per sec = (active*0.1 + 0.1/divisor) ; per 0.1s = that/10
        // So drain (in 0.01% units where 10000=100%) per 100ms = (active*10 + 10/divisor) 
        // active 1 => 10, nightly night1 10/96≈0 -> but need 0.1/9.6=0.0104% per 0.1s => 1.04 units (since 10000=100%)
        // Use fixed: nightly_units = 100/divisor  (since 0.1/divisor*10000/10 =100/divisor)
        int nightly_units = 100 / (divisor/10); // divisor 96 => 9.6 => 100/9.6≈10
        // Actually compute nightly_units = 1000/divisor
        nightly_units = 1000 / divisor; // 1000/96≈10, 1000/60≈16, 1000/30≈33
        int drain = active*10 + nightly_units;
        // drain is in 0.01% per 100ms? Let's test: active1 night1 =>10+10=20 => 0.2% per sec? Expected 0.2104% per sec -> per 100ms 0.021% => 2.1 units, we have 20 units 0.2% per 100ms => 2% per sec too high 10x
        // Need scale: drain should be ~2 units per 100ms for idle. So divide by 10
        drain = (active*10 + nightly_units + 5)/10; // ~2 for idle
        if(drain<1) drain=1;
        power -= drain;
        if(power<0) power=0;
    }
}

static void update_hours(void){
    if(fnaf_state!=ST_PLAYING) return;
    if(power_out){
        // 1:1 power-out Freddy phases: 20% every 5s up to 20s to appear with music at left door, then 20% every 5s up to 20s song, then 20% every 2s jumpscare
        uint32_t elapsed = uptime_ticks - power_out_phase_start;
        if(power_out_phase==0){
            // waiting for Freddy to appear at left door (music)
            if(elapsed >= 5000){
                power_out_phase_start = uptime_ticks;
                int roll = rng_range(5); // 0..4 => 20% is 0
                if(roll==0 || elapsed >= 20000){
                    power_out_phase=1;
                    freddy_inside=0;
                    // lights flicker handled in render
                }
            }
        } else if(power_out_phase==1){
            if(elapsed >= 5000){
                power_out_phase_start = uptime_ticks;
                int roll = rng_range(5);
                if(roll==0 || elapsed >= 20000){
                    power_out_phase=2;
                }
            }
        } else if(power_out_phase==2){
            if(elapsed >= 2000){
                power_out_phase_start = uptime_ticks;
                int roll = rng_range(5);
                if(roll==0){
                    trigger_jumpscare(3);
                }
            }
            // also win check still
            if(uptime_ticks - last_hour_tick >= 89000){
                // still clock advances even in power out - can win
                last_hour_tick = uptime_ticks;
                fnaf_hour++;
                if(fnaf_hour>=6){ fnaf_state=ST_WIN; win_time=uptime_ticks; }
            }
        }
        return;
    }
    // Clock: 12AM lasts 90s, 1AM-6AM each 89s => total 535s
    uint32_t need = (fnaf_hour==0) ? 90000 : 89000;
    if(uptime_ticks - last_hour_tick >= need){
        last_hour_tick = uptime_ticks;
        fnaf_hour++;
        update_ai_boosts();
        if(fnaf_hour>=6){
            fnaf_state=ST_WIN;
            win_time=uptime_ticks;
        }
    }
}

// --- 1:1 movement paths ---
static int bonnie_move_1to1(void){
    // Bonnie chooses random anywhere on left side (not necessarily adjacent)
    // Possible left side rooms for Bonnie: 1A(0),1B(1),5(8),3(5),2A(3),2B(4)
    // But implement exact random table from technicalFNaF:
    // From each pos, random choice:
    if(bonnie_pos==0){ // 1A always to 1B
        return 1;
    } else if(bonnie_pos==1){ // 1B -> 5 or 3 or 2A (random 1..3)
        int r=rng_range(3);
        if(r==0) return 2; // 5
        if(r==1) return 3; // 3
        return 4; // 2A
    } else if(bonnie_pos==2){ // 5 Backstage -> 1B or 2A
        return rng_range(2)==0 ? 1 : 4;
    } else if(bonnie_pos==3){ // 3 Supply Closet -> 1B or 2A
        return rng_range(2)==0 ? 1 : 4;
    } else if(bonnie_pos==4){ // 2A West Hall -> 3 or 2B (or to door)
        int r=rng_range(3);
        if(r==0) return 3;
        if(r==1) return 5;
        return 6; // door
    } else if(bonnie_pos==5){ // 2B West Corner -> to door
        return 6;
    } else if(bonnie_pos==6){ // Door -> will attempt entry handled elsewhere
        return 6;
    }
    return bonnie_pos;
}
static int chica_move_1to1(void){
    // Chica adjacent random on right side
    if(chica_pos==0){ // 1A -> 1B
        return 1;
    } else if(chica_pos==1){ // 1B -> 7 or 6
        return rng_range(2)==0 ? 2 : 3;
    } else if(chica_pos==2){ // 7 Restrooms -> 6 or 4A
        return rng_range(2)==0 ? 3 : 4;
    } else if(chica_pos==3){ // 6 Kitchen -> 7 or 4A
        return rng_range(2)==0 ? 2 : 4;
    } else if(chica_pos==4){ // 4A East Hall -> 1B or 5(4B)
        return rng_range(2)==0 ? 1 : 5;
    } else if(chica_pos==5){ // 4B East Corner -> to door
        return 6;
    } else if(chica_pos==6){ // Door
        return 6;
    }
    return chica_pos;
}
static int freddy_move_1to1(void){
    // Deterministic forward only
    if(freddy_pos==0) return 1; //1A->1B
    if(freddy_pos==1) return 2; //1B->7
    if(freddy_pos==2) return 3; //7->6
    if(freddy_pos==3) return 4; //6->4A
    if(freddy_pos==4) return 5; //4A->4B
    if(freddy_pos==5) return 6; //4B->door
    return freddy_pos;
}

static void ai_move_logic(void){
    if(fnaf_state!=ST_PLAYING) return;
    if(power_out) return;
    // Bonnie every 4.97s
    if(uptime_ticks - last_bonnie_tick >= 4970){
        last_bonnie_tick = uptime_ticks;
        int roll = rng_range(20)+1;
        if(roll <= ai_bonnie){
            if(bonnie_inside){
                // already inside office -> will be handled in check_jumpscares
            } else if(bonnie_pos==6){
                // at left door, next success attempts to enter
                if(left_door){
                    // blocked, go back to dining (1B)
                    bonnie_pos = 1;
                } else {
                    // enter office
                    bonnie_inside=1;
                    left_door_jammed=1; left_light_jammed=1;
                    office_entry_time=uptime_ticks;
                    bonnie_door_time=uptime_ticks;
                }
            } else {
                // normal move
                int np = bonnie_move_1to1();
                // cannot leave stage if Freddy blocked? In real, Freddy blocked unless Bonnie/Chica leave, handled in freddy logic
                bonnie_pos = np;
            }
        }
    }
    // Chica every 4.98s
    if(uptime_ticks - last_chica_tick >= 4980){
        last_chica_tick = uptime_ticks;
        int roll = rng_range(20)+1;
        if(roll <= ai_chica){
            if(chica_inside){
            } else if(chica_pos==6){
                if(right_door){
                    chica_pos = 1;
                } else {
                    chica_inside=1;
                    right_door_jammed=1; right_light_jammed=1;
                    office_entry_time=uptime_ticks;
                    chica_door_time=uptime_ticks;
                }
            } else {
                chica_pos = chica_move_1to1();
            }
        }
    }
    // Freddy every 3.02s with camera stall and countdown
    if(uptime_ticks - last_freddy_tick >= 3020){
        last_freddy_tick = uptime_ticks;
        // Camera stall: if cameras up and not at 4B (pos5), fail
        // If at 4B (pos5), only viewing 4B stalls, else viewing any cam not 4B allows entry
        int cam_stalls = 0;
        if(camera_up){
            if(freddy_pos==5){ // 4B
                if(current_cam==7) cam_stalls=1; // viewing 4B
            } else {
                cam_stalls=1; // any camera stalls when not at 4B
            }
        }
        if(cam_stalls){
            // fail, but also if freddy already counting down? countdown continues
        } else {
            int roll = rng_range(20)+1;
            if(roll <= ai_freddy){
                // success starts countdown (1000 -100*AI) at 60fps => convert to ms: frames/60*1000
                int frames = 1000 - 100*ai_freddy;
                if(frames<0) frames=0;
                int delay_ms = (frames * 1000) / 60;
                if(freddy_countdown==0){
                    freddy_countdown = delay_ms;
                    // start timer - we will count down in foxy_update? Use separate tick
                    // Store countdown start
                    last_freddy_tick = uptime_ticks; // will handle countdown in separate logic
                }
            }
        }
        // Also handle Freddy blocked by Bonnie/Chica on stage: cannot leave 1A if they are still at 1A
        // Real: Freddy cannot leave stage while Bonnie and/or Chica are still there
        // Implement: if freddy_pos==0 and (bonnie_pos==0 || chica_pos==0) then cancel move
    }
    // Freddy countdown handling (separate fast tick 16ms)
    if(freddy_countdown>0){
        // decrement by ~16ms per tick? Use uptime diff
        // Approximate: reduce by elapsed since last check (we are at 3s intervals, need finer)
        // Instead handle in per-tick: if countdown>0 and cameras down, count down
        // For simplicity, check if cameras down and not stalled
        int can_count = !camera_up;
        if(freddy_pos==5) can_count = (current_cam!=7); // at 4B, viewing 4B blocks even countdown
        if(can_count){
            freddy_countdown -= 16;
            if(freddy_countdown<=0){
                freddy_countdown=0;
                // perform move
                if(freddy_inside){
                } else if(freddy_pos==6){
                    if(right_door){
                        freddy_pos=4; // back to 4A if blocked
                    } else {
                        freddy_inside=1;
                        right_door_jammed=1;
                        office_entry_time=uptime_ticks;
                    }
                } else {
                    // check stage block
                    if(freddy_pos==0 && (bonnie_pos==0 || chica_pos==0)){
                        // blocked
                    } else {
                        freddy_pos = freddy_move_1to1();
                    }
                }
            }
        }
    }
}

static void foxy_update(void){
    if(fnaf_state!=ST_PLAYING) return;
    if(power_out) return;
    // Foxy 5.01s opportunities, but while cameras up always fail
    if(foxy_sprint){
        if(uptime_ticks - foxy_sprint_timer > 1200){
            if(left_door){
                foxy_stage=0; foxy_sprint=0; foxy_timer=0; foxy_stall=0;
                foxy_attack_pending=0;
            } else {
                trigger_jumpscare(4);
            }
            foxy_sprint=0;
        }
        return;
    }
    // Handle stall timer after camera down
    if(foxy_stall>0){
        foxy_stall -= (int)(uptime_ticks - last_foxy_tick2);
        last_foxy_tick2 = uptime_ticks;
        if(foxy_stall<=0) foxy_stall=0;
        else return; // cannot move while stalling
    }
    if(uptime_ticks - last_foxy_tick < 5010) return;
    last_foxy_tick = uptime_ticks;
    last_foxy_tick2 = uptime_ticks;
    // If any camera is up, fail always and also reset stall? Real: while cameras up fail, and after cameras down random 50..1050 ticks (0.83-16.67s) stall
    if(camera_up){
        // fail, and do not advance
        return;
    }
    // If foxy not at 3 yet, check if we just came from cameras up -> set stall
    // Real: when monitor lowered, random 50-1050 ticks at 60fps => 833ms to 17500ms
    // We implement: if previous was camera_up, set random stall
    // For simplicity, after each camera close, set stall if not already
    // But our check is after 5s, so we need to detect transition: use camera_up previous? Keep simple: after cameras down, 1 in N chance to stall
    // Instead: on camera close, set foxy_stall = 50 + rng%1000 frames => convert to ms
    // We'll set stall when we detect camera was just closed: we need state
    // Alternative: mimic real by randomizing stall each time cameras go down: handled in fnaf_handle_key/mouse where camera_up toggles
    int roll = rng_range(20)+1;
    if(roll <= ai_foxy){
        foxy_stage++;
        if(foxy_stage>3) foxy_stage=3;
        if(foxy_stage==3){
            foxy_sprint=1;
            foxy_sprint_timer=uptime_ticks;
        }
    }
}
// Called when cameras are closed to set stall
static void foxy_set_stall(void){
    int frames = 50 + rng_range(1000); // 50..1050
    foxy_stall = (frames * 1000) / 60;
}


static uint32_t bonnie_attack_start=0;
static uint32_t chica_attack_start=0;
static uint32_t freddy_attack_start=0;
static uint32_t freddy_door_time=0;
static int menu_last_night=-1;
static uint32_t menu_last_click=0;

static void check_jumpscares2(void){
    if(fnaf_state!=ST_PLAYING) return;
    if(power_out) return;
    uint32_t now=uptime_ticks;
    // Bonnie/Chica in office: jams door, waits for camera down, then jumpscare next camera down OR 30s auto
    if(bonnie_inside){
        if(camera_up){
            // if in camera, will jumpscare after coming down
            // check 30s auto even with camera up
            if(now - office_entry_time > 30000){
                trigger_jumpscare(1);
            }
        } else {
            // camera down and Bonnie inside -> jumpscare after small delay (real: instant next frame)
            if(now - office_entry_time > 300){
                trigger_jumpscare(1);
            }
        }
        return;
    }
    if(chica_inside){
        if(camera_up){
            if(now - office_entry_time > 30000) trigger_jumpscare(2);
        } else {
            if(now - office_entry_time > 300) trigger_jumpscare(2);
        }
        return;
    }
    if(freddy_inside){
        // Freddy only jumpscares when cameras down, 25% per second
        if(!camera_up){
            if(now - freddy_door_time > 1000){
                freddy_door_time = now;
                if(rng_range(4)==0) trigger_jumpscare(3);
            }
        }
        return;
    }
    // Foxy sprint already handled in foxy_update (needs left door)
    // Check if Freddy at door (pos6) and should enter (handled in ai logic), but if inside door jam case for Foxy
}
static void render(void){
    if(fnaf_state==ST_MENU){
        draw_menu_screen();
        return;
    }
    if(fnaf_state==ST_JUMPSCARE){
        draw_jumpscare();
        if(uptime_ticks - jumpscare_start > 1800){
            fnaf_state=ST_GAMEOVER;
            gameover_time=uptime_ticks;
        }
        return;
    }
    if(fnaf_state==ST_GAMEOVER){
        draw_gameover();
        return;
    }
    if(fnaf_state==ST_WIN){
        draw_win();
        return;
    }
    if(power_out){
        fill_rect(0,0,320,200,C_BLACK);
        if((uptime_ticks/500)%2==0){
            draw_office_walls();
            draw_text_center(80,"* POWER OUT *",C_RED);
        } else {
            fill_rect(0,0,320,200,C_BLACK);
        }
        draw_camera_hud();
        draw_text_center(100,"Emergency lights failed",C_GREY);
        draw_text_center(115,"Freddy is coming...",C_RED);
        if(power_out_phase==1) draw_text_center(130,"* music *",C_YELLOW);
        return;
    }
    if(camera_up){
        draw_camera_feed();
        draw_camera_map();
        draw_camera_hud();
    } else {
        draw_office_walls();
        draw_animatronic_at_door();
        if(left_light || right_light){
            // light beam overlay on doorway already handled in draw_office_walls
            if(rng_range(60)==0) flash=4;
            if(flash>0){ flash--; fill_rect(0,0,320,200,C_STATIC1); }
        }
        draw_office_hud();
    }
}

void fnaf_set_window_rect(int x,int y,int w,int h){
    window_x=x; window_y=y; window_w=w; window_h=h;
}

void fnaf_init(void){
    running=1;
    build_pal();
    memset(fnaf_screen,0,sizeof(fnaf_screen));
    fnaf_state=ST_MENU;
    fnaf_night=1;
    reset_positions();
    rng_state = uptime_ticks ^ 0xA5A5A5A5;
    if(rng_state==0) rng_state=0x12345678;
}

void fnaf_cleanup(void){
    running=0;
}

void fnaf_tick(void){
    if(!running) return;
    update_power();
    update_hours();
    ai_move_logic();
    foxy_update();
    check_jumpscares2();
    render();
    blit();
}

void fnaf_draw_frame(void){
    render();
    blit();
}

void fnaf_handle_key(int key){
    if(fnaf_state==ST_MENU){
        if(key=='q' || key=='Q'){
            fnaf_night--; if(fnaf_night<1) fnaf_night=1;
        } else if(key=='e' || key=='E'){
            fnaf_night++; if(fnaf_night>7) fnaf_night=7;
        } else if(key=='\n' || key==' '){
            fnaf_state=ST_PLAYING;
            reset_positions();
            fnaf_hour=0;
            bonnie_attack_start=chica_attack_start=freddy_attack_start=0;
            last_hour_tick=uptime_ticks;
            last_power_tick=uptime_ticks;
            last_ai_tick=uptime_ticks;
            last_foxy_tick=uptime_ticks;
        } else if(key==27){
        }
        return;
    }
    if(fnaf_state==ST_GAMEOVER){
        if(key=='\n' || key==' '){
            fnaf_state=ST_MENU;
            reset_positions();
        } else if(key==27){
            fnaf_state=ST_MENU;
        }
        return;
    }
    if(fnaf_state==ST_WIN){
        if(key=='\n' || key==' '){
            if(fnaf_night<7) fnaf_night++;
            fnaf_state=ST_MENU;
            reset_positions();
        } else if(key==27){
            fnaf_state=ST_MENU;
        }
        return;
    }
    if(fnaf_state==ST_JUMPSCARE){
        return;
    }
    if(fnaf_state==ST_PLAYING){
        if(key=='q' || key=='Q'){
            if(power_out || left_door_jammed) {} else {
                left_door = !left_door;
                if(left_door) left_light=0;
            }
        } else if(key=='e' || key=='E'){
            if(power_out || right_door_jammed) {} else {
                right_door = !right_door;
                if(right_door) right_light=0;
            }
        } else if(key=='a' || key=='A'){
            if(!power_out) left_light = !left_light;
            if(left_light) right_light=0;
            if(left_light) camera_up=0;
        } else if(key=='d' || key=='D'){
            if(!power_out && !right_light_jammed) right_light = !right_light;
            if(right_light) left_light=0;
            if(right_light) camera_up=0;
        } else if(key=='c' || key=='C' || key==' '){
            if(!power_out){
                {
                    int was_up = camera_up;
                    camera_up = !camera_up;
                    if(camera_up){ left_light=0; right_light=0; }
                    else if(was_up){ foxy_set_stall(); }
                }
            }
        } else if(key>='1' && key<='9'){
            if(camera_up){
                current_cam = key-'1';
                if(current_cam>=CAM_COUNT) current_cam=CAM_COUNT-1;
            }
        } else if(key=='0'){
            if(camera_up) current_cam = 9; // Kitchen
        } else if(key=='-' || key=='_'){
            if(camera_up) current_cam = 10; // Restrooms
        } else if(key=='\t'){
            if(camera_up){
                current_cam = (current_cam+1)%CAM_COUNT;
            }
        } else if(key==27){
            if(camera_up){ camera_up=0; }
            else { fnaf_state=ST_MENU; camera_up=0; }
        }
    }
}

void fnaf_handle_mouse(int mx, int my, int buttons){
    // Office panning based on mouse X within window (authentic 1600px pan effect)
    {
        int lx = mx - window_x;
        if(lx>=0 && lx<window_w && fnaf_state==ST_PLAYING && !camera_up){
            int fx = lx * 320 / (window_w>0?window_w:320);
            if(fx < 80) office_pan = 0;
            else if(fx > 240) office_pan = 80;
            else office_pan = 40;
        }
    }
    if(!(buttons & 1)) return;
    int lx = mx - window_x;
    int ly = my - window_y;
    if(lx<0 || ly<0 || lx>=window_w || ly>=window_h) return;
    int fx = lx * 320 / (window_w>0?window_w:320);
    int fy = ly * 200 / (window_h>0?window_h:200);
    if(fx<0) fx=0; if(fx>=320) fx=319;
    if(fy<0) fy=0; if(fy>=200) fy=199;

    if(fnaf_state==ST_MENU){
        for(int n=1;n<=7;n++){
            int by=122 + (n-1)*10;
            if(fy>=by-1 && fy<by+9 && fx>=80 && fx<240){
                fnaf_night=n;
                if(menu_last_night==n && uptime_ticks - menu_last_click < 400){
                    fnaf_state=ST_PLAYING;
                    reset_positions();
                    fnaf_hour=0;
                    bonnie_attack_start=chica_attack_start=freddy_attack_start=0;
                    last_hour_tick=uptime_ticks;
                    last_power_tick=uptime_ticks;
                    last_ai_tick=uptime_ticks;
                    last_foxy_tick=uptime_ticks;
                }
                menu_last_night=n; menu_last_click=uptime_ticks;
                break;
            }
        }
        if(fy>116-5 && fy<116+5 && fx>80 && fx<240) {
        }
        return;
    }
    if(fnaf_state==ST_GAMEOVER || fnaf_state==ST_WIN){
        fnaf_state=ST_MENU;
        reset_positions();
        return;
    }
    if(fnaf_state==ST_PLAYING){
        if(power_out){
            return;
        }
        if(camera_up){
            if(fy>=188 && fy<198){
                for(int i=0;i<CAM_COUNT;i++){
                    int bx2 = 4 + i*28;
                    if(fx>=bx2 && fx<bx2+26){
                        current_cam=i;
                        return;
                    }
                }
            }
            if(fy>=170 && fy<188 && fx>=100 && fx<220){
                if(camera_up) foxy_set_stall();
                camera_up=0; return;
            }
            return;
        } else {
            if(fx>=8 && fx<20 && fy>=88 && fy<104){
                if(power_out || left_door_jammed) return;
                left_door=!left_door;
                if(left_door) left_light=0;
                return;
            }
            if(fx>=8 && fx<20 && fy>=108 && fy<120){
                if(left_light_jammed) return;
                left_light=!left_light;
                if(left_light){ right_light=0; if(camera_up) foxy_set_stall(); camera_up=0; }
                return;
            }
            if(fx>=300 && fx<312 && fy>=88 && fy<104){
                if(power_out || right_door_jammed) return;
                right_door=!right_door;
                if(right_door) right_light=0;
                return;
            }
            if(fx>=300 && fx<312 && fy>=108 && fy<120){
                if(right_light_jammed) return;
                right_light=!right_light;
                if(right_light){ left_light=0; if(camera_up) foxy_set_stall(); camera_up=0; }
                return;
            }
            if(fx>=100 && fx<220 && fy>=170 && fy<188){
                {
                    int was_up = camera_up;
                    camera_up=!camera_up;
                    if(camera_up){ left_light=0; right_light=0; }
                    else if(was_up) foxy_set_stall();
                }
                return;
            }
            if(fx>=28 && fx<73 && fy>=38 && fy<122){
                left_light=!left_light;
                if(left_light){ right_light=0; if(camera_up) foxy_set_stall(); camera_up=0; }
                return;
            }
            if(fx>=248 && fx<293 && fy>=38 && fy<122){
                right_light=!right_light;
                if(right_light){ left_light=0; if(camera_up) foxy_set_stall(); camera_up=0; }
                return;
            }
        }
    }
}

void fnaf_set_kernel_mode(void){}
void fnaf_restore_kernel_mode(void){
    if(current_kernel_mode==KERNEL_MODE_DESKTOP){ desktop.dirty=true; return; }
    terminal_initialize(); redraw_all_panes(); print_prompt();
}
bool fnaf_is_active(void){ return running && fnaf_state!=ST_MENU; }
