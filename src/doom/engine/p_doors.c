  
                                           
                                      
  
                                                                
                                                              
                                                                 
                                                         
  
                                                                  
                                                                 
                                                                
                                               
  
                                                     
  



#include "z_zone.h"
#include "doomdef.h"
#include "deh_main.h"
#include "p_local.h"

#include "s_sound.h"


         
#include "doomstat.h"
#include "r_state.h"

        
#include "dstrings.h"
#include "sounds.h"

#if 0
  
                                 
  
slidename_t	slideFrameNames[MAXSLIDEDOORS] =
{
    {"GDOORF1","GDOORF2","GDOORF3","GDOORF4",	        
     "GDOORB1","GDOORB2","GDOORB3","GDOORB4"},	       
	 
    {"\0","\0","\0","\0"}
};
#endif


  
                 
  

  
                 
  
void T_VerticalDoor (vldoor_t* door)
{
    result_e	res;
	
    switch(door->direction)
    {
      case 0:
	          
	if (!--door->topcountdown)
	{
	    switch(door->type)
	    {
	      case vld_blazeRaise:
		door->direction = -1;                        
		S_StartSound(&door->sector->soundorg, sfx_bdcls);
		break;
		
	      case vld_normal:
		door->direction = -1;                        
		S_StartSound(&door->sector->soundorg, sfx_dorcls);
		break;
		
	      case vld_close30ThenOpen:
		door->direction = 1;
		S_StartSound(&door->sector->soundorg, sfx_doropn);
		break;
		
	      default:
		break;
	    }
	}
	break;
	
      case 2:
	                
	if (!--door->topcountdown)
	{
	    switch(door->type)
	    {
	      case vld_raiseIn5Mins:
		door->direction = 1;
		door->type = vld_normal;
		S_StartSound(&door->sector->soundorg, sfx_doropn);
		break;
		
	      default:
		break;
	    }
	}
	break;
	
      case -1:
	       
	res = T_MovePlane(door->sector,
			  door->speed,
			  door->sector->floorheight,
			  false,1,door->direction);
	if (res == pastdest)
	{
	    switch(door->type)
	    {
	      case vld_blazeRaise:
	      case vld_blazeClose:
		door->sector->specialdata = NULL;
		P_RemoveThinker (&door->thinker);                    
		S_StartSound(&door->sector->soundorg, sfx_bdcls);
		break;
		
	      case vld_normal:
	      case vld_close:
		door->sector->specialdata = NULL;
		P_RemoveThinker (&door->thinker);                    
		break;
		
	      case vld_close30ThenOpen:
		door->direction = 0;
		door->topcountdown = TICRATE*30;
		break;
		
	      default:
		break;
	    }
	}
	else if (res == crushed)
	{
	    switch(door->type)
	    {
	      case vld_blazeClose:
	      case vld_close:		                     
		break;
		
	      default:
		door->direction = 1;
		S_StartSound(&door->sector->soundorg, sfx_doropn);
		break;
	    }
	}
	break;
	
      case 1:
	     
	res = T_MovePlane(door->sector,
			  door->speed,
			  door->topheight,
			  false,1,door->direction);
	
	if (res == pastdest)
	{
	    switch(door->type)
	    {
	      case vld_blazeRaise:
	      case vld_normal:
		door->direction = 0;               
		door->topcountdown = door->topwait;
		break;
		
	      case vld_close30ThenOpen:
	      case vld_blazeOpen:
	      case vld_open:
		door->sector->specialdata = NULL;
		P_RemoveThinker (&door->thinker);                    
		break;
		
	      default:
		break;
	    }
	}
	break;
    }
}


  
                  
                             
  

int
EV_DoLockedDoor
( line_t*	line,
  vldoor_e	type,
  mobj_t*	thing )
{
    player_t*	p;
	
    p = thing->player;
	
    if (!p)
	return 0;
		
    switch(line->special)
    {
      case 99:	            
      case 133:
	if ( !p )
	    return 0;
	if (!p->cards[it_bluecard] && !p->cards[it_blueskull])
	{
	    p->message = DEH_String(PD_BLUEO);
	    S_StartSound(NULL,sfx_oof);
	    return 0;
	}
	break;
	
      case 134:            
      case 135:
	if ( !p )
	    return 0;
	if (!p->cards[it_redcard] && !p->cards[it_redskull])
	{
	    p->message = DEH_String(PD_REDO);
	    S_StartSound(NULL,sfx_oof);
	    return 0;
	}
	break;
	
      case 136:	              
      case 137:
	if ( !p )
	    return 0;
	if (!p->cards[it_yellowcard] &&
	    !p->cards[it_yellowskull])
	{
	    p->message = DEH_String(PD_YELLOWO);
	    S_StartSound(NULL,sfx_oof);
	    return 0;
	}
	break;	
    }

    return EV_DoDoor(line,type);
}


