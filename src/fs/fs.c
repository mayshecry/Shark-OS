#include "kernel.h"

struct fs_node* find_node(struct fs_node* parent, const char* name) {
    if (!parent) return NULL;
    for (int i = 0; i < parent->num_children; i++) {
        if (strcmp(parent->children[i]->name, name) == 0) return parent->children[i];
    }
    return NULL;
}

struct fs_node* search_path(const char* name) {
    struct fs_node* n = find_node(current_dir, name);
    if (n && n->type == FS_FILE) return n;

    struct fs_node* sys = find_node(root, "System");
    if (sys) {
        struct fs_node* bin = find_node(sys, "Bin");
        if (bin) {
            n = find_node(bin, name);
            if (n && n->type == FS_FILE) return n;
        }
    }

    return NULL;
}

struct fs_node* create_node(const char* name, node_type_t type, struct fs_node* parent) {
    if (pool_index >= MAX_NODES) return NULL;
    struct fs_node* node = &node_pool[pool_index++];
    strcpy(node->name, name);
    node->type = type;
    node->parent = parent;
    node->num_children = 0;
    node->content[0] = '\0';
    node->content_len = 0;
    if (parent && parent->num_children < MAX_CHILDREN) {
        parent->children[parent->num_children++] = node;
    }
    return node;
}

void fs_initialize() {
    root = create_node("/", FS_DIRECTORY, NULL);
    current_dir = root;

    struct fs_node* user = create_node("User", FS_DIRECTORY, root);
    create_node("Documents", FS_DIRECTORY, user);
    create_node("Photos", FS_DIRECTORY, user);
    struct fs_node* readme = create_node("readme.txt", FS_FILE, user);
    strcpy(readme->content, "Welcome to SharkOS!");
    readme->content_len = strlen(readme->content);

    struct fs_node* system = create_node("System", FS_DIRECTORY, root);
    struct fs_node* bin_dir = create_node("Bin", FS_DIRECTORY, system);
    create_node("Drivers", FS_DIRECTORY, system);
    create_node("Kernel.sys", FS_FILE, system);

    /* SHS interpreter is now built directly into the kernel source.
     * Use: shs <script.shx> from the shell to run .shx scripts. */
    create_node("shs", FS_FILE, bin_dir);
}