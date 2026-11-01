  
                                           
                                      
  
                                                                
                                                              
                                                                 
                                                         
  
                                                                  
                                                                 
                                                                
                                               
  
               
  


#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "doomtype.h"
#include "i_system.h"
#include "m_misc.h"
#include "m_argv.h"                                  

int		myargc;
char**		myargv;




  
              
                                 
                                           
                                            
                      
  

int M_CheckParmWithArgs(char *check, int num_args)
{
    int i;

    for (i = 1; i < myargc - num_args; i++)
    {
	if (!strcasecmp(check, myargv[i]))
	    return i;
    }

    return 0;
}

  
               
  
                                                                      
                                
  

boolean M_ParmExists(char *check)
{
    return M_CheckParm(check) != 0;
}

int M_CheckParm(char *check)
{
    return M_CheckParmWithArgs(check, 0);
}

#define MAXARGVS        100

static void LoadResponseFile(int argv_index)
{
#if ORIGCODE
    FILE *handle;
    int size;
    char *infile;
    char *file;
    char *response_filename;
    char **newargv;
    int newargc;
    int i, k;

    response_filename = myargv[argv_index] + 1;

                                         
    handle = fopen(response_filename, "rb");

    if (handle == NULL)
    {
        printf ("\nNo such response file!");
#if ORIGCODE
        exit(1);
#endif
    }

    printf("Found response file %s!\n", response_filename);

    size = M_FileLength(handle);

                              
                                                                     
                                                                    
              

    file = malloc(size + 1);

    i = 0;

    while (i < size)
    {
        k = fread(file + i, 1, size - i, handle);

        if (k < 0)
        {
            I_Error("Failed to read full contents of '%s'", response_filename);
        }

        i += k;
    }

    fclose(handle);

                                      

    newargv = malloc(sizeof(char *) * MAXARGVS);
    newargc = 0;
    memset(newargv, 0, sizeof(char *) * MAXARGVS);

                                                                 

    for (i=0; i<argv_index; ++i)
    {
        newargv[i] = myargv[i];
        ++newargc;
    }

    infile = file;
    k = 0;

    while(k < size)
    {
                                                          

        while(k < size && isspace((int)infile[k]))
        {
            ++k;
        }

        if (k >= size)
        {
            break;
        }

                                                                 
                                                                         
                           

        if (infile[k] == '\"')
        {
                                          
            ++k;

            newargv[newargc++] = &infile[k];

                                                 

            while (k < size && infile[k] != '\"' && infile[k] != '\n')
            {
                ++k;
            }

            if (k >= size || infile[k] == '\n')
            {
                I_Error("Quotes unclosed in response file '%s'",
                        response_filename);
            }

                                                      

            infile[k] = '\0';
            ++k;
        }
        else
        {
                                                                 

            newargv[newargc++] = &infile[k];

            while(k < size && !isspace((int)infile[k]))
            {
                ++k;
            }

                                                                 

            infile[k] = '\0';

            ++k;
        }
    }

                                                         

    for (i=argv_index + 1; i<myargc; ++i)
    {
        newargv[newargc] = myargv[i];
        ++newargc;
    }

    myargv = newargv;
    myargc = newargc;

#if 0
                                                
                        

    printf("%d command-line args:\n", myargc);

    for (k=1; k<myargc; k++)
    {
        printf("'%s'\n", myargv[k]);
    }
#endif
#endif
}

  
                       
  

void M_FindResponseFile(void)
{
    int             i;

    for (i = 1; i < myargc; i++)
    {
        if (myargv[i][0] == '@')
        {
            LoadResponseFile(i);
        }
    }
}

                                                               

char *M_GetExecutableName(void)
{
    char *sep;

    sep = strrchr(myargv[0], DIR_SEPARATOR);

    if (sep == NULL)
    {
        return myargv[0];
    }
    else
    {
        return sep + 1;
    }
}

