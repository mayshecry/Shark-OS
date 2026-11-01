  
                                           
                                      
  
                                                                
                                                              
                                                                 
                                                         
  
                                                                  
                                                                 
                                                                
                                               
  
               
                                                                        
          
  

#ifndef __D_MODE__
#define __D_MODE__

#include "doomtype.h"

                                                   

typedef enum
{
    doom,                     
    doom2,                    
    pack_tnt,                                     
    pack_plut,                                             
    pack_chex,                                  
    pack_hacx,                             
    heretic,                   
    hexen,                   
    strife,                   

    none
} GameMission_t;

                                                                        
                                                                       
                                      

typedef enum
{
    shareware,                                
    registered,                                
    commercial,                      
    retail,                          
    indetermined                
} GameMode_t;

                                 

typedef enum
{
    exe_doom_1_2,                                         
    exe_doom_1_666,                                                         
    exe_doom_1_7,                       
    exe_doom_1_8,                  
    exe_doom_1_9,                  
    exe_hacx,               
    exe_ultimate,                             
    exe_final,                    
    exe_final2,                                   
    exe_chex,                                                      

    exe_heretic_1_3,               

    exe_hexen_1_1,               
    exe_strife_1_2,                
    exe_strife_1_31                 
} GameVersion_t;

               

typedef enum
{
    sk_noitems = -1,                              
    sk_baby = 0,
    sk_easy,
    sk_medium,
    sk_hard,
    sk_nightmare
} skill_t;

boolean D_ValidGameMode(GameMission_t mission, GameMode_t mode);
boolean D_ValidGameVersion(GameMission_t mission, GameVersion_t version);
boolean D_ValidEpisodeMap(GameMission_t mission, GameMode_t mode,
                          int episode, int map);
int D_GetNumEpisodes(GameMission_t mission, GameMode_t mode);
boolean D_IsEpisodeMap(GameMission_t mission);
char *D_GameMissionString(GameMission_t mission);

#endif                         

