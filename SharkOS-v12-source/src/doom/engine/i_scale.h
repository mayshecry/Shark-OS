  
                                           
                                      
  
                                                                
                                                              
                                                                 
                                                         
  
                                                                  
                                                                 
                                                                
                                               
  
               
                                          
  


#ifndef __I_SCALE__
#define __I_SCALE__

#include "doomtype.h"

void I_InitScale(byte *_src_buffer, byte *_dest_buffer, int _dest_pitch);
void I_ResetScaleTables(byte *palette);

                                             

extern screen_mode_t mode_scale_1x;
extern screen_mode_t mode_scale_2x;
extern screen_mode_t mode_scale_3x;
extern screen_mode_t mode_scale_4x;
extern screen_mode_t mode_scale_5x;

                                                               

extern screen_mode_t mode_stretch_1x;
extern screen_mode_t mode_stretch_2x;
extern screen_mode_t mode_stretch_3x;
extern screen_mode_t mode_stretch_4x;
extern screen_mode_t mode_stretch_5x;

                                                                

extern screen_mode_t mode_squash_1x;
extern screen_mode_t mode_squash_2x;
extern screen_mode_t mode_squash_3x;
extern screen_mode_t mode_squash_4x;
extern screen_mode_t mode_squash_5x;

#endif                          

