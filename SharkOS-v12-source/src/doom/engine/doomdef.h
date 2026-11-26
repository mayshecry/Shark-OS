  
                                           
                                      
  
                                                                
                                                              
                                                                 
                                                         
  
                                                                  
                                                                 
                                                                
                                               
  
               
                                                             
                         
  

#ifndef __DOOMDEF__
#define __DOOMDEF__

#include <stdio.h>
#include <string.h>

#include "doomtype.h"
#include "i_timer.h"
#include "d_mode.h"

  
                             
  
               
#define DOOM_VERSION 109

                                                 
#define DOOM_191_VERSION 111


                              
                                                                
#define RANGECHECK

                                                         
#define MAXPLAYERS 4

                                                
                                              
                                        
typedef enum
{
    GS_LEVEL,
    GS_INTERMISSION,
    GS_FINALE,
    GS_DEMOSCREEN,
} gamestate_t;

typedef enum
{
    ga_nothing,
    ga_loadlevel,
    ga_newgame,
    ga_loadgame,
    ga_savegame,
    ga_playdemo,
    ga_completed,
    ga_victory,
    ga_worlddone,
    ga_screenshot
} gameaction_t;

  
                                     
  

               
#define	MTF_EASY		1
#define	MTF_NORMAL		2
#define	MTF_HARD		4

                                       
#define	MTF_AMBUSH		8


  
             
  
typedef enum
{
    it_bluecard,
    it_yellowcard,
    it_redcard,
    it_blueskull,
    it_yellowskull,
    it_redskull,
    
    NUMCARDS
    
} card_t;



                       
                                 
                                
typedef enum
{
    wp_fist,
    wp_pistol,
    wp_shotgun,
    wp_chaingun,
    wp_missile,
    wp_plasma,
    wp_bfg,
    wp_chainsaw,
    wp_supershotgun,

    NUMWEAPONS,
    
                                
    wp_nochange

} weapontype_t;


                            
typedef enum
{
    am_clip,	                          
    am_shell,	                                     
    am_cell,	                     
    am_misl,	                    
    NUMAMMO,
    am_noammo	                                  

} ammotype_t;


                      
typedef enum
{
    pw_invulnerability,
    pw_strength,
    pw_invisibility,
    pw_ironfeet,
    pw_allmap,
    pw_infrared,
    NUMPOWERS
    
} powertype_t;



  
                      
                                     
                                        
  
typedef enum
{
    INVULNTICS	= (30*TICRATE),
    INVISTICS	= (60*TICRATE),
    INFRATICS	= (120*TICRATE),
    IRONTICS	= (60*TICRATE)
    
} powerduration_t;

#endif                        
