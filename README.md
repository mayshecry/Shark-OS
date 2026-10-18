# SharkOS

**SharkOS is a freestanding 32-bit x86 operating system implemented primarily in C and x86 assembly.** It combines a custom kernel, a command-line shell, a graphical desktop environment, a native plugin interface, a custom scripting language, and a hand-written networking stack.

The project is designed around direct hardware access and a compact system architecture. Rather than relying on a conventional hosted runtime or a separate desktop server, SharkOS implements its core functionality within a single privileged execution environment.

The graphical environment, known as SharkOS 98, provides a Windows 98-inspired desktop with window management, application windows, a taskbar, desktop icons, bundled utilities, and games.

**Version:** 2.2 — Sharkslayer  
**Kernel:** SHKRNL  
**Shell:** nemo-shell  
**Architecture:** 32-bit x86  
**Primary languages:** C and x86 assembly  
**Boot protocol:** Multiboot 1  
**License:** GNU General Public License v3.0

---

## Table of Contents

- [Overview](#overview)
- [Design Goals](#design-goals)
- [System Architecture](#system-architecture)
- [Boot Process](#boot-process)
- [Kernel Architecture](#kernel-architecture)
- [Memory Management](#memory-management)
- [Interrupts and Hardware](#interrupts-and-hardware)
- [Execution Model and Scheduling](#execution-model-and-scheduling)
- [Graphics and Desktop Environment](#graphics-and-desktop-environment)
- [Shell and Command Execution](#shell-and-command-execution)
- [Filesystem](#filesystem)
- [Networking](#networking)
- [Plugin System](#plugin-system)
- [ELF Executable Loading](#elf-executable-loading)
- [SharkScript](#sharkscript)
- [Bundled Applications and Games](#bundled-applications-and-games)
- [Repository Structure](#repository-structure)
- [Building from Source](#building-from-source)
- [Running SharkOS](#running-sharkos)
- [Troubleshooting](#troubleshooting)
- [Security Considerations](#security-considerations)
- [Limitations](#limitations)
- [Contributing](#contributing)
- [License](#license)

---

## Overview

SharkOS is a custom operating system targeting the 32-bit x86 architecture. Its kernel, hardware interfaces, memory-management facilities, shell, graphical interface, and system utilities are implemented as part of the project.

The system includes the following major components:

- A Multiboot-compatible kernel entry point.
- x86 descriptor tables, exception handling, and hardware interrupt handling.
- Physical memory management and kernel allocation facilities.
- Keyboard, mouse, timer, real-time clock, and PCI support.
- A framebuffer-based graphical desktop.
- A built-in command interpreter with filesystem, system, networking, and utility commands.
- An in-memory filesystem.
- A custom TCP/IP implementation and supported Ethernet device drivers.
- A native plugin interface called SharkAPI.
- An ELF executable loader.
- SharkScript and its compiler.
- Bundled applications, utilities, and games.

SharkOS also provides multiple boot configurations through GRUB. These include the full desktop environment and lighter console-oriented configurations.

## Design Goals

### Direct hardware access

SharkOS implements its own low-level interfaces for interacting with the processor, memory, interrupts, display, input devices, and supported peripherals.

This makes the project suitable for studying how operating systems communicate with hardware without depending on the services of a host operating system.

### A compact, integrated architecture

The shell, desktop, drivers, filesystem, and extension facilities are developed as parts of one operating system rather than as a collection of independent user-space services.

This reduces architectural separation and makes direct calls between components possible, but it also increases the importance of maintaining clear interfaces and carefully managing shared state.

### An integrated graphical environment

SharkOS 98 provides a graphical interface on top of the kernel's display and input facilities. Window management, application windows, menus, desktop icons, and rendering are implemented within the operating system.

### Extensibility

SharkAPI, the plugin manager, the `spkg` command, and SharkScript provide several mechanisms for extending the system's functionality.

These facilities are intended to make it possible to add commands, applications, and other functionality without manually integrating every feature into the main shell implementation.

---

## System Architecture

SharkOS is organized into several closely integrated subsystems.

| Component | Responsibility |
|---|---|
| Boot assembly | Establishes the initial execution environment and transfers control to the kernel. |
| Kernel | Coordinates processor-level functionality, memory, interrupts, and system services. |
| Architecture layer | Provides x86-specific operations and CPU-related functionality. |
| Drivers | Interfaces with supported hardware devices. |
| Memory manager | Provides memory-management and allocation facilities. |
| Filesystem | Represents files and directories in memory. |
| Shell | Parses commands and dispatches operations to built-in functionality and extensions. |
| Desktop | Implements windows, menus, icons, applications, and framebuffer rendering. |
| Networking | Implements supported network-device interfaces and network protocols. |
| Plugin manager | Manages native extensions and their entry points. |
| ELF loader | Loads supported 32-bit executable binaries. |
| SharkScript | Provides a custom scripting language and compilation tools. |

These components are compiled together as a freestanding kernel image. They are not independent processes protected by separate address spaces.

### Freestanding execution

SharkOS is built without the normal hosted C runtime and standard-library environment.

The kernel provides or implements the facilities required by its own code, including low-level memory operations, string utilities, hardware access, and system-specific interfaces.

The build configuration uses 32-bit compilation, freestanding compilation settings, and explicit linking without the standard system libraries.

This arrangement gives the project control over its runtime environment, but it also means that functionality commonly supplied by a hosted operating system must be implemented or incorporated explicitly.

---

## Boot Process

SharkOS uses GRUB and the Multiboot 1 boot protocol.

The boot process consists of several stages.

### 1. Bootloader initialization

The machine's firmware starts the bootloader. GRUB presents the available SharkOS configurations and loads the selected kernel image.

The configuration defines entries for the standard system, Lite, Legacy, and Kawaii modes.

### 2. Multiboot handoff

GRUB transfers control to the kernel's entry point and supplies boot information, including information about available memory and the initial display configuration when supported.

The kernel uses this information to establish its initial operating environment.

### 3. Assembly initialization

The `_start` entry point in `boot.s` performs the initial processor-level setup.

The startup code:

1. Preserves the Multiboot parameters.
2. Clears the BSS memory region.
3. Initializes the kernel stack.
4. Loads the Global Descriptor Table.
5. Reloads the segment registers.
6. Calls `kmain` to begin kernel initialization.

The assembly code also defines interrupt and exception entry stubs used by the C interrupt-handling code.

### 4. Kernel initialization

The kernel proceeds to initialize the facilities required for normal operation.

These include processor and memory information, interrupt handling, timers, device interfaces, and the system's core services.

The exact ordering of all initialization routines should be determined from the current kernel initialization source.

### 5. System mode selection

The selected boot configuration determines which operating mode is requested.

The full configuration provides the graphical desktop, while the Lite and Legacy entries select console-oriented modes. Other configurations may enable different themes or behavior.

---

## Kernel Architecture

### Processor and architecture support

The architecture-specific code provides low-level operations required by the kernel, including processor identification and hardware I/O.

The project includes a Global Descriptor Table, an Interrupt Descriptor Table, exception handlers, hardware interrupt handlers, and a software interrupt entry for `int 0x80`.

The kernel also includes PCI enumeration and CPU identification facilities.

### Interrupt handling

Interrupts provide a mechanism for handling processor exceptions and asynchronous hardware events.

The assembly layer establishes entry stubs that preserve processor state and transfer execution to C handlers. After the handler returns, the saved state is restored and execution resumes through the appropriate interrupt-return instruction.

The PIC is configured with remapped interrupt vectors so hardware IRQs do not overlap the processor's standard exception range.

Correct interrupt handling is essential because errors in interrupt entry, register preservation, shared-state management, or return handling can destabilize the entire system.

### Timers and clock facilities

SharkOS documents a PIT configured at 1,000 Hz and an RTC interface for real-time clock information.

The timer provides periodic hardware events. The RTC supplies clock-related information.

The timer frequency should not be interpreted as a guarantee that the scheduler switches tasks at the same frequency.

### System calls

The kernel includes an `int 0x80` interface.

In SharkOS, this interface must not be confused with the privilege boundary provided by system calls in a conventional protected operating system.

The project documents a single privileged execution environment. The existence of a syscall calling convention does not independently isolate the calling code from kernel memory or hardware access.

---

## Memory Management

The kernel includes physical memory-management facilities and a bump allocator.

### Physical memory management

The kernel uses the bootloader's memory information to determine which physical memory regions are available.

Memory-management code must distinguish usable memory from regions reserved for firmware, devices, the kernel, and other purposes.

### Allocation

A bump allocator is a simple allocation strategy in which an allocation pointer advances through an available memory region.

Its primary advantages are simplicity and low allocation overhead. Its limitations depend on how the allocator is used and whether other allocation or reclamation mechanisms are available.

The precise allocation limits, alignment guarantees, exhaustion behavior, and reclamation semantics should be documented alongside the implementation.

### Shared address space

The kernel and its built-in functionality operate in a shared address space.

Consequently, incorrect pointer operations and buffer overruns can affect unrelated components. Memory safety is a system-wide concern rather than something enforced independently for each application.

---

## Interrupts and Hardware

SharkOS implements interfaces for several fundamental hardware components.

| Subsystem | Documented functionality |
|---|---|
| CPU | Processor identification and architecture-specific operations |
| Interrupt controller | Exception handling and hardware IRQ dispatch |
| Timer | Periodic timer events |
| Keyboard | PS/2 keyboard input |
| Mouse | PS/2 mouse input |
| RTC | Real-time clock access |
| PCI | Peripheral device enumeration |
| Graphics | Linear framebuffer rendering |
| Ethernet | RTL8139 and AMD PCnet network interfaces |

The supported hardware list describes the project's intended compatibility. Actual device behavior may depend on hardware revisions, emulator configuration, and driver implementation.

---

## Execution Model and Scheduling

SharkOS does not provide conventional user-mode process isolation.

Its shell commands, plugins, and application code execute with kernel privileges. The system therefore has no ordinary separation between trusted kernel code and untrusted application code.

The documentation describes cooperative yielding. In a cooperative execution model, a task must reach a yielding point or otherwise return control for other work to proceed.

This differs from preemptive scheduling, where a timer or another scheduling event can interrupt a running task and transfer control to another task.

The task implementation should be consulted for the exact scheduling behavior, task-state transitions, and yielding rules.

### Architectural consequences

- A task that fails to yield may prevent other work from progressing.
- A faulty task can modify kernel data.
- A task that corrupts its execution state can destabilize the system.
- There is no conventional process boundary to contain arbitrary native code.

These properties are important when evaluating reliability, responsiveness, and the safety of plugins.

---

## Graphics and Desktop Environment

SharkOS 98 is a framebuffer-based desktop environment implemented within the operating system.

### Framebuffer rendering

The kernel renders the desktop into memory and flushes the completed image to the framebuffer.

The project uses a back-buffer compositing approach in which the scene is assembled before being presented to the display.

This supports consistent redraws of windows and interface elements without requiring a separate window server.

### Window manager

The window manager supports up to 16 windows and documents the following operations:

- Window dragging and edge resizing.
- Focus and stacking-order management.
- Minimization and maximization.
- Restoration and closing.
- Taskbar integration.

### Desktop interface

The desktop includes a Start menu, desktop icons, window decorations, a system tray, and bundled wallpaper and icon resources.

The Settings application provides wallpaper options and shutdown functionality.

### Input

Mouse and keyboard input are used to interact with windows, menus, applications, and games.

Desktop interactions include selecting icons, opening applications through double-clicking, dragging window title bars, resizing windows, and switching window focus.

### Bundled applications

The graphical environment includes a terminal, System Info, Notepad, File Manager, Network, Settings, About, and FAQ interfaces.

These are integrated into the desktop rather than requiring a separate graphical application framework.

---

## Shell and Command Execution

The default command interpreter is `nemo-shell`.

The shell provides a command-line interface to the operating system's built-in functionality and extension mechanisms.

Its source is organized under `src/shell/`.

### Command categories

**Filesystem and text processing**

Commands include directory navigation, file inspection, copying, moving, deletion, searching, text transformation, and file editing.

**System inspection**

Commands provide information about the processor, memory, uptime, running tasks, environment, and system state.

**Networking**

Commands include `ifconfig`, `dhcp`, `ping`, `dns`, `wget`, and `netstat`.

**Utilities**

Additional commands provide basic calculations, formatting, date-related output, games, and miscellaneous utilities.

**System control**

Power-management commands include `poweroff` and `reboot`.

**Extensions**

The shell also exposes commands associated with plugins and package management.

The published README lists more than 90 built-in commands. Their exact syntax and semantics should be documented against the corresponding command implementations.

---

## Filesystem

SharkOS documents an in-memory tree filesystem.

The published design describes a fixed pool of 128 nodes, a maximum of eight children per directory, and a maximum file size of 2 KiB.

The initial filesystem contains user-oriented directories under `/sharkuser/` and system-oriented directories under `/System/`.

The system directory includes locations for binaries, drivers, configuration, logs, modules, networking, plugins, and other system resources.

### Storage behavior

An in-memory filesystem stores its data in RAM. Unless a separate persistence mechanism is used, changes to this data do not survive a reboot or power loss.

The repository also contains ATA-related code. The presence of a disk driver does not by itself establish that ordinary filesystem operations are backed by persistent disk storage.

Disk access, partition handling, filesystem mounting, and persistence should be documented separately according to the actual implementation.

---

## Networking

SharkOS includes a custom networking implementation with support for selected Ethernet adapters and common network protocols.

### Device drivers

The documented drivers support:

- Realtek RTL8139.
- AMD PCnet.

Device discovery uses PCI enumeration.

### Protocols

The README documents support for:

- Ethernet.
- ARP.
- IPv4.
- ICMP.
- UDP.
- DHCP.
- DNS.
- TCP.
- HTTP GET requests.

The source tree also includes separate cryptography and TLS implementation files.

The presence of these files does not establish complete HTTPS compatibility or a particular level of cryptographic security. Protocol coverage, TLS versions, certificate validation, hostname verification, and supported cipher suites must be confirmed from the implementation.

### Network commands

The shell provides commands for network configuration, connectivity testing, DNS lookups, HTTP retrieval, and interface status.

A working configuration depends on the selected network driver and the environment in which SharkOS is running.

---

## Plugin System

SharkOS provides a native extension interface called SharkAPI.

The plugin-related implementation is organized around:

- `include/sharkapi.h` — API declarations.
- `src/lib/plugin_manager.c` — plugin management.
- `src/shell/spkg.c` — package-management commands.
- `plugins/` — plugin source code.

### Plugin interface

The plugin documentation defines metadata and entry points for initialization, cleanup, command execution, and plugin information retrieval.

Plugins can use SharkAPI to access selected operating-system facilities, including console output and other shared system services.

### Package management

The `spkg` command provides the documented interface for listing, installing, and uninstalling plugins.

Typical commands are:

```text
spkg list
spkg install <name>
spkg uninstall <name>
```

The exact installation and removal behavior depends on the package-management implementation.

### Loading behavior

The README describes automatic discovery of `.plg` files in `/System/plugins/`.

The plugin guide also describes plugins being compiled as part of the build and included in the ISO.

These mechanisms should be documented as separate workflows if both are supported. The precise relationship between build-time compilation, runtime discovery, installation, symbol resolution, and unloading must be verified in the plugin manager.

### Security implications

Native plugins execute with kernel privileges and can potentially access shared memory and hardware directly.

Plugins should therefore be treated as trusted system code. The documented architecture does not provide a user-mode sandbox that isolates a plugin from the kernel.

---

## ELF Executable Loading

SharkOS includes an ELF loader for 32-bit executable binaries.

The README describes executing binaries located in `System/Bin/` through the shell.

ELF is a binary format that describes executable code, data, and associated metadata. A loader must interpret the relevant headers and arrange the executable's contents in memory before transferring control to it.

The supported ELF subset, relocation behavior, entry-point validation, memory requirements, and error-handling rules depend on the loader implementation.

Because SharkOS does not provide conventional user-mode isolation, a loaded executable must not be assumed to have the protections provided by an application process on a general-purpose operating system.

---

## SharkScript

SharkScript is a custom scripting language included in SharkOS.

The project includes a compiler and documents the `shs` command as the scripting interface. The compiler is also described as being embedded in the ISO.

A custom language introduces several distinct components:

- Language syntax and parsing.
- Representation of expressions and statements.
- Compilation or translation.
- Execution of the generated output.
- Integration with available operating-system services.

The precise language semantics, generated output format, supported features, and runtime behavior should be documented against the compiler and execution implementation.

SharkScript should not be assumed to have the same capabilities or security properties as Python, a conventional shell, or a sandboxed scripting runtime.

---

## Bundled Applications and Games

SharkOS includes a collection of utilities and games intended to demonstrate its graphical and execution facilities.

### Applications

- Terminal.
- System Info.
- Notepad.
- File Manager.
- Network.
- Settings.
- About and FAQ.

### Games

- Doom, including a custom BSP renderer.
- Flappy Bird.
- Pong.
- Super Mario Bros, world 1-1.
- Geometry Dash.

The project also documents a Python interpreter plugin.

These components provide practical examples of the system's rendering, input, plugin, and application interfaces.

---

## Repository Structure

The following structure summarizes the principal source directories and top-level build files.

```text
Shark-OS/
├── Makefile
├── boot.s
├── linker.ld
├── grub.cfg
├── compiler.elf
├── include/
├── src/
│   ├── arch/
│   ├── drivers/
│   ├── fs/
│   ├── lib/
│   ├── shell/
│   ├── ui/
│   ├── desktop/
│   ├── net/
│   ├── sharkscript/
│   ├── browser/
│   ├── doom/
│   ├── flappybird/
│   ├── pong/
│   ├── smb/
│   └── geometrydash/
├── plugins/
├── pc/
├── icons/
├── media/
├── PLUGIN_SYSTEM.md
├── PLUGIN_AUTO_DETECT.md
└── LICENSE
```

### Important files

| Path | Purpose |
|---|---|
| `boot.s` | Assembly entry point and low-level interrupt stubs |
| `linker.ld` | Kernel memory layout and link-time section placement |
| `grub.cfg` | Boot menu and kernel loading configuration |
| `Makefile` | Compilation and ISO-generation rules |
| `include/` | Kernel interfaces, shared declarations, and embedded resources |
| `src/arch/` | Architecture-specific code |
| `src/drivers/` | Hardware drivers |
| `src/fs/` | Filesystem implementation |
| `src/lib/` | Shared utilities, memory management, ELF loading, and plugin management |
| `src/shell/` | Shell implementation and command handling |
| `src/ui/` | Terminal and user-interface components |
| `src/desktop/` | Desktop, windows, menus, and graphical applications |
| `src/net/` | Networking, cryptography, and TLS code |
| `src/browser/` | Browser-related components |
| `src/sharkscript/` | SharkScript compiler implementation |
| `plugins/` | Native plugin source code |
| `media/` | Screenshots and other project media |

The tree is a functional overview rather than a complete file listing. Individual modules should be inspected before modifying subsystem interfaces or initialization behavior.

---

## Building from Source

### Prerequisites

A Linux development environment is recommended.

The published build instructions list the following requirements:

- GCC with 32-bit compilation support, or an appropriate `i686-elf` cross-compiler.
- GNU assembler (`as`).
- GRUB rescue-image tooling (`grub-mkrescue`).
- `xorriso`.
- `mtools`.
- QEMU or another compatible emulator for testing.

On Debian or Ubuntu, the README identifies `gcc-multilib` and `grub-pc-bin` among the required packages.

Additional dependencies may be needed depending on the host environment and the current Makefile.

### Clone the repository

```bash
git clone https://github.com/mayshecry/Shark-OS.git
cd Shark-OS
```

### Build the ISO

Run the default build target:

```bash
make
```

The documented output is `sharkos.iso`.

### Clean generated build artifacts

```bash
make clean
```

Cleaning and rebuilding can help when generated objects or other intermediate files are inconsistent with the current source.

---

## Running SharkOS

### Run in QEMU

```bash
qemu-system-i386 -cdrom sharkos.iso -m 512M
```

The memory value in this example configures the emulator. It is not a statement of SharkOS's minimum memory requirement.

### Run with networking

```bash
qemu-system-i386 \
  -cdrom sharkos.iso \
  -m 512M \
  -netdev user,id=n0 \
  -device rtl8139,netdev=n0
```

The example configures a virtual RTL8139 network adapter using QEMU's user-mode networking.

Once the system has booted, the documented workflow is to configure the network and test connectivity using the shell's networking commands.

For example:

```text
dhcp
ifconfig
ping 10.0.2.2
```

Successful operation depends on the network stack, driver, and emulator configuration.

### Other environments

The project also documents use with VirtualBox, Bochs, and real hardware.

Hardware testing should be performed carefully. Kernel-level errors can hang or crash the system, and the absence of process isolation means failures may affect the entire machine.

---

## Troubleshooting

### Build failures

Check that the required compiler, 32-bit toolchain support, GRUB tools, and ISO-generation utilities are installed.

If the build uses stale intermediate files, run:

```bash
make clean
make
```

### Display problems

SharkOS depends on the display configuration provided by the boot environment. Verify that the emulator supports the expected linear framebuffer mode and that the selected graphics configuration is compatible.

### Network problems

Verify that the virtual machine has a supported network adapter configured. For the QEMU example, check the RTL8139 device configuration and run the DHCP command after boot.

### Plugin loading failures

Verify that the plugin matches the expected architecture, file format, naming convention, and exported entry points.

Consult `PLUGIN_SYSTEM.md` and `PLUGIN_AUTO_DETECT.md` for the documented plugin interface and discovery behavior.

### Crashes and unexpected behavior

Because components execute with kernel privileges, a crash may originate from the shell, a plugin, the graphics code, a driver, or another subsystem.

Reproduce the failure with the smallest possible workload and inspect the relevant logs and source code. A fault in one component can corrupt shared kernel state, so the apparent location of a crash may not be the original cause.

---

## Security Considerations

SharkOS deliberately uses a highly integrated, privileged execution model.

There is no conventional user-space isolation between the kernel and its native applications. Shell commands, plugins, and supported executable binaries run within the privileged environment.

This has several consequences:

1. Native code can potentially access kernel memory and hardware directly.
2. Memory corruption in a plugin can affect the entire system.
3. Malicious or defective code is not contained by a conventional process boundary.
4. A command or executable can potentially disrupt kernel operation.
5. Networking and executable-loading code require particular care when processing untrusted input.

The existence of an API, package manager, executable format, or syscall interface does not independently provide isolation.

**SharkOS should be treated as an experimental operating system, not as a hardened multi-user platform.** Do not execute untrusted plugins or binaries, and avoid exposing the system to untrusted networks or workloads.

---

## Limitations

The architecture and documented features imply several important limitations.

- **Privilege isolation:** There is no conventional separation between kernel code and applications.
- **Memory safety:** Native code shares the kernel's address space.
- **Scheduling:** Cooperative yielding does not provide the same guarantees as preemptive scheduling.
- **Storage:** The documented filesystem is in memory; persistent storage behavior requires separate verification.
- **Hardware compatibility:** Device support is limited to implemented drivers and supported configurations.
- **Protocol compatibility:** The existence of a protocol implementation does not guarantee complete standards compliance.
- **Executable compatibility:** The ELF loader supports the format and features implemented by the project, not necessarily every 32-bit ELF executable.
- **Resource constraints:** Fixed-size structures and bounded buffers can limit workload size and flexibility.

These limitations do not negate the project's value as an operating-system implementation. They define the trade-offs that should be understood when using, extending, or evaluating it.

---

## Contributing

Contributions should preserve the project's freestanding execution model and respect the interfaces between architecture code, drivers, memory management, the shell, the desktop, and extension facilities.

When submitting changes:

1. Identify the subsystem affected by the change.
2. Review the corresponding headers and existing implementation.
3. Keep architecture-specific code separate from higher-level logic where appropriate.
4. Check buffer bounds, memory ownership, interrupt interactions, and failure paths.
5. Build the ISO and test the affected functionality.
6. Document new commands, configuration options, hardware requirements, and limitations.

Changes to shared interfaces should be reviewed for their effects on dependent modules. Kernel-level regressions can affect the entire operating system.

---

## License

SharkOS is distributed under the GNU General Public License version 3.0.

See [`LICENSE`](LICENSE) for the complete license text and applicable terms.

## Author and Acknowledgments

Created by [mayshecry](https://github.com/mayshecry/Shark-OS).

The project acknowledges the OSDev community and draws inspiration from the Windows 98 desktop aesthetic.

