#ifndef SMB_H
#define SMB_H

#include <stdint.h>
#include <stdbool.h>

#define SMB_SCREEN_W 320
#define SMB_SCREEN_H 200

extern uint8_t smb_screen[SMB_SCREEN_H][SMB_SCREEN_W];
extern uint32_t smb_palette[256];

void smb_init(void);
void smb_cleanup(void);
void smb_run(void);
void smb_draw_frame(void);
void smb_handle_key(int key);
void smb_set_kernel_mode(void);
void smb_restore_kernel_mode(void);

#endif