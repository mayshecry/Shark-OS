/* JawsWM: floating desktop, clip, dock, and two-level root menu. */
#include "jaws_tiles.inc"
static bool jaws, nemo_tile=true, menu_up, menu_latch;
static int menu_x,menu_y,branch=-1,subhit=-1,backdrop;
static const uint32_t backdrops[]={0xff505075,0xff354959,0xff3d514d,0xff34343b};
static const desktop_theme_t *nemo_palette;
static const char *wm_message="Choose the desktop to use at startup.";
bool wm_jaws(void){return jaws;}
bool wm_menu_open(void){return jaws && menu_up;}
const char *wm_status(void){return wm_message;}
uint32_t wm_solid_color(void){return backdrops[backdrop<4?backdrop:0];}
int wm_backdrop(void){return backdrop;}
/* The dock owns the rightmost tile column, including while clients resize. */
void wm_fit(window_t *w){
    if(!jaws || !w)return;int aw=(int)screen_width-64,ah=(int)screen_height;
    if(w->rect.width>aw)w->rect.width=aw;if(w->rect.height>ah)w->rect.height=ah;
    if(w->rect.x+w->rect.width>aw)w->rect.x=aw-w->rect.width;
    if(w->rect.y+w->rect.height>ah)w->rect.y=ah-w->rect.height;
    if(w->rect.x<0)w->rect.x=0;if(w->rect.y<0)w->rect.y=0;
}
static void wm_apply(bool next){
    if(next==jaws)return;
    start_menu_close();desktop_context_menu_close();menu_up=false;
    if(next){nemo_palette=theme_current;nemo_tile=tiling_enabled;theme_current=&theme_windowmaker;}
    else theme_current=nemo_palette;
    wallpaper_dirty=true;jaws=next;tiling_set(next?0:nemo_tile);
    if(next)for(int i=0;i<desktop.window_count;i++){
        window_t *w=desktop.windows+i;
        if(nemo_tile && w->state==WINDOW_STATE_NORMAL){
            int ww=w->type==WINDOW_TYPE_TERMINAL?480:w->type==WINDOW_TYPE_SETTINGS?416:720;
            if(w->rect.width>ww)w->rect.width=ww;
            int hh=w->type==WINDOW_TYPE_TERMINAL?320:w->type==WINDOW_TYPE_SETTINGS?376:500;
            if(w->rect.height>hh)w->rect.height=hh;
            w->rect.x=72+(i%4)*22;w->rect.y=48+(i%4)*22;
        }wm_fit(w);window_update_client_rect(w);w->needs_redraw=true;
    }
    window_title_icons_invalidate();desktop_icons_load();
    desktop.dirty=true;desktop.flush_partial=false;
}
static void wm_save(void){
    desktop.dirty=true;
    if(!store_installed()){wm_message="Active for this live session only.";return;}
    struct fs_node *d=fs_resolve_path("/System"), *f=d?find_node(d,"wm.pref"):NULL;
    if(!f && d && d->type==FS_DIRECTORY)f=create_node("wm.pref",FS_FILE,d);
    uint8_t data[]={1,jaws,nemo_tile,backdrop};
    bool ok=f && f->type==FS_FILE && !f->disk_backed && !fs_node_write(f,0,data,sizeof data) && !fs_node_truncate(f,sizeof data) && desktop_prefs_capture() && store_sync();
    wm_message=ok?"Saved for the next boot.":"Active now; preference could not be saved.";
}
void wm_set(bool next){wm_apply(next);wm_save();}
void wm_set_backdrop(int i){if(i>=0 && i<4+3*WP_COUNT){backdrop=i;wallpaper_dirty=true;wm_save();}}
void wm_restore_pref(void){
    struct fs_node *f=store_installed()?fs_resolve_path("/System/wm.pref"):NULL;
    if(f && f->type==FS_FILE && f->content_len==4){const uint8_t *p=fs_node_data(f);
        if(p && p[0]==1 && p[1]<=1 && p[2]<=1 && p[3]<4+3*WP_COUNT){bool tile=tiling_enabled;wm_apply(p[1]);if(jaws){nemo_tile=p[2];tiling_set(tile);}backdrop=p[3];}}
    serial_puts(jaws?"[wm] restored JawsWM\n":"[wm] restored NemoWM\n");
}
void wm_command(const char *s){
    if(!desktop.initialized){terminal_writestring("wmprefer requires the desktop.\n");return;}
    if(*s){if(!strcasecmp(s,"JawsWM"))wm_set(true);else if(!strcasecmp(s,"NemoWM"))wm_set(false);
        else {terminal_writestring("Usage: wmprefer [NemoWM|JawsWM]\n");return;}}
    terminal_writestring(jaws?"JawsWM":"NemoWM");if(*s){terminal_writestring(": ");terminal_writestring(wm_message);}terminal_putchar('\n');
}
#define JW 132
#define JR 20
static const char *const roots[]={"Terminal","Explorer","Navigator","Applications","Windows","Appearance","About SharkOS"};
static int clamp_menu(int v,int max){return v<0||max<0?0:v>max?max:v;}
static int entries(void){return branch==3?desktop.icon_count:branch==4?(desktop.window_count?desktop.window_count:1):branch==5?4:0;}
static int sub_x(void){return menu_x+2*JW<=(int)screen_width?menu_x+JW:menu_x-JW;}
static int sub_y(void){return clamp_menu(menu_y+(1+branch)*JR,(int)screen_height-(entries()+1)*JR);}
static void root_open(int x,int y){menu_up=true;branch=subhit=-1;menu_x=clamp_menu(x,(int)screen_width-JW);menu_y=clamp_menu(y,(int)screen_height-8*JR);desktop.dirty=true;desktop.flush_partial=false;}
static void menu_row(int x,int y,const char *name,bool title,bool lit,bool arrow){
    unsigned bg=title?0xff080808:lit?0xffdedede:0xffababab,fg=title?0xffffffff:0xff101010;
    w98_fill(x,y,JW,JR,0xff101010);w98_fill(x+1,y+1,JW-2,JR-2,bg);
    w98_fill(x+1,y+1,JW-2,1,title?0xff777777:0xffededed);w98_fill(x+1,y+1,1,JR-2,0xffdddddd);
    char fit[40];w98_text_fit(name,fit,sizeof fit,JW-20,1);
    if(title)w98_text_bold(fit,x+6,y+6,fg,bg,1,NULL);else w98_text(fit,x+6,y+6,fg,bg,1,NULL);
    if(arrow)for(int i=0;i<4;i++)w98_fill(x+JW-10+i,y+6+i,1,7-2*i,fg);
}
static const char *const pages[]={"Window manager","Style","Background","Display"};
static void jaws_front(void){
    for(int n=1;n<=3;n++)w98_blit_rgb(jaws_tiles[n],64,64,(int)screen_width-64,(n-1)*64,NULL);
    if(!menu_up)return;
    menu_row(menu_x,menu_y,"Applications",true,false,false);
    for(int i=0;i<7;i++)menu_row(menu_x,menu_y+(i+1)*JR,roots[i],false,i==branch,i>=3 && i<=5);
    if(entries()){
        int x=sub_x(),y=sub_y();menu_row(x,y,roots[branch],true,false,false);
        for(int i=0;i<entries();i++)menu_row(x,y+(i+1)*JR,branch==3?desktop.icons[i].label:branch==5?pages[i]:desktop.window_count?desktop.windows[i].title:"(No windows)",false,i==subhit,false);
    }
}
static void prefs_open(int i){settings_page_set(i);for(int j=0;j<desktop.window_count;j++)if(desktop.windows[j].type==WINDOW_TYPE_SETTINGS){if(desktop.windows[j].state==WINDOW_STATE_MINIMIZED)window_restore(j);window_focus(j);return;}desktop_icon_launch_offset(WINDOW_TYPE_SETTINGS,"Preferences");}
static void menu_action(int b,int i){
    if(i>=0){if(b==3)desktop_icon_launch(i);else if(b==5)prefs_open(i);else if(b==4 && i<desktop.window_count){if(desktop.windows[i].state==WINDOW_STATE_MINIMIZED)window_restore(i);window_focus(i);}return;}
    if(b==0)desktop_icon_launch_offset(WINDOW_TYPE_TERMINAL,"Terminal");if(b==1)desktop_icon_launch_offset(WINDOW_TYPE_FILEMANAGER,"Explorer");
    if(b==2)desktop_icon_launch_offset(WINDOW_TYPE_BROWSER,"Navigator");if(b==6)desktop_icon_launch_offset(WINDOW_TYPE_ABOUT,"About SharkOS");
}
static int root_hit(int x,int y){if(x<menu_x || x>=menu_x+JW || y<menu_y+JR || y>=menu_y+8*JR)return -1;return (y-menu_y)/JR-1;}
void wm_hover(int x,int y){if(!wm_menu_open())return;int b=root_hit(x,y),s=-1;if(b<0 && entries() && x>=sub_x() && x<sub_x()+JW && y>=sub_y()+JR && y<sub_y()+(1+entries())*JR)s=(y-sub_y())/JR-1;
    if((b>=0 && b!=branch)||s!=subhit){if(b>=0)branch=b;subhit=s;desktop.dirty=true;desktop.flush_partial=false;}}
static bool jaws_mouse(int x,int y,int buttons){
    if(!jaws)return false;
    if(!(buttons&3))menu_latch=false;
    if(menu_latch)return true;
    if(desktop_mouse_down)return false;
    if(menu_up){wm_hover(x,y);if(buttons&3){int b=root_hit(x,y);menu_latch=true;
        if(!(buttons&2) && b>=3 && b<=5)return true;
        menu_up=false;if(!(buttons&2) && (subhit>=0 || b>=0))menu_action(branch,subhit);desktop.dirty=true;desktop.flush_partial=false;}return true;}
    if(!(buttons&3))return false;
    if(x>=(int)screen_width-64 && y<192){menu_latch=true;if((buttons&2)||y<64)root_open(screen_width-64-JW,0);else if(y<128)menu_action(0,-1);else prefs_open(0);return true;}
    for(int i=0;i<desktop.window_count;i++){window_t *w=&desktop.windows[i];if(w->visible && w->state!=WINDOW_STATE_MINIMIZED && x>=w->rect.x && y>=w->rect.y && x<w->rect.x+w->rect.width && y<w->rect.y+w->rect.height)return false;}
    if((buttons&2)||(x<64 && y<64)){menu_latch=true;root_open(x,y);return true;}return false;
}
