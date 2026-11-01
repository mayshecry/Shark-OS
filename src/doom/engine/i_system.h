  
                                           
                                      
  
                                                                
                                                              
                                                                 
                                                         
  
                                                                  
                                                                 
                                                                
                                               
  
               
                                   
  


#ifndef __I_SYSTEM__
#define __I_SYSTEM__

#include "d_ticcmd.h"
#include "d_event.h"


typedef void (*atexit_func_t)(void);

                      
void I_Init (void);

                         
                                         
                           
byte*	I_ZoneBase (int *size);

boolean I_ConsoleStdout(void);


                                                                  
                                             
                               

                                
                                          
                                                    
                    
ticcmd_t* I_BaseTiccmd (void);


                                               
                                   
void I_Quit (void);

void I_Error (char *error, ...);

void I_Tactile (int on, int off, int total);

boolean I_GetMemoryValue(unsigned int offset, void *value, int size);

                                                           
                                                              
                               

void I_AtExit(atexit_func_t func, boolean run_if_error);

                                                         

void I_BindVariables(void);

                                          

void I_PrintStartupBanner(char *gamedescription);

                                                            

void I_PrintBanner(char *text);

                                             

void I_PrintDivider(void);

#endif

