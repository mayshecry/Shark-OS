#include "kernel.h"
#include "plugin_manager.h"

#define MAX_PLUGIN_DESC 512
#define MAX_PACKAGES 16

typedef struct {
    char name[64];
    char description[MAX_PLUGIN_DESC];
    char author[64];
    uint8_t available;
    uint8_t installed;
} package_t;

static package_t g_packages[MAX_PACKAGES];
static int g_package_count = 0;

void spkg_discover_packages(void) {
    g_package_count = 0;

    int plugin_count = 0;
    plugin_t* plugins = plugin_get_list(&plugin_count);

    for (int i = 0; i < plugin_count && g_package_count < MAX_PACKAGES; i++) {
        strcpy(g_packages[g_package_count].name, plugins[i].name);
        strcpy(g_packages[g_package_count].description, "Plugin package");
        strcpy(g_packages[g_package_count].author, "Unknown");
        g_packages[g_package_count].available = 1;
        g_packages[g_package_count].installed = plugins[i].loaded;

        g_package_count++;
    }
}

void spkg_list(void) {
    spkg_discover_packages();

    terminal_writestring("Available packages:\n\n");

    for (int i = 0; i < g_package_count; i++) {
        terminal_writestring("  ");
        terminal_writestring(g_packages[i].name);
        terminal_writestring(" - ");
        terminal_writestring(g_packages[i].description);

        if (g_packages[i].installed) {
            terminal_writestring(" [INSTALLED]");
        }
        terminal_writestring("\n");
    }
}

int spkg_install(const char* pkg_name) {
    spkg_discover_packages();

    for (int i = 0; i < g_package_count; i++) {
        if (strcmp(g_packages[i].name, (char*)pkg_name) == 0) {
            if (g_packages[i].installed) {
                terminal_writestring("Package already installed!\n");
                return -1;
            }

            terminal_writestring("Installing ");
            terminal_writestring(g_packages[i].name);
            terminal_writestring("...\n");

            int result = plugin_load((char*)pkg_name);
            if (result >= 0) {
                plugin_t* plugin = plugin_get_by_name((char*)pkg_name);
                if (plugin && plugin->loaded) {
                    g_packages[i].installed = 1;
                    terminal_writestring("Installation successful!\n");
                    return 0;
                }
            }

            terminal_writestring("Installation failed.\n");
            return -1;
        }
    }

    terminal_writestring("Package not found.\n");
    return -1;
}

int spkg_uninstall(const char* pkg_name) {
    spkg_discover_packages();

    for (int i = 0; i < g_package_count; i++) {
        if (strcmp(g_packages[i].name, (char*)pkg_name) == 0) {
            if (!g_packages[i].installed) {
                terminal_writestring("Package not installed.\n");
                return -1;
            }

            terminal_writestring("Uninstalling ");
            terminal_writestring(g_packages[i].name);
            terminal_writestring("...\n");

            plugin_unload(i);
            g_packages[i].installed = 0;
            terminal_writestring("Uninstall successful!\n");
            return 0;
        }
    }

    terminal_writestring("Package not found.\n");
    return -1;
}

void spkg_info(const char* pkg_name) {
    spkg_discover_packages();

    for (int i = 0; i < g_package_count; i++) {
        if (strcmp(g_packages[i].name, (char*)pkg_name) == 0) {
            plugin_t* plugin = plugin_get_by_name((char*)pkg_name);
            uint8_t installed = (plugin && plugin->loaded) ? 1 : g_packages[i].installed;

            terminal_writestring("\n=== ");
            terminal_writestring(g_packages[i].name);
            terminal_writestring(" ===\n");
            terminal_writestring("Author: ");
            terminal_writestring(g_packages[i].author);
            terminal_writestring("\n");
            terminal_writestring("Description: ");
            terminal_writestring(g_packages[i].description);
            terminal_writestring("\n");
            terminal_writestring("Status: ");
            terminal_writestring(installed ? "INSTALLED" : "NOT INSTALLED");
            terminal_writestring("\n\n");
            return;
        }
    }

    terminal_writestring("Package not found.\n");
}

int cmd_spkg(int argc, char** argv) {
    if (argc == 1) {
        terminal_writestring("spkg - SharkOS Package Manager\n\n");
        terminal_writestring("Usage: spkg [command] [package]\n\n");
        terminal_writestring("Commands:\n");
        terminal_writestring("  list                 - List available packages\n");
        terminal_writestring("  install <package>   - Install a package\n");
        terminal_writestring("  uninstall <package> - Uninstall a package\n");
        terminal_writestring("  info <package>      - Show package information\n");
        terminal_writestring("  search <query>      - Search for packages\n");
        return 0;
    }

    const char* command = argv[1];

    if (strcmp(command, "list") == 0) {
        spkg_list();
        return 0;
    }

    if (strcmp(command, "install") == 0) {
        if (argc < 3) {
            terminal_writestring("Usage: spkg install <package>\n");
            return -1;
        }
        return spkg_install(argv[2]);
    }

    if (strcmp(command, "uninstall") == 0) {
        if (argc < 3) {
            terminal_writestring("Usage: spkg uninstall <package>\n");
            return -1;
        }
        return spkg_uninstall(argv[2]);
    }

    if (strcmp(command, "info") == 0) {
        if (argc < 3) {
            terminal_writestring("Usage: spkg info <package>\n");
            return -1;
        }
        spkg_info(argv[2]);
        return 0;
    }

    if (strcmp(command, "search") == 0) {
        if (argc < 3) {
            terminal_writestring("Usage: spkg search <query>\n");
            return -1;
        }

        spkg_discover_packages();

        const char* query = argv[2];
        terminal_writestring("Search results for '");
        terminal_writestring(query);
        terminal_writestring("':\n\n");

        for (int i = 0; i < g_package_count; i++) {
            if (strstr(g_packages[i].name, (char*)query) ||
                strstr(g_packages[i].description, (char*)query)) {
                terminal_writestring("  - ");
                terminal_writestring(g_packages[i].name);
                terminal_writestring("\n");
            }
        }
        return 0;
    }

    terminal_writestring("Unknown command. Use 'spkg' for help.\n");
    return -1;
}