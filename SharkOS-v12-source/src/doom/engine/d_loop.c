  
                                           
                                      
  
                                                                
                                                              
                                                                 
                                                         
  
                                                                  
                                                                 
                                                                
                                               
  
               
                      
  

#include <stdlib.h>
#include <string.h>

#include "doomfeatures.h"

#include "d_event.h"
#include "d_loop.h"
#include "d_ticcmd.h"

#include "i_system.h"
#include "i_timer.h"
#include "i_video.h"

#include "m_argv.h"
#include "m_fixed.h"

#include "net_client.h"
#include "net_gui.h"
#include "net_io.h"
#include "net_query.h"
#include "net_server.h"
#include "net_sdl.h"
#include "net_loop.h"

                                                 

typedef struct
{
    ticcmd_t cmds[NET_MAXPLAYERS];
    boolean ingame[NET_MAXPLAYERS];
} ticcmd_set_t;

  
                                                       
                                                             
                                                      
  
                                                            
                    
  

static ticcmd_set_t ticdata[BACKUPTICS];

                                                                     

static int maketic;

                                                               

static int recvtic;

                                                               

int gametic;

                                                                          
                                   

boolean singletics = false;

                             

static int localplayer;

                               

static int      skiptics = 0;

                                                                           
                                                                        

int		ticdup;

                                            

fixed_t         offsetms;

                                     

static boolean  new_sync = true;

                                    

static loop_interface_t *loop_interface = NULL;

                                           
                                                                        
                                                             

static boolean local_playeringame[NET_MAXPLAYERS];

                                                          
                                                                             
                                  

static int player_class;


                                                 

static int GetAdjustedTime(void)
{
    int time_ms;

    time_ms = I_GetTimeMS();

    if (new_sync)
    {
	                                                       
	                           

        time_ms += (offsetms / FRACUNIT);
    }

    return (time_ms * TICRATE) / 1000;
}

static boolean BuildNewTic(void)
{
    int	gameticdiv;
    ticcmd_t cmd;

    gameticdiv = gametic/ticdup;

    I_StartTic ();
    loop_interface->ProcessEvents();

                          

    loop_interface->RunMenu();

    if (drone)
    {
                                                      

        return false;
    }

    if (new_sync)
    {
                                                               
                     

       if (!net_client_connected && maketic - gameticdiv > 2)
           return false;

                                         

       if (maketic - gameticdiv > 8)
           return false;
    }
    else
    {
       if (maketic - gameticdiv >= 5)
           return false;
    }

                                
    memset(&cmd, 0, sizeof(ticcmd_t));
    loop_interface->BuildTiccmd(&cmd, maketic);

#ifdef FEATURE_MULTIPLAYER

    if (net_client_connected)
    {
        NET_CL_SendTiccmd(&cmd, maketic);
    }

#endif
    ticdata[maketic % BACKUPTICS].cmds[localplayer] = cmd;
    ticdata[maketic % BACKUPTICS].ingame[localplayer] = true;

    ++maketic;

    return true;
}

  
            
                                     
                     
  
int      lasttime;

void NetUpdate (void)
{
    int nowtime;
    int newtics;
    int	i;

                                                              
                              

    if (singletics)
        return;

#ifdef FEATURE_MULTIPLAYER

                             

    NET_CL_Run();
    NET_SV_Run();

#endif

                 
    nowtime = GetAdjustedTime() / ticdup;
    newtics = nowtime - lasttime;

    lasttime = nowtime;

    if (skiptics <= newtics)
    {
        newtics -= skiptics;
        skiptics = 0;
    }
    else
    {
        skiptics -= newtics;
        newtics = 0;
    }

                                           

    for (i=0 ; i<newtics ; i++)
    {
        if (!BuildNewTic())
        {
            break;
        }
    }
}

static void D_Disconnected(void)
{
                                                                 

    if (drone)
    {
        I_Error("Disconnected from server in drone mode.");
    }

                               

    printf("Disconnected from server.\n");
}

  
                                                                  
             
  

void D_ReceiveTic(ticcmd_t *ticcmds, boolean *players_mask)
{
    int i;

                                

    if (ticcmds == NULL && players_mask == NULL)
    {
        D_Disconnected();
        return;
    }

    for (i = 0; i < NET_MAXPLAYERS; ++i)
    {
        if (!drone && i == localplayer)
        {
                                               
        }
        else
        {
            ticdata[recvtic % BACKUPTICS].cmds[i] = ticcmds[i];
            ticdata[recvtic % BACKUPTICS].ingame[i] = players_mask[i];
        }
    }

    ++recvtic;
}

  
                  
  
                                                                     
  

