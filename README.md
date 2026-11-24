# <p align="center"> SharkOS</p>

<p align="center"> > A freestanding x86-64 hobby operating system built from scratch in C and assembly. < </p>

<p align="center">
  <img src="sharkos-fish-promo.gif" alt="SharkOS pixel-art shark promo animation" width="720">
</p>

<p align="center">
  <a href="https://sharkos.lol">Website</a> ·
  <a href="https://github.com/mayshecry/Shark-OS">Source code</a> ·
  <a href="https://github.com/mayshecry/Shark-OS/releases">Releases</a>
</p>

SharkOS is an experimental operating system not a Linux distribution, and not an application running on top of another OS. Its kernel, drivers, memory manager, filesystem, shell, desktop, browser and networking components are built into a freestanding system image. It boots through Limine on BIOS and UEFI systems and can be tried in QEMU or VirtualBox.

The project is designed to be hands-on and hackable. It deliberately keeps the shell, desktop, drivers and plugins in kernel space; this keeps the system architecture compact, but it also means a faulty pointer or plugin can crash the whole machine. **SharkOS is experimental and should not be used to store important data or exposed as a production server.**

## At a glance

- **Architecture:** 64-bit x86 (x86-64), freestanding C and assembly
- **Boot:** Limine; hybrid BIOS/UEFI ISO
- **Desktop:** graphical framebuffer desktop, window manager, four themes and wallpaper support
- **Shell:** `nemo-shell`, with 90+ commands
- **Networking:** in-kernel Ethernet/IP stack, browser, firewall and a separate server build
- **Extensions:** C/ELF plugins using SharkAPI and the `spkg` package manager
- **Website:** [sharkos.lol](https://sharkos.lol)
- **License:** GPL-3.0 — see [`LICENSE`](LICENSE)

## Project timeline

- **May 2025:** development began.
- **Through January 2026:** development was paused.
- **January 2026 onward:** development resumed, with continuous development planned through **December 2026**.

## What’s included

### Graphical desktop

SharkOS starts a framebuffer desktop with a desktop grid, icons, a start menu, wallpaper support and a window manager (up to 16 windows). The visual style takes inspiration from Windows 98, with four themes: **Win98, Modern, Kawaii and WindowMaker**. Theme-aware icons, borders and taskbar styling change with the selected theme.

Desktop interactions include a right-click menu for view options, arranging and refreshing icons, opening a terminal and settings. Icons can be hidden or removed. The desktop also includes a file manager and **Shark IDE**, a small editor with line numbers, basic C syntax highlighting, keyboard navigation, Tab indentation and Ctrl+S saving.

The image and animation decoders listed in the project include PNG, JPEG, GIF and WebP.

### Shell and system tools

The `nemo-shell` provides more than 90 commands, including:

- **Files:** `ls`, `cd`, `cat`, `mkdir`, `rm`, `cp`, `mv`
- **System inspection:** `sysinfo`, `ps`, `htop`, `devices`, `lspci`
- **Networking:** `ifconfig`, `dhcp`, `ping`, `dns`, `wget`, `curl`, `netstat`
- **Storage:** `mount`, `umount`
- **Audio:** `audio status`, `audio test`, `audio tone`, `audio wav`, `audio beep`
- **Packages and plugins:** `spkg list`, `spkg install`, `spkg uninstall`, `spkg info`
- **Utilities:** `sharkfetch`, `neofetch`, `imgfetch`, `banner`, `cowsay`, `fortune`

`imgfetch` shows the SharkOS logo next to the neofetch panel: the desktop shell blits the real artwork straight into the framebuffer, while serial, SSH and the server image fall back to a coloured ASCII version of the same picture. `imgfetch <cols>` picks a smaller or larger logo.

Exact command availability can differ between the desktop and server images; use `help` in the running system for the current command list.

### Filesystem and storage

The kernel provides an in-memory filesystem for the running session and an ATA PIO-backed **FAT16** mount driver. The RAM filesystem is intentionally small (up to 128 nodes, 32 children per directory and files up to 64 KiB). FAT16 volumes can be mounted and existing files read or edited, but writes are limited by the file’s existing allocated clusters. Persistent file creation and directory changes are not implemented; FAT32 and long filenames are also not supported. RAM-backed content is lost on reboot.

### Browser and networking

**Shark Navigator** is a compact, in-kernel browser and networking stack—not a Chromium replacement. The stack includes Ethernet, ARP, IPv4, ICMP, UDP, DHCP, DNS, TCP, HTTP/HTTPS, and a stateful firewall. Browser features include HTML/CSS rendering, JavaScript and SVG support, plain-text and common image viewing, URL-encoded GET/POST forms, bounded `fetch()` and a synchronous XMLHttpRequest compatibility layer. A WebSocket client is also available.

The codebase includes drivers for several common emulated and physical NIC families, including Realtek RTL8139/RTL8168/RTL8169, AMD PCnet and supported Intel e1000 devices. Hardware support depends on the exact device and environment.

**Important networking limitations:** only one HTTP/WebSocket connection is active at a time in the desktop network stack; browser APIs are intentionally limited, and TLS certificate identity is not validated. The network stack has not been fuzz-tested against hostile input. Do not treat it as security-hardened.

### Audio

Audio support probes for AC’97 and Intel HD Audio devices. Both feed an eight-channel mixer that resamples audio to 48 kHz, 16-bit stereo. WAV playback and plugin/game audio are supported; the PC speaker is available as a fallback when a DMA audio device is unavailable.

Try commands such as `audio status`, `audio test`, `audio tone 523 300`, or `audio wav /System/Sounds/startup.wav` from the shell.

### Plugins, package manager and games

Plugins are C/ELF modules that use the **SharkAPI** for services such as console I/O, memory, files, graphics, input, tasks and audio. The plugin manager and `spkg` provide discovery, installation, command dispatch and removal. Plugins run in Ring 0 in the kernel’s address space: only install or run code you trust.

The repository includes projects such as Doom, Flappy Bird, Pong, Super Mario Bros. 1-1, Geometry Dash, SharkCube, SharkCraft, FNAF and a Python interpreter plugin. Availability and installation state may vary by image/release. The website’s v4.2 overview also highlights SharkCraft LAN peer-to-peer multiplayer, including discovery, chat and usernames; check the [release notes](https://github.com/mayshecry/Shark-OS/releases) for the exact state of a feature in a particular build.

### SharkOS Server image

The separate server image boots to a framebuffer/serial text console rather than the desktop. It includes HTTP server management, a basic web root, SSH and SFTP support, and a router subsystem with DHCP, DNS forwarding/resolution, WAN management, NAT/port forwarding and a web management console. The server also provides firewall controls and a JSON management API.

**Server safety and persistence:** the documented default SSH credentials are `root` / `root123`; change them before connecting the server to any untrusted network. Website files and credentials are RAM-backed and lost on reboot. HTTP is plaintext, and this experimental server is not a hardened production system. Review the router and firewall documentation before using those components:

- [`docs/SHARKOS-ROUTER.md`](docs/SHARKOS-ROUTER.md)
- [`docs/SHARKOS-FIREWALL.md`](docs/SHARKOS-FIREWALL.md)

## How SharkOS works

### Boot and startup

1. The machine firmware starts Limine from the bootable ISO (BIOS or UEFI).
2. Limine loads the SharkOS kernel image and passes the boot information to the kernel entry point.
3. The assembly entry code establishes the 64-bit execution environment, initializes the kernel’s early memory/paging setup and transfers control to `kmain`.
4. Kernel startup initializes core CPU/interrupt/timer services, reads the platform clock, scans PCI devices and probes storage, audio and network drivers.
5. The desktop build initializes the Limine framebuffer, desktop, theme, icons and window manager. The server build instead initializes its text console and server services.
6. The main loop handles input and system polling. In the desktop build it also polls networking/audio and redraws changed screen regions to the framebuffer.

The 64-bit branch uses a 128 GiB identity-mapped address space as documented by the project. Limine supplies the boot protocol and framebuffer information; the kernel contains the device drivers and system components needed after hand-off.

### Kernel architecture

| Area | Role |
| --- | --- |
| `src/arch/` | Early x86-64 setup, GDT/IDT, interrupt handling, PIC, timer, CPU identification and memory-type configuration. |
| `src/drivers/` | Keyboard, mouse, PCI enumeration, ATA/RTC and audio hardware drivers. |
| `src/lib/` | Physical memory allocation, ELF loading, SharkAPI and plugin management. |
| `src/fs/` | RAM filesystem and FAT16 storage support. |
| `src/desktop/` | Window manager, desktop, themes, icons, image decoders, bootscreen and editor. |
| `src/browser/` | Browser rendering and client-side HTML/CSS/JS/SVG functionality. |
| `src/net/` | Network protocols, HTTP services, TLS-related code, firewall, router and server networking. |
| `src/shell/` | `nemo-shell`, built-in commands and package manager. |
| `src/server/` | Server image entry point, console and server-specific command support. |
| `plugins/` | Optional applications and games built for SharkOS. |

### Kernel-space design

SharkOS does not currently provide a protected userspace boundary. Kernel code, shell commands, drivers, desktop components and plugins share privileged execution and address space. The system has no full POSIX environment and no preemptive scheduler; tasks must yield cooperatively. This architecture is useful for learning and experimentation, but it substantially limits reliability and security.

## Try SharkOS

### Download a release

Visit [sharkos.lol](https://sharkos.lol) or the [GitHub Releases page](https://github.com/mayshecry/Shark-OS/releases) for the current ISO and release notes. The website currently features the v4.2 preview. Release assets and feature sets may change over time.

### Run in QEMU

After downloading or building `sharkos.iso`, a basic QEMU launch is:

```bash
qemu-system-x86_64 \
  -m 512 -smp 2 \
  -cdrom sharkos.iso -boot order=d \
  -vga std \
  -netdev user,id=net0 -device e1000,netdev=net0
```

For serial boot logs:

```bash
qemu-system-x86_64 -m 512 -cdrom sharkos.iso \
  -serial stdio -display none
```

A multimedia QEMU configuration with audio is shown in the repository’s release/build instructions; available QEMU audio backends vary by host. VirtualBox can also be used with a 64-bit VM and EFI enabled.

## Build from source

A Linux host is required. Install a C toolchain and the ISO/boot utilities:

```bash
sudo apt install build-essential nasm xorriso mtools curl qemu-system-x86 ovmf
```

Clone and build:

```bash
git clone https://github.com/mayshecry/Shark-OS.git
cd Shark-OS
make
```

The default `make` target builds the desktop ISO (`sharkos.iso`). The project also defines `make server` for the server image. The build uses freestanding x86-64 GCC flags and assembles/links the kernel before staging a hybrid ISO with Limine. Build targets and dependencies can evolve, so consult the `Makefile` and project docs if a command fails.

## Repository map

```text
.
├── assets/       # Project media
├── docs/         # Architecture, router, firewall and feature documentation
├── include/      # Kernel and subsystem interfaces
├── limine/       # Limine boot files
├── plugins/      # Optional applications and games
├── src/          # Kernel, drivers, desktop, browser, networking and shell
├── tests/        # Host-side and subsystem tests
├── tools/        # Build helpers and test utilities
├── Makefile
└── boot.s        # Kernel entry assembly
```

## Known limitations

- Experimental, single-privilege kernel design; no userspace isolation.
- No preemptive scheduler or full POSIX compatibility.
- RAM filesystem contents are volatile; FAT16 support has limited write behavior.
- The ELF loader handles only the subset of ELF used by SharkOS.
- Browser features and network concurrency are limited; TLS certificate identity is not validated.
- Networking and server components are not security-hardened or fuzz-tested for hostile input.
- Hardware support depends on available drivers; a kernel panic may leave the machine hung.

## Contributing

Issues and contributions are welcome. Before submitting a change, keep kernel code freestanding, respect the existing subsystem boundaries, check buffer and pointer bounds, and test changes in QEMU where practical. Boot failures may require serial output for diagnosis:

```bash
qemu-system-x86_64 -cdrom sharkos.iso -serial stdio -display none
```

## Documentation and links

- **Website:** [sharkos.lol](https://sharkos.lol)
- **Source:** [github.com/mayshecry/Shark-OS](https://github.com/mayshecry/Shark-OS)
- **Releases:** [GitHub Releases](https://github.com/mayshecry/Shark-OS/releases)
- **Plugin system:** [`PLUGIN_SYSTEM.md`](PLUGIN_SYSTEM.md)
- **Automatic plugin discovery:** [`PLUGIN_AUTO_DETECT.md`](PLUGIN_AUTO_DETECT.md)
- **Router:** [`docs/SHARKOS-ROUTER.md`](docs/SHARKOS-ROUTER.md)
- **Firewall:** [`docs/SHARKOS-FIREWALL.md`](docs/SHARKOS-FIREWALL.md)

## License

SharkOS is distributed under the GNU General Public License, version 3.0. See [`LICENSE`](LICENSE) for the full license text.
