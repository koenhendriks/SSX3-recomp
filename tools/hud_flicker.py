#!/usr/bin/env python3
"""Count HUD layout flicker in a log made with RECOMP_HUD_TRACE=2.

A frame is "mixed" when some of its 2D pieces were kept in the middle for a
menu and others were moved to the screen edges in the same frame: the
picture is half one layout, half the other. A "flip" is a frame whose layout
(any piece moved or not) differs from the frames on both sides of it; frames
with no 2D pieces have no layout and are skipped.
Usage: python tools/hud_flicker.py stderr.txt [from_s] [to_s]
"""
import re, sys

def frames(path, t0, t1):
    cur = None
    for line in open(path, errors='replace'):
        m = re.search(r'\+\s*([\d.]+) \w+\s+\[HUD\] (.*)', line)
        if not m:
            continue
        t, body = float(m.group(1)), m.group(2)
        if body.startswith('frame at'):
            if cur and t0 <= cur['t'] <= t1:
                yield cur
            cur = {'t': t, 'moved': 0, 'menu': 0, 'n': 0}
        elif cur and body.startswith('#'):
            cur['n'] += 1
            mm = re.search(r'shift ([-+\d.]+) (.*)', body)
            if abs(float(mm.group(1))) > 1e-6:
                cur['moved'] += 1
            if mm.group(2).startswith('menu up'):
                cur['menu'] += 1

def main():
    path = sys.argv[1]
    t0 = float(sys.argv[2]) if len(sys.argv) > 2 else 0.0
    t1 = float(sys.argv[3]) if len(sys.argv) > 3 else 1e9
    fs = [f for f in frames(path, t0, t1) if f['n']]   # frames with no 2D have no layout
    mixed = [f for f in fs if f['moved'] and f['menu']]
    lay = [f['moved'] > 0 for f in fs]
    flips = sum(1 for i in range(1, len(lay) - 1) if lay[i] != lay[i - 1] and lay[i] != lay[i + 1])
    print(f'frames {len(fs)}, mixed {len(mixed)}, one-frame flips {flips}')
    for f in mixed[:5]:
        print(f"  mixed at {f['t']:.2f}s: {f['moved']} moved, {f['menu']} kept for a menu")
    return 1 if mixed or flips else 0

if __name__ == '__main__':
    sys.exit(main())
