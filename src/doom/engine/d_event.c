  
                                           
                                      
  
                                                                
                                                              
                                                                 
                                                         
  
                                                                  
                                                                 
                                                                
                                               
  
  
                               
  
                                                                       
                                                      
  

#include <stdlib.h>
#include "d_event.h"

#define MAXEVENTS 64

static event_t events[MAXEVENTS];
static int eventhead;
static int eventtail;

  
              
                                                     
  
void D_PostEvent (event_t* ev)
{
    events[eventhead] = *ev;
    eventhead = (eventhead + 1) % MAXEVENTS;
}

                                

event_t *D_PopEvent(void)
{
    event_t *result;

                              

    if (eventtail == eventhead)
    {
        return NULL;
    }
    
    result = &events[eventtail];

                                              

    eventtail = (eventtail + 1) % MAXEVENTS;

    return result;
}


