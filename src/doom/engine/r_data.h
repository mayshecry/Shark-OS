  
                                           
                                      
  
                                                                
                                                              
                                                                 
                                                         
  
                                                                  
                                                                 
                                                                
                                               
  
               
                                                            
            
  


#ifndef __R_DATA__
#define __R_DATA__

#include "r_defs.h"
#include "r_state.h"


                                          
byte*
R_GetColumn
( int		tex,
  int		col );


                             
void R_InitData (void);
void R_PrecacheLevel (void);


             
                                      
                                 
int R_FlatNumForName (char* name);


                                                  
                                                   
int R_TextureNumForName (char *name);
int R_CheckTextureNumForName (char *name);

#endif
