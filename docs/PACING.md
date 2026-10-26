# Frame pacing and variable refresh

SSX 3 runs its game logic at 60 Hz. This is how each of those frames gets
to the display evenly, on a 60/120/240 Hz screen, on a G-Sync / FreeSync /
HDMI VRR one, and (as evenly as it can be) on 144/165 Hz without VRR.

## For players

Launcher, **Frame pacing**:

| Choice | `RECOMP_PRESENT_TIMING` | What it does |
|---|---|---|
| Auto (default) | `auto` | Locks to a 60/120/240 Hz display; presents 1/60 s apart under VRR; on a fixed 144/165 Hz display it says the rate cannot be even. |
| Variable refresh display (G-Sync/FreeSync) | `timed` | Always presents 1/60 s apart. Use it for a VRR display set to 60 Hz, which Auto cannot tell from a fixed one. |
| Legacy | `legacy` | The old presenter: each frame as soon as it is ready. |

G-Sync applies to the window in front only. The F3 counter shows
"UNEVEN AT THIS HZ" when a fixed 144/165 Hz display has been in use for 20 s.

## How it works

- **The emulated vblank follows the display** (`RECOMP_VBLANK_SYNC=display`,
  toolkit `vblank_grid.c`). A DXGI WaitForVBlank thread (`dxgi_vblank.c`)
  reports the window's display refreshes; on a fixed display at a multiple
  of 60 Hz (within 1%) the vblank grid locks onto them, otherwise (VRR,
  144/165 Hz, reports stale for 250 ms) it free-runs at 60.000 Hz.
  `RECOMP_VBLANK_SYNC=free` is the old free-running clock.
- **The game's frame timer fires on that grid.** SSX 3's one-shot frame
  timer (`sub_001AE030`) is re-armed for the grid's next edge, so a tick
  is a vblank; a late wake-up catches up rather than losing a tick.
- **Every frame carries its vblank** to the presenter (a FIFO filled by the
  game's present, `sub_001F3020`, emptied per flip; `frame_vblank.c`).
- **The presenter aims each frame** (`present_cadence.c`, ported from the
  takedawn toolkit with its tests): *lock* hands it to the refresh a set
  number after its vblank; *timed* presents at its vblank plus a lead that
  follows how late frames really arrive (90th percentile + 2 ms,
  12..33 ms); *timed-odd* is timed on a fixed non-multiple-of-60 display.
- **D3D11 flip model** (FLIP_DISCARD, 3 buffers, frame latency 1 with a
  waitable object, tearing allowed so VRR works in a window). On-glass
  times come from GetFrameStatistics and feed back into the pacer.

## Measuring it

`RECOMP_FRAME_STATS=1` adds, once a second:
- `[GLASS]` what reached the display: frames shown, on-glass interval range,
  frames over 25 ms, present spacing, vblank-to-frame lateness, the lead;
- `[PRESENT-SLOW]` where the time went for a present over 25 ms late.

`tools\pacing_matrix.ps1 -Rates 60,120,144 [-Fullscreen]` switches the
main display through those rates (`tools\display_mode.ps1`, put back
afterwards), runs a race at each and writes a table from the `[GLASS]`
lines. `tools\package.ps1 -Diag` builds a package whose `play_diag.bat`
turns these logs on for a tester.

## Results (Ryzen 9 3900X, RTX 2080 Ti, 3440x1440 144 Hz, Snow Jam race)

| Display | Strategy | On glass |
|---|---|---|
| 60 Hz fixed | lock | 16.7 ms (33.3 on a slow game frame), 38 of 40 seconds even |
| 120 Hz fixed | lock | 16.7 ms, 34 of 40 seconds even |
| 144 Hz fixed | timed-odd + warning | 13.9 / 20.8 ms (cannot be even) |
| 144 Hz G-Sync | timed | presents 16.6-16.8 ms apart |

Before, at 144 Hz: frames presented when ready, 13.9 / 20.8 ms in no fixed
pattern, and no VRR in a window. Game speed stays 1.0000 (ticks per second
/ 60); the input ring held its depth for 7.5 min on a 59.94 Hz grid.

## Zero-copy presentation (Windows)

At 4K the renderer read every frame back to the CPU, the window thread
copied it and the presenter uploaded it again: 33 MB three times a frame,
9-14 ms of upload plus 10-35 ms of frame-latency wait, and a VRR TV held
frames 25-45 ms every few seconds. Now four D3D11 textures (NT handles,
keyed mutex) are imported into Vulkan (`VK_KHR_external_memory_win32`,
`VK_KHR_win32_keyed_mutex`, matched by adapter LUID); each finished frame
is copied into the next one on the GPU and drawn from there. The F3
counter is drawn as its own small box. `RECOMP_PRESENT_SHARED=0` = the
readback path; recording (`RECOMP_RECORD`) still reads frames back.

Quiet PC, 60 s Snow Jam races, G-Sync on:

| Run | Shared | FPS avg | Race seconds with an on-glass frame > 25 ms |
|---|---|---|---|
| 1440p window x3 | on | 60.5 | 4, 0, 1 |
| 1440p window x3 | off | 60.5-60.6 | 8, 11, 11 |
| 4K fullscreen | on | 60.5 | 0 of 55 |

On an RTX 5090 + LG G4 (4K 144 Hz, HDMI VRR): a 4-minute session at 60.0
FPS, 233 of 237 seconds without an on-glass frame over 25 ms (the others
at load, race end and exit); before, a stall every 5-25 s.

Two pacing fixes came with it: lateness is measured when a frame is
published (measured at pickup, a long lead read as lateness and raised the
lead to its cap after a single hitch), and a queued frame is shown while
the newer one's own time is still ahead (`pc_pacer_skip`; showing only the
newest dropped every other frame once the lead passed a refresh).
