# V12 desktop: RAM budget and tested minimums

## Measured, not estimated

The metric is *configured guest RAM minus the free bytes reported by the guest physical allocator*. It includes kernel code, boot structures, reserved guest RAM and rendering buffers. It is not Linux host RSS, VRAM, or a claim that every nonfree byte is application-owned. The steady sample is taken ten seconds after first render, with no apps open, after desktop preference capture and periodic storage sync. `memstat` in Terminal reports allocator-free KiB; `cat /proc/meminfo` and Task Manager expose memory information too.

| QEMU guest (one qemu64 vCPU, std VGA) | Nonfree RAM |
|---|---:|
| v11, installed NemoWM, 1024x768, SeaBIOS, 128 MiB, first frame | 102.09 MiB |
| v12, same configuration, first frame | 35.99 MiB |
| v12, same configuration, steady idle | 36.05 MiB |
| v12, installed NemoWM, 1024x768, OVMF, 192 MiB, steady idle | 41.79 MiB |
| v12, live ISO with installer available, OVMF 192 MiB, desktop idle | 44.22 MiB |
| v12, installed JawsWM, saved 1920x1080, OVMF 192 MiB, steady idle | 46.82 MiB (49.09 decimal MB) |

The startup comparison saves 66.10 MiB (about 65%) on the same VM. In the 1080p guest a local JS page reduced free RAM from 148662 to 121490 KiB. Closing Navigator returned it to 148138 KiB. A second open/close cycle returned to exactly that level. Small parser blocks and other high-water caches may remain; this is not a claim that every app returns to cold-boot usage. Different resolutions, image wallpaper caches, installed files, firmware and devices change the figures.

## What to configure

* **Installed BIOS:** 48 MiB was the lowest passing *interactive* configuration. It rendered, opened Terminal, compiled and ran `hello`, switched to JawsWM and saved the choice. At 47 MiB it rendered and linked `hello` but did not complete the run test; 46 MiB did not reach the desktop. Use **96 MiB or more** for Navigator/FreeDoom, not the 48 MiB floor.
* **Installed UEFI:** 168 MiB passed the interactive test with this OVMF image; 160 MiB did not reach the kernel. Use **192 MiB or more**. Limine/OVMF needs staging memory during boot that the idle desktop does not need. These are tested settings, not universal firmware limits.
* **Booting the full installation ISO:** use **96 MiB BIOS or 192 MiB UEFI**. Both completed an actual installation to a blank disk exactly 76,546,560 bytes long, matched every byte of the expected image, then booted from disk and ran the compiled program. At 88 MiB BIOS and 176 MiB UEFI the ISO did not reach setup in this configuration. These installer numbers do not describe the separate live-only menu entry.
* **CPU and devices:** tested with one emulated x86-64 `qemu64` CPU, a 32-bit-color std VGA framebuffer, PS/2 keyboard and mouse, and IDE or Q35 SATA/AHCI storage. Live 1080p switching uses QEMU std VGA as in v11. There is no measured GHz floor, and no physical hardware claim. For a VM, use a **blank 128 MiB disk**; 74 MiB is sufficient for this particular image. Installing writes the disk, not a new partition alongside existing data.

## Where the savings came from

* Navigator leases its large HTML, style, layout and JS arenas from the high ends of usable physical regions rather than reserving them in kernel BSS. Actual close releases the leases in reverse order; minimize leaves them intact. An unsuccessful reservation rolls back without starting the parser. No reduction in the parser's existing maximum array sizes.
* FreeDoom's WAD is losslessly compressed in independently addressable blocks. The game reads through a two-block cache, so startup doesn't need a permanent 28 MiB uncompressed WAD. Wallpapers retain the original pixel values, decoding pairs of lossless strips for the scaler.
* The snapshot writer uses small scratch storage for small snapshots, allocating full-bank capacity only if needed. Existing volume format and recovery behavior are unchanged. The display shadow can grow and reuse its old span instead of permanently pinning two framebuffers. Explicitly usable RAM between 2 MiB and the kernel load address is no longer thrown away; the loaded ELF and boot modules are excluded.
* The desktop installer module describes zero sectors, literal sectors and sectors already present in immutable kernel text/data. It no longer materializes a duplicate ~73 MiB disk in RAM. The build verifies the reconstructed CRC, and the installer checks bounds, CRC and blank space before writing; read-back verification and MBR-last commit remain in place. Installed disks still contain a standalone bootable kernel.

## Reproducing the checks

From the source root on Linux with Python 3, QEMU, SeaBIOS and OVMF (`/usr/share/OVMF` or `--firmware-dir`), build with `make -j2 all`, then run for example:

```
MEM_STEADY=1 python3 tests/qemu_memory.py --bios-ram 48
MEM_PROFILE=efi MEM_STEADY=1 python3 tests/qemu_memory.py --efi-ram 168
MEM_WORK=doom python3 tests/qemu_memory.py --bios-ram 96
python3 tests/qemu_warm_memory.py --efi-ram 192
python3 tests/qemu_min_install.py --iso-ram 96 --bios-ram 48
MEM_PROFILE=efi python3 tests/qemu_min_install.py --iso-ram 192 --efi-ram 192
python3 tests/qemu_install.py --ahci --iso-ram 192 --bios-ram 96 --efi-ram 192
python3 tests/qemu_wmprefer.py
python3 tests/qemu_selection_video.py
make test-install test-desktop-memory test-native-link
```

Tests create disposable VM disks. Firmware sensitivity is significant: results above are from QEMU 10.0.13, 4 MiB OVMF images and SeaBIOS, not estimates from the ELF size.
