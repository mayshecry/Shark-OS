  
                                      
  
                                                                
                                                              
                                                                 
                                                         
  
                                                                  
                                                                 
                                                                
                                               
  
               
                           
  

#ifdef ORIGCODE
#include "SDL.h"
#include "SDL_joystick.h"
#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "doomtype.h"
#include "d_event.h"
#include "i_joystick.h"
#include "i_system.h"

#include "m_config.h"
#include "m_misc.h"

                                                           
                                

#define DEAD_ZONE (32768 / 3)

#ifdef ORIGCODE
static SDL_Joystick *joystick = NULL;
#endif

                           

                                               

static int usejoystick = 0;

                                             

static int joystick_index = -1;

                                                                     
                        

static int joystick_x_axis = 0;
static int joystick_x_invert = 0;

                                                                   
                        

static int joystick_y_axis = 1;
static int joystick_y_invert = 0;

                                           

static int joystick_strafe_axis = -1;
static int joystick_strafe_invert = 0;

                                                                      
                         
static int joystick_physical_buttons[NUM_VIRTUAL_BUTTONS] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9
};

void I_ShutdownJoystick(void)
{
#ifdef ORIGCODE
    if (joystick != NULL)
    {
        SDL_JoystickClose(joystick);
        joystick = NULL;
        SDL_QuitSubSystem(SDL_INIT_JOYSTICK);
    }
#endif
}

#ifdef ORIGCODE
static boolean IsValidAxis(int axis)
{
    int num_axes;

    if (axis < 0)
    {
        return true;
    }

    if (IS_BUTTON_AXIS(axis))
    {
        return true;
    }

    if (IS_HAT_AXIS(axis))
    {
        return HAT_AXIS_HAT(axis) < SDL_JoystickNumHats(joystick);
    }

    num_axes = SDL_JoystickNumAxes(joystick);

    return axis < num_axes;
}
#endif

void I_InitJoystick(void)
{
#ifdef ORIGCODE
    if (!usejoystick)
    {
        return;
    }

    if (SDL_Init(SDL_INIT_JOYSTICK) < 0)
    {
        return;
    }

    if (joystick_index < 0 || joystick_index >= SDL_NumJoysticks())
    {
        printf("I_InitJoystick: Invalid joystick ID: %i\n", joystick_index);
        SDL_QuitSubSystem(SDL_INIT_JOYSTICK);
        return;
    }

                        

    joystick = SDL_JoystickOpen(joystick_index);

    if (joystick == NULL)
    {
        printf("I_InitJoystick: Failed to open joystick #%i\n",
               joystick_index);
        SDL_QuitSubSystem(SDL_INIT_JOYSTICK);
        return;
    }

    if (!IsValidAxis(joystick_x_axis)
     || !IsValidAxis(joystick_y_axis)
     || !IsValidAxis(joystick_strafe_axis))
    {
        printf("I_InitJoystick: Invalid joystick axis for joystick #%i "
               "(run joystick setup again)\n",
               joystick_index);

        SDL_JoystickClose(joystick);
        joystick = NULL;
        SDL_QuitSubSystem(SDL_INIT_JOYSTICK);
    }

    SDL_JoystickEventState(SDL_ENABLE);

                        

    printf("I_InitJoystick: %s\n", SDL_JoystickName(joystick_index));

    I_AtExit(I_ShutdownJoystick, true);
#endif
}

#ifdef ORIGCODE
static boolean IsAxisButton(int physbutton)
{
    if (IS_BUTTON_AXIS(joystick_x_axis))
    {
        if (physbutton == BUTTON_AXIS_NEG(joystick_x_axis)
         || physbutton == BUTTON_AXIS_POS(joystick_x_axis))
        {
            return true;
        }
    }
    if (IS_BUTTON_AXIS(joystick_y_axis))
    {
        if (physbutton == BUTTON_AXIS_NEG(joystick_y_axis)
         || physbutton == BUTTON_AXIS_POS(joystick_y_axis))
        {
            return true;
        }
    }
    if (IS_BUTTON_AXIS(joystick_strafe_axis))
    {
        if (physbutton == BUTTON_AXIS_NEG(joystick_strafe_axis)
         || physbutton == BUTTON_AXIS_POS(joystick_strafe_axis))
        {
            return true;
        }
    }

    return false;
}

                                             

