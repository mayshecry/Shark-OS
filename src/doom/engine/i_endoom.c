  
                                      
  
                                                                
                                                              
                                                                 
                                                         
  
                                                                  
                                                                 
                                                                
                                               
  
               
                                   
  

#include <stdio.h>
#include <string.h>

#include "config.h"
#include "doomtype.h"
#include "i_video.h"

#ifdef ORIGCODE
#include "txt_main.h"
#endif


#ifdef __DJGPP__
#include <go32.h>
#endif              


#define ENDOOM_W 80
#define ENDOOM_H 25

   
                                                            
  

void I_Endoom(byte *endoom_data)
{
#ifdef ORIGCODE
    unsigned char *screendata;
    int y;
    int indent;

                              

    TXT_Init();
    I_InitWindowTitle();
    I_InitWindowIcon();

                                          

    screendata = TXT_GetScreenData();

    indent = (ENDOOM_W - TXT_SCREEN_W) / 2;

    for (y=0; y<TXT_SCREEN_H; ++y)
    {
        memcpy(screendata + (y * TXT_SCREEN_W * 2),
               endoom_data + (y * ENDOOM_W + indent) * 2,
               TXT_SCREEN_W * 2);
    }

                          

    while (true)
    {
        TXT_UpdateScreen();

        if (TXT_GetChar() > 0)
        {
            break;
        }

        TXT_Sleep(0);
    }

                                 

    TXT_Shutdown();

#elif defined(__DJGPP__)

    int y;

                            
                                                                                     
    for (y = 0; y < ENDOOM_H; y++) {
        puts("\n");
    }

                                                                                       
    movedata(_my_ds(), (unsigned) endoom_data, _dos_ds, 0xB8000UL, ENDOOM_W * ENDOOM_H * 2);

#endif
}

