# Install SharkOS Server from the CLI

Use the Server ISO (64-bit BIOS/UEFI) or Server32 ISO (32-bit BIOS).
Boot **Live + CLI installer**. Nothing is installed automatically. The
lower-RAM live-only entry omits the payload. For a reproducible VM setup
use 512 MiB of RAM and a new, zero-filled 128 MiB IDE disk.

From the host's local keyboard or serial console:

1. Type `install` to list verified ATA disk numbers, models and capacities.
2. Type `install N`, substituting the number of a spare blank disk.
3. Check the model and size, then enter the exact `INSTALL N` confirmation.
   A different line cancels. Do not install if you cannot identify the target.
4. Wait for the success message. Remove the ISO and reboot from the disk.
   Installed boots start the server shell and do not offer installation.
5. Use `sync` from the installed host console to save the host filesystem
   before reboot. Live sessions do not restore or save the installed tree.
   Runtime settings such as running services are not a filesystem snapshot.

The installer checks the payload CRC, checks the disk range it will overwrite
and the last sector for existing data, writes and reads back the image, and
commits the boot record last. This is not a partition-preserving installer.
Verified 512-byte SATA disks on AHCI (DMA48) or legacy IDE (LBA28) can be written.
NVMe and USB mass storage are not supported install targets.

Build: `make -j2 server server32`. Test: `make test-install` followed by
`python3 tests/qemu_server_install.py` (QEMU and OVMF; disposable disks only).
