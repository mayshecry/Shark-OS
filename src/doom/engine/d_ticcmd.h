  
                                           
                                        
                                      
  
                                                                
                                                              
                                                                 
                                                         
  
                                                                  
                                                                 
                                                                
                                               
  
               
                                   
  


#ifndef __D_TICCMD__
#define __D_TICCMD__

#include "doomtype.h"


                                            
                                                
                                                  
                                                  

typedef struct
{
    signed char	forwardmove;	                 
    signed char	sidemove;	                 
    short angleturn;                                   
    byte chatchar;
    byte buttons;
                                            
                                         
    byte consistancy;                                 

                                

    byte buttons2;
    int inventory;
   
                              

    byte lookfly;                                            
    byte arti;                                      
} ticcmd_t;



#endif
