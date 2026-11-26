# Doom Port

SharkOS now builds the DoomGeneric engine into the 64-bit kernel and embeds Freedoom Phase 1 as its IWAD. This is the actual Doom engine and game data, not the former raycaster demo. No proprietary id Software IWAD is included.

The desktop Doom app and the existing `doom` shell command both call the same engine wrapper. The current port renders at 320 × 200 internally and scales the indexed Doom framebuffer into the active SharkOS display. Sound is disabled in the launch arguments; keyboard events are passed into DoomGeneric.

The controls support the Doom defaults and a modern layout: arrow keys turn/move, WASD moves and strafes, mouse movement turns, left mouse fires, Ctrl fires, Space uses, Shift runs, and number keys select weapons. The renderer scales the 320 × 200 engine image to a centered 4:3 viewport using nearest-neighbor sampling.

The kernel build embeds `assets/freedoom1.wad` through `src/doom/freedoom_wad.s`. The data copyright and licensing notes are in `docs/FREEDOOM-COPYRIGHT`. DoomGeneric's upstream GPL-2 license is in `src/doom/engine/LICENSE`. Review both before redistributing a built image.

Validation completed: the 64-bit kernel links successfully with the engine and embedded IWAD. A native smoke test loaded Freedoom Phase 1, initialized the Doom renderer, advanced 1,000 engine ticks into the attract/demo sequence, and produced a non-uniform 320 × 200 frame that differed from the initial title frame. Full SharkOS boot and in-kernel controls remain untested because this workspace has neither QEMU nor `xorriso`; ISO creation stopped when `xorriso` was not found.
