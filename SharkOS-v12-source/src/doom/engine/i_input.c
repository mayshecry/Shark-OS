  
                                           
                                      
  
                                                                
                                                              
                                                                 
                                                         
  
                                                                  
                                                                 
                                                                
                                               
  


#include <stdlib.h>
#include <ctype.h>
#include <math.h>
#include <string.h>
#include <fcntl.h>
#include <stdio.h>

#include "config.h"
#include "deh_str.h"
#include "doomtype.h"
#include "doomkeys.h"
#include "i_joystick.h"
#include "i_system.h"
#include "i_swap.h"
#include "i_timer.h"
#include "i_video.h"
#include "i_scale.h"
#include "m_argv.h"
#include "m_config.h"
#include "m_misc.h"
#include "tables.h"
#include "v_video.h"
#include "w_wad.h"
#include "z_zone.h"

#include "doomgeneric.h"

int vanilla_keyboard_mapping = 1;

                                   

static int shiftdown = 0;

                                                             
static const char at_to_doom[] =
{
               0x00,
               KEY_ESCAPE,
               '1',
               '2',
               '3',
               '4',
               '5',
               '6',
               '7',
               '8',
               '9',
               '0',
               '-',
               '=',
               KEY_BACKSPACE,
               KEY_TAB,
               'q',
               'w',
               'e',
               'r',
               't',
               'y',
               'u',
               'i',
               'o',
               'p',
               '[',
               ']',
               KEY_ENTER,
               KEY_FIRE,                 
               'a',
               's',
               'd',
               'f',
               'g',
               'h',
               'j',
               'k',
               'l',
               ';',
               '\'',
               '`',
               KEY_RSHIFT,
               '\\',
               'z',
               'x',
               'c',
               'v',
               'b',
               'n',
               'm',
               ',',
               '.',
               '/',
               KEY_RSHIFT,
               KEYP_MULTIPLY,
               KEY_LALT,
               KEY_USE,
               KEY_CAPSLOCK,
               KEY_F1,
               KEY_F2,
               KEY_F3,
               KEY_F4,
               KEY_F5,
               KEY_F6,
               KEY_F7,
               KEY_F8,
               KEY_F9,
               KEY_F10,
               KEY_NUMLOCK,
               0x0,
               0x0,                         
               0x0,                       
               0x0,                         
               0x0,                    
               0x0,                         
               0x0,                    
               0x0,                          
               0x0,                    
               0x0,                        
               0x0,                         
               0x0,                         
               0x0,                        
               0x0,                        
               0x0,                                           
               0x0,
               0x0,
               0x0,
               0x0,
               0x0,
               0x0,
               0x0,
               0x0,
               0x0,
               0x0,
               0x0,
               0x0,
               0x0,
               0x0,
               0x0,
               0x0,
               0x0,
               0x0,
               KEY_UPARROW,
               0x0,
               KEY_LEFTARROW,
               KEY_RIGHTARROW,
               0x0,
               KEY_DOWNARROW,
               0x0,
               0x0,
               0x0,
               0x0,
               0x0,
               0x0,
               0x0,
               0x0,
               0x0,
               0x0,
               0x0,
               0x0,
               0x0,
               0x0,
               0x0,
               0x0,
               0x0,
               0x0,
               KEY_FIRE,             
};

                                                                     
                                                   
static const char shiftxform[] =
{
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10,
    11, 12, 13, 14, 15, 16, 17, 18, 19, 20,
    21, 22, 23, 24, 25, 26, 27, 28, 29, 30,
    31, ' ', '!', '"', '#', '$', '%', '&',
    '"',           
    '(', ')', '*', '+',
    '<',           
    '_',           
    '>',           
    '?',           
    ')',           
    '!',           
    '@',           
    '#',           
    '$',           
    '%',           
    '^',           
    '&',           
    '*',           
    '(',           
    ':',
    ':',           
    '<',
    '+',           
    '>', '?', '@',
    'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N',
    'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z',
    '[',           
    '!',                                                
    ']',           
    '"', '_',
    '\'',           
    'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N',
    'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z',
    '{', '|', '}', '~', 127
};


static unsigned char TranslateKey(unsigned char key)
{
	return key;

	  
                                 
                               
        
                   
   

              
                            
}

                                                                

static unsigned char GetTypedChar(unsigned char key)
{
    key = TranslateKey(key);

                                                         

    if (shiftdown > 0)
    {
        if (key >= 0 && key < arrlen(shiftxform))
        {
            key = shiftxform[key];
        }
        else
        {
            key = 0;
        }
    }

    return key;
}

static void UpdateShiftStatus(int pressed, unsigned char key)
{
    int change;

    if (pressed) {
        change = 1;
    } else {
        change = -1;
    }

    if (key == KEY_RSHIFT) {
        shiftdown += change;
    }
}


void I_GetEvent(void)
{
    event_t event;
    int pressed;
    unsigned char key;

    
	while (DG_GetKey(&pressed, &key))
    {
        UpdateShiftStatus(pressed, key);

                        
        
        if (pressed)
        {
                                                                 
                                      
            event.type = ev_keydown;
            event.data1 = TranslateKey(key);
            event.data2 = GetTypedChar(key);

            if (event.data1 != 0)
            {
                D_PostEvent(&event);
            }
        }
        else
        {
            event.type = ev_keyup;
            event.data1 = TranslateKey(key);

                                                              
                                                                
                                                               
                                                          
                                                

            event.data2 = 0;

            if (event.data1 != 0)
            {
                D_PostEvent(&event);
            }
            break;
        }
    }


                  
                                 
                                      
                                                 
                                                                    
                                                                     
                                    
                      
                  
}

void I_InitInput(void)
{
}

