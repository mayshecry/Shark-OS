  
                                           
                                      
  
                                                                
                                                              
                                                                 
                                                         
  
                                                                  
                                                                 
                                                                
                                               
  
               
                   
  

#ifndef __D_LOOP__
#define __D_LOOP__

#include "net_defs.h"

                                                                    
                                                                   
                                                        

typedef boolean (*netgame_startup_callback_t)(int ready_players,
                                              int num_players);

typedef struct
{
                                                          

    void (*ProcessEvents)();

                                                                         
                                                  

    void (*BuildTiccmd)(ticcmd_t *cmd, int maketic);

                                                                          

    void (*RunTic)(ticcmd_t *cmds, boolean *ingame);

                                                     

    void (*RunMenu)();
} loop_interface_t;

                                                             
void D_RegisterLoopCallbacks(loop_interface_t *i);

                                                         
void NetUpdate (void);

                                              
                          
void D_QuitNetGame (void);

                          
void TryRunTics (void);

                                                    
void D_StartGameLoop(void);

                                                    

boolean D_InitNetGame(net_connect_data_t *connect_data);

                                                                    
                                         

void D_StartNetGame(net_gamesettings_t *settings,
                    netgame_startup_callback_t callback);

extern boolean singletics;
extern int gametic, ticdup;

#endif

