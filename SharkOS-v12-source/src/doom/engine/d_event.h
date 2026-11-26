  
                                           
                                      
  
                                                                
                                                              
                                                                 
                                                         
  
                                                                  
                                                                 
                                                                
                                               
  
               
  
      


#ifndef __D_EVENT__
#define __D_EVENT__


#include "doomtype.h"


  
                  
  

                     
typedef enum
{
    ev_keydown,
    ev_keyup,
    ev_mouse,
    ev_joystick,
    ev_quit
} evtype_t;

                   
typedef struct
{
    evtype_t type;

                                                            
      
                           
                                                               
                                     
                                                              
                                                                
                                       
      
                
                                                         
                                                               
                                              
                                                          
      
                   
                                                       
                                              
                                                          
                                                    

    int data1, data2, data3, data4;
} event_t;

 
  
                                  
  
typedef enum
{
                    
    BT_ATTACK		= 1,
                                                    
    BT_USE		= 2,

                                             
    BT_SPECIAL		= 128,
    BT_SPECIALMASK	= 3,
    
                                   
                                                
    BT_CHANGE		= 4,
                                                   
    BT_WEAPONMASK	= (8+16+32),
    BT_WEAPONSHIFT	= 3,

                      
    BTS_PAUSE		= 1,
                                     
    BTS_SAVEGAME	= 2,

                            
                                              
    BTS_SAVEMASK	= (4+8+16),
    BTS_SAVESHIFT 	= 2,
  
} buttoncode_t;

                                          
                      
typedef enum
{
                          
    BT2_LOOKUP          = 1,
                            
    BT2_LOOKDOWN        = 2,
                           
    BT2_CENTERVIEW      = 4,
                         
    BT2_INVUSE          = 8,
                          
    BT2_INVDROP         = 16,
                       
    BT2_JUMP            = 32,
                 
    BT2_HEALTH          = 128,
  
} buttoncode2_t;




                                                 
void D_PostEvent (event_t *ev);

                                     

event_t *D_PopEvent(void);


#endif