int
EV_DoDoor
( line_t*	line,
  vldoor_e	type )
{
    int		secnum,rtn;
    sector_t*	sec;
    vldoor_t*	door;
	
    secnum = -1;
    rtn = 0;
    
    while ((secnum = P_FindSectorFromLineTag(line,secnum)) >= 0)
    {
	sec = &sectors[secnum];
	if (sec->specialdata)
	    continue;
		
	
	                   
	rtn = 1;
	door = Z_Malloc (sizeof(*door), PU_LEVSPEC, 0);
	P_AddThinker (&door->thinker);
	sec->specialdata = door;

	door->thinker.function.acp1 = (actionf_p1) T_VerticalDoor;
	door->sector = sec;
	door->type = type;
	door->topwait = VDOORWAIT;
	door->speed = VDOORSPEED;
		
	switch(type)
	{
	  case vld_blazeClose:
	    door->topheight = P_FindLowestCeilingSurrounding(sec);
	    door->topheight -= 4*FRACUNIT;
	    door->direction = -1;
	    door->speed = VDOORSPEED * 4;
	    S_StartSound(&door->sector->soundorg, sfx_bdcls);
	    break;
	    
	  case vld_close:
	    door->topheight = P_FindLowestCeilingSurrounding(sec);
	    door->topheight -= 4*FRACUNIT;
	    door->direction = -1;
	    S_StartSound(&door->sector->soundorg, sfx_dorcls);
	    break;
	    
	  case vld_close30ThenOpen:
	    door->topheight = sec->ceilingheight;
	    door->direction = -1;
	    S_StartSound(&door->sector->soundorg, sfx_dorcls);
	    break;
	    
	  case vld_blazeRaise:
	  case vld_blazeOpen:
	    door->direction = 1;
	    door->topheight = P_FindLowestCeilingSurrounding(sec);
	    door->topheight -= 4*FRACUNIT;
	    door->speed = VDOORSPEED * 4;
	    if (door->topheight != sec->ceilingheight)
		S_StartSound(&door->sector->soundorg, sfx_bdopn);
	    break;
	    
	  case vld_normal:
	  case vld_open:
	    door->direction = 1;
	    door->topheight = P_FindLowestCeilingSurrounding(sec);
	    door->topheight -= 4*FRACUNIT;
	    if (door->topheight != sec->ceilingheight)
		S_StartSound(&door->sector->soundorg, sfx_doropn);
	    break;
	    
	  default:
	    break;
	}
		
    }
    return rtn;
}


  
                                                       
  
void
EV_VerticalDoor
( line_t*	line,
  mobj_t*	thing )
{
    player_t*	player;
    sector_t*	sec;
    vldoor_t*	door;
    int		side;
	
    side = 0;	                               

                      
    player = thing->player;
		
    switch(line->special)
    {
      case 26:             
      case 32:
	if ( !player )
	    return;
	
	if (!player->cards[it_bluecard] && !player->cards[it_blueskull])
	{
	    player->message = DEH_String(PD_BLUEK);
	    S_StartSound(NULL,sfx_oof);
	    return;
	}
	break;
	
      case 27:               
      case 34:
	if ( !player )
	    return;
	
	if (!player->cards[it_yellowcard] &&
	    !player->cards[it_yellowskull])
	{
	    player->message = DEH_String(PD_YELLOWK);
	    S_StartSound(NULL,sfx_oof);
	    return;
	}
	break;
	
      case 28:            
      case 33:
	if ( !player )
	    return;
	
	if (!player->cards[it_redcard] && !player->cards[it_redskull])
	{
	    player->message = DEH_String(PD_REDK);
	    S_StartSound(NULL,sfx_oof);
	    return;
	}
	break;
    }
	
                                                  
    sec = sides[ line->sidenum[side^1]] .sector;

    if (sec->specialdata)
    {
	door = sec->specialdata;
	switch(line->special)
	{
	  case	1:                                       
	  case	26:
	  case	27:
	  case	28:
	  case	117:
	    if (door->direction == -1)
		door->direction = 1;	             
	    else
	    {
		if (!thing->player)
		    return;		                                  

                                             
                                                                  
                                                                    

                if (door->thinker.function.acp1 == (actionf_p1) T_VerticalDoor)
                {
                    door->direction = -1;	                               
                }
                else if (door->thinker.function.acp1 == (actionf_p1) T_PlatRaise)
                {
                                                       
                                                                          
                                                                       
                                              
                                                                              
                                                                      

                    plat_t *plat;

                    plat = (plat_t *) door;
                    plat->wait = -1;
                }
                else
                {
                                                                          

                    fprintf(stderr, "EV_VerticalDoor: Tried to close "
                                    "something that wasn't a door.\n");

                                                                             
                                

                    door->direction = -1;
                }
	    }
	    return;
	}
    }
	
                       
    switch(line->special)
    {
      case 117:	                     
      case 118:	                    
	S_StartSound(&sec->soundorg,sfx_bdopn);
	break;
	
      case 1:	                    
      case 31:
	S_StartSound(&sec->soundorg,sfx_doropn);
	break;
	
      default:	                    
	S_StartSound(&sec->soundorg,sfx_doropn);
	break;
    }
	
    
                       
    door = Z_Malloc (sizeof(*door), PU_LEVSPEC, 0);
    P_AddThinker (&door->thinker);
    sec->specialdata = door;
    door->thinker.function.acp1 = (actionf_p1) T_VerticalDoor;
    door->sector = sec;
    door->direction = 1;
    door->speed = VDOORSPEED;
    door->topwait = VDOORWAIT;

    switch(line->special)
    {
      case 1:
      case 26:
      case 27:
      case 28:
	door->type = vld_normal;
	break;
	
      case 31:
      case 32:
      case 33:
      case 34:
	door->type = vld_open;
	line->special = 0;
	break;
	
      case 117:	                     
	door->type = vld_blazeRaise;
	door->speed = VDOORSPEED*4;
	break;
      case 118:	                    
	door->type = vld_blazeOpen;
	line->special = 0;
	door->speed = VDOORSPEED*4;
	break;
    }
    
                                                    
    door->topheight = P_FindLowestCeilingSurrounding(sec);
    door->topheight -= 4*FRACUNIT;
}


  
                                            
  
void P_SpawnDoorCloseIn30 (sector_t* sec)
{
    vldoor_t*	door;
	
    door = Z_Malloc ( sizeof(*door), PU_LEVSPEC, 0);

    P_AddThinker (&door->thinker);

    sec->specialdata = door;
    sec->special = 0;

    door->thinker.function.acp1 = (actionf_p1)T_VerticalDoor;
    door->sector = sec;
    door->direction = 0;
    door->type = vld_normal;
    door->speed = VDOORSPEED;
    door->topcountdown = 30 * TICRATE;
}

  
                                          
  
void
P_SpawnDoorRaiseIn5Mins
( sector_t*	sec,
  int		secnum )
{
    vldoor_t*	door;
	
    door = Z_Malloc ( sizeof(*door), PU_LEVSPEC, 0);
    
    P_AddThinker (&door->thinker);

    sec->specialdata = door;
    sec->special = 0;

    door->thinker.function.acp1 = (actionf_p1)T_VerticalDoor;
    door->sector = sec;
    door->direction = 2;
    door->type = vld_raiseIn5Mins;
    door->speed = VDOORSPEED;
    door->topheight = P_FindLowestCeilingSurrounding(sec);
    door->topheight -= 4*FRACUNIT;
    door->topwait = VDOORWAIT;
    door->topcountdown = 5 * 60 * TICRATE;
}



         
                             

