# Desktop memory

On the same 256 MiB BIOS QEMU installed-disk configuration, free physical allocator space after the first desktop render went from **148,689 KiB** (v2) to **173,645 KiB**: **24.37 MiB more headroom** before opening an application. Both builds used an identical serial probe at the same point, an initially empty data volume, default desktop settings and an emulated RTL8139. This measures guest memory, not the QEMU process's host RSS, ISO size, or a browser peak.

The browser used to reserve its maximum JavaScript token, syntax-tree node, and property storage in the kernel at boot. Each now starts with 256 entries and grows in stable-address blocks, up to the same 200,000 entries per pool. Tokens and nodes also have less alignment padding. Allocated blocks are reused on navigation, not returned when a tab or window closes. Consequently the startup saving is not a promise of identical savings on a large script. The physical allocator reuses short tails in the memory map rather than discarding them when a larger request cannot fit. The Task Manager now counts used and reserved bytes from allocator free space, not from a heap address.

To inspect an installed disk without writing back test changes:

```sh
qemu-system-x86_64 -m 256 -drive file=install-build/disk.img,if=ide,format=raw,snapshot=on -nic user,model=rtl8139 -display none -serial stdio
```

Read `[desktop] free KiB=` in the serial output. For regression checks use `make test-desktop-memory test-installed-boot`. The host tests cover chunk boundaries, capacity, pointer stability and allocation failure. The guest test installs with 512 MiB, boots the installed BIOS desktop with 128 MiB, compiles C, downloads an image and executes two different pages that cross all three pool boundaries. It restores the disk under UEFI with 512 MiB. The BIOS figure is a tested configuration, not a universal minimum. Keep the 512 MiB recommendation for the live installer and UEFI; smaller UEFI configurations can stop in the bootloader before the desktop starts.