void D_StartGameLoop(void)
{
    lasttime = GetAdjustedTime() / ticdup;
}

#if ORIGCODE
  
                                                                  
  

static void BlockUntilStart(net_gamesettings_t *settings,
                            netgame_startup_callback_t callback)
{
    while (!NET_CL_GetSettings(settings))
    {
        NET_CL_Run();
        NET_SV_Run();

        if (!net_client_connected)
        {
            I_Error("Lost connection to server");
        }

        if (callback != NULL && !callback(net_client_wait_data.ready_players,
                                          net_client_wait_data.num_players))
        {
            I_Error("Netgame startup aborted.");
        }

        I_Sleep(100);
    }
}

#endif

void D_StartNetGame(net_gamesettings_t *settings,
                    netgame_startup_callback_t callback)
{
#if ORIGCODE
    int i;

    offsetms = 0;
    recvtic = 0;

    settings->consoleplayer = 0;
    settings->num_players = 1;
    settings->player_classes[0] = player_class;

       
                    
      
                                                               
                                                                  
                     
      
    if (M_CheckParm("-newsync") > 0)
        settings->new_sync = 1;
    else
        settings->new_sync = 0;

                                                                 
                         
                                      
                                 
          
                                 

       
                    
               
      
                                                                     
               
      

    i = M_CheckParmWithArgs("-extratics", 1);

    if (i > 0)
        settings->extratics = atoi(myargv[i+1]);
    else
        settings->extratics = 1;

       
                    
               
      
                                                                   
                                              
      

    i = M_CheckParmWithArgs("-dup", 1);

    if (i > 0)
        settings->ticdup = atoi(myargv[i+1]);
    else
        settings->ticdup = 1;

    if (net_client_connected)
    {
                                                                        
                           

        NET_CL_StartGame(settings);
        BlockUntilStart(settings, callback);

                                                     

        NET_CL_GetSettings(settings);
    }

    if (drone)
    {
        settings->consoleplayer = 0;
    }

                                                      

    localplayer = settings->consoleplayer;

    for (i = 0; i < NET_MAXPLAYERS; ++i)
    {
        local_playeringame[i] = i < settings->num_players;
    }

                                         

    ticdup = settings->ticdup;
    new_sync = settings->new_sync;

                                                    
                    
       
                                                          
       
#else
    settings->consoleplayer = 0;
	settings->num_players = 1;
	settings->player_classes[0] = player_class;
	settings->new_sync = 0;
	settings->extratics = 1;
	settings->ticdup = 1;

	ticdup = settings->ticdup;
	new_sync = settings->new_sync;
#endif
}

boolean D_InitNetGame(net_connect_data_t *connect_data)
{
    boolean result = false;
#ifdef FEATURE_MULTIPLAYER
    net_addr_t *addr = NULL;
    int i;
#endif

                                  

    I_AtExit(D_QuitNetGame, true);

    player_class = connect_data->player_class;

#ifdef FEATURE_MULTIPLAYER

       
                    
      
                                                             
      

    if (M_CheckParm("-server") > 0
     || M_CheckParm("-privateserver") > 0)
    {
        NET_SV_Init();
        NET_SV_AddModule(&net_loop_server_module);
        NET_SV_AddModule(&net_sdl_module);
        NET_SV_RegisterWithMaster();

        net_loop_client_module.InitClient();
        addr = net_loop_client_module.ResolveAddress(NULL);
    }
    else
    {
           
                        
          
                                                               
                              
          

        i = M_CheckParm("-autojoin");

        if (i > 0)
        {
            addr = NET_FindLANServer();

            if (addr == NULL)
            {
                I_Error("No server found on local LAN");
            }
        }

           
                         
                        
          
                                                               
                   
          

        i = M_CheckParmWithArgs("-connect", 1);

        if (i > 0)
        {
            net_sdl_module.InitClient();
            addr = net_sdl_module.ResolveAddress(myargv[i+1]);

            if (addr == NULL)
            {
                I_Error("Unable to resolve '%s'\n", myargv[i+1]);
            }
        }
    }

    if (addr != NULL)
    {
        if (M_CheckParm("-drone") > 0)
        {
            connect_data->drone = true;
        }

        if (!NET_CL_Connect(addr, connect_data))
        {
            I_Error("D_InitNetGame: Failed to connect to %s\n",
                    NET_AddrToString(addr));
        }

        printf("D_InitNetGame: Connected to %s\n", NET_AddrToString(addr));

                                                        

        NET_WaitForLaunch();

        result = true;
    }
#endif

    return result;
}


  
                
                                             
                                    
  
void D_QuitNetGame (void)
{
#ifdef FEATURE_MULTIPLAYER
    NET_SV_Shutdown();
    NET_CL_Disconnect();
#endif
}