#if 0		                                    
  
                                             
                                                 
  


slideframe_t slideFrames[MAXSLIDEDOORS];

void P_InitSlidingDoorFrames(void)
{
    int		i;
    int		f1;
    int		f2;
    int		f3;
    int		f4;
	
                      
    if ( gamemode != commercial)
	return;
	
    for (i = 0;i < MAXSLIDEDOORS; i++)
    {
	if (!slideFrameNames[i].frontFrame1[0])
	    break;
			
	f1 = R_TextureNumForName(slideFrameNames[i].frontFrame1);
	f2 = R_TextureNumForName(slideFrameNames[i].frontFrame2);
	f3 = R_TextureNumForName(slideFrameNames[i].frontFrame3);
	f4 = R_TextureNumForName(slideFrameNames[i].frontFrame4);

	slideFrames[i].frontFrames[0] = f1;
	slideFrames[i].frontFrames[1] = f2;
	slideFrames[i].frontFrames[2] = f3;
	slideFrames[i].frontFrames[3] = f4;
		
	f1 = R_TextureNumForName(slideFrameNames[i].backFrame1);
	f2 = R_TextureNumForName(slideFrameNames[i].backFrame2);
	f3 = R_TextureNumForName(slideFrameNames[i].backFrame3);
	f4 = R_TextureNumForName(slideFrameNames[i].backFrame4);

	slideFrames[i].backFrames[0] = f1;
	slideFrames[i].backFrames[1] = f2;
	slideFrames[i].backFrames[2] = f3;
	slideFrames[i].backFrames[3] = f4;
    }
}


  
                                        
                             
  
