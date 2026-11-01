  
                                           
                                      
  
                                                                
                                                              
                                                                 
                                                         
  
                                                                  
                                                                 
                                                                
                                               
  
               
                        
                                                          
                                           
  


#ifndef __V_VIDEO__
#define __V_VIDEO__

#include "doomtype.h"

                                             
#include "v_patch.h"

  
        
  

#define CENTERY			(SCREENHEIGHT/2)


extern int dirtybox[4];

extern byte *tinttable;

                                                   
                                                                          
          
typedef boolean (*vpatchclipfunc_t)(patch_t *, int, int);
void V_SetPatchClipCallback(vpatchclipfunc_t func);


                                                
void V_Init (void);

                                                               

void V_CopyRect(int srcx, int srcy, byte *source,
                int width, int height,
                int destx, int desty);

void V_DrawPatch(int x, int y, patch_t *patch);
void V_DrawPatchFlipped(int x, int y, patch_t *patch);
void V_DrawTLPatch(int x, int y, patch_t *patch);
void V_DrawAltTLPatch(int x, int y, patch_t * patch);
void V_DrawShadowedPatch(int x, int y, patch_t *patch);
void V_DrawXlaPatch(int x, int y, patch_t * patch);                       
void V_DrawPatchDirect(int x, int y, patch_t *patch);

                                                      

void V_DrawBlock(int x, int y, int width, int height, byte *src);

void V_MarkRect(int x, int y, int width, int height);

void V_DrawFilledBox(int x, int y, int w, int h, int c);
void V_DrawHorizLine(int x, int y, int w, int c);
void V_DrawVertLine(int x, int y, int h, int c);
void V_DrawBox(int x, int y, int w, int h, int c);

                         

void V_DrawRawScreen(byte *raw);

                                                                        

void V_UseBuffer(byte *buffer);

                                                             

void V_RestoreBuffer(void);

                                                                   
                                                             
                 

void V_ScreenShot(char *format);

                                                                       
        

void V_LoadTintTable(void);

                  
                                                                      
        

void V_LoadXlaTable(void);

void V_DrawMouseSpeedBox(int speed);

#endif

