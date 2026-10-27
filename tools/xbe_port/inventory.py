"""Every build-specific address the project uses, with where and how.

    python3 inventory.py REPO OUT.json

Scans the hand-written C (src/), the added lines of patches/xboxrecomp-local.patch
and analysis/icall_seeds.json. Each row: addr, file, line, kind ("sub" for a
sub_XXXXXXXX name, "hex" for a hex literal, with or without a u/l suffix, "seed") and the source line.
Literals outside the XBE's image (below 0x10000 or past 0x400000) are skipped:
those are sizes, masks and device registers, not game addresses.
"""
import json
import pathlib
import re
import sys

IMAGE = (0x10000, 0x400000)
SUB = re.compile(r"\bsub_([0-9A-F]{8})(?:_gen)?\b")
HEX = re.compile(r"\b0x([0-9A-Fa-f]{5,8})[uUlL]*\b")   # 0x1781B0, 0x00280000u, ...
HAND = ["src/*.c", "src/*.h", "src/vk/*.c", "src/web/*.c", "src/launcher/*.c"]


def scan_line(rows, path, n, line):
    for m in SUB.finditer(line):
        rows.append({"addr": int(m.group(1), 16), "file": path, "line": n, "kind": "sub", "text": line.strip()})
    for m in HEX.finditer(line):
        a = int(m.group(1), 16)
        if IMAGE[0] <= a < IMAGE[1]:
            rows.append({"addr": a, "file": path, "line": n, "kind": "hex", "text": line.strip()})


def inventory(repo):
    repo = pathlib.Path(repo)
    rows = []
    for pat in HAND:
        for f in sorted(repo.glob(pat)):
            for n, line in enumerate(f.read_text(errors="replace").splitlines(), 1):
                scan_line(rows, str(f.relative_to(repo)), n, line)
    cur = None
    for n, line in enumerate((repo / "patches/xboxrecomp-local.patch").read_text(errors="replace").splitlines(), 1):
        if line.startswith("+++ b/"):
            cur = "xboxrecomp/" + line[6:]
        elif line.startswith("+") and not line.startswith("+++"):
            scan_line(rows, f"{cur} (patch line {n})", n, line[1:])
    for s in json.load(open(repo / "analysis/icall_seeds.json")):
        rows.append({"addr": int(s["start"], 16), "file": "analysis/icall_seeds.json", "line": 0,
                     "kind": "seed", "text": s.get("note", "")})
    return rows


if __name__ == "__main__":
    rows = inventory(sys.argv[1])
    for r in rows:
        r["addr"] = f"0x{r['addr']:08X}"
    json.dump(rows, open(sys.argv[2], "w"), indent=1)
    print(len(rows), "uses of", len({r["addr"] for r in rows}), "addresses")
