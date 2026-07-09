# SharkOS

A hobby 32-bit x86 operating system built from scratch in C, featuring a custom graphical shell, multi-pane terminal, in-memory filesystem, ELF binary execution, and a comprehensive plugin system.

## Screenshots & Media

### Running on Real Hardware
![SharkOS on real hardware](media/tilingonrealhardware.png)

**Boot on real hardware**  
![SharkOS on real hardware](media/hwdboot.mp4)

### VirtualBox (8 MB RAM)
![SharkOS running in VirtualBox with only 8 MB RAM](media/vbox.png)

### SharkScript Compiler + Tiling
![Multi-pane terminal with SharkScript (shs) running](media/shs.png)

## Features

### Graphical User Interface
- **32-bit Linear Framebuffer** — boots directly into 32-bit color graphics mode via GRUB/VBE
- **Scalable Font Rendering** — 8×8 bitmap font scaled 1×–4× based on screen resolution
- **Multi-Pane Terminal** — split the screen into up to 8 independent panes with `+` / `-` keys
- **Pane Tabs** — each pane has a labeled tab bar; `TAB` cycles focus between panes
- **Chrome UI** — header bar with title, footer bar with keybind hints, border accents
- **FAQ Overlay** — press `?` to open a styled FAQ panel; `ESC` or `?` again to close

### Shell & Commands

| Command          | Description                                      |
|------------------|--------------------------------------------------|
| `ls` / `dir`     | List files and directories                       |
| `cd <dir>`       | Change directory (`cd ..` goes up)               |
| `cat <file>`     | Display file contents                            |
| `touch <file>`   | Create an empty file                             |
| `edit <file>`    | Built-in line editor (`ESC` to save & exit)      |
| `whoami`         | Print current user                               |
| `ping [host]`    | Send ICMP echo (RTL8139 or loopback)             |
| `sysinfo`        | Fastfetch-style system info panel                |
| `colors`         | Display 16-color VGA palette                     |
| `lspci`          | Scan and list PCI devices                        |
| `ps`             | List running tasks                               |
| `clear` / `cls`  | Clear active pane                                |
| `help`           | Show command reference                           |
| `bokop` / `poweroff` | Shut down the machine                        |
| `spkg`           | Plugin package manager                           |

### Plugin System

SharkOS includes a powerful plugin system that allows extending the OS with custom functionality. For detailed documentation, see [PLUGIN_SYSTEM.md](PLUGIN_SYSTEM.md).

#### Key Features
- **Automatic Detection** — plugins are discovered dynamically from `/System/plugins/` without hardcoded references
- **SharkAPI** — comprehensive API for plugin development (console I/O, memory management, strings, graphics, filesystem, tasks, and more)
- **Package Manager** — `spkg` command for installing, uninstalling, and managing plugins
- **ELF Binary Format** — plugins compile to standard ELF binaries with `.plg` extension
- **Built-in Plugins** — includes plugins like Python interpreter, Doom, Flappy Bird, Pong, and Super Mario Bros

#### Quick Start
```bash
spkg list              # List available plugins
spkg install python    # Install a plugin
python                 # Use the plugin
spkg uninstall python  # Remove plugin
```

### Filesystem
- **In-Memory Tree FS** — 64-node pool, 16 children per directory
- **Pre-seeded structure**:
  ```
  /
  ├── User/
  │   ├── Documents/
  │   ├── Photos/
  │   └── readme.txt
  └── System/
      ├── Bin/
      │   └── shs          (SharkScript binary)
      ├── Drivers/
      ├── Kernel.sys
      └── plugins/
          ├── doom.plg
          ├── flappybird.plg
          ├── pong.plg
          ├── smb.plg
          └── ...
  ```
- **ELF Execution** — load and run 32-bit ELF binaries from `System/Bin/`

### Hardware Support
- **RTL8139 NIC** — PCI detection, packet send, loopback ping
- **Mouse Support** — PS/2 mouse with cursor rendering
- **PS/2 Keyboard** — full scancode support with shift
- **CPUID** — brand string detection
- **Memory** — GRUB memory map parsing

### Kernel Internals
- Multiboot 1 compliant
- GDT/IDT + full ISR/IRQ handling
- PIC remapped to 32–47
- Bump allocator + physical memory manager (PMM)
- Basic task scheduler
- Syscalls via `INT 0x80`
- Cooperative multitasking

## Building

### Prerequisites
- `gcc` (i686-elf cross-compiler recommended)
- GNU assembler (`as`)
- `grub-mkrescue` + `xorriso`

### Build
```bash
make
```
Produces `sharkos.iso` (~40 MB).

### Run
```bash
qemu-system-i386 -cdrom sharkos.iso -m 512M
```

Or boot on real hardware / VirtualBox / Bochs.

### Clean
```bash
make clean
```

## Project Structure

