# SharkOS

SharkOS is a hobby operating system written in C and x86_64 assembly. It boots on real hardware and in QEMU/VirtualBox, and brings up a full graphical desktop from a single freestanding kernel image.

This is the 64-bit branch (v3.0 Suzuna). The old 32-bit GRUB-based build has been replaced by a long-mode kernel with a 128GB identity-mapped address space, booted by Limine in both BIOS and UEFI mode.

If you just want to try it:

```bash
qemu-system-x86_64 -m 512 -smp 2 -enable-kvm -cpu host \
  -cdrom sharkos.iso -boot order=d \
  -vga std -display gtk,zoom-to-fit=on \
  -audiodev pa,id=snd0 -device intel-hda -device hda-duplex,audiodev=snd0 \
  -netdev user,id=net0 -device e1000,netdev=net0
```

---

## What it is

SharkOS isn't a Linux distribution and it doesn't run on top of another OS. It's a kernel, drivers, memory manager, filesystem, shell, desktop, browser and networking stack compiled together into one privileged image.

There is no userspace separation. The shell, desktop, window manager, drivers and plugins all run in Ring 0 in the same address space. That's intentional — it makes the system simple to hack on — but it means a bad pointer can take down the whole machine. Treat it as experimental.

The desktop will look familiar if you've used Windows 98. That's on purpose. The current release ships four themes (Win98, Modern, Kawaii, WindowMaker) and several wallpapers including the Suzuna series. Icons, window borders and the taskbar all adapt per theme.

---

## What changed in 64-bit

The move to 64-bit was not just recompiling with `-m64`. The boot path, paging and toolchain all changed:

**Long mode from the first instruction.** `boot.asm` sets up a 64-bit GDT, enables PAE and LME in EFER, builds the PML4 in `arch/early.c`, and does a far jump `0x08:long_mode_entry`. There is no 32-bit protected-mode kernel anymore.

**128GB identity map.** Instead of a higher-half trick, the kernel creates 64 PDP entries of 1GB each (2MiB pages) identity-mapped at boot. Drivers can use `virt_to_phys()`/`phys_to_virt()` directly without remapping. Limine's framebuffer and ACPI tables are already where the kernel expects them.

**Limine, not GRUB.** `limine.conf` defines a single entry with `protocol: multiboot1` and `path: /SharkOS`. `make` builds a hybrid ISO with `limine-bios.sys`, `limine-bios-cd.bin`, `limine-uefi-cd.bin` and `BOOTX64.EFI`, then runs `limine bios-install` for GPT embedding. It works on QEMU `-bios OVMF.fd` and on real USB sticks without the old `sudo grub-mkrescue` permission issues.

**Audio was added at the same time.** AC'97 (ICH, `NABMBAR` at `PCI BAR 0x10`) with a 32-entry BDL and 64KB circular DMA, and Intel HDA (`BAR0` MMIO with `GCAP/GCTL/CORB/RIRB/SD` at `0x80` stride `0x20`) are both probed via PCI. Both resample to 48kHz stereo through the same 8-channel mixer.

---

## How it boots

1. Firmware (BIOS or UEFI) loads Limine from the ISO. Limine finds `boot/sharkos.bin` on the ISO filesystem.
2. `_start` in `boot.asm` saves the Multiboot info, zeroes BSS, loads the GDT64, enables paging and long mode, and calls `kmain`.
3. `kmain` in `src/shell/main.c` runs driver init in order: serial, GDT/IDT/PIC, PIT at 100Hz, RTC, PCI scan, ATA, audio, networking.
4. The desktop is initialized: framebuffer from Limine, desktop icons from `desktop/icons.c` (now 4 variants per `WINDOW_TYPE`), window manager (16 windows max), start menu and wallpapers.
5. The main loop polls keyboard/mouse, `net_poll()`, `audio_tick()` every 10ms, and composites the dirty region to the backbuffer before flipping to `lfbptr`.

You can watch this on serial:

```bash
qemu-system-x86_64 -cdrom sharkos.iso -serial stdio -display none
```

Look for `[audio] found ac97/hda ...`, `[net] RTL8139/e1000`, and `[desktop] wallpaper suzuna`.

---

## Architecture

