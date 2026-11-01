# SharkOS IDE, storage, networking, and GCC workflow

## Shark IDE

The desktop Notepad entry is now Shark IDE. It edits files in the SharkOS filesystem, opens files from File Manager, highlights common C keywords, strings, comments, and numeric literals, shows line numbers, supports Tab indentation and arrow-key movement, and saves with Ctrl+S. The terminal `edit <path>` command opens the IDE while running in desktop mode. The RAM filesystem currently limits individual files to 65,535 bytes plus the terminating zero byte.

## ATA FAT16 volumes

SharkOS probes ATA PIO disks during boot. The shell `mount` command detects available drives and mounts FAT16 volumes from either a partition table or a superfloppy volume. The first mount without an explicit mountpoint creates `/mnt/diskN`.

```text
mount
mount 0
cd /mnt/disk0
ls
cat readme.txt
edit readme.txt
```

An explicit existing directory can be supplied as the second argument: `mount 0 /mnt`. Use `umount /mnt/disk0` to detach a mounted volume. Filesystem traversal supports FAT16 8.3 names and ordinary directories. File data is cached in the kernel filesystem, and Ctrl+S writes it back to the original FAT16 clusters and updates the directory entry. Edits must fit in the file's existing cluster allocation; this driver does not yet allocate or free clusters, create persistent directory entries, support FAT32, long filenames, or large files beyond SharkOS's RAM file limit. `touch`, `mkdir`, and copies into a mounted directory are RAM overlays and disappear after unmount. Back up disks before testing: SharkOS runs entirely in ring 0.

For QEMU, create a disk image on the Linux host, format it as FAT16, and attach it as an IDE drive:

```bash
truncate -s 64M shark-data.img
mkfs.fat -F 16 shark-data.img
qemu-system-x86_64 -m 512 -cdrom sharkos.iso -drive file=shark-data.img,format=raw,if=ide,index=0
```

ATA drive indexes are the order reported by `mount`, not Linux device names. The existing ATA driver supports PIO reads and writes.

## curl and WebSocket

`curl` uses the kernel HTTP/TLS stack. It supports GET, HEAD, POST data, custom request headers, redirect following, verbose status output, and saving responses with `-o`. `-d` selects POST unless `-X` overrides the method. The response buffer and per-file RAM limit are both 64KiB. FAT16 writes must also fit the file's existing cluster allocation. HTTPS inherits the kernel TLS stack's certificate-validation limitations.

```text
curl -L https://example.com
curl -v -I https://example.com
curl -X POST -H 'Content-Type: application/json' -d '{"hello":"world"}' https://example.com/api
curl -o page.html https://example.com
cat page.html
```

The `websocket` command performs a WebSocket upgrade over `ws://` or `wss://`, sends an optional text message, and prints the first server message. The kernel API supports masked frames up to 65,535 bytes and incremental receive parsing across fragments and partial TCP reads. Browser JavaScript also has a single-connection `WebSocket` constructor with open, message, error, and close events. The network layer has one active TCP connection, so a live WebSocket cannot coexist with HTTP fetches.

```text
websocket ws://echo.websocket.events 'hello from SharkOS'
websocket wss://echo.websocket.events 'secure hello'
```

The browser renders top-level plain text, JSON, XML, JavaScript source, and supported image MIME types in addition to HTML. Image input is capped at 256KiB, JavaScript strings and `fetch()` bodies/responses at 64KiB, and URL-encoded GET/POST forms are supported. `fetch()` follows basic redirects but runs synchronously and only emulates immediate `then()` chains. XMLHttpRequest has a synchronous compatibility implementation with basic status, text/JSON response, headers, and events; neither API provides browser-style asynchronous scheduling or streaming. Browser `WebSocket` supports one active socket and message events, but that socket occupies the network stack's single connection. Modern JavaScript frameworks, PDF/video playback, and strict TLS certificate validation are not supported.

## GNU GCC

The kernel is already compiled and linked with GNU GCC using its freestanding x86-64 kernel flags. The host-side `tools/sharkcc` wrapper applies those same compile flags to an individual C source file and emits an ELF64 relocatable object suitable for the SharkOS kernel build. `make sharkcc` runs a GCC smoke compile and verifies the ELF object; `make gcc-info` reports the host compiler and target.

```bash
make gcc-info
make sharkcc
CC=x86_64-elf-gcc tools/sharkcc src/example.c example.o
```

The `gcc --version` shell command reports the compiler version used to build the kernel. GCC itself is not launched inside SharkOS: this project has no hosted POSIX userspace, process isolation, or C runtime, and a Linux GCC executable cannot run in the freestanding kernel. To build the operating system, use `make` on the Linux host. To compile source files there with the project ABI and flags, use `tools/sharkcc`.
