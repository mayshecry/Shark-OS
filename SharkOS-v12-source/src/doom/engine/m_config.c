  
                                           
                                        
                                      
  
                                                                
                                                              
                                                                 
                                                         
  
                                                                  
                                                                 
                                                                
                                               
  
               
                                   
  


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

#include "config.h"

#include "doomtype.h"
#include "doomkeys.h"
#include "doomfeatures.h"
#include "i_system.h"
#include "m_argv.h"
#include "m_misc.h"

#include "z_zone.h"

  
           
  

                                                     
                               

char *configdir;

                                             

static char *default_main_config;
static char *default_extra_config;

typedef enum 
{
    DEFAULT_INT,
    DEFAULT_INT_HEX,
    DEFAULT_STRING,
    DEFAULT_FLOAT,
    DEFAULT_KEY,
} default_type_t;

typedef struct
{
                           
    char *name;

                                                        
    void *location;

                           
    default_type_t type;

                                                                         
                                                                       
                                                             
    int untranslated;

                                                                  
                                                                       
                                                                      
                                    
    int original_translated;

                                                                 
                         
    boolean bound;
} default_t;

typedef struct
{
    default_t *defaults;
    int numdefaults;
    char *filename;
} default_collection_t;

#define CONFIG_VARIABLE_GENERIC(name, type) \
    { #name, NULL, type, 0, 0, false }

#define CONFIG_VARIABLE_KEY(name) \
    CONFIG_VARIABLE_GENERIC(name, DEFAULT_KEY)
#define CONFIG_VARIABLE_INT(name) \
    CONFIG_VARIABLE_GENERIC(name, DEFAULT_INT)
#define CONFIG_VARIABLE_INT_HEX(name) \
    CONFIG_VARIABLE_GENERIC(name, DEFAULT_INT_HEX)
#define CONFIG_VARIABLE_FLOAT(name) \
    CONFIG_VARIABLE_GENERIC(name, DEFAULT_FLOAT)
#define CONFIG_VARIABLE_STRING(name) \
    CONFIG_VARIABLE_GENERIC(name, DEFAULT_STRING)

                              

static default_t	doom_defaults_list[] =
{
       
                                                                     
                                                          
      
                                                                
                                                                     
                                                        
      

    CONFIG_VARIABLE_INT(mouse_sensitivity),

       
                                           
      

    CONFIG_VARIABLE_INT(sfx_volume),

       
                                           
      

    CONFIG_VARIABLE_INT(music_volume),

       
                   
      
                                                                        
                                            
      

    CONFIG_VARIABLE_INT(show_talk),

       
                   
      
                                                 
      

    CONFIG_VARIABLE_INT(voice_volume),

       
                 
      
                                                                  
                                                                      
                         
      

    CONFIG_VARIABLE_INT(show_messages),

       
                                  
      

    CONFIG_VARIABLE_KEY(key_right),

       
                                 
      

    CONFIG_VARIABLE_KEY(key_left),

       
                                    
      

    CONFIG_VARIABLE_KEY(key_up),

       
                                     
      

    CONFIG_VARIABLE_KEY(key_down),

       
                                   
      

    CONFIG_VARIABLE_KEY(key_strafeleft),

       
                                    
      

    CONFIG_VARIABLE_KEY(key_straferight),

       
                   
      
                                  
      

    CONFIG_VARIABLE_KEY(key_useHealth),

       
                  
      
                            
      

    CONFIG_VARIABLE_KEY(key_jump),

       
                          
      
                                  
      

    CONFIG_VARIABLE_KEY(key_flyup),

       
                          
      
                                     
      

    CONFIG_VARIABLE_KEY(key_flydown),

       
                          
      
                                     
      

    CONFIG_VARIABLE_KEY(key_flycenter),

       
                          
      
                               
      

    CONFIG_VARIABLE_KEY(key_lookup),

       
                          
      
                                 
      

    CONFIG_VARIABLE_KEY(key_lookdown),

       
                          
      
                                       
      

    CONFIG_VARIABLE_KEY(key_lookcenter),

       
                   
      
                                       
      

    CONFIG_VARIABLE_KEY(key_invquery),

       
                   
      
                                                 
      

    CONFIG_VARIABLE_KEY(key_mission),

       
                   
      
                                               
      

    CONFIG_VARIABLE_KEY(key_invPop),

       
                   
      
                                          
      

    CONFIG_VARIABLE_KEY(key_invKey),

       
                   
      
                                                  
      

    CONFIG_VARIABLE_KEY(key_invHome),

       
                   
      
                                                
      

    CONFIG_VARIABLE_KEY(key_invEnd),

       
                          
      
                                                    
      

    CONFIG_VARIABLE_KEY(key_invleft),

       
                          
      
                                                     
      

    CONFIG_VARIABLE_KEY(key_invright),

       
                   
      
                                                    
      

    CONFIG_VARIABLE_KEY(key_invLeft),

       
                   
      
                                                     
      

    CONFIG_VARIABLE_KEY(key_invRight),

       
                          
      
                                                             
      

    CONFIG_VARIABLE_KEY(key_useartifact),

       
                   
      
                                          
      

    CONFIG_VARIABLE_KEY(key_invUse),

       
                   
      
                                              
      

    CONFIG_VARIABLE_KEY(key_invDrop),

       
                   
      
                               
      

    CONFIG_VARIABLE_KEY(key_lookUp),

       
                   
      
                                 
      

    CONFIG_VARIABLE_KEY(key_lookDown),

       
                                                          
      

    CONFIG_VARIABLE_KEY(key_fire),

       
                                                             
      

    CONFIG_VARIABLE_KEY(key_use),

       
                                                                      
                                                                    
                     
      

    CONFIG_VARIABLE_KEY(key_strafe),

       
                                           
      

    CONFIG_VARIABLE_KEY(key_speed),

       
                                                                    
                
      

    CONFIG_VARIABLE_INT(use_mouse),

       
                                                          
      

    CONFIG_VARIABLE_INT(mouseb_fire),

       
                                                                    
                                                                    
      

    CONFIG_VARIABLE_INT(mouseb_strafe),

       
                                    
      

    CONFIG_VARIABLE_INT(mouseb_forward),

       
                         
      
                            
      

    CONFIG_VARIABLE_INT(mouseb_jump),

       
                                              
      

    CONFIG_VARIABLE_INT(use_joystick),

       
                                                             
      

    CONFIG_VARIABLE_INT(joyb_fire),

       
                                                                 
                 
      

    CONFIG_VARIABLE_INT(joyb_strafe),

       
                                                                        
      

    CONFIG_VARIABLE_INT(joyb_use),

       
                                                                   
            
      
                                                                        
                                 
      

    CONFIG_VARIABLE_INT(joyb_speed),

       
                         
      
                                                          
      

    CONFIG_VARIABLE_INT(joyb_jump),

       
                               
      
                               
      
                                                                     
                                                                  
                            
      

    CONFIG_VARIABLE_INT(screenblocks),

       
                   
      
                               
      
                                                                     
                                                                  
                            
      

    CONFIG_VARIABLE_INT(screensize),

       
                 
      
                                                                  
                                                
      

    CONFIG_VARIABLE_INT(detaillevel),

       
                                                           
      

    CONFIG_VARIABLE_INT(snd_channels),

       
                                                                      
                                            
      

    CONFIG_VARIABLE_INT(snd_musicdevice),

       
                                                                    
                                                                    
                                                                  
               
      

    CONFIG_VARIABLE_INT(snd_sfxdevice),

       
                                     
      

    CONFIG_VARIABLE_INT(snd_sbport),

       
                                 
      

    CONFIG_VARIABLE_INT(snd_sbirq),

       
                                         
      

    CONFIG_VARIABLE_INT(snd_sbdma),

       
                                                         
      

    CONFIG_VARIABLE_INT(snd_mport),

       
                                                              
                                                                  
                                  
      

    CONFIG_VARIABLE_INT(usegamma),

       
                  
      
                                             
      

    CONFIG_VARIABLE_STRING(savedir),

       
                  
      
                                                                       
                                                            
      

    CONFIG_VARIABLE_INT(messageson),

       
                   
      
                                                   
      

    CONFIG_VARIABLE_STRING(back_flat),

       
                   
      
                                
      

    CONFIG_VARIABLE_STRING(nickname),

       
                                                                     
      

    CONFIG_VARIABLE_STRING(chatmacro0),

       
                                                                     
      

    CONFIG_VARIABLE_STRING(chatmacro1),

       
                                                                     
      

    CONFIG_VARIABLE_STRING(chatmacro2),

       
                                                                     
      

    CONFIG_VARIABLE_STRING(chatmacro3),

       
                                                                     
      

    CONFIG_VARIABLE_STRING(chatmacro4),

       
                                                                     
      

    CONFIG_VARIABLE_STRING(chatmacro5),

       
                                                                     
      

    CONFIG_VARIABLE_STRING(chatmacro6),

       
                                                                     
      

    CONFIG_VARIABLE_STRING(chatmacro7),

       
                                                                     
      

    CONFIG_VARIABLE_STRING(chatmacro8),

       
                                                                     
      

    CONFIG_VARIABLE_STRING(chatmacro9),

       
                   
      
                                                           
      

    CONFIG_VARIABLE_INT(comport),
};

static default_collection_t doom_defaults =
{
    doom_defaults_list,
    arrlen(doom_defaults_list),
    NULL,
};

                               

static default_t extra_defaults_list[] =
{
       
                                 
      
                                                         
      

    CONFIG_VARIABLE_INT(graphical_startup),

       
                                                                  
                                                                      
                                            
      

    CONFIG_VARIABLE_INT(autoadjust_video_settings),

       
                                                                    
                                     
      

    CONFIG_VARIABLE_INT(fullscreen),

       
                                                                      
                                              
      

    CONFIG_VARIABLE_INT(aspect_ratio_correct),

       
                                                                     
                                                                 
                                                                     
                                                       
      

    CONFIG_VARIABLE_INT(startup_delay),

       
                                                                       
                                                               
                                                                       
                
      

    CONFIG_VARIABLE_INT(screen_width),

       
                                                                        
                                                               
                                                                        
                
      

    CONFIG_VARIABLE_INT(screen_height),

       
                                          
                                                                        
                                                              
      

    CONFIG_VARIABLE_INT(screen_bpp),

       
                                                                    
                                                                  
                                                    
      

    CONFIG_VARIABLE_INT(grabmouse),

       
                                                                 
                                                                     
                                       
      

    CONFIG_VARIABLE_INT(novert),

       
                                                                   
                                                                  
                                
      

    CONFIG_VARIABLE_FLOAT(mouse_acceleration),

       
                                                                      
                                                                  
                                                
      

    CONFIG_VARIABLE_INT(mouse_threshold),

       
                                                                  
                                     
      

    CONFIG_VARIABLE_INT(snd_samplerate),

       
                                                                      
                                                                    
      

    CONFIG_VARIABLE_INT(snd_cachesize),

       
                                                                    
                                                                      
                                                                
                                                                    
                    

    CONFIG_VARIABLE_INT(snd_maxslicetime_ms),

       
                                                                     
                                                                    
                                                                      
                   

    CONFIG_VARIABLE_STRING(snd_musiccmd),

       
                                                                      
                                       
      

    CONFIG_VARIABLE_INT_HEX(opl_io_port),

       
                                
      
                                                                        
                                                         
      

    CONFIG_VARIABLE_INT(show_endoom),

       
                                                   
      

    CONFIG_VARIABLE_INT(png_screenshots),

       
                        
      
                                                                  
                                                                     
                                                                   
                             
      

    CONFIG_VARIABLE_INT(vanilla_savegame_limit),

       
                        
      
                                                                     
                                                                  
                                                                     
                                  
      

    CONFIG_VARIABLE_INT(vanilla_demo_limit),

       
                                                                       
                                                                      
                                                       
      

    CONFIG_VARIABLE_INT(vanilla_keyboard_mapping),

       
                                                                        
                                        
      

    CONFIG_VARIABLE_STRING(video_driver),

       
                                                                    
                                                                 
                                                                 
                                           

    CONFIG_VARIABLE_STRING(window_position),

#ifdef FEATURE_MULTIPLAYER

       
                                                                     
                                                                        
      

    CONFIG_VARIABLE_STRING(player_name),

#endif

       
                                                                     
                                                             
      

    CONFIG_VARIABLE_INT(joystick_index),

       
                                                           
      

    CONFIG_VARIABLE_INT(joystick_x_axis),

       
                                                                         
      

    CONFIG_VARIABLE_INT(joystick_x_invert),

       
                                                         
      

    CONFIG_VARIABLE_INT(joystick_y_axis),

       
                                                                       
      

    CONFIG_VARIABLE_INT(joystick_y_invert),

       
                                                     
      

    CONFIG_VARIABLE_INT(joystick_strafe_axis),

       
                                                                   
                   
      

    CONFIG_VARIABLE_INT(joystick_strafe_invert),

       
                                                                
                         
      

    CONFIG_VARIABLE_INT(joystick_physical_button0),

       
                                                                
                         
      

    CONFIG_VARIABLE_INT(joystick_physical_button1),

       
                                                                
                         
      

    CONFIG_VARIABLE_INT(joystick_physical_button2),

       
                                                                
                         
      

    CONFIG_VARIABLE_INT(joystick_physical_button3),

       
                                                                
                         
      

    CONFIG_VARIABLE_INT(joystick_physical_button4),

       
                                                                
                         
      

    CONFIG_VARIABLE_INT(joystick_physical_button5),

       
                                                                
                         
      

    CONFIG_VARIABLE_INT(joystick_physical_button6),

       
                                                                
                         
      

    CONFIG_VARIABLE_INT(joystick_physical_button7),

       
                                                                
                         
      

    CONFIG_VARIABLE_INT(joystick_physical_button8),

       
                                                                
                         
      

    CONFIG_VARIABLE_INT(joystick_physical_button9),

       
                                                              
      

    CONFIG_VARIABLE_INT(joyb_strafeleft),

       
                                                               
      

    CONFIG_VARIABLE_INT(joyb_straferight),

       
                                                    
      

    CONFIG_VARIABLE_INT(joyb_menu_activate),

       
                                                                  
      

    CONFIG_VARIABLE_INT(joyb_prevweapon),

       
                                                              
      

    CONFIG_VARIABLE_INT(joyb_nextweapon),

       
                                   
      

    CONFIG_VARIABLE_INT(mouseb_strafeleft),

       
                                    
      

    CONFIG_VARIABLE_INT(mouseb_straferight),

       
                                                             
      

    CONFIG_VARIABLE_INT(mouseb_use),

       
                                      
      

    CONFIG_VARIABLE_INT(mouseb_backward),

       
                                                    
      

    CONFIG_VARIABLE_INT(mouseb_prevweapon),

       
                                                
      

    CONFIG_VARIABLE_INT(mouseb_nextweapon),

       
                                                                     
                                                                    
      

    CONFIG_VARIABLE_INT(dclick_use),

#ifdef FEATURE_SOUND

       
                                                                    
                                                                  
                                         
      
                                                                
                                                                     
                                                                
                                                             
                                                                
                                                     
      

    CONFIG_VARIABLE_INT(use_libsamplerate),

       
                                                                         
                                                                       
                                                                      
                                                                        
                                                                         
                                                                       
                                                                          
                                           

    CONFIG_VARIABLE_FLOAT(libsamplerate_scale),

       
                                                                 
                                                                    
                                                                 
               
      

    CONFIG_VARIABLE_STRING(timidity_cfg_path),

       
                                                                     
            
      

    CONFIG_VARIABLE_STRING(gus_patch_path),

       
                                                                     
                                        
      

    CONFIG_VARIABLE_INT(gus_ram_kb),

#endif

       
                                        
      

    CONFIG_VARIABLE_KEY(key_pause),

       
                                                
      

    CONFIG_VARIABLE_KEY(key_menu_activate),

       
                                                
      

    CONFIG_VARIABLE_KEY(key_menu_up),

       
                                                  
      

    CONFIG_VARIABLE_KEY(key_menu_down),

       
                                                                     
      

    CONFIG_VARIABLE_KEY(key_menu_left),

       
                                                                      
      

    CONFIG_VARIABLE_KEY(key_menu_right),

       
                                           
      

    CONFIG_VARIABLE_KEY(key_menu_back),

       
                                                        
      

    CONFIG_VARIABLE_KEY(key_menu_forward),

       
                                                     
      

    CONFIG_VARIABLE_KEY(key_menu_confirm),

       
                                                    
      

    CONFIG_VARIABLE_KEY(key_menu_abort),

       
                                                     
      

    CONFIG_VARIABLE_KEY(key_menu_help),

       
                                                        
      

    CONFIG_VARIABLE_KEY(key_menu_save),

       
                                                        
      

    CONFIG_VARIABLE_KEY(key_menu_load),

       
                                                           
      

    CONFIG_VARIABLE_KEY(key_menu_volume),

       
                                                    
      

    CONFIG_VARIABLE_KEY(key_menu_detail),

       
                                                       
      

    CONFIG_VARIABLE_KEY(key_menu_qsave),

       
                                         
      

    CONFIG_VARIABLE_KEY(key_menu_endgame),

       
                                                     
      

    CONFIG_VARIABLE_KEY(key_menu_messages),

       
                                                    
      

    CONFIG_VARIABLE_KEY(key_menu_qload),

       
                                          
      

    CONFIG_VARIABLE_KEY(key_menu_quit),

       
                                                              
      

    CONFIG_VARIABLE_KEY(key_menu_gamma),

       
                                                       
      

    CONFIG_VARIABLE_KEY(key_spy),

       
                                                     
      

    CONFIG_VARIABLE_KEY(key_menu_incscreen),

       
                                                     
      

    CONFIG_VARIABLE_KEY(key_menu_decscreen),

       
                                              
      

    CONFIG_VARIABLE_KEY(key_menu_screenshot),

       
                                  
      

    CONFIG_VARIABLE_KEY(key_map_toggle),

       
                                             
      

    CONFIG_VARIABLE_KEY(key_map_north),

       
                                             
      

    CONFIG_VARIABLE_KEY(key_map_south),

       
                                            
      

    CONFIG_VARIABLE_KEY(key_map_east),

       
                                            
      

    CONFIG_VARIABLE_KEY(key_map_west),

       
                                           
      

    CONFIG_VARIABLE_KEY(key_map_zoomin),

       
                                            
      

    CONFIG_VARIABLE_KEY(key_map_zoomout),

       
                                                               
      

    CONFIG_VARIABLE_KEY(key_map_maxzoom),

       
                                                      
      

    CONFIG_VARIABLE_KEY(key_map_follow),

       
                                                           
      

    CONFIG_VARIABLE_KEY(key_map_grid),

       
                                              
      

    CONFIG_VARIABLE_KEY(key_map_mark),

       
                                                   
      

    CONFIG_VARIABLE_KEY(key_map_clearmark),

       
                              
      

    CONFIG_VARIABLE_KEY(key_weapon1),

       
                              
      

    CONFIG_VARIABLE_KEY(key_weapon2),

       
                              
      

    CONFIG_VARIABLE_KEY(key_weapon3),

       
                              
      

    CONFIG_VARIABLE_KEY(key_weapon4),

       
                              
      

    CONFIG_VARIABLE_KEY(key_weapon5),

       
                              
      

    CONFIG_VARIABLE_KEY(key_weapon6),

       
                              
      

    CONFIG_VARIABLE_KEY(key_weapon7),

       
                              
      

    CONFIG_VARIABLE_KEY(key_weapon8),

       
                                           
      

    CONFIG_VARIABLE_KEY(key_prevweapon),

       
                                       
      

    CONFIG_VARIABLE_KEY(key_nextweapon),

       
                  
      
                                       
      

    CONFIG_VARIABLE_KEY(key_arti_all),

       
                  
      
                                          
      

    CONFIG_VARIABLE_KEY(key_arti_health),

       
                  
      
                                       
      

    CONFIG_VARIABLE_KEY(key_arti_poisonbag),

       
                  
      
                                               
      

    CONFIG_VARIABLE_KEY(key_arti_blastradius),

       
                  
      
                                          
      

    CONFIG_VARIABLE_KEY(key_arti_teleport),

       
                  
      
                                               
      

    CONFIG_VARIABLE_KEY(key_arti_teleportother),

       
                  
      
                                        
      

    CONFIG_VARIABLE_KEY(key_arti_egg),

       
                  
      
                                                  
      

    CONFIG_VARIABLE_KEY(key_arti_invulnerability),

       
                                      
      

    CONFIG_VARIABLE_KEY(key_message_refresh),

       
                                                  
      

    CONFIG_VARIABLE_KEY(key_demo_quit),

       
                                                      
      

    CONFIG_VARIABLE_KEY(key_multi_msg),

       
                                                                  
      

    CONFIG_VARIABLE_KEY(key_multi_msgplayer1),

       
                                                                  
      

    CONFIG_VARIABLE_KEY(key_multi_msgplayer2),

       
                                                                  
      

    CONFIG_VARIABLE_KEY(key_multi_msgplayer3),

       
                                                                  
      

    CONFIG_VARIABLE_KEY(key_multi_msgplayer4),

       
                         
      
                                                                  
      

    CONFIG_VARIABLE_KEY(key_multi_msgplayer5),

       
                         
      
                                                                  
      

    CONFIG_VARIABLE_KEY(key_multi_msgplayer6),

       
                         
      
                                                                  
      

    CONFIG_VARIABLE_KEY(key_multi_msgplayer7),

       
                         
      
                                                                  
      

    CONFIG_VARIABLE_KEY(key_multi_msgplayer8),
};

static default_collection_t extra_defaults =
{
    extra_defaults_list,
    arrlen(extra_defaults_list),
    NULL,
};

                                     

static default_t *SearchCollection(default_collection_t *collection, char *name)
{
    int i;

    for (i=0; i<collection->numdefaults; ++i) 
    {
        if (!strcmp(name, collection->defaults[i].name))
        {
            return &collection->defaults[i];
        }
    }

    return NULL;
}

                                                                       
                                                                     
                                         
                                                                       
                                                                 
                                        
                                                                      
                                                                     
                                                                     
                       
static const int scantokey[128] =
{
    0  ,    27,     '1',    '2',    '3',    '4',    '5',    '6',
    '7',    '8',    '9',    '0',    '-',    '=',    KEY_BACKSPACE, 9,
    'q',    'w',    'e',    'r',    't',    'y',    'u',    'i',
    'o',    'p',    '[',    ']',    13,		KEY_RCTRL, 'a',    's',
    'd',    'f',    'g',    'h',    'j',    'k',    'l',    ';',
    '\'',   '`',    KEY_RSHIFT,'\\',   'z',    'x',    'c',    'v',
    'b',    'n',    'm',    ',',    '.',    '/',    KEY_RSHIFT,KEYP_MULTIPLY,
    KEY_RALT,  ' ',  KEY_CAPSLOCK,KEY_F1,  KEY_F2,   KEY_F3,   KEY_F4,   KEY_F5,
    KEY_F6,   KEY_F7,   KEY_F8,   KEY_F9,   KEY_F10,                  KEY_PAUSE,KEY_SCRLCK,KEY_HOME,
    KEY_UPARROW,KEY_PGUP,KEY_MINUS,KEY_LEFTARROW,KEYP_5,KEY_RIGHTARROW,KEYP_PLUS,KEY_END,
    KEY_DOWNARROW,KEY_PGDN,KEY_INS,KEY_DEL,0,   0,      0,      KEY_F11,
    KEY_F12,  0,      0,      0,      0,      0,      0,      0,
    0,      0,      0,      0,      0,      0,      0,      0,
    0,      0,      0,      0,      0,      0,      0,      0,
    0,      0,      0,      0,      0,      0,      0,      0,
    0,      0,      0,      0,      0,      0,      KEY_PRTSCR, 0
};


static void SaveDefaultCollection(default_collection_t *collection)
{
#if ORIGCODE
    default_t *defaults;
    int i, v;
    FILE *f;
	
    f = fopen (collection->filename, "w");
    if (!f)
	return;                                            

    defaults = collection->defaults;
		
    for (i=0 ; i<collection->numdefaults ; i++)
    {
        int chars_written;

                                   

        if (!defaults[i].bound)
        {
            continue;
        }

                                                                 

        chars_written = fprintf(f, "%s ", defaults[i].name);

        for (; chars_written < 30; ++chars_written)
            fprintf(f, " ");

                          

        switch (defaults[i].type) 
        {
            case DEFAULT_KEY:

                                                                    
                                                                   
                       
                
                v = * (int *) defaults[i].location;

                if (v == KEY_RSHIFT)
                {
                                                                   
                                                                 
                                                                    
                                                                    
                                                                    

                    v = 54;
                }
                else if (defaults[i].untranslated
                      && v == defaults[i].original_translated)
                {
                                                                  
                                            

                    v = defaults[i].untranslated;
                }
                else
                {
                                                                      
                                             

                    int s;

                    for (s=0; s<128; ++s)
                    {
                        if (scantokey[s] == v)
                        {
                            v = s;
                            break;
                        }
                    }
                }

	        fprintf(f, "%i", v);
                break;

            case DEFAULT_INT:
	        fprintf(f, "%i", * (int *) defaults[i].location);
                break;

            case DEFAULT_INT_HEX:
	        fprintf(f, "0x%x", * (int *) defaults[i].location);
                break;

            case DEFAULT_FLOAT:
                fprintf(f, "%f", * (float *) defaults[i].location);
                break;

            case DEFAULT_STRING:
	        fprintf(f,"\"%s\"", * (char **) (defaults[i].location));
                break;
        }

        fprintf(f, "\n");
    }

    fclose (f);
#endif
}

                                                  

static int ParseIntParameter(char *strparm)
{
    int parm;

    if (strparm[0] == '0' && strparm[1] == 'x')
        sscanf(strparm+2, "%x", &parm);
    else
        sscanf(strparm, "%i", &parm);

    return parm;
}

static void SetVariable(default_t *def, char *value)
{
    int intparm;

                      

    switch (def->type)
    {
        case DEFAULT_STRING:
            * (char **) def->location = strdup(value);
            break;

        case DEFAULT_INT:
        case DEFAULT_INT_HEX:
            * (int *) def->location = ParseIntParameter(value);
            break;

        case DEFAULT_KEY:

                                                   
                                                        

            intparm = ParseIntParameter(value);
            def->untranslated = intparm;
            if (intparm >= 0 && intparm < 128)
            {
                intparm = scantokey[intparm];
            }
            else
            {
                intparm = 0;
            }

            def->original_translated = intparm;
            * (int *) def->location = intparm;
            break;

        case DEFAULT_FLOAT:
            * (float *) def->location = (float) atof(value);
            break;
    }
}

static void LoadDefaultCollection(default_collection_t *collection)
{
#if ORIGCODE
    FILE *f;
    default_t *def;
    char defname[80];
    char strparm[100];

                                                    
    f = fopen(collection->filename, "r");

    if (f == NULL)
    {
                                                
                                                               

        return;
    }

    while (!feof(f))
    {
        if (fscanf(f, "%79s %99[^\n]\n", defname, strparm) != 2)
        {
                                      

            continue;
        }

                                       

        def = SearchCollection(collection, defname);

        if (def == NULL || !def->bound)
        {
                                                                    
                          

            continue;
        }

                                                                     
                               

        while (strlen(strparm) > 0 && !isprint(strparm[strlen(strparm)-1]))
        {
            strparm[strlen(strparm)-1] = '\0';
        }

                                                    
        if (strlen(strparm) >= 2
         && strparm[0] == '"' && strparm[strlen(strparm) - 1] == '"')
        {
            strparm[strlen(strparm) - 1] = '\0';
            memmove(strparm, strparm + 1, sizeof(strparm) - 1);
        }

        SetVariable(def, strparm);
    }

    fclose (f);
#endif
}

                                                            

void M_SetConfigFilenames(char *main_config, char *extra_config)
{
    default_main_config = main_config;
    default_extra_config = extra_config;
}

  
                 
  

void M_SaveDefaults (void)
{
    SaveDefaultCollection(&doom_defaults);
    SaveDefaultCollection(&extra_defaults);
}

  
                                       
  

void M_SaveDefaultsAlternate(char *main, char *extra)
{
    char *orig_main;
    char *orig_extra;

                                       

    orig_main = doom_defaults.filename;
    orig_extra = extra_defaults.filename;

    doom_defaults.filename = main;
    extra_defaults.filename = extra;

    M_SaveDefaults();

                               

    doom_defaults.filename = orig_main;
    extra_defaults.filename = orig_extra;
}

  
                 
  

void M_LoadDefaults (void)
{
    int i;
 
                                      

       
                  
               
      
                                                                      
               
      

    i = M_CheckParmWithArgs("-config", 1);

    if (i)
    {
	doom_defaults.filename = myargv[i+1];
	printf ("	default file: %s\n",doom_defaults.filename);
    }
    else
    {
        doom_defaults.filename
            = M_StringJoin(configdir, default_main_config, NULL);
    }

    printf("saving config in %s\n", doom_defaults.filename);

       
                  
      
                                                                        
                   
      

    i = M_CheckParmWithArgs("-extraconfig", 1);

    if (i)
    {
        extra_defaults.filename = myargv[i+1];
        printf("        extra configuration file: %s\n", 
               extra_defaults.filename);
    }
    else
    {
        extra_defaults.filename
            = M_StringJoin(configdir, default_extra_config, NULL);
    }

    LoadDefaultCollection(&doom_defaults);
    LoadDefaultCollection(&extra_defaults);
}

                                                

static default_t *GetDefaultForName(char *name)
{
    default_t *result;

                                       

    result = SearchCollection(&doom_defaults, name);

    if (result == NULL)
    {
        result = SearchCollection(&extra_defaults, name);
    }

                                 

    if (result == NULL)
    {
        I_Error("Unknown configuration variable: '%s'", name);
    }

    return result;
}

  
                                                                   
  

void M_BindVariable(char *name, void *location)
{
    default_t *variable;

    variable = GetDefaultForName(name);

    variable->location = location;
    variable->bound = true;
}

                                                                    
                                                                     

boolean M_SetVariable(char *name, char *value)
{
    default_t *variable;

    variable = GetDefaultForName(name);

    if (variable == NULL || !variable->bound)
    {
        return false;
    }

    SetVariable(variable, value);

    return true;
}

                               

int M_GetIntVariable(char *name)
{
    default_t *variable;

    variable = GetDefaultForName(name);

    if (variable == NULL || !variable->bound
     || (variable->type != DEFAULT_INT && variable->type != DEFAULT_INT_HEX))
    {
        return 0;
    }

    return *((int *) variable->location);
}

const char *M_GetStrVariable(char *name)
{
    default_t *variable;

    variable = GetDefaultForName(name);

    if (variable == NULL || !variable->bound
     || variable->type != DEFAULT_STRING)
    {
        return NULL;
    }

    return *((const char **) variable->location);
}

float M_GetFloatVariable(char *name)
{
    default_t *variable;

    variable = GetDefaultForName(name);

    if (variable == NULL || !variable->bound
     || variable->type != DEFAULT_FLOAT)
    {
        return 0;
    }

    return *((float *) variable->location);
}

                                                                
                               

static char *GetDefaultConfigDir(void)
{
    char *result = (char *)malloc(2);
    result[0] = '.';
    result[1] = '\0';

    return result;
}

   
                
  
                                                                        
                                                                      
  

void M_SetConfigDir(char *dir)
{
                                                              

    if (dir != NULL)
    {
        configdir = dir;
    }
    else
    {
        configdir = GetDefaultConfigDir();
    }

    if (strcmp(configdir, "") != 0)
    {
        printf("Using %s for configuration and saves\n", configdir);
    }

                                                      

    M_MakeDirectory(configdir);
}

  
                                                                  
                                      
  

char *M_GetSaveGameDir(char *iwadname)
{
    char *savegamedir;
#if ORIGCODE
    char *topdir;
#endif

                                                                     
                                    

    if (!strcmp(configdir, ""))
    {
    	savegamedir = strdup("");
    }
    else
    {
#if ORIGCODE
                                      

        topdir = M_StringJoin(configdir, "savegame", NULL);
        M_MakeDirectory(topdir);

                                                     

        savegamedir = M_StringJoin(topdir, DIR_SEPARATOR_S, iwadname,
                                   DIR_SEPARATOR_S, NULL);

        M_MakeDirectory(savegamedir);

        free(topdir);
#else
        savegamedir = M_StringJoin(configdir, DIR_SEPARATOR_S, ".savegame/", NULL);

        M_MakeDirectory(savegamedir);

        printf ("Using %s for savegames\n", savegamedir);
#endif
    }

    return savegamedir;
}