**arch/** — GDT64, IDT, PIC remap, PIT, CPU identification, MTRR setup. Interrupts 32-47 are remapped so the keyboard (33), mouse (44) and audio IRQs don't collide with CPU exceptions.

**drivers/** — Keyboard (PS/2), mouse, PCI enumeration, ATA PIO, RTC. `drivers/audio/` holds `audio.c` (PCI scan + `audio_init()`), `mixer.c` (8 channels, linear resample to 48kHz, master volume), `ac97.c` (`NABMBAR` PO `BDBAR/CIV/LVI/SR/PICB/CR`), `hda.c` (MMIO `CORB/RIRB` verbs), `wav.c` (RIFF/WAVE PCM parser) and `pcspkr.c` (PIT channel 2 at `0x42/0x61` fallback).

**lib/** — Physical memory bump allocator, ELF64 loader, SharkAPI, plugin manager. `pmm.c` hands out page-aligned frames from the 128GB pool; `elf.c` validates and maps `PT_LOAD` segments.

**fs/** — In-memory filesystem (`shmfs`). 128 nodes, 8 children per directory, 2KiB max file. `/sharkuser/` and `/System/` are populated at boot. ATA provides persistence if a disk is present, but by default everything lives in RAM.

**desktop/** — Window manager, desktop grid, icons, theme engine, PNG/JPEG/GIF/WebP decoders, bootscreen. Right-click on the desktop gives View -> Show Desktop Icons (checked state), Arrange, Refresh, Open Terminal and Settings. Each icon can be Hidden or Deleted, with screen-edge clamping and flat bevels for the WM theme.

**browser/** — Shark Navigator: HTML, CSS, layout, JS and SVG in `browser/`. It's intentionally small — enough for docs and `https://example.com`, not a full Chromium replacement.

**net/** — Custom stack: Ethernet, ARP, IPv4, ICMP, UDP, DHCP, DNS, TCP, HTTP/HTTPS (with `crypto.c`/`tls.c`). Drivers for Realtek RTL8139 (`10EC:8139`), AMD PCnet (`1022:2000`) and Intel e1000 (`8086:10D3/1502`).

**shell/** — `nemo-shell`, 90+ commands. Filesystem (`ls/cd/cat/mkdir/rm/cp/mv`), system (`sysinfo/ps/htop/sysinfo`), networking (`ifconfig/dhcp/ping/dns/wget/netstat`), utilities (`sharkfetch/neofetch/banner/cowsay/fortune`), audio (`audio status/beep/tone/test/wav`), and package management (`spkg install/list/uninstall`).

---

## Audio

Audio is probed after ATA at boot screen 73%. If a PCI class `0x0401` (AC97) or `0x0403` (HDA) device exists, its IRQ is unmasked in the PIC and `audio_irq_handler()` is wired to vectors 37-47.

The kernel mixer always runs at 48kHz s16 stereo, 480 frames per tick (10ms, 1920 bytes). Per-channel state is `sample_rate/channels/bits/volume/loop/frac_pos/frac_step`. Resampling is linear interpolation: `frac_step = src_rate * 65536 / 48000`, mono is duplicated to stereo, 8-bit unsigned is shifted to signed 16-bit.

For plugins and games:

```c
#include "sharkapi.h"
int h = sharkapi_audio_play(pcm, bytes, 44100, 1, 16, 220);
sharkapi_audio_play_wav(wav_data, wav_len, 200);
sharkapi_beep(880, 200);
sharkapi_audio_stop(h);
```

In the shell:

```
audio status
audio test        # 440Hz square wave through the mixer
audio tone 523 300
audio wav /System/Sounds/startup.wav
audio beep
```

If no DMA device is present, `audio_tick()` still mixes but discards, and `pcspkr_beep()` is available. Host-side proof is in `tools/audio_harness/` — a standalone `host_mixer.c` that reproduces the kernel mixer and writes `output.wav` (1.5s, 48k stereo, 282KB) without QEMU.

---

## Networking

The kernel scans PCI for `10EC:8139`, `1022:2000` or `8086:*` and brings up the first match. QEMU user-mode networking is enough:

```text
dhcp
ifconfig
ping 10.0.2.2
dns example.com
wget https://example.com
browser https://example.com
```

TLS is a minimal implementation in `net/tls.c` — it will fetch most HTTPS sites but don't rely on it for strict certificate validation.

---

## Plugins and games

Plugins are ELF objects built as `.plg` and discovered in `/System/Plugins/`. They expose `plugin_init/cleanup/command/info` and call back into the kernel through SharkAPI.

Included:

- doom (BSP renderer), flappybird, pong, Super Mario Bros 1-1, Geometry Dash
- sharkcube (builtin), sharkcraft (Minecraft parody with per-theme textures)
- fnaf — Five Nights at Freddy's 1 port, shipped uninstalled with a desktop icon; double-click runs `spkg install fnaf`
- python-interp

```text
spkg list
spkg install fnaf
spkg uninstall fnaf
```

SharkCraft and FNAF demonstrate the new icon system: each `WINDOW_TYPE` resolves `theme_get_id()` (0=Win98, 1=Modern, 2=Kawaii, 3=WM) to a different `micon_*` array.

---

## Building

You need a Linux host. Cross or native `x86_64` GCC will work as long as it supports `-m64 -ffreestanding`.

```bash
sudo apt install build-essential nasm xorriso mtools curl qemu-system-x86 ovmf
git clone https://github.com/mayshecry/Shark-OS.git
cd Shark-OS
make              # builds sharkos.bin (9.2M) and sharkos.iso (14M)
```

What `make` does:

- Compiles everything with `-m64 -ffreestanding -Os -Wall -Wextra -fno-pie -fno-stack-protector -mno-red-zone -mcmodel=kernel -Iinclude`
- Links with `linker.ld` to `sharkos.bin`
- Fetches Limine 8.x if `limine/` is missing, copies `limine-bios.sys`, `limine-uefi-cd.bin`, `BOOTX64.EFI`
- Builds the ISO with `xorriso` (`-b boot/limine/limine-bios-cd.bin --efi-boot boot/limine/limine-uefi-cd.bin`) and runs `limine bios-install`

Other targets:

```bash
make clean        # removes isodir/ arch/ drivers/ fs/ ... sharkos.bin sharkos.iso
make limine       # re-fetch Limine binaries only
```

Common issues:

- `limine/limine: Permission denied` — `chmod +x limine/limine`, it loses the bit when copied via `curl`.
- `Failed to open device` from xorriso — don't run `sudo make`. It leaves `sharkos.iso` owned by root. `sudo rm -f sharkos.iso && sudo chown -R $USER .` then `make` again.
- `Boot protocol not specified` / `Blank Entry` on Limine — `limine.conf` must use `/SharkOS` with `protocol: multiboot1`, not `:` syntax. The Makefile already installs it to `isodir/boot/limine/limine.conf` and `isodir/limine.cfg`.

---

## Running

QEMU is the easiest way to develop. The single command with video, audio and internet is at the top of this file.

A few variations:

```bash
# AC97 instead of HDA, with serial logs
qemu-system-x86_64 -cdrom sharkos.iso -m 512 -soundhw ac97 -serial stdio

# Just video + net, no audio
qemu-system-x86_64 -cdrom sharkos.iso -m 512 -vga std -netdev user,id=n0 -device e1000,netdev=n0

# UEFI
qemu-system-x86_64 -cdrom sharkos.iso -m 512 -bios /usr/share/OVMF/OVMF.fd -vga std
```

VirtualBox: create a type Other/Unknown (64-bit) VM, attach `sharkos.iso` as optical drive, enable EFI, VT-x/AMD-V and audio Intel HD.

Bochs/real hardware: write `sharkos.iso` to USB with `dd` or Rufus. The ISO is El Torito + GPT hybrid so it boots both ways. Real hardware testing will hang on kernel panic — there's no recovery shell once Ring 0 state is corrupt.

---

## Project structure

```
Shark-OS/
  boot.asm          # long-mode entry, far jump
  linker.ld         # kernel layout
  limine.conf       # Limine entry: /SharkOS multiboot1
  Makefile
  include/          # kernel.h audio.h sharkapi.h desktop.h theme.h ...
  src/
    arch/           # early.c io.c interrupts.c cpu.c mtrr.c
    drivers/        # keyboard / pci / mouse / rtc / ata / audio/*
    fs/             # in-memory fs
    lib/            # pmm / elf / sharkapi / plugin_manager
    desktop/        # windowmanager / desktop / icons / theme / bootscreen / png/jpeg/gif/webp
    browser/        # html / css / layout / js / svg / font
    shell/          # main.c commands.c spkg.c lite.c
    net/            # net.c crypto.c tls.c
    sharkscript/    # shs.c
    doom/ flappybird/ pong/ smb/ geometrydash/ fnaf/
  plugins/          # doom / flappybird / pong / smb / gdash / sharkcube / sharkcraft / fnaf
  tools/audio_harness/  # host_mixer.c -> output.wav
  docs/             # AUDIO.md
  isodir/           # staged ISO root
```

Start by reading `include/kernel.h` and `src/shell/main.c:kmain`. For desktop changes, see `src/desktop/desktop.c` and `src/desktop/icons.c`. For networking, `src/net/net.c:net_init()`.

---

## Limitations

No privilege separation. No preemptive scheduler (tasks must `yield()`). No persistent filesystem without ATA. No full POSIX. The ELF loader only handles the subset SharkOS emits. The network stack is not fuzzed for hostile input.

These are tradeoffs for a small, hackable codebase, not bugs to file.

---

## Contributing

Keep arch code in `src/arch/`, don't add hosted libc dependencies, check buffer bounds before touching `fs/` or `desktop/`, and test both BIOS and UEFI in QEMU before opening a PR. Build the ISO and boot it — if it triple-faults, the screen will be blank and serial is the only log and you can use: https://github.com/mayshecry/byos.git to make ur own distro

---

## License

GPL-3.0. See `LICENSE`.

Created by mayshecry. Thanks to the OSDev community and the Limine project.

