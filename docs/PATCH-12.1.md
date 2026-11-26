# SharkOS Desktop 12.1

In **JawsWM → Preferences → Backdrop**, select one of the four solid colors or one of the six bundled wallpaper images. The + button pages forward to Fill, Fit and Stretch. The installed desktop saves JawsWM's selection immediately and independently of NemoWM. Live-ISO selections last for the session only. Existing v12 solid-color preferences are accepted unchanged.

Changed allocation lifetimes: terminal scrollback/capture state is allocated per open Terminal and released on close; SVG raster scratch is allocated only during a render; a single scaled wallpaper buffer is sized for the current desktop and returned when no image is displayed. The kernel and compressed built-in assets remain resident. This patch does not implement disk paging or remove desktop applications.

Measured using the v12 RAM-test protocol (configured guest RAM minus allocator-free RAM after 10 seconds, no apps open): BIOS/1024x768 NemoWM in a 128 MiB VM was 33.67 MiB versus 36.05 MiB in v12. UEFI/1920x1080 JawsWM on solid slate in a 192 MiB VM was 44.44 MiB versus 46.82 MiB. A full-screen 1080p wallpaper costs 8,100 KiB extra while displayed; the guest memstat test recovered all 8,100 KiB after choosing a solid.

Reproduce with `make -j2 all test-desktop-memory`, `MEM_STEADY=1 python3 tests/qemu_memory.py --bios-ram 128`, `python3 tests/qemu_wmprefer.py --efi-ram 192`, and `python3 tests/qemu_warm_memory.py --efi-ram 192`. The latter checks actual SVG color pixels in two browser sessions, then measures the wallpaper cache release. The install test verifies every byte written and boots disk-only at 48 MiB BIOS.
