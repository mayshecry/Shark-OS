# Selection and live video modes

Open **Settings → Video...**, choose a mode and click **Keep** within 15
seconds. Revert or let the preview expire to return to the previous mode.
An installed session restores the kept mode; the live ISO does not write a
preference. The shell equivalent is `video 1920x1080`, then `video keep` or
`video revert`. QEMU: use `-vga std` and 512 MiB for the live installer.

The display driver changes the hardware scanout, not just the canvas size.
It only enables switching for PCI 1234:1111 (QEMU/Bochs standard VGA) when
BAR0 matches the boot framebuffer, the DISPI registers describe the current
32-bit mode, and the requested mode fits the reported video memory. Other
adapters retain their boot framebuffer. Intel/AMD/NVIDIA, QXL and virtio-gpu
native modesetting are not implemented. Output modes: 800x600, 1024x768,
1280x720, 1600x900, 1920x1080. Scaling a VM viewer is a separate host setting.

* IDE: drag over text, Shift+arrow to extend, Ctrl+A for the document. Copy
  with Ctrl+C. Typing, Backspace or paste replaces an active selection.
* Terminal: drag across output or input. Ctrl+C copies the highlighted bytes;
  Ctrl+A selects the editable command. Replacing a selection never edits prior output.
* Navigator: drag across rendered page text, or Ctrl+A with the page focused,
  then Ctrl+C. Ctrl+L focuses the address for pasting. Text clipboard limit:
  4095 bytes. The clipboard is local to the guest.
* Explorer: click one item, Ctrl+click to toggle, Shift+click or Shift+arrow
  for a range, Ctrl+A for all. Copy/paste supports batches of files (256 KiB
  total); it doesn't copy folders recursively. Paste uses numbered names on
  collisions. Rename/delete remain single-item operations.

After `make -j2 all`, run `python3 tests/qemu_selection_video.py` for the
live-mode framebuffer size, unattended rollback, persistence, and selection
checks. `python3 tests/qemu_install.py --ahci` exercises the installation path.
