  
                                           
                                      
  
                                                                
                                                              
                                                                 
                                                         
  
                                                                  
                                                                 
                                                                
                                               
  
               
                           
                   
  


#include "z_zone.h"
#include "p_local.h"

#include "doomstat.h"


int	leveltime;

  
           
                                               
                                        
                                           
                                           
  



                                              
thinker_t	thinkercap;


  
                 
  
void P_InitThinkers (void)
{
    thinkercap.prev = thinkercap.next  = &thinkercap;
}




  
               
                                             
  
void P_AddThinker (thinker_t* thinker)
{
    thinkercap.prev->next = thinker;
    thinker->next = &thinkercap;
    thinker->prev = thinkercap.prev;
    thinkercap.prev = thinker;
}



  
                  
                                                        
                                    
  
void P_RemoveThinker (thinker_t* thinker)
{
                
  thinker->function.acv = (actionf_v)(-1);
}



  
                    
                                                                  
  
void P_AllocateThinker (thinker_t*	thinker)
{
}



  
                
  
void P_RunThinkers (void)
{
    thinker_t*	currentthinker;

    currentthinker = thinkercap.next;
    while (currentthinker != &thinkercap)
    {
	if ( currentthinker->function.acv == (actionf_v)(-1) )
	{
	                        
	    currentthinker->next->prev = currentthinker->prev;
	    currentthinker->prev->next = currentthinker->next;
	    Z_Free (currentthinker);
	}
	else
	{
	    if (currentthinker->function.acp1)
		currentthinker->function.acp1 (currentthinker);
	}
	currentthinker = currentthinker->next;
    }
}



  
           
  

void P_Ticker (void)
{
    int		i;
    
                  
    if (paused)
	return;
		
                                                         
    if ( !netgame
	 && menuactive
	 && !demoplayback
	 && players[consoleplayer].viewz != 1)
    {
	return;
    }
    
		
    for (i=0 ; i<MAXPLAYERS ; i++)
	if (playeringame[i])
	    P_PlayerThink (&players[i]);
			
    P_RunThinkers ();
    P_UpdateSpecials ();
    P_RespawnSpecials ();

                    
    leveltime++;	
}
