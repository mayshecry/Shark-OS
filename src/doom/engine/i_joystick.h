  
                                      
  
                                                                
                                                              
                                                                 
                                                         
  
                                                                  
                                                                 
                                                                
                                               
  
               
                                           
  


#ifndef __I_JOYSTICK__
#define __I_JOYSTICK__

                                                                       
                                                                    
                                                                      
#define NUM_VIRTUAL_BUTTONS 10

                                                                     
                                                                     
                                                                  
                                                                     
                                                                  
                            
#define BUTTON_AXIS 0x10000

                                                            
#define IS_BUTTON_AXIS(axis) ((axis) >= 0 && ((axis) & BUTTON_AXIS) != 0)

                                                       
#define BUTTON_AXIS_NEG(axis)  ((axis) & 0xff)
#define BUTTON_AXIS_POS(axis)  (((axis) >> 8) & 0xff)

                                                     
#define CREATE_BUTTON_AXIS(neg, pos) (BUTTON_AXIS | (neg) | ((pos) << 8))

                                                                  
                                                                       
                            
#define HAT_AXIS    0x20000

#define IS_HAT_AXIS(axis) ((axis) >= 0 && ((axis) & HAT_AXIS) != 0)

                                            
#define HAT_AXIS_HAT(axis)         ((axis) & 0xff)
                                                 
#define HAT_AXIS_DIRECTION(axis)   (((axis) >> 8) & 0xff)

#define CREATE_HAT_AXIS(hat, direction) \
    (HAT_AXIS | (hat) | ((direction) << 8))

#define HAT_AXIS_HORIZONTAL 1
#define HAT_AXIS_VERTICAL   2

void I_InitJoystick(void);
void I_ShutdownJoystick(void);
void I_UpdateJoystick(void);

void I_BindJoystickVariables(void);

#endif                             

