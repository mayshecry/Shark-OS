# Installing the desktop system

The desktop ISO doubles as the installer. It includes a **separate, bootable disk image** of the same build, not a copy of the running kernel's memory. The image has an MBR, Limine BIOS stages and a FAT32 EFI System Partition with the boot files, kernel, desktop assets and plugins. The desktop ISO and installed disk use the same 16 MiB-linked kernel for BIOS and UEFI. The live kernel and installer payload are gzip-compressed; the installed kernel stays uncompressed to lower bootloader RAM use.

Use a disposable virtual disk first. The following commands **create a new** 128 MiB image and boot the live media (QEMU refuses to create the disk if you skip the first command and it does not exist):

```sh
test ! -e shark-test.raw && qemu-img create -f raw shark-test.raw 128M
qemu-system-x86_64 -m 512 -boot order=d -cdrom sharkos.iso -drive file=shark-test.raw,format=raw,if=ide,index=0
```

1. Start the default **SharkOS** entry in Limine. In the same setup screen as **Window manager**, select **Install to disk**, then Enter.
2. Select the AHCI or ATA disk number. Check the model, capacity and interface. The installer displays the required image size. Use a spare, blank disk only. Esc cancels before confirmation.
3. Type the displayed phrase (for example, `install 0`) and press Enter. The installer verifies the payload CRC, reads the target's last sector and scans **every byte it would overwrite**. Nonzero data there, an unreadable sector, an invalid image or insufficient capacity prevents the first write. It does **not** promise to detect unrelated data elsewhere on a larger drive.
4. Leave power on. Each chunk is flushed and read back; LBA 0 is committed last. If writing fails, there is no rollback. Recreate the disposable blank disk before retrying. On success, shut down and detach the ISO. To boot the result in QEMU:

```sh
qemu-system-x86_64 -m 512 -boot order=c -drive file=shark-test.raw,format=raw,if=ide,index=0
```

The installer targets verified 512-byte logical-sector AHCI/DMA48 and legacy ATA/LBA28 disks. NVMe, USB and virtio targets are not supported. It does not resize, dual-boot, repartition a used drive or claim the unused disk tail. Installation creates a fresh system and a private data partition. It does not import the live session or upgrade an older disk. Back up anything needed and install to a new blank target. Existing FAT16 data-volume mounting still works separately. The installed OS does not include another installer payload. Lite and Legacy stay available; the live-only entry skips loading the installer into memory. Use 512 MiB or more for the live installer in QEMU.

# Files, Explorer and image downloads

Boot the installed disk, not the ISO. The desktop skips setup and restores the saved tree. Hold **F2** and press **G** once to toggle tiling/floating. This also works on the live desktop, but live sessions do not save the setting or their working files. Explicit installer and FAT16 mount operations remain separate.

Open **File Manager** or type `explorer` in a desktop terminal. Use the arrows to select, Enter or a double-click to open, and Backspace to go up. Folder, File, Rename and Delete are on the toolbar (keyboard: N, F, R, D); Enter confirms and Esc cancels. Non-empty directories cannot be deleted. Text opens in Shark IDE. Images open in Shark Navigator; try `/pc/wallpaper.png`. PNG, JPEG, GIF, WebP and SVG use the existing decoders, with a 256 KiB input limit. Navigator can save a direct image using **Save** or **Ctrl+S**. **Ctrl+click** saves a displayed inline image while its original bytes are cached (1 MiB cache per page). Downloads go in `/Downloads`; repeat saves get numbered names instead of overwriting files.

The working tree has up to 128 nodes, 64 children per directory, and 31-byte names. Each on-disk snapshot has just under 4 MiB for file bytes **and** metadata. It covers the whole tree except the contents of separate mounted FAT volumes. Browser and Explorer saves request a commit immediately. Other changes are checked periodically; `sync` in the shell reports the storage status. Run it before cutting power. If a save fails or the snapshot is too large, the working copy is still in RAM; free space and retry `sync` before rebooting.

