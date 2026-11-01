#include "doom.h"
#include <stddef.h>
#include <stdint.h>
#include "engine/i_system.h"
#include "engine/i_timer.h"
#include "engine/i_video.h"
#include "engine/i_sound.h"
#include "engine/i_joystick.h"
#include "engine/m_argv.h"
#include "engine/m_misc.h"
#include "engine/z_zone.h"
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

typedef struct doom_exit_entry {
    atexit_func_t func;
    boolean run_on_error;
    struct doom_exit_entry* next;
} doom_exit_entry_t;

static doom_exit_entry_t* doom_exit_list;

extern void* kmalloc(uint64_t size);
extern void serial_puts(const char* text);

void I_AtExit(atexit_func_t func, boolean run_on_error) {
    doom_exit_entry_t* entry = malloc(sizeof(*entry));
    if (!entry) return;
    entry->func = func;
    entry->run_on_error = run_on_error;
    entry->next = doom_exit_list;
    doom_exit_list = entry;
}

void I_Init(void) {
    I_InitTimer();
    I_InitJoystick();
}

void I_BindVariables(void) {
    I_BindVideoVariables();
    I_BindJoystickVariables();
    I_BindSoundVariables();
}

byte* I_ZoneBase(int* size) {
    int requested = 12;
    int minimum = 6;
    int parm = M_CheckParmWithArgs("-mb", 1);
    if (parm > 0) {
        requested = atoi(myargv[parm + 1]);
        if (requested < minimum) requested = minimum;
    }
    while (requested >= minimum) {
        uint32_t bytes = (uint32_t)requested * 1024u * 1024u;
        void* zone = kmalloc(bytes);
        if (zone) {
            *size = (int)bytes;
            memset(zone, 0, bytes);
            return zone;
        }
        requested--;
    }
    *size = 0;
    return NULL;
}

boolean I_ConsoleStdout(void) {
    return true;
}

void I_Quit(void) {
    doom_exit_entry_t* entry = doom_exit_list;
    while (entry) {
        entry->func();
        entry = entry->next;
    }
    doom_engine_exit();
}

void I_Error(char* error, ...) {
    char message[512];
    va_list args;
    va_start(args, error);
    M_vsnprintf(message, sizeof(message), error, args);
    va_end(args);
    serial_puts("DOOM error: ");
    serial_puts(message);
    serial_puts("\n");
    doom_engine_exit();
}

void I_Tactile(int on, int off, int total) {
    (void)on;
    (void)off;
    (void)total;
}

boolean I_GetMemoryValue(unsigned int offset, void* value, int size) {
    (void)offset;
    (void)value;
    (void)size;
    return false;
}

void I_PrintBanner(char* message) {
    serial_puts(message);
    serial_puts("\n");
}

void I_PrintDivider(void) {
}

void I_PrintStartupBanner(char* description) {
    serial_puts("Starting DOOM: ");
    serial_puts(description);
    serial_puts("\n");
}
