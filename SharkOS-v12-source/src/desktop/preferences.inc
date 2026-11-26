/* Versioned preferences kept inside the installed filesystem's checked snapshot. */
struct optional_app { const char *pkg, *label; window_type_t type; };
static const struct optional_app apps[] = {
    {"ide","Shark IDE",WINDOW_TYPE_NOTEPAD}, {"doom","Doom",WINDOW_TYPE_DOOM},
    {"flappybird","Flappy Bird",WINDOW_TYPE_FLAPPYBIRD}, {"smb","Super Mario Bros",WINDOW_TYPE_SMB},
    {"pong","Pong",WINDOW_TYPE_PONG}, {"gdash","Geometry Dash",WINDOW_TYPE_GDASH},
    {"sharkcube","SharkCube",WINDOW_TYPE_SHARKCUBE}, {"sharkcraft","SharkCraft",WINDOW_TYPE_SHARKCRAFT},
    {"fnaf","FNAF 1",WINDOW_TYPE_FNAF}
};
#define APP_COUNT (sizeof(apps)/sizeof(apps[0]))
extern int br_js_disabled;
static bool prefs_ready;
static uint32_t package_mask(void) {
    uint32_t bits=0;
    for(unsigned i=0;i<APP_COUNT;i++) { plugin_t *p=plugin_get_by_name(apps[i].pkg); if(p && p->loaded) bits|=1u<<i; }
    return bits;
}
bool desktop_app_available(window_type_t type) {
    for(unsigned i=0;i<APP_COUNT;i++) if(apps[i].type==type){plugin_t *p=plugin_get_by_name(apps[i].pkg);return p && p->loaded;}
    return true;
}
void desktop_packages_refresh(void) {
    if(!desktop.initialized)return;
    for(unsigned i=0;i<APP_COUNT;i++){
        int at=-1; for(int j=0;j<desktop.icon_count;j++) if(desktop.icons[j].type==apps[i].type) at=j;
        bool on=desktop_app_available(apps[i].type);
        if(at<0 && on) desktop_icon_add(apps[i].label,apps[i].type);
        if(at>=0 && !on) desktop_icon_remove(at);
    }
    desktop.selected_icon=desktop.last_click_icon=-1; desktop_icons_load(); desktop.dirty=true;
}
static struct fs_node *prefs_file(bool create) {
    struct fs_node *dir=find_node(root,"System"); if(!dir || dir->type!=FS_DIRECTORY)return NULL;
    struct fs_node *f=find_node(dir,"desktop.prefs");
    if(!f && create) f=create_node("desktop.prefs",FS_FILE,dir);
    return f && f->type==FS_FILE && !f->disk_backed ? f:NULL;
}
void desktop_prefs_restore(void) {
    uint32_t mask=store_installed()?0:((1u<<APP_COUNT)-1);
    struct fs_node *f=store_installed()?prefs_file(false):NULL; uint32_t v[10];
    if(f && f->content_len==sizeof v){
        memcpy(v,fs_node_data(f),sizeof v);
        if(v[0]==0x31504644 && v[1]<THEME_COUNT && v[2]<3 && v[3]<WP_COUNT && v[4]<3 && v[7]<2 && v[8]<2){
            theme_set(v[1]); desktop_set_wallpaper(v[3]); desktop_set_wallpaper_mode(v[2]);
            desktop_set_wallpaper_fit(v[4]);desktop_set_wallpaper_color(v[5],v[6]);desktop_set_show_icons(v[7]);
            br_js_disabled=v[8]; mask=v[9];
        }
    }
    for(unsigned i=0;i<APP_COUNT;i++){ plugin_t *p=plugin_get_by_name(apps[i].pkg); if(p)p->loaded=!!(mask&(1u<<i)); }
    desktop_packages_refresh(); prefs_ready=true;
    serial_puts("[prefs] packages=");serial_putdec(package_mask());serial_puts(" theme=");serial_putdec(theme_get_id());serial_puts("\n");
}
bool desktop_prefs_capture(void) {
    if(!prefs_ready || !store_installed()) return true;
    uint32_t v[]={0x31504644,theme_get_id(),desktop.wallpaper_mode,desktop.wallpaper_id,desktop.wallpaper_fit,
                  wallpaper_top,wallpaper_bot,desktop.show_desktop_icons,!!br_js_disabled,package_mask()};
    struct fs_node *f=prefs_file(true); if(!f)return false;
    if(f->content_len==sizeof v){uint32_t old[10];memcpy(old,fs_node_data(f),sizeof old);
        unsigned i=0;while(i<10 && old[i]==v[i])i++; if(i==10)return true;}
    return !fs_node_write(f,0,v,sizeof v) && !fs_node_truncate(f,sizeof v);
}
static char clipboard[4096];
void desktop_copy_text(const char *str,unsigned len){ if(len>=sizeof clipboard)len=sizeof clipboard-1;memcpy(clipboard,str,len);clipboard[len]=0; }
const char *desktop_paste_text(void){return clipboard;}
