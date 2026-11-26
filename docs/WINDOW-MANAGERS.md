# SharkOS desktop v11

`wmprefer` prints the active manager. `wmprefer JawsWM` and `wmprefer NemoWM`
switch immediately without closing apps. Names are case-insensitive. Installed
sessions commit the choice immediately to `/System/wm.pref` in SharkFS; live
ISO sessions only change in memory. A missing or unrecognized record defaults
to NemoWM. `wm` still controls window layout (tiling or floating), not the
manager preference. Both are built into the same SharkOS compositor.

NemoWM retains the icon desktop, Start menu, taskbar, wallpaper, palette and
optional packages. JawsWM starts floating with a slate-violet background, black
title bars and three 64-pixel dock tiles. Click the top-right tile, the `Main`
clip, or right-click the background for Applications. Hover over a branch and
move across into its submenu to choose an item. Escape dismisses the menu.
The second dock tile opens Terminal; the third opens Preferences. Windows lists
running windows, including minimized ones. This is a native SharkOS desktop,
not an X11 server or a port of Window Maker. Main is a single workspace.

Settings has five pages: **Manager**, **Style**, **Backdrop**, **Video** and
**System**. Use the +/- buttons to page through long lists in a smaller window.
JawsWM's palette and backdrop are independent of NemoWM. To change resolution
choose Video, pick a supported mode, and Keep it within 15 seconds.

Build from an extracted ZIP with `make -j2` or
`make -j2 PYTHON=/absolute/path/to/python3`. The ISO is `sharkos.iso`.
After building, `python3 tests/qemu_wmprefer.py` uses a disposable disk for
BIOS/UEFI reboot, CLI, framebuffer, menu and Settings checks.
