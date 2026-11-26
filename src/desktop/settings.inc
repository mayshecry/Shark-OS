/* One layout for painting and hit testing, including small tiled windows. */
static int prefpage,firstrow;static bool pref_held;
static const char *const tabs[]={"Manager","Style","Backdrop","Video","System"};
static const char *const titles[]={"Window manager","Desktop style","Desktop background","Display mode","System"};
static const char *const reslabels[]={"800x600","1024x768","1280x720","1600x900","1920x1080"};
static const char *const jcolors[]={"Slate violet","Deep harbor","Pine","Graphite"};
static const char *const fitnames[]={"Fill the desktop","Fit to desktop","Stretch to desktop"};
static const char *const mode_names[]={"Dithered background","Solid colour","Wallpaper image"};
void settings_page_set(int i){prefpage=i>=0 && i<5?i:0;firstrow=0;desktop.dirty=true;}
static int row_count(void){switch(prefpage){case 0:return 2;case 1:return wm_jaws()?1:THEME_COUNT;case 2:return wm_jaws()?4+WP_COUNT+3:3+WP_COUNT+8+3;case 3:return 7;default:return 2;}}
static const char *pref_row(int i,bool *active,bool *enabled){switch(prefpage){
 case 0:*active=(i==!!wm_jaws());return i?"JawsWM  -  UNIX clip and dock":"NemoWM  -  icons and taskbar";
 case 1:*active=wm_jaws()||i==theme_get_id();return wm_jaws()?"Jaws classic  -  graphite and silver":theme_get_name(i);
 case 2:if(wm_jaws()){int choice=wm_backdrop();if(i<4){*active=i==choice;return jcolors[i];}
   i-=4;if(i<WP_COUNT){*active=choice>=4 && (choice-4)%WP_COUNT==i;return desktop_wallpaper_name(i);}
   i-=WP_COUNT;*enabled=choice>=4;*active=*enabled && (choice-4)/WP_COUNT==i;return fitnames[i];}
   if(i<3){*active=i==desktop.wallpaper_mode;return mode_names[i];}i-=3;
   if(i<WP_COUNT){*active=desktop.wallpaper_mode==2 && i==desktop.wallpaper_id;return desktop_wallpaper_name(i);}i-=WP_COUNT;
   if(i<8){*active=desktop.wallpaper_mode==1 && settings_colors[i].color==wallpaper_top;return settings_colors[i].label;}i-=8;*active=i==desktop.wallpaper_fit;return fitnames[i];
 case 3:*enabled=i<5?video_available():video_preview();if(i<5){const int widths[]={800,1024,1280,1600,1920};*active=screen_width==widths[i];return reslabels[i];}return i==5?"Keep this resolution":"Revert preview";
 default:if(i==0){*active=tiling_enabled;return "Tile open windows";}return "Shut down SharkOS";
}}
static void pref_act(int i){switch(prefpage){case 0:wm_set(i);break;case 1:if(!wm_jaws())theme_set(i);break;
case 2:if(wm_jaws()){int old=wm_backdrop();if(i<4)wm_set_backdrop(i);
   else if(i<4+WP_COUNT)wm_set_backdrop(i+(old<4?0:(old-4)/WP_COUNT*WP_COUNT));
   else if(old>=4)wm_set_backdrop(4+(old-4)%WP_COUNT+(i-4-WP_COUNT)*WP_COUNT);}else if(i<3)desktop_set_wallpaper_mode(i);else if((i-=3)<WP_COUNT)desktop_set_wallpaper(i);else if((i-=WP_COUNT)<8){uint32_t c=settings_colors[i].color;desktop_set_wallpaper_color(c,0xff000000|((c&0xfefefe)>>1));desktop_set_wallpaper_mode(W98_WALL_SOLID);}else desktop_set_wallpaper_fit(i-8);break;
case 3:video_choose(i<5?reslabels[i]:i==5?"keep":"revert");break;case 4:if(i==0)tiling_set(!tiling_enabled);else shutdown();break;}desktop.dirty=true;}
static void pref_layout(window_t *w,bool click,int mx,int my){
 int x=w->rect.client_x,y=w->rect.client_y,cw=w->rect.client_w,ch=w->rect.client_h;
 if(cw<40||ch<35)return;
 w98_rect_t clip={x,y,cw,ch};uint32_t bg=W98_BTNFACE,ink=W98_BTNTEXT,head=wm_jaws()?0xff141416:W98_ACTIVE_TITLE;
 if(!click){w98_fill(x,y,cw,ch,bg);w98_fill(x,y,cw,26,head);w98_text_bold(wm_jaws()?"JawsWM  /  Preferences":"NemoWM  /  Settings",x+7,y+8,0xffffffff,head,1,&clip);}
 int tw=cw/5;
 for(int i=0;i<5;i++){int xx=x+i*tw,ww=i==4?cw-i*tw:tw;
   if(click && mx>=xx && mx<xx+ww && my>=y+28 && my<y+50){settings_page_set(i);return;}
   if(!click && ch>=50){char fit[18];w98_text_fit(tabs[i],fit,sizeof fit,ww-4,1);w98_button(xx,y+28,ww,22,fit,prefpage==i,true,false);}}
 if(ch<94)return;
 if(!click)w98_text_bold(titles[prefpage],x+8,y+60,ink,bg,1,&clip);
 int n=row_count(), visible=(ch-115)/23;if(visible<0)visible=0;if(visible>n)visible=n;
 if(firstrow>n-visible)firstrow=n-visible;if(firstrow<0)firstrow=0;
 for(int k=0;k<visible;k++){int i=k+firstrow,ry=y+78+23*k;bool active=false,enabled=true;const char *s=pref_row(i,&active,&enabled);
   if(click){if(mx>=x+6 && mx<x+cw-6 && my>=ry && my<ry+21 && enabled){pref_act(i);return;}continue;}
   uint32_t rowbg=active?W98_HIGHLIGHT:W98_WINDOW,rowfg=active?W98_HIGHLIGHTTEXT:W98_WINDOWTEXT;
   w98_fill(x+6,ry,cw-12,21,rowbg);w98_fill(x+6,ry+20,cw-12,1,W98_BTNSHADOW);
   if(active)w98_fill(x+12,ry+8,5,5,rowfg);char fit[80];w98_text_fit(s,fit,sizeof fit,cw-35,1);w98_text(fit,x+24,ry+6,enabled?rowfg:W98_GRAYTEXT,rowbg,1,&clip);
 }
 const char *note=prefpage==0?wm_status():prefpage==3?video_status():prefpage==2?"Choose a desktop background.":prefpage==1?(wm_jaws()?"JawsWM uses its own palette.":"Choose a NemoWM palette."):"Desktop session controls";
 int fy=y+ch-25;if(!click){char fit[92];w98_text_fit(note,fit,sizeof fit,cw-(n>visible?75:15),1);w98_text(fit,x+7,fy+7,ink,bg,1,&clip);}
 if(n>visible){for(int i=0;i<2;i++){int bx=x+cw-60+28*i;bool ok=i?firstrow+visible<n:firstrow>0;if(!click)w98_button(bx,fy,26,21,i?"+":"-",false,ok,false);else if(ok && mx>=bx && mx<bx+26 && my>=fy && my<fy+21){firstrow+=i?(visible?visible:1):-(visible?visible:1);desktop.dirty=true;}}}
}
void app_window_draw_settings(window_t *w){pref_layout(w,false,0,0);}
void app_window_mouse_settings(window_t *w,int mx,int my,int buttons){if(!(buttons&1)){pref_held=false;return;}if(!pref_held){pref_held=true;pref_layout(w,true,mx,my);}}
