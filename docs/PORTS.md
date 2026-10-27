# Other builds of the game (ports)

The hand-written code (`src/`) uses addresses from the PAL XBE. Other discs of SSX 3 share its title ID
(`4541003C`), XDK (5558) and build date (2003-09-19), but not its layout: the NTSC-U code is not PAL shifted by
a few bytes. Whole blocks are linked elsewhere (the app loop +0x57E30, the world streamer -0x688B0), some
neighbouring functions swap order, and the data sections drift by 20 KB to 230 KB. A port file maps every PAL
address the code uses to another build's.

| Build | Port file | Status |
|---|---|---|
| PAL | none (the source is PAL) | everything in the README |
| NTSC-U | `analysis/ports/ntsc.json` | boots, menus, races and Conquer the Mountain on Linux (2026-10-25). Not tested on Windows. |

The release zip is built from the PAL disc, so it plays PAL only. For NTSC-U, build from source.

## How it plugs in

```text
scripts/regen.sh | regen.ps1
  tools/xbe_port/port.py XBE           # sha256 of .text -> analysis/ports/*.json, or none = PAL
    work/port/port_map.h              # -> src/recomp/gen/port_map.h after the lift
    work/port/icall_seeds.json        # the seeds at this build's addresses
    work/port/ssx3_overrides.c        # sub_ names renamed, for --exclude-manual
  tools.disasm / split / recomp       # use the ported seeds and override names
  merge_names.py                      # PAL only: the Ghidra names are at PAL addresses
CMake
  -include port_map.h                 # hand-written sources and the launchers, never the generated code
```

`port_map.h` holds:

- `#define sub_<PAL> sub_<build>` and the `_gen` twin, so an override written as `sub_0009C7A0` defines and calls
  the NTSC function `sub_0009C760`.
- `#define VA_0x<PAL> 0x<build>u` for every `VA(0x...)` in the code. Write any game address in `src/` as
  `VA(0x00369534)`: global, vtable, return address, entry point. In a PAL build it is the same number.
- `SSX3_CODE_END`, `SSX3_VTABLE_LO`, `SSX3_VTABLE_HI`, `SSX3_OVERLAY_PAGE`: bounds for the "points into code",
  "looks like a vtable" and "an in-game overlay screen" tests. These are not addresses of anything, so a port gives
  them by name.
- `SSX3_BUILD_VARIANT` and `SSX3_BUILD_NAME`. The launcher compares the XBE's entry point with `VA(0x00181AD2)` and
  refuses a folder that is another build than the one it was made for; `main.c` makes the same check at start, for
  runs without the launcher (`tools/play.sh`, the test scripts).

`port.py` stops the regen when the code uses an address the port does not map. An address only in a comment is not
required; comments keep their PAL addresses.

A port's `absent` list holds things the build does not have (the NTSC-U frontend has no "message box" or "dialog"
screen vtable at a findable place). Each gets a value above guest memory, distinct per address, so nothing compares
equal to it and `case` labels stay unique.

## Porting another build

There is no need for the PAL XBE. Everything known about PAL is in this repo.

1. Disassemble the new XBE **without** the PAL icall seeds (they plant wrong function starts):
   `python -m tools.disasm XBE --text-only --extra-sections D3D,D3DX,XGRPH,DSOUND,XPP` in the toolkit, then copy
   `tools/disasm/output/functions.json` aside.
2. `tools/xbe_port/inventory.py REPO OUT` lists every PAL address in `src/`, the toolkit patch and the seeds, with
   each use. It matches `0x00289618u` too: a `\b0x...\b` regex misses a `u` suffix.
3. `tools/xbe_port/align.py` suggests addresses by matching the spacing of known PAL function starts against the new
   build's starts (fewest drift changes). Trust only long exact runs (the CRT: 138 functions at one drift). Isolated
   suggestions are coincidences: functions are dense and 16-byte aligned.
4. `tools/xbe_port/worksheet.py` joins the two. Check every needed row by content against the new build:
   `tools/xbe_port/dis.py XBE ADDR --func functions.json` disassembles with call targets named. What the source says
   about the function is the evidence: argument count, `ret N`, struct offsets, a quoted instruction, calls to
   functions already mapped. Map globals through the instructions that use them; data drift is not constant.
   The five XDK library sections share one drift (NTSC-U: -0x53E0).
5. Also check string literals: the intro skip builds a poke as text.
6. `tools/xbe_port/merge.py XBE VARIANT OUT CHECKED...` writes the port file from checked rows (`high` and `medium`
   only; it stops on a conflict). Add `name`, `bounds` and `absent` by hand.
7. `scripts/regen.sh`, build, and run `RECOMP_AUTONAV=race` (a race) and `RECOMP_AUTONAV=race,main=2`
   (Conquer the Mountain).

`python3 tools/xbe_port/test_port.py` tests the header and translation step.

A checkout from before ports has no `port_map.h`: CMake stops and asks for a regen (`scripts/regen.sh lift` is
enough). An XBE whose `.text` matches no port is built as PAL. Adding `analysis/ports/pal.json` with only
`{"variant": "pal", "text_sha256": "<PAL .text sha256>"}` turns an unknown build into a regen error instead.

## NTSC-U port

200 addresses: 186 checked by content with two independent matches (`high`), 14 with one (`medium`), plus the 3
bounds and 2 absent vtables. The medium ones, if something looks off:

| PAL | NTSC-U | What |
|---|---|---|
| `0x000D7460` | `0x000D83F0` | cGameLoadScreen render (entry 6 of its vtable) |
| `0x0016B4F0` | `0x0016B440` | LunoVMRegister array allocation (a twin at 0x16A930) |
| `0x00173B36` | `0x00173A86` | return address after the stream walk call |
| `0x00177E10` | `0x00177D60` | stream poll |
| `0x001ADF60` | `0x00205D90` | frame timer wait (reached only by a tail jump) |
| `0x0020C4A0`, `0x0020C520` | `0x001A3C90`, `0x001A3D10` | streamer pass entries, placed by neighbours' drift |
| `0x00297B60` | `0x00290D88` | quit dialog vtable (one of 3 yes/no variants) |
| `0x002981D8` | `0x00291400` | pause menu vtable (one of 6 pause variants) |
| `0x00298448`, `0x002984B0` | `0x00291670`, `0x002916D8` | race end / run result vtables |
| `0x00346440` | `0x003176E0` | event id (0x40 above the profile table, as in PAL) |
| `0x00369458` | `0x0033A578` | results place array: NTSC-U keeps it below the score array, not 0x50 after it |

Not ported: `scripts/race_loop.sh` and the Windows test scripts in `tools/` still hold PAL vtables and a PAL poke
address. Use `RECOMP_AUTONAV` on another build.