```
sharkos/
├── Makefile                 # Build system
├── boot.s                   # 32-bit bootloader (GRUB multiboot)
├── linker.ld                # Linker script
├── grub.cfg                 # GRUB configuration
├── compiler.elf             # SharkScript compiler embedded binary
├── include/
│   ├── kernel.h             # Main kernel header
│   ├── sharkapi.h           # Plugin development API
│   ├── plugin_manager.h     # Plugin system interface
│   ├── doom.h, flappybird.h, pong.h, smb.h  # Game headers
│   └── sharkscript.h        # Script compiler header
├── src/
│   ├── arch/                # Architecture-specific (CPU, interrupts, I/O)
│   ├── drivers/             # Hardware drivers (keyboard, mouse, PCI)
│   ├── fs/                  # In-memory filesystem
│   ├── ui/                  # UI framework (terminal, mouse, fastfetch)
│   ├── shell/               # Command shell and spkg package manager
│   ├── lib/                 # Kernel library (PMM, ELF loader, plugin manager)
│   └── *.c files            # Core OS components (doom, flappybird, pong, smb, sharkscript)
├── plugins/                 # Plugin source code (compiled into ISO)
│   ├── TEMPLATE.c           # Template for new plugins
│   ├── doom/
│   ├── flappybird/
│   ├── pong/
│   ├── smb/
│   └── python-interp/
├── isodir/                  # ISO filesystem structure
│   └── boot/                # Boot files for ISO
│   └── System/
│       ├── Plugins/         # Pre-loaded plugins (.plg files)
│       └── ...
├── doom/                    # Compiled game objects
├── pong/                    # Compiled pong objects
├── flappybird/              # Compiled flappybird objects
├── smb/                     # Compiled SMB objects
├── sharkscript/             # Compiled SharkScript objects
├── ui/                      # Compiled UI objects
├── shell/                   # Compiled shell objects
├── lib/                     # Compiled library objects
├── fs/                      # Compiled filesystem objects
├── drivers/                 # Compiled driver objects
├── arch/                    # Compiled arch objects
└── media/                   # Screenshots and demo videos
    ├── tilingonrealhardware.png
    ├── hwdboot.mp4
    ├── vbox.png
    └── shs.png
```

## Key Bindings

### Shell & Navigation
| Key       | Action                        |
|-----------|-------------------------------|
| `TAB`     | Cycle pane focus              |
| `+`       | Split pane horizontally       |
| `-`       | Close active pane             |
| `?`       | Toggle FAQ overlay            |
| `ESC`     | Close FAQ / Save & exit editor|
| `Enter`   | Execute command               |

### Text Editor
| Key       | Action                        |
|-----------|-------------------------------|
| `ESC`     | Save and exit                 |
| Backspace | Delete character              |
| Arrows    | Navigate text                 |

### Pane Management
- Maximum of 8 panes supported
- Each pane maintains independent command history
- Active pane highlighted with accent border

## Built-in Games & Applications

### Doom
Classic Doom game plugin. Install via:
```bash
spkg install doom
doom
```

### Flappy Bird
Flappy Bird clone plugin. Install via:
```bash
spkg install flappybird
flappybird
```

### Pong
Classic Pong game plugin. Install via:
```bash
spkg install pong
pong
```

### Super Mario Bros
Super Mario Bros world 1-1 plugin. Install via:
```bash
spkg install smb
smb
```

### Python Interpreter
Simple Python-like interpreter plugin. Install via:
```bash
spkg install python
python
python script.py
python -i
```

## Design Notes
- **No standard library** (freestanding) — no libc dependency
- **Single address space** — all code runs in kernel mode for simplicity
- **Cooperative multitasking** — tasks yield voluntarily
- **Static 8×8 bitmap font** — scalable rendering engine
- **In-memory only** — no disk I/O, filesystem is volatile
- **Plugin architecture** — extensible via dynamically loaded ELF modules

## Development

### Writing Plugins
For complete plugin development documentation, see [PLUGIN_SYSTEM.md](PLUGIN_SYSTEM.md) and [PLUGIN_AUTO_DETECT.md](PLUGIN_AUTO_DETECT.md).

Quick example:
```c
#include "sharkapi.h"

plugin_info_t plugin_info = {
    .version = SHARKAPI_VERSION,
    .name = "My Plugin",
    .author = "Your Name",
    .description = "Plugin description",
    .major = 1,
    .minor = 0
};

int plugin_init(void) {
    sharkapi_println("Plugin loaded!");
    return 0;
}

void plugin_cleanup(void) {
    sharkapi_println("Plugin unloaded");
}

int plugin_command(int argc, char** argv) {
    sharkapi_println("Hello from plugin!");
    return 0;
}

/* Export symbols */
int plugin_init_entry(void) { return plugin_init(); }
void plugin_cleanup_entry(void) { plugin_cleanup(); }
int plugin_command_entry(int argc, char** argv) { return plugin_command(argc, argv); }
plugin_info_t* plugin_get_info(void) { return &plugin_info; }
```

### Writing Shell Commands
Shell commands are added to `src/shell/commands.c` and registered in the command table.

### Writing Games
Games follow the plugin architecture. See `src/doom/`, `src/flappybird/`, `src/pong/`, and `src/smb/` for examples.

## Testing
```bash
# Build and run in QEMU
make
qemu-system-i386 -cdrom sharkos.iso -m 512M

# Boot on real hardware
# Copy sharkos.iso to USB and boot from it
```

## Troubleshooting

### Build Issues
- Ensure you have `i686-elf-gcc` cross-compiler installed
- Install `grub-mkrescue` and `xorriso` for ISO creation
- Run `make clean` before rebuilding if you encounter linker errors

### Runtime Issues
- If screen is garbled, ensure VBE is supported in QEMU/VM
- For real hardware, use VirtualBox or Bochs if issues occur
- Plugins must be compiled as 32-bit ELF executables

## License
MIT — see [LICENSE](LICENSE) for details.

## Author
Built by **[mayshecry](https://github.com/mayshecry)**.

## Acknowledgments
- Inspired by [OSDev.org](https://wiki.osdev.org/) and the OS development community
- Built as a learning project for operating system development