static int GetLowTic(void)
{
    int lowtic;

    lowtic = maketic;

#ifdef FEATURE_MULTIPLAYER
    if (net_client_connected)
    {
        if (drone || recvtic < lowtic)
        {
            lowtic = recvtic;
        }
    }
#endif

    return lowtic;
}

static int frameon;
static int frameskip[4];
static int oldnettics;

static void OldNetSync(void)
{
    unsigned int i;
    int keyplayer = -1;

    frameon++;

                                                        
                                                   

    for (i=0 ; i<NET_MAXPLAYERS ; i++)
    {
        if (local_playeringame[i])
        {
            keyplayer = i;
            break;
        }
    }

    if (keyplayer < 0)
    {
                                                               

        return;
    }

    if (localplayer == keyplayer)
    {
                                        
    }
    else
    {
        if (maketic <= recvtic)
        {
            lasttime--;
                            
        }

        frameskip[frameon & 3] = oldnettics > recvtic;
        oldnettics = maketic;

        if (frameskip[0] && frameskip[1] && frameskip[2] && frameskip[3])
        {
            skiptics = 1;
                            
        }
    }
}

                                                 

static boolean PlayersInGame(void)
{
    boolean result = false;
    unsigned int i;

                                                                      
                   

    if (net_client_connected)
    {
        for (i = 0; i < NET_MAXPLAYERS; ++i)
        {
            result = result || local_playeringame[i];
        }
    }

                                                                        
                          

    if (!drone)
    {
        result = true;
    }

    return result;
}

                                                                     
                         

static void TicdupSquash(ticcmd_set_t *set)
{
    ticcmd_t *cmd;
    unsigned int i;

    for (i = 0; i < NET_MAXPLAYERS ; ++i)
    {
        cmd = &set->cmds[i];
        cmd->chatchar = 0;
        if (cmd->buttons & BT_SPECIAL)
            cmd->buttons = 0;
    }
}

                                                                   
                           

static void SinglePlayerClear(ticcmd_set_t *set)
{
    unsigned int i;

    for (i = 0; i < NET_MAXPLAYERS; ++i)
    {
        if (i != localplayer)
        {
            set->ingame[i] = false;
        }
    }
}

  
             
  

void TryRunTics (void)
{
    int	i;
    int	lowtic;
    int	entertic;
    static int oldentertics;
    int realtics;
    int	availabletics;
    int	counts;

                    
    entertic = I_GetTime() / ticdup;
    realtics = entertic - oldentertics;
    oldentertics = entertic;

                                                                    
                 

    if (singletics)
    {
        BuildNewTic();
    }
    else
    {
        NetUpdate ();
    }

    lowtic = GetLowTic();

    availabletics = lowtic - gametic/ticdup;

                                  

    if (new_sync)
    {
	counts = availabletics;
    }
    else
    {
                                      
        if (realtics < availabletics-1)
            counts = realtics+1;
        else if (realtics < availabletics)
            counts = realtics;
        else
            counts = availabletics;

        if (counts < 1)
            counts = 1;

        if (net_client_connected)
        {
            OldNetSync();
        }
    }

    if (counts < 1)
	counts = 1;

                                  

    while (!PlayersInGame() || lowtic < gametic/ticdup + counts)
    {
	NetUpdate ();

        lowtic = GetLowTic();

	if (lowtic < gametic/ticdup)
	    I_Error ("TryRunTics: lowtic < gametic");

                                                                       
                                         

	if (I_GetTime() / ticdup - entertic > 0)
	{
	    return;
	}

        I_Sleep(1);
    }

                                  
    while (counts--)
    {
        ticcmd_set_t *set;

        if (!PlayersInGame())
        {
            return;
        }

        set = &ticdata[(gametic / ticdup) % BACKUPTICS];

        if (!net_client_connected)
        {
            SinglePlayerClear(set);
        }

	for (i=0 ; i<ticdup ; i++)
	{
            if (gametic/ticdup > lowtic)
                I_Error ("gametic>lowtic");

            memcpy(local_playeringame, set->ingame, sizeof(local_playeringame));

            loop_interface->RunTic(set->cmds, set->ingame);
	    gametic++;

	                                         

            TicdupSquash(set);
	}

	NetUpdate ();	                                 
    }
}

void D_RegisterLoopCallbacks(loop_interface_t *i)
{
    loop_interface = i;
}
