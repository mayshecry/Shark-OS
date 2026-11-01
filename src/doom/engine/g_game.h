  
                                           
                                      
  
                                                                
                                                              
                                                                 
                                                         
  
                                                                  
                                                                 
                                                                
                                               
  
               
         
   


#ifndef __G_GAME__
#define __G_GAME__

#include "doomdef.h"
#include "d_event.h"
#include "d_ticcmd.h"


  
       
  
void G_DeathMatchSpawnPlayer (int playernum);

void G_InitNew (skill_t skill, int episode, int map);

                                                    
                                 
                                      
void G_DeferedInitNew (skill_t skill, int episode, int map);

void G_DeferedPlayDemo (char* demo);

                                                    
                                      
void G_LoadGame (char* name);

void G_DoLoadGame (void);

                         
void G_SaveGame (int slot, char* description);

                               
void G_RecordDemo (char* name);

void G_BeginRecording (void);

void G_PlayDemo (char* name);
void G_TimeDemo (char* name);
boolean G_CheckDemoStatus (void);

void G_ExitLevel (void);
void G_SecretExitLevel (void);

void G_WorldDone (void);

                                                                     

void G_BuildTiccmd (ticcmd_t *cmd, int maketic); 

void G_Ticker (void);
boolean G_Responder (event_t*	ev);

void G_ScreenShot (void);

void G_DrawMouseSpeedBox(void);
int G_VanillaVersionCode(void);

extern int vanilla_savegame_limit;
extern int vanilla_demo_limit;
#endif

