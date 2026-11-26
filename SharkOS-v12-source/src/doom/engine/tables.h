  
                                           
                                        
                                      
  
                                                                
                                                              
                                                                 
                                                         
  
                                                                  
                                                                 
                                                                
                                               
  
               
                 
                                  
                               
  
                                       
                                                  
                                  
  
                                      
                                      
                                                    
  
                                     
                                                  
      


#ifndef __TABLES__
#define __TABLES__

#include "doomtype.h"

#include "m_fixed.h"
	
#define FINEANGLES		8192
#define FINEMASK		(FINEANGLES-1)


                        
#define ANGLETOFINESHIFT	19		

                           
extern const fixed_t finesine[5*FINEANGLES/4];

                                         
extern const fixed_t *finecosine;


                          
extern const fixed_t finetangent[FINEANGLES/2];

                           
extern const byte gammatable[5][256];

                               

#define ANG45           0x20000000
#define ANG90           0x40000000
#define ANG180          0x80000000
#define ANG270          0xc0000000
#define ANG_MAX         0xffffffff

#define ANG1            (ANG45 / 45)
#define ANG60           (ANG180 / 3)

                                                                 
                                                          

#define ANG1_X          0x01000000

#define SLOPERANGE		2048
#define SLOPEBITS		11
#define DBITS			(FRACBITS-SLOPEBITS)

typedef unsigned angle_t;


                          
                                              
                                
extern const angle_t tantoangle[SLOPERANGE+1];


                    
                             
int SlopeDiv(unsigned int num, unsigned int den);


#endif

