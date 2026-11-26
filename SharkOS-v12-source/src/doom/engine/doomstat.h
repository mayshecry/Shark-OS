  
                                           
                                      
  
                                                                
                                                              
                                                                 
                                                         
  
                                                                  
                                                                 
                                                                
                                               
  
               
                                                            
                                                             
                                                           
                                                          
                       
                                         
  


#ifndef __D_STATE__
#define __D_STATE__

                                           
                                            
#include "doomdata.h"
#include "d_loop.h"

                                            
#include "d_player.h"

                    
#include "d_mode.h"

#include "net_defs.h"



                           
                           
  
extern  boolean	nomonsters;	                           
extern  boolean	respawnparm;	                        
extern  boolean	fastparm;	                     

extern  boolean	devparm;	                                


                                                        
                                                      
  
extern GameMode_t	gamemode;
extern GameMission_t	gamemission;
extern GameVersion_t    gameversion;
extern char            *gamedescription;

                                                             
extern boolean bfgedition;

                     
                                                                      
                                                                      
                                

#define logical_gamemission                             \
    (gamemission == pack_chex ? doom :                  \
     gamemission == pack_hacx ? doom2 : gamemission)

                                             
extern  boolean	modifiedgame;


                                              
                                
  

                               
extern  skill_t		startskill;
extern  int             startepisode;
extern	int		startmap;

                                                                   
                                                                    

extern  int             startloadgame;

extern  boolean		autostart;

                     
extern  skill_t         gameskill;
extern  int		gameepisode;
extern  int		gamemap;

                                                           
extern  int             timelimit;

                                      
extern  boolean         respawnmonsters;

                                   
extern  boolean	netgame;

                                          
extern int deathmatch;

                            
                                           
                                              
                                          
                                               

                 
                                       
                                    
                             
extern int sfxVolume;
extern int musicVolume;

                                         
                                          
                                        
                                      
extern int snd_MusicDevice;
extern int snd_SfxDevice;
                                         
extern int snd_DesiredMusicDevice;
extern int snd_DesiredSfxDevice;


                            
                            
  

                                          
                                           
                           
extern  boolean statusbaractive;

extern  boolean automapactive;	                   
extern  boolean	menuactive;	                  
extern  boolean	paused;		              


extern  boolean		viewactive;

extern  boolean		nodrawers;


extern  boolean         testcontrols;
extern  int             testcontrols_mousespeed;




                                                    
                                    
extern  int	viewangleoffset;

                                        
extern  int	consoleplayer;	
extern  int	displayplayer;


                                        
                  
                                               
  
extern  int	totalkills;
extern	int	totalitems;
extern	int	totalsecret;

                     
extern  int	levelstarttic;	                         
extern  int	leveltime;	                            



                                         
                                         
                                              
                         
extern  boolean	usergame;

   
extern  boolean	demoplayback;
extern  boolean	demorecording;

                                                                    
                                       

extern boolean lowres_turn;

                                          
extern  boolean		singledemo;	




   
extern  gamestate_t     gamestate;






                               
                              
                                               
                                              
                                    



                                  
extern	player_t	players[MAXPLAYERS];

                       
extern  boolean		playeringame[MAXPLAYERS];


                                     
#define MAX_DM_STARTS   10
extern  mapthing_t      deathmatchstarts[MAX_DM_STARTS];
extern  mapthing_t*	deathmatch_p;

                      
extern  mapthing_t      playerstarts[MAXPLAYERS];

                      
                                           
extern  wbstartstruct_t		wminfo;	







                                           
                                        
  

                       
extern  char *          savegamedir;
extern	char		basedefault[1024];

                                           
extern  boolean         precache;


                                 
                                    
extern  gamestate_t     wipegamestate;

extern  int             mouseSensitivity;

extern  int             bodyqueslot;



                                                    
                      
                                        
extern int		skyflatnum;



                                                      


extern	int		rndindex;

extern  ticcmd_t       *netcmds;


#endif
