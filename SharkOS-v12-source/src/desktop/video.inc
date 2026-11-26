/* QEMU/Bochs stdvga, only when BAR0 is the framebuffer we actually booted on. */
static const struct {unsigned x,y;const char *name;} modes[]={
 {800,600,"800x600"},{1024,768,"1024x768"},{1280,720,"1280x720"},{1600,900,"1600x900"},{1920,1080,"1920x1080"}};
#define NMODES 5
static bool dispi,preview;
static unsigned oldx,oldy; static uint32_t until,vram;

static const char *vmsg="Firmware framebuffer: live switching unavailable.";
static uint16_t vread(unsigned idx){outw(0x1ce,idx);return inw(0x1cf);}
static void vwrite(unsigned idx,unsigned value){outw(0x1ce,idx);outw(0x1cf,value);}
static void program(unsigned x,unsigned y){
    vwrite(4,0);vwrite(1,x);vwrite(2,y);vwrite(3,32);vwrite(4,0x41);
    vwrite(6,x);vwrite(8,0);vwrite(9,0);
}
static bool switch_mode(unsigned x,unsigned y){
    if(!dispi || x>1920 || y>1080 || !x || !y || x*y*4>vram)return false;
    unsigned ox=screen_width,oy=screen_height;mouse_restore_under_cursor();
    uint32_t *video_shadow=pmm_shadow((size_t)x*y*4);if(!video_shadow)return false;
    /* The old framebuffer contents were copied when the buffer moved. */
    if(lfbptr!=hw_lfbptr)lfbptr=video_shadow;
    program(x,y);
    if(vread(1)!=x || vread(2)!=y || vread(3)!=32 || vread(6)!=x || !(vread(4)&1)){
        program(ox,oy);desktop.dirty=true;return false;
    }
    lfbptr=video_shadow;screen_width=x;screen_height=y;screen_pitch=x*4;ui_init_metrics();
    if(mouse_cursor_x>=(int)x)mouse_cursor_x=x-1;if(mouse_cursor_y>=(int)y)mouse_cursor_y=y-1;
    for(int i=0;i<desktop.window_count;i++){
        window_t *w=desktop.windows+i;
        int maxh=y-TASKBAR_HEIGHT;
        if(w->rect.width>(int)x)w->rect.width=x;if(w->rect.height>maxh)w->rect.height=maxh;
        if(w->rect.x+w->rect.width>(int)x)w->rect.x=x-w->rect.width;
        if(w->rect.y+w->rect.height>maxh)w->rect.y=maxh-w->rect.height;
        window_update_client_rect(w);w->needs_redraw=true;
    }
    desktop_layout_icons();tiling_arrange();desktop.flush_partial=false;desktop.dirty=true;wallpaper_dirty=true;
    serial_puts("[video] mode=");serial_putdec(x);serial_puts("x");serial_putdec(y);serial_puts("\n");return true;
}
void video_init(void){
    for(unsigned bus=0;bus<256 && !dispi;bus++)for(unsigned s=0;s<32 && !dispi;s++){
        if(pci_config_read(bus,s,0,0)!=0x11111234 || (pci_config_read(bus,s,0,8)>>16)!=0x0300)continue;
        unsigned bar=pci_config_read(bus,s,0,0x10);if((bar&1) || !(bar&~15u) || (uintptr_t)hw_lfbptr!=(bar&~15u))continue;
        unsigned id=vread(0);if(id<0xb0c4 || id>0xb0c5 || vread(3)!=32 || vread(1)!=screen_width || vread(2)!=screen_height || vread(6)!=screen_width || screen_pitch!=screen_width*4 || vread(8) || vread(9) || screen_width>1920 || screen_height>1080)continue;
        vram=(uint32_t)vread(10)<<16; dispi=vram>=screen_pitch*screen_height;
    }
    if(!dispi)return;
    vmsg="Choose a mode. Keep it within 15 seconds.";
    struct fs_node *f=store_installed()?fs_resolve_path("/System/display.mode"):NULL;
    if(f && f->type==FS_FILE && f->content_len==1){unsigned i=(uint8_t)f->content[0];if(i<NMODES && (screen_width!=modes[i].x || screen_height!=modes[i].y))switch_mode(modes[i].x,modes[i].y);}
}
static void revert_mode(void){if(preview){if(switch_mode(oldx,oldy))vmsg="Previous resolution restored.";else vmsg="Could not restore mode.";preview=false;}}
void video_tick(void){if(preview && (int32_t)(uptime_ticks-until)>=0)revert_mode();}
static void video_action(const char *arg){
    if(!dispi)return;
    if(!strcmp(arg,"revert"))revert_mode();
    else if(!strcmp(arg,"keep") && preview){
        preview=false;vmsg="Resolution kept for this session.";
        if(store_installed()){
            struct fs_node *d=fs_resolve_path("/System");struct fs_node *f=d?find_node(d,"display.mode"):NULL;
            if(d && d->type==FS_DIRECTORY && !f)f=create_node("display.mode",FS_FILE,d);
            uint8_t i=0;while(i<NMODES && modes[i].x!=screen_width)i++;
            if(i<NMODES && f && !f->disk_backed && !fs_node_write(f,0,&i,1) && !fs_node_truncate(f,1) && store_sync())vmsg="Resolution saved.";
        }
    } else if(arg[0]){
        int i=0;while(i<NMODES && strcmp(modes[i].name,arg))i++;
        if(i==NMODES){vmsg="Use video WIDTHxHEIGHT, video keep, or video revert.";return;}
        if(!preview){oldx=screen_width;oldy=screen_height;}
        if(switch_mode(modes[i].x,modes[i].y)){preview=true;until=uptime_ticks+15*TICKS_PER_SEC;vmsg="Preview: Keep or revert within 15 seconds.";}
        else vmsg="Mode unavailable: memory or adapter limit.";
    }
    desktop.dirty=true;
}
const char *video_status(void){return vmsg;}
bool video_available(void){return dispi;}
bool video_preview(void){return preview;}

void video_choose(const char *name){video_action(name);}
void video_command(const char *arg){video_action(arg);terminal_writestring(vmsg);terminal_putchar('\n');}
