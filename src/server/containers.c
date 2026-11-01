#include "kernel.h"
#include "containers.h"
#include "net.h"

static shark_container_t shark_containers[SHARK_CONTAINER_LIMIT];

static int shark_container_name_valid(const char* name) {
    if (!name || !name[0]) return 0;
    uint32_t length = (uint32_t)strlen(name);
    if (length >= sizeof(shark_containers[0].name)) return 0;
    for (uint32_t i = 0; i < length; i++) {
        char c = name[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '-' || c == '_')) return 0;
    }
    return 1;
}

static void shark_container_make_mac(uint32_t index, const char* name, uint8_t mac[6]) {
    uint32_t hash = 2166136261u;
    for (uint32_t i = 0; name[i]; i++) hash = (hash ^ (uint8_t)name[i]) * 16777619u;
    mac[0] = 0x02;
    mac[1] = 0x53;
    mac[2] = 0x48;
    mac[3] = (uint8_t)(index + 1);
    mac[4] = (uint8_t)(hash >> 8);
    mac[5] = (uint8_t)hash;
}

static void shark_container_remove_tree(struct fs_node* node) {
    if (!node) return;
    while (node->num_children > 0) {
        struct fs_node* child = node->children[node->num_children - 1];
        shark_container_remove_tree(child);
        if (child->allocated && child->parent) fs_node_remove(child);
    }
    if (node->content && node->type == FS_FILE) {
        node->content_len = 0;
        node->content[0] = 0;
    }
}

void shark_containers_init(void) {
    memset(shark_containers, 0, sizeof(shark_containers));
}

int shark_container_find(const char* name) {
    if (!name) return -1;
    for (uint32_t i = 0; i < SHARK_CONTAINER_LIMIT; i++) {
        if (shark_containers[i].state != SHARK_CONTAINER_EMPTY &&
            strcmp(shark_containers[i].name, name) == 0) return (int)i;
    }
    return -1;
}

const shark_container_t* shark_container_get(uint32_t index) {
    if (index >= SHARK_CONTAINER_LIMIT || shark_containers[index].state == SHARK_CONTAINER_EMPTY) return NULL;
    return &shark_containers[index];
}

int shark_container_create(const char* name) {
    if (!shark_container_name_valid(name) || shark_container_find(name) >= 0) return -1;
    uint32_t slot = SHARK_CONTAINER_LIMIT;
    for (uint32_t i = 0; i < SHARK_CONTAINER_LIMIT; i++) {
        if (shark_containers[i].state == SHARK_CONTAINER_EMPTY) { slot = i; break; }
    }
    if (slot == SHARK_CONTAINER_LIMIT) return -1;

    struct fs_node* instance_root = create_node(name, FS_DIRECTORY, NULL);
    if (!instance_root) return -1;
    struct fs_node* user = create_node("User", FS_DIRECTORY, instance_root);
    struct fs_node* etc = create_node("etc", FS_DIRECTORY, instance_root);
    struct fs_node* var = create_node("var", FS_DIRECTORY, instance_root);
    struct fs_node* www = user ? create_node("www", FS_DIRECTORY, user) : NULL;
    struct fs_node* index = www ? create_node("index.html", FS_FILE, www) : NULL;
    const char* page = "<!doctype html><html><head><title>SharkOS Server Container</title></head><body><h1>SharkOS Server Container</h1></body></html>\n";
    if (!user || !etc || !var || !www || !index ||
        fs_node_write(index, 0, page, (uint32_t)strlen(page)) != 0) {
        shark_container_remove_tree(instance_root);
        instance_root->allocated = 0;
        instance_root->name[0] = 0;
        fs_cache_clear();
        return -1;
    }

    strcpy(shark_containers[slot].name, name);
    shark_containers[slot].root = instance_root;
    shark_containers[slot].cwd = instance_root;
    shark_container_make_mac(slot, name, shark_containers[slot].mac);
    shark_containers[slot].network_mode = SHARK_CONTAINER_NETWORK_BRIDGE;
    shark_containers[slot].state = SHARK_CONTAINER_STOPPED;
    return (int)slot;
}

int shark_container_set_cwd(uint32_t index, struct fs_node* cwd) {
    if (index >= SHARK_CONTAINER_LIMIT || shark_containers[index].state == SHARK_CONTAINER_EMPTY || !cwd) return -1;
    struct fs_node* node = cwd;
    while (node && node != shark_containers[index].root) node = node->parent;
    if (node != shark_containers[index].root || cwd->type != FS_DIRECTORY) return -1;
    shark_containers[index].cwd = cwd;
    return 0;
}

int shark_container_start_mode(uint32_t index, uint32_t mode) {
    if (index >= SHARK_CONTAINER_LIMIT || shark_containers[index].state != SHARK_CONTAINER_STOPPED || mode > SHARK_CONTAINER_NETWORK_NAT) return -1;
    if (net_has_nic()) {
        if (net_vnic_attach(index, shark_containers[index].mac) != 0) return -1;
        if (net_vnic_set_mode(index, mode) != 0) {
            net_vnic_detach(index);
            return -1;
        }
        if (net_vnic_dhcp_start(index) != 0) {
            net_vnic_detach(index);
            return -1;
        }
    }
    shark_containers[index].network_mode = (uint8_t)mode;
    shark_containers[index].state = SHARK_CONTAINER_RUNNING;
    return 0;
}

int shark_container_start(uint32_t index) {
    if (index >= SHARK_CONTAINER_LIMIT) return -1;
    return shark_container_start_mode(index, shark_containers[index].network_mode);
}

int shark_container_stop(uint32_t index) {
    if (index >= SHARK_CONTAINER_LIMIT || shark_containers[index].state != SHARK_CONTAINER_RUNNING) return -1;
    net_vnic_detach(index);
    shark_containers[index].state = SHARK_CONTAINER_STOPPED;
    return 0;
}

int shark_container_remove(uint32_t index) {
    if (index >= SHARK_CONTAINER_LIMIT || shark_containers[index].state != SHARK_CONTAINER_STOPPED) return -1;
    struct fs_node* instance_root = shark_containers[index].root;
    if (!instance_root || instance_root->num_children > MAX_CHILDREN) return -1;
    net_vnic_detach(index);
    shark_container_remove_tree(instance_root);
    instance_root->allocated = 0;
    instance_root->name[0] = 0;
    instance_root->parent = NULL;
    instance_root->num_children = 0;
    memset(&shark_containers[index], 0, sizeof(shark_containers[index]));
    fs_cache_clear();
    return 0;
}