int P_FindSlidingDoorType(line_t*	line)
{
    int		i;
    int		val;
	
    for (i = 0;i < MAXSLIDEDOORS;i++)
    {
	val = sides[line->sidenum[0]].midtexture;
	if (val == slideFrames[i].frontFrames[0])
	    return i;
    }
	
    return -1;
}

void T_SlidingDoor (slidedoor_t*	door)
{
    switch(door->status)
    {
      case sd_opening:
	if (!door->timer--)
	{
	    if (++door->frame == SNUMFRAMES)
	    {
		                             
		sides[door->line->sidenum[0]].midtexture = 0;
		sides[door->line->sidenum[1]].midtexture = 0;
		door->line->flags &= ML_BLOCKING^0xff;
					
		if (door->type == sdt_openOnly)
		{
		    door->frontsector->specialdata = NULL;
		    P_RemoveThinker (&door->thinker);
		    break;
		}
					
		door->timer = SDOORWAIT;
		door->status = sd_waiting;
	    }
	    else
	    {
		                                            
		door->timer = SWAITTICS;
					
		sides[door->line->sidenum[0]].midtexture =
		    slideFrames[door->whichDoorIndex].
		    frontFrames[door->frame];
		sides[door->line->sidenum[1]].midtexture =
		    slideFrames[door->whichDoorIndex].
		    backFrames[door->frame];
	    }
	}
	break;
			
      case sd_waiting:
	                             
	if (!door->timer--)
	{
	                      
	    if (door->frontsector->thinglist != NULL ||
		door->backsector->thinglist != NULL)
	    {
		door->timer = SDOORWAIT;
		break;
	    }

	                                 
	    door->status = sd_closing;
	    door->timer = SWAITTICS;
	}
	break;
			
      case sd_closing:
	if (!door->timer--)
	{
	    if (--door->frame < 0)
	    {
		                             
		door->line->flags |= ML_BLOCKING;
		door->frontsector->specialdata = NULL;
		P_RemoveThinker (&door->thinker);
		break;
	    }
	    else
	    {
		                                            
		door->timer = SWAITTICS;
					
		sides[door->line->sidenum[0]].midtexture =
		    slideFrames[door->whichDoorIndex].
		    frontFrames[door->frame];
		sides[door->line->sidenum[1]].midtexture =
		    slideFrames[door->whichDoorIndex].
		    backFrames[door->frame];
	    }
	}
	break;
    }
}



void
EV_SlidingDoor
( line_t*	line,
  mobj_t*	thing )
{
    sector_t*		sec;
    slidedoor_t*	door;
	
                      
    if (gamemode != commercial)
	return;
    
                                                  
    sec = line->frontsector;
    door = NULL;
    if (sec->specialdata)
    {
	if (!thing->player)
	    return;
			
	door = sec->specialdata;
	if (door->type == sdt_openAndClose)
	{
	    if (door->status == sd_waiting)
		door->status = sd_closing;
	}
	else
	    return;
    }
    
                             
    if (!door)
    {
	door = Z_Malloc (sizeof(*door), PU_LEVSPEC, 0);
	P_AddThinker (&door->thinker);
	sec->specialdata = door;
		
	door->type = sdt_openAndClose;
	door->status = sd_opening;
	door->whichDoorIndex = P_FindSlidingDoorType(line);

	if (door->whichDoorIndex < 0)
	    I_Error("EV_SlidingDoor: Can't use texture for sliding door!");
			
	door->frontsector = sec;
	door->backsector = line->backsector;
	door->thinker.function = T_SlidingDoor;
	door->timer = SWAITTICS;
	door->frame = 0;
	door->line = line;
    }
}
#endif
