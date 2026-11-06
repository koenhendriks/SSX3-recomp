# macOS (Apple silicon)

A native macOS arm64 build: the game's code runs as arm64, the GPU renderer is the
Vulkan one on [MoltenVK](https://github.com/KhronosGroup/MoltenVK) (Metal), audio is
CoreAudio through SDL. Tested on an M4 Pro, macOS 27: boot, menus, launcher, a full
Snow Jam race at 60 FPS (2583x1440, MSAA 4x).

## Play

1. Download `SSX3-recomp-macos-arm64.dmg` from the [releases](https://github.com/Zangetsu-Sama/SSX3-recomp/releases), open it and drag `SSX3.app`
   onto Applications (a `.zip` of the same app is there too).
2. The app is ad-hoc signed, not notarised. First start: right-click (Control-click)
   `SSX3.app` > Open > Open. On macOS 15 and newer, if that is refused: System Settings >
   Privacy & Security > "Open Anyway". Or in Terminal: `xattr -dr com.apple.quarantine SSX3.app`.
3. In the launcher press **Browse**, pick your SSX 3 (PAL) rip (`default.xbe` + `data/`),
   choose the settings, **Play**.

Keyboard = player 1 (`keys.ini`, example in `SSX3.app/Contents/Resources`). Xbox One /
Series and other controllers SDL knows work over Bluetooth or USB. Requires macOS 14+.

## Build (from macOS, nothing installed on the Mac; Docker Desktop needed)

```bash
# <parent>/ssx3-recomp (this repo), <parent>/SSX3/default.xbe (your rip)
scripts/setup.sh                                   # toolkit + patch (needs Python 3 + venv)
macos/setup.sh                                     # SDK copy, Docker image, deps (once)
docker run --rm -v "$(dirname "$PWD")":/work ssx3-macx bash -c \
  'python3 -m venv /work/xboxrecomp/.venv && /work/xboxrecomp/.venv/bin/pip -q install capstone==5.0.9 \
   && cd /work/ssx3-recomp && scripts/regen.sh'    # lift the game (Linux venv in the container)
docker run --rm -v "$(dirname "$PWD")":/work ssx3-macx /work/ssx3-recomp/macos/build.sh
macos/package.sh                                   # <parent>/dist-macos/SSX3.app, .dmg, .zip
                                                   # (git lfs pull first for the HD textures;
                                                   #  <parent>/icon.png becomes the app icon)
```

Cross-compiled with clang/ld64.lld 18 against a MacOSX 26.x SDK (the 27.x SDK's
`arm64e.x1` .tbd entries are not readable by lld 18). Deps (macos/deps.sh): SDL 2.32
and SDL2_ttf 2.24 (+FreeType) static, libepoxy static, MoltenVK 1.4.2 bundled.

## What is different on macOS

- **Window on the main thread.** Cocoa only creates windows there, so the game runs on a
  second thread (256 MB stack) and the main thread runs the SDL window loop
  (`xbox_FramebufferMainThreadRun`, toolkit patch).
- **Fault handler.** Darwin's `ucontext` layout (`src/uc_regs.h`); protection faults can
  arrive as SIGBUS.
- **`isatty`.** Ghidra named a CRT function of the game `isatty`; macOS binds the
  executable's own definition first, so the lifted one is renamed for the generated sources.
- **No hardware TSO.** Asahi Linux turns on the cores' x86 memory ordering with `prctl`;
  macOS offers native apps no such switch. The guest lock (one guest thread at a time)
  covers most of it; a rare ordering bug cannot be ruled out.
- Thread dumps use Mach threads; the launcher uses the system fonts (no fontconfig), the
  Finder folder dialog and `open`.

HD textures ship in `SSX3.app/Contents/Resources/hd-textures` (the game looks there).
Not done yet: Intel Macs, notarisation.
