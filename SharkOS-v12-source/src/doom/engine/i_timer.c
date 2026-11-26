  
                                           
                                      
  
                                                                
                                                              
                                                                 
                                                         
  
                                                                  
                                                                 
                                                                
                                               
  
               
                        
  

#include "i_timer.h"
#include "doomtype.h"

#include "doomgeneric.h"

#include <stdarg.h>

                       
                     


  
            
                                     
  

static uint32_t basetime = 0;


int I_GetTicks(void)
{
	return DG_GetTicksMs();
}

int  I_GetTime (void)
{
    uint32_t ticks;

    ticks = I_GetTicks();

    if (basetime == 0)
        basetime = ticks;

    ticks -= basetime;

    return (ticks * TICRATE) / 1000;
}


  
                                                      
  

int I_GetTimeMS(void)
{
    uint32_t ticks;

    ticks = I_GetTicks();

    if (basetime == 0)
        basetime = ticks;

    return ticks - basetime;
}

                                     

void I_Sleep(int ms)
{
                    
                         

	DG_SleepMs(ms);
}

void I_WaitVBL(int count)
{
                                   
}


void I_InitTimer(void)
{
                       

                               
}

