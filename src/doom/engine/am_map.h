  
                                           
                                      
  
                                                                
                                                              
                                                                 
                                                         
  
                                                                  
                                                                 
                                                                
                                               
  
               
                   
  

#ifndef __AMMAP_H__
#define __AMMAP_H__

#include "d_event.h"
#include "m_cheat.h"

                              
#define AM_MSGHEADER (('a'<<24)+('m'<<16))
#define AM_MSGENTERED (AM_MSGHEADER | ('e'<<8))
#define AM_MSGEXITED (AM_MSGHEADER | ('x'<<8))


                       
boolean AM_Responder (event_t* ev);

                       
void AM_Ticker (void);

                       
                                                   
void AM_Drawer (void);

                                      
                                            
void AM_Stop (void);


extern cheatseq_t cheat_amap;


#endif
