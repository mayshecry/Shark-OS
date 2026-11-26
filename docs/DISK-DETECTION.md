# Disk detection (v8)

Boot the updated desktop ISO, choose a live session, and run `disks` in the shell.
A disk is listed only after a completed ATA IDENTIFY and record checks. The model
and byte count come from the device; `inventory only` means the driver will not
issue data transfers. The shell reports PCI AHCI separately: a controller is not
evidence of an attached disk. An active direct SATA port must answer IDENTIFY before an AHCI disk is listed.
It is not necessary to change firmware settings to check this release.

`/System/devices` and `/System/partitions` no longer contain sample disk entries;
the latter is a whole-device inventory, not a partition parser. Installed sessions
use their boot-time enumeration to avoid renumbering their backing device.

Run `make test-identify` for the malformed-record cases, `make all` followed by
`python3 tests/qemu_disks.py` for ISO-only IDE and AHCI and two actual virtual
disk sizes, and `make test-installed-boot` for the disposable installation test.
Do not test installation on a disk containing data you need.

AHCI uses polled DMA, a fixed per-port bounce buffer, non-queued LBA48 reads/writes
and a cache flush on writes. A command failure stops further I/O on that port.
Only 512-byte logical sectors with DMA48 and cache-flush support are writable.
Enumeration is boot-time only (up to four verified disks); no hot-swap, port
multipliers, RAID/RST/VMD, native 4Kn, or optical installs. This x86 path expects
identity-mapped, coherent low-memory DMA and a 32-bit AHCI BAR, not a translating
IOMMU. Tested on QEMU Q35, not certified on physical controllers.

`make -j2 all server server32` generates both bootable `.iso` files and matching
`.iso.xz` downloads. Decompress the latter before attaching or flashing them.
Compression does not reduce the RAM needed to unpack the live installer; use
512 MiB or more. To exercise native SATA, add `-machine q35` to the example QEMU
command or run `python3 tests/qemu_install.py --ahci` and
`python3 tests/qemu_server_install.py --ahci`.
