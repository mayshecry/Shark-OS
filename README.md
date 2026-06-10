# SharkOS v0.1

SharkOS is a lightweight, 32-bit x86 operating system featuring a graphical terminal, a virtual file system (VFS), and basic networking support.

## Features

*   **Graphical Kernel:** Utilizes a 32-bit Linear Framebuffer (LFB) with a custom 8x8 bitmap font engine.
*   **Virtual File System (VFS):** Supports hierarchical directories and files in memory.
*   **Networking:** Includes a driver for the Realtek RTL8139 NIC, capable of sending ICMP ping packets.
*   **ELF Loader:** Can load and execute external 32-bit ELF binaries.
*   **Built-in Shell:** A CLI with support for various system and filesystem commands.
*   **Text Editor:** A basic built-in editor to create and modify files on the fly.
*   **System Discovery:** PCI bus scanning and CPUID-based hardware identification.

## Command List

| Command | Description |
| :--- | :--- |
| `help` | Displays the help menu |
| `ls` / `dir` | Lists files in the current directory |
| `cd <dir>` | Changes the current directory |
| `cat <file>` | Prints file contents to the terminal |
| `touch <file>` | Creates a new empty file |
| `edit <file>` | Opens the built-in text editor |
| `sysinfo` | Displays CPU, memory, and display specifications |
| `lspci` | Scans and lists devices on the PCI bus |
| `ping <ip>` | Sends a test Ethernet frame via the RTL8139 |
| `clear` / `cls` | Clears the workspace |
| `poweroff` | Shuts down the system (Emulator compatible) |

## Building and Running

### Prerequisites
*   `gcc` (with support for `-m32`)
*   `nasm` or `as` (x86 assembler)
*   `make`
*   `qemu-system-i386` (for emulation)

### Build
```bash
make clean && make
```

### Run
```bash
qemu-system-i386 -cdrom sharkos.iso -net nic,model=rtl8139 -net user
```

## License
This project is licensed under the MIT License.