  
                                           
                                      
  
                                                                
                                                              
                                                                 
                                                         
  
                                                                  
                                                                 
                                                                
                                               
  
               
                                       
  


#ifndef __I_TIMER__
#define __I_TIMER__

#define TICRATE 35

                        
                                
int I_GetTime (void);

                             
int I_GetTimeMS (void);

                                     
void I_Sleep(int ms);

                   
void I_InitTimer(void);

                                            
void I_WaitVBL(int count);

#endif

