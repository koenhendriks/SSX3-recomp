"""Map one build's code addresses onto another build of the same game.

The other build's XBE is not needed. Two builds of one game are laid out by the
linker in the same order; only functions whose source differs change size, so
the drift (new - old) is constant over long runs. Given the old build's known
function starts (from the project's own code and analysis files) and the new
build's detected function starts, a Viterbi pass picks for each old address a
new function start, minimising the number of drift changes.

    python3 align.py OLD_ADDRS.json NEW_FUNCTIONS.json OUT.json [--window N]

OLD_ADDRS.json: [{"addr": "0x...", "src": "..."}...]; NEW_FUNCTIONS.json is
tools.disasm's functions.json for the new build. OUT.json lists each address
with its drift and the run it belongs to (run_len = anchors sharing the drift).
"""
import argparse
import bisect
import json

CHANGE = 1.0           # cost of a drift change between neighbouring anchors...
PER_BYTE = 1 / 256     # ...plus this per byte of change, capped at 1: prefer small
                       # changes without making one real jump cost more than many fake steps
MISS = 1.5             # cost of an anchor with no start under the current drift


def align(old, starts, window):
    """old: sorted ints; starts: sorted ints. Returns [(addr, drift or None)]."""
    cands = []
    for a in old:
        lo = bisect.bisect_left(starts, a - window)
        hi = bisect.bisect_right(starts, a + window)
        cands.append([s - a for s in starts[lo:hi]])
    # Viterbi over drift values; None = "no match here" keeps the previous drift.
    prev = {}
    back = []
    for i, ds in enumerate(cands):
        cur, bp = {}, {}
        gmin = min(prev, key=prev.get) if prev else None
        for d in ds:
            if not prev:
                cur[d], bp[d] = 0.0, None
                continue
            # Exact search would be O(states^2); the same drift, the cheapest
            # state and the nearest few drifts cover the cases that win.
            near = sorted(prev, key=lambda p: abs(p - d))[:8]
            opts = [(prev[p] + (0 if p == d else CHANGE + min(abs(p - d) * PER_BYTE, 1.0)), p)
                    for p in set(near) | {gmin}]
            cur[d], bp[d] = min(opts)
        # Carry each previous drift through as a miss, so one bad anchor
        # doesn't force a change.
        for p, c in prev.items():
            if p not in cur or c + MISS < cur[p]:
                cur[p], bp[p] = c + MISS, ("miss", p)
        if not cur:            # no candidates and nothing to carry: skip this anchor
            back.append({})
            continue
        best = min(cur.values())
        prev = {k: v for k, v in cur.items() if v <= best + 3}
        back.append(bp)
    d = min(prev, key=prev.get)
    out = [None] * len(old)
    for i in range(len(old) - 1, -1, -1):
        if d not in back[i]:
            continue
        b = back[i][d]
        hit = (d in cands[i]) and not (isinstance(b, tuple) and b[0] == "miss")
        out[i] = d if hit else None
        d = b[1] if isinstance(b, tuple) else (b if b is not None else d)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("old")
    ap.add_argument("new_functions")
    ap.add_argument("out")
    ap.add_argument("--window", type=lambda s: int(s, 0), default=0x1000)
    a = ap.parse_args()
    entries = json.load(open(a.old))
    by_addr = {}
    for e in entries:
        by_addr.setdefault(int(e["addr"], 16), []).append(e["src"])
    old = sorted(by_addr)
    starts = sorted(int(f["start"], 16) for f in json.load(open(a.new_functions)))
    drifts = align(old, starts, a.window)
    rows = []
    for addr, d in zip(old, drifts):
        rows.append({"old": f"0x{addr:08X}", "drift": d,
                     "new": None if d is None else f"0x{addr + d:08X}",
                     "src": sorted(set(by_addr[addr]))})
    # Run length: consecutive matched anchors with the same drift.
    i = 0
    while i < len(rows):
        j = i
        while j + 1 < len(rows) and rows[j + 1]["drift"] == rows[i]["drift"]:
            j += 1
        for k in range(i, j + 1):
            rows[k]["run_len"] = j - i + 1 if rows[i]["drift"] is not None else 0
        i = j + 1
    json.dump(rows, open(a.out, "w"), indent=1)
    matched = sum(r["drift"] is not None for r in rows)
    weak = sum(r["drift"] is not None and r["run_len"] < 3 for r in rows)
    print(f"{matched}/{len(rows)} matched, {weak} in runs shorter than 3")


if __name__ == "__main__":
    main()
