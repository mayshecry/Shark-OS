#ifndef SERVER_CLI_H
#define SERVER_CLI_H

#include <stdint.h>
#include "kernel.h"

uint32_t server_execute_remote(const char* command, char* output, uint32_t output_capacity, struct fs_node** working_directory);
struct fs_node* server_webroot_node(void);
void server_format_path(struct fs_node* node, char* output, uint32_t output_capacity);
void server_output_capture_begin(char* output, uint32_t capacity);
uint32_t server_output_capture_end(void);
int server_output_capture_char(char c);
int server_output_capture_active(void);
int server_htop_is_active(void);
void server_htop_start(void);
void server_htop_poll(void);
int server_htop_handle_key(int key);
uint32_t server_htop_render_remote(char* output, uint32_t output_capacity);

#endif
