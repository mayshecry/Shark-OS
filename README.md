# SharkOS

A hobby 32-bit x86 operating system built from scratch in C, featuring a custom graphical shell, multi-pane terminal, in-memory filesystem, and ELF binary execution.

## Screenshot

Boot shows a fastfetch-style system info panel with ASCII shark logo, CPU model, memory, display resolution, and network status. Below is a full-featured shell prompt (`shark>`).

## Features

### Graphical User Interface
- **32-bit Linear Framebuffer** — boots directly into a 32-bit color graphics mode via GRUB/VBE
- **Scalable Font Rendering** — 8×8 bitmap font scaled 1×–4× based on screen resolution
- **Multi-Pane Terminal** — split the screen into up to 8 independent panes with `+` / `-` keys
- **Pane Tabs** — each pane has a labeled tab bar; `TAB` cycles focus between panes
- **Chrome UI** — header bar with title, footer bar with keybind hints, border accents
- **FAQ Overlay** — press `?` to open a styled FAQ panel; `ESC` or `?` again to close

### Shell & Commands
| Command | Description |
|---------|-------------|
| `ls` / `dir` | List files and directories in current directory |
| `cd <dir>` | Change directory; `cd ..` goes up |
| `cat <file>` | Display file contents |
| `touch <file>` | Create an empty file |
| `edit <file>` | Open built-in line editor; `ESC` saves and exits |
| `whoami` | Print current user |
| `ping [host]` | Send ICMP echo (hardware or loopback) |
| `sysinfo` | Show fastfetch system info panel |
| `colors` | Display the 16-color VGA palette |
| `lspci` | Scan and list all PCI devices |
| `ps` | List running tasks (PID, name, state, CPU) |
| `clear` / `cls` | Clear the active pane |
| `help` | Print full command reference |
| `bokop` / `poweroff` | Shut down the machine |

### Filesystem
- **In-Memory Tree FS** — 64-node pool, 16 children per directory
- **Pre-seeded Structure**:
  ```
  /
  ├── User/
  │   ├── Documents/
  │   ├── Photos/
  │   └── readme.txt
  └── System/
      ├── Bin/
      │   └── shs        (SharkScript binary)
      ├── Drivers/
      └── Kernel.sys
  ```
- **ELF Execution** — drop an ELF 32-bit binary into `System/Bin/` and run it by name from the shell

### Hardware Support
- **RTL8139 NIC** — PCI device detection, packet send stub, loopback ping fallback
- **PS/2 Keyboard** — scancode ring buffer, shifted character map, real-time input
- **CPU Identification** — reads brand string via `CPUID` (leaf 0x80000002–04)
- **Memory Detection** — parses GRUB memory map for total RAM

### Kernel Internals
- **Multiboot 1 Boot** — loaded by GRUB, receives framebuffer and memory map
- **GDT / IDT** — full descriptor table setup with 32 ISRs + 16 IRQs
- **PIC Remap** — IRQ 0–15 mapped to INT 32–47
- **Bump Allocator** — simple `kmalloc` above kernel end for early allocations
- **Task Skeleton** — `task_t` linked list, per-CPU structs, spinlock-protected list
- **Syscall Handler** — INT 0x80 dispatches read-file, write-char, yield, exit

## Building

### Prerequisites
- `gcc` (i686-elf cross-compiler recommended, system GCC works)
- `as` (GNU assembler, 32-bit mode)
- `grub-mkrescue` (GRUB 2)
- `xorriso`

### Build
```bash
make
```
Produces `sharkos.iso` (~40 MB).

### Run
```bash
qemu-system-i386 -cdrom sharkos.iso -m 512M
```
Or boot the ISO in VirtualBox / Bochs.

(Also runs on real 64-bit and 32-bit processors)
### Clean
```bash
make clean
```

## Project Structure

```
sharkos/
├── Makefile              # Build system with per-module object dirs
├── boot.s                # 32-bit multiboot entry assembly
├── linker.ld             # Linker script
├── grub.cfg              # GRUB boot menu config
├── sharkscript           # Embedded SharkScript binary
├── include/
│   └── kernel.h          # Master header: types, declarations, inline I/O
├── src/
│   ├── arch/
│   │   ├── io.c          # outb/inw/inl, GDT/IDT setup
│   │   ├── interrupts.c  # IRQ handler, ISR handler, syscall handler
│   │   └── cpu.c         # CPUID, shutdown
│   ├── drivers/
│   │   ├── keyboard.c    # PS/2 scancode reader, key buffer
│   │   └── pci.c         # RTL8139 NIC, PCI config space, ping
│   ├── fs/
│   │   └── fs.c          # In-memory filesystem, ELF loader
│   ├── ui/
│   │   ├── terminal.c    # Font blitter, scroll, pane text I/O
│   │   ├── ui.c          # Chrome, tabs, pane split/close, FAQ
│   │   └── fastfetch.c   # System info display with ASCII logo
│   ├── shell/
│   │   ├── commands.c    # CLI command parser and dispatch
│   │   └── main.c         # kmain, task creation, main event loop
│   └── lib/
│       ├── lib.c         # C library: strcmp, memcpy, memset, strlen…
│       ├── globals.c     # All global variable definitions
│       ├── pmm.c         # Physical memory manager (bump allocator)
│       └── elf.c         # ELF32 binary loader
└── isodir/               # ISO staging (generated)
```

## Key Bindings

| Key | Action |
|-----|--------|
| `TAB` | Cycle pane focus |
| `+` | Split active pane horizontally |
| `-` | Close active pane |
| `?` | Toggle FAQ overlay |
| `ESC` | In FAQ: close; In editor: save & exit |
| `Enter` | Execute command in CLI mode |
| `Backspace` | Delete character / undo editor input |

## Design Notes

- **No standard library** — everything is freestanding; no `libc` linked
- **Single address space** — kernel and "processes" share the same page map; ELF binaries are loaded and jumped to directly
- **Cooperative multitasking** — `yield()` syscall exists but the main loop is poll-driven via `hlt`
- **Static font** — 8×8 fixed-width bitmap, no font files or rasterization libraries needed

## License

MIT — see [LICENSE](LICENSE) for details.

## Author

Built by [mayshecry](https://github.com/mayshecry).