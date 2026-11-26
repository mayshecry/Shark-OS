# Using the installed desktop

Fresh disk installs show system tools; games and Shark IDE are optional. Run
`spkg list`, `spkg install ide`, `spkg install pong`, or `spkg uninstall pong`.
These activate bundled packages offline. The icon and Start menu update immediately.
The live ISO still exposes the bundled apps for trying them without installing.

Theme, wallpaper image, color, mode and fit, global desktop-icon visibility,
Navigator's JavaScript setting and installed app choices are saved to the
installed SharkFS filesystem. Use `sync` before switching off; live changes
are temporary. `appearance` reports the theme; `appearance 0` through
`appearance 3` select Classic, Modern, Kawaii or Window Maker respectively.

Explorer has a quick-access pane in wider windows, an address display and a
name/size details list. Double-click or press Enter to open. Ctrl+C copies the
selected file (up to 256 KiB); Ctrl+V pastes in the current RAM-tree/installed
folder. Collisions get numbered copy names, not overwrites. No recursive folder
copy, mounted-volume paste or host clipboard is implemented.

Text clipboard: in Terminal copy the input line; in the IDE copy the current
line; in Navigator copy the address or focused non-password field. Paste inserts
text, not keyboard commands, so it never presses Enter for you. The clipboard
is session-local and holds up to 4095 bytes.

Navigator now treats noscript as inactive whenever scripting is enabled, even
if it cannot run an advanced script. A blank layout no longer triggers a
JavaScript-disabled redirect. Google remains Google. This is not a modern
browser engine and successful live Google results are not guaranteed. Test:
`make -j2 all && python3 tests/qemu_install.py --ahci`.