static int ReadButtonState(int vbutton)
{
    int physbutton;

                                                        
    if (vbutton < NUM_VIRTUAL_BUTTONS)
    {
        physbutton = joystick_physical_buttons[vbutton];
    }
    else
    {
        physbutton = vbutton;
    }

                                          
    if (IsAxisButton(physbutton))
    {
        return 0;
    }

    return SDL_JoystickGetButton(joystick, physbutton);
}

                                                 

static int GetButtonsState(void)
{
    int i;
    int result;

    result = 0;

    for (i = 0; i < 20; ++i)
    {
        if (ReadButtonState(i))
        {
            result |= 1 << i;
        }
    }

    return result;
}

                                                     

static int GetAxisState(int axis, int invert)
{
    int result;

                              

    if (axis < 0)
    {
        return 0;
    }

                                            
                                             

    result = 0;

    if (IS_BUTTON_AXIS(axis))
    {
        if (SDL_JoystickGetButton(joystick, BUTTON_AXIS_NEG(axis)))
        {
            result -= 32767;
        }
        if (SDL_JoystickGetButton(joystick, BUTTON_AXIS_POS(axis)))
        {
            result += 32767;
        }
    }
    else if (IS_HAT_AXIS(axis))
    {
        int direction = HAT_AXIS_DIRECTION(axis);
        int hatval = SDL_JoystickGetHat(joystick, HAT_AXIS_HAT(axis));

        if (direction == HAT_AXIS_HORIZONTAL)
        {
            if ((hatval & SDL_HAT_LEFT) != 0)
            {
                result -= 32767;
            }
            else if ((hatval & SDL_HAT_RIGHT) != 0)
            {
                result += 32767;
            }
        }
        else if (direction == HAT_AXIS_VERTICAL)
        {
            if ((hatval & SDL_HAT_UP) != 0)
            {
                result -= 32767;
            }
            else if ((hatval & SDL_HAT_DOWN) != 0)
            {
                result += 32767;
            }
        }
    }
    else
    {
        result = SDL_JoystickGetAxis(joystick, axis);

        if (result < DEAD_ZONE && result > -DEAD_ZONE)
        {
            result = 0;
        }
    }

    if (invert)
    {
        result = -result;
    }

    return result;
}
#endif
void I_UpdateJoystick(void)
{
#ifdef ORIGCODE
    if (joystick != NULL)
    {
        event_t ev;

        ev.type = ev_joystick;
        ev.data1 = GetButtonsState();
        ev.data2 = GetAxisState(joystick_x_axis, joystick_x_invert);
        ev.data3 = GetAxisState(joystick_y_axis, joystick_y_invert);
        ev.data4 = GetAxisState(joystick_strafe_axis, joystick_strafe_invert);

        D_PostEvent(&ev);
    }
#endif
}

void I_BindJoystickVariables(void)
{
    int i;

    M_BindVariable("use_joystick",          &usejoystick);
    M_BindVariable("joystick_index",        &joystick_index);
    M_BindVariable("joystick_x_axis",       &joystick_x_axis);
    M_BindVariable("joystick_y_axis",       &joystick_y_axis);
    M_BindVariable("joystick_strafe_axis",  &joystick_strafe_axis);
    M_BindVariable("joystick_x_invert",     &joystick_x_invert);
    M_BindVariable("joystick_y_invert",     &joystick_y_invert);
    M_BindVariable("joystick_strafe_invert",&joystick_strafe_invert);

    for (i = 0; i < NUM_VIRTUAL_BUTTONS; ++i)
    {
        char name[32];
        M_snprintf(name, sizeof(name), "joystick_physical_button%i", i);
        M_BindVariable(name, &joystick_physical_buttons[i]);
    }
}