The partition has two checksummed banks: a new payload is flushed before its header is published. If the newest snapshot is damaged at boot, SharkOS tries the older one. If neither is readable it runs in RAM rather than formatting the volume. The installed boot configuration identifies the data partition for that build. Do not attach multiple installations made from the same image at once: ambiguous matches disable saving. The boot ESP is not used as a writable desktop filesystem.

# Compiling inside SharkOS

Open **Terminal** in the desktop. A fresh filesystem includes `hello.c` and the library example `square.c`. Try:

```text
cc hello.c
./hello
```

Output from the program: `Hello, world!`. `cc` links by default but does not execute code during compilation. `run hello` is another way to launch the saved program. On installed boots run `sync` before power-off if you want to keep the file. A linked application does not need its sources to run, even after switching between BIOS and UEFI.

```c
#include <sharkapi.h>

int main(void) {
    sharkapi_println("Hello, world!");
    return 0;
}
```

Use `cc hello.c -o greeting` to name the output, or `cc main.c helper.c -o demo` to compile and link several C source files in one command. There must be one `main`; `int main(int argc, char **argv)` also works. `run demo one two` passes whitespace-separated arguments. Up to 16 source files and 16 program arguments are accepted. `cc -c square.c` still produces a standard x86-64 ELF relocatable `square.o` for the separate object-file workflow. `cc` and `tcc` are aliases. The in-guest application-link command takes C sources, not host libraries or arbitrary object files.

The compiler emits a position-independent linked image. The saved `SAP1` application uses an ELF64 layout with a SharkOS signature, not a Linux executable. The loader binds supported SharkAPI symbols for the current kernel, locates `main`, and starts each invocation with fresh program data. Missing `main` and unresolved imports are link errors; a failed link does not overwrite a previous output. Application imports include the SharkAPI console, string, drawing, time, file-existence/size and beep helpers, plus `memcpy`, `memset`, `strlen`, `strcmp` and `strcpy`. See `cc_export` for the exact list. There is no hosted `stdio.h`, POSIX process model, background callback support, TLS, or constructor/destructor runtime.

Quoted project headers and built-in `stdint.h`, `stddef.h`, `stdbool.h` and `sharkapi.h` are available. The compiler uses an 8 MiB reusable arena plus 1 MiB output buffer on demand. The launcher allocates a reusable 2 MiB image slot on first run. Programs run synchronously at kernel privilege: compile and run only trusted code. Save outputs in the working tree rather than a FAT mount. The Linux host `tools/sharkcc` and the existing plugin workflow are separate.

# Building and testing

On x86-64 Linux install GNU make, GCC, binutils, Python 3.8+, gzip, bzip2, tar, xorriso and mtools. TinyCC and musl sources and their license notices are included. `make -j2` builds the ISO offline. QEMU x86-64 and OVMF are only needed for the VM checks.

```sh
make -j2
make test-native-cc test-native-link test-install test-framebuffer test-sharkcraft
make test-installed-boot
make test-vnic test-firewall test-router test-sftp sharkcc
make -j2 server server32
```

The installed-boot test uses only a temporary raw file: it boots the ISO with OVMF, rejects a wrong confirmation, installs, compares the entire written extent and untouched tail, then removes the CD, builds an object file, links and runs the Hello World app under BIOS, and runs the persisted app under UEFI without recompiling. For non-Debian OVMF paths use `python3 tests/qemu_install.py --firmware-dir DIR` with `OVMF_CODE_4M.fd` and `OVMF_VARS_4M.fd` in that directory.

The test also downloads a PNG over HTTP, compares saved file bytes, reopens it in Explorer after changing firmware, verifies the saved window mode, and checks that running the live desktop leaves the attached installed disk byte-identical. It damages disposable snapshot banks to check fallback and read-only recovery.
