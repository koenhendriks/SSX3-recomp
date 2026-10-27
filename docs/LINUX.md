# Linux build

One command does all of it, from your rip folder or disc image (it checks the
dependencies and says what to install):

```bash
scripts/linux-setup.sh ~/SSX3        # or: scripts/linux-setup.sh ssx3.iso
```

By hand:

```bash
scripts/setup.sh && scripts/regen.sh      # as on Windows (XBE in disk/SSX3 or ../SSX3)
ln -s disk/SSX3 game                      # exe opens game/default.xbe from the cwd
cmake -S . -B build-linux -G Ninja -DCMAKE_BUILD_TYPE=Release
ninja -C build-linux && tools/play.sh       # keyboard = player 1, logs in play_std*.txt
build-linux/launcher                      # or: the settings window, then Play
```

`build-linux/launcher` (src/launcher/launcher_sdl.c, SDL2 + SDL2_ttf +
fontconfig; skipped by CMake when those are missing) is the Windows launcher's
counterpart. It has the same settings, the same `launcher.ini` next to it, and
the same `RECOMP_*` hand-off. "Auto" resolution comes from the desktop the
launcher is on, "Auto" aspect ratio follows the game window, and
`RECOMP_USB_PADS` comes from the number of controllers. There are no Fullscreen, FPS counter or 5.1 rows, because the
Linux game window and audio don't support them yet. The folder picker is zenity
or kdialog, and you can also type the path. The game starts in the launcher's
folder, or its parent when `hd-textures/` is there (`build-linux/` in this
tree). Without a terminal, its output goes to `logs/` beside the game folder.

Deps: `tools/linux/install_deps.sh` in the toolkit (SDL2, libepoxy, OpenSSL),
plus the Vulkan headers and glslc for the renderer (Arch: `vulkan-headers`,
`shaderc`). Tested on aarch64 (Asahi) with GCC 16 and on x86-64 (Arch,
GCC, NVIDIA RTX 4080 SUPER).

## Status x86-64 (2026-10-20)

Boot -> menus -> race load -> briefing in ~15 s with `RECOMP_AUTONAV=race`, then
the race at a steady 60 FPS (p99 ~19 ms) on the Vulkan renderer. Three fresh
system dirs in a row booted to the frontend. Not yet checked by hand: playing
with the keyboard or a pad, audio by ear.

## Status (2026-10-14)

Boots through intro to the menus on aarch64 (Asahi, M2) with a window (SDL2,
`RECOMP_FB_WINDOW`, default in main.c). Not yet checked: playing, audio, input,
speed. Run with `timeout -s KILL` (plain `timeout` leaves the process behind).

What makes it work on POSIX:

- `src/mmio_posix.c`: SIGSEGV handler decodes the host load/store for APU / OHCI
  registers (device callbacks, `xbox_OhciMmioAccess`) and steps over it.
  aarch64: LDP/STP, SIMD and exclusives are declined. x86-64: moves,
  movzx/movsx/movsxd, ALU/test/cmp with a register or an immediate, xchg,
  inc/dec, SSE movd/movq; string instructions are declined. A declined access
  shows up as a `[CRASH]` with its instruction bytes.
- AC'97 bus-master reset (RR bit) trap: page made read-only, writes emulated in
  `main.c` (`ac97_arm`).
- Hardware TSO (`PR_SET_MEM_MODEL`, Apple cores) for the guest's x86 ordering.
- Guest memory reserved at 0x100000000000 so the 0xFD000000+ apertures fit.
- Case-insensitive path resolution (`fix_path_case`). Partition images like on
  Windows: `Partition0.img` with the table and sparse `Partition1-5.img` in the
  system dir (`~/.local/share/xboxrecomp`, or `RECOMP_SYSTEM_DIR`). The bare
  device name opens the image, which XAPI formats as the cache drive at boot.
  With a trailing separator it opens the rules-table directory (the E: mount).
  Without the images the cache format failed (partition length 0) and the title
  quit to the dashboard at boot.
- `video/fb_present_sdl.c`: framebuffer window, BMP dump, `RECOMP_SNAP_TRIGGER`.
- Thread dump (`RECOMP_THREAD_DUMP_SECS`, SIGUSR2) and `RECOMP_PEEK_EVERY_MS`.

## Not ported

- `RECOMP_WATCH` (hardware watchpoints), Toolhelp-based dumps.
- Audio: SDL2 audio queue behind the `xa2_*` interface (apu_xaudio2.c), not checked by ear. Gamepad/keyboard: untested.
- SEH (`RtlUnwind`, `RaiseException`) is a shim in `win32_compat.c`.
