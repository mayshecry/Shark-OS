  
                                           
                                      
  
                                                                
                                                              
                                                                 
                                                         
  
                                                                  
                                                                 
                                                                
                                               
  
               
                                   
  


#ifndef __I_VIDEO__
#define __I_VIDEO__

#include "doomtype.h"

                           

#define SCREENWIDTH  320
#define SCREENHEIGHT 200

                                                 

#define SCREENWIDTH_4_3 256

                                                    

#define SCREENHEIGHT_4_3 240

#define MAX_MOUSE_BUTTONS 8

typedef struct
{
                              

    int width;
    int height;

                                                            
                                                 
      
                                           

    void (*InitMode)(byte *palette);
    
                                                                  
                                          

    boolean (*DrawScreen)(int x1, int y1, int x2, int y2);

                                                              
                                                                 
                          
      
                                                                  
      
                                                                    
                                                                   
                                                                    
                
      
                                                               
                                                                     
                                                                     
                                                                     
                                                                   
                                                           
      
                                                                     
                                                                      
                                                                    
                                                               
               
      
                                                                    
                                                                    
                                                                   
                                                                   
                                                                     

    boolean poor_quality;
} screen_mode_t;

typedef boolean (*grabmouse_callback_t)(void);

                        
                                        
                             
void I_InitGraphics (void);

void I_GraphicsCheckCommandLine(void);

void I_ShutdownGraphics(void);

                           
void I_SetPalette (byte* palette);
int I_GetPaletteIndex(int r, int g, int b);

void I_UpdateNoBlit (void);
void I_FinishUpdate (void);

void I_ReadScreen (byte* scr);

void I_BeginRead (void);

void I_SetWindowTitle(char *title);

void I_CheckIsScreensaver(void);
void I_SetGrabMouseCallback(grabmouse_callback_t func);

void I_DisplayFPSDots(boolean dots_on);
void I_BindVideoVariables(void);

void I_InitWindowTitle(void);
void I_InitWindowIcon(void);

                                                                                
                                                                              

void I_StartFrame (void);

                                                
                                                  

void I_StartTic (void);

                                                                  

void I_EnableLoadingDisk(void);

void I_EndRead (void);

struct color {
    uint32_t b:8;
    uint32_t g:8;
    uint32_t r:8;
    uint32_t a:8;
};


extern char *video_driver;
extern boolean screenvisible;

extern float mouse_acceleration;
extern int mouse_threshold;
extern int vanilla_keyboard_mapping;
extern boolean screensaver_mode;
extern int usegamma;
extern byte *I_VideoBuffer;

extern int screen_width;
extern int screen_height;
extern int screen_bpp;
extern int fullscreen;
extern int aspect_ratio_correct;

extern int show_diskicon;
extern int diskicon_readbytes;

#ifdef CMAP256

extern boolean palette_changed;
extern struct color colors[256];

#endif            

#endif
