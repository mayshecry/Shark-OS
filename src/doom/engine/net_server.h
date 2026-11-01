  
                                      
  
                                                                
                                                              
                                                                 
                                                         
  
                                                                  
                                                                 
                                                                
                                               
  
                      
  

#ifndef NET_SERVER_H
#define NET_SERVER_H

                                             

void NET_SV_Init(void);

                                                  

void NET_SV_Run(void);

                       
                                                                   

void NET_SV_Shutdown(void);

                                                         

void NET_SV_AddModule(net_module_t *module);

                                      

void NET_SV_RegisterWithMaster(void);

#endif                           

