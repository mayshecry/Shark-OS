  
                                           
                                      
  
                                                                
                                                              
                                                                 
                                                         
  
                                                                  
                                                                 
                                                                
                                               
  
               
                   
                                              
                                                                    
  

#ifndef __STSTUFF_H__
#define __STSTUFF_H__

#include "doomtype.h"
#include "d_event.h"
#include "m_cheat.h"

                     
                             
#define ST_HEIGHT	32
#define ST_WIDTH	SCREENWIDTH
#define ST_Y		(SCREENHEIGHT - ST_HEIGHT)


  
             
  

                       
boolean ST_Responder (event_t* ev);

                       
void ST_Ticker (void);

                       
void ST_Drawer (boolean fullscreen, boolean refresh);

                                                           
void ST_Start (void);

                          
void ST_Init (void);



                              
typedef enum
{
    AutomapState,
    FirstPersonState
    
} st_stateenum_t;


                            
typedef enum
{
    StartChatState,
    WaitDestState,
    GetChatState
    
} st_chatstateenum_t;



extern byte *st_backing_screen;
extern cheatseq_t cheat_mus;
extern cheatseq_t cheat_god;
extern cheatseq_t cheat_ammo;
extern cheatseq_t cheat_ammonokey;
extern cheatseq_t cheat_noclip;
extern cheatseq_t cheat_commercial_noclip;
extern cheatseq_t cheat_powerup[7];
extern cheatseq_t cheat_choppers;
extern cheatseq_t cheat_clev;
extern cheatseq_t cheat_mypos;


#endif
