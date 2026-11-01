  
                                           
                                      
  
                                                                
                                                              
                                                                 
                                                         
  
                                                                  
                                                                 
                                                                
                                               
  
               
                     
  


#ifndef __P_PSPR__
#define __P_PSPR__

                    
                                     
#include "m_fixed.h"
#include "tables.h"


  
                                   
                            
                                        
                                                  
                                  
                                
#include "info.h"



  
               
                                                                    
  
#define FF_FULLBRIGHT	0x8000	                       
#define FF_FRAMEMASK	0x7fff



  
                                     
                                     
                                                   
  
typedef enum
{
    ps_weapon,
    ps_flash,
    NUMPSPRITES

} psprnum_t;

typedef struct
{
    state_t*	state;	                                
    int		tics;
    fixed_t	sx;
    fixed_t	sy;

} pspdef_t;

#endif
