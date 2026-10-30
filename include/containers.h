#ifndef SHARKOS_CONTAINERS_H
#define SHARKOS_CONTAINERS_H

#include <stdint.h>

#define SHARK_CONTAINER_LIMIT 4
#define SHARK_CONTAINER_NETWORK_BRIDGE 0
#define SHARK_CONTAINER_NETWORK_NAT 1

struct fs_node;

typedef enum {
    SHARK_CONTAINER_EMPTY = 0,
    SHARK_CONTAINER_STOPPED = 1,
    SHARK_CONTAINER_RUNNING = 2
} shark_container_state_t;

typedef struct {
    char name[32];
    struct fs_node* root;
    struct fs_node* cwd;
    uint8_t mac[6];
    uint8_t network_mode;
    shark_container_state_t state;
} shark_container_t;

void shark_containers_init(void);
int shark_container_create(const char* name);
int shark_container_start(uint32_t index);
int shark_container_start_mode(uint32_t index, uint32_t mode);
int shark_container_stop(uint32_t index);
int shark_container_remove(uint32_t index);
int shark_container_set_cwd(uint32_t index, struct fs_node* cwd);
int shark_container_find(const char* name);
const shark_container_t* shark_container_get(uint32_t index);

#endif
