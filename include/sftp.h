#ifndef SSH_SFTP_H
#define SSH_SFTP_H

#include <stdint.h>

struct fs_node;

typedef struct {
    uint32_t id;
    uint32_t index;
    struct fs_node* node;
    uint8_t active;
    uint8_t directory;
    uint8_t writable;
} sftp_handle_t;

typedef struct {
    uint32_t version;
    uint32_t input_length;
    uint32_t next_handle;
    struct fs_node* root;
    struct fs_node* cwd;
    uint8_t initialized;
    uint8_t input[65540];
    sftp_handle_t handles[8];
} sftp_session_t;

typedef int (*sftp_send_fn)(void* context, const uint8_t* data, uint32_t length);

void sftp_session_init(sftp_session_t* session, struct fs_node* root);
int sftp_session_feed(sftp_session_t* session, const uint8_t* data, uint32_t length, sftp_send_fn send, void* context);

#endif
