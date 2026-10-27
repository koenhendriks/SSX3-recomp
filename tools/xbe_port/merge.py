"""Collect checked mappings into a port file (analysis/ports/<variant>.json).

    python3 merge.py XBE VARIANT OUT.json CHECKED.json [CHECKED.json ...]

CHECKED files are lists of {"pal", "ntsc", "confidence", "evidence"}. Only
"high" and "medium" rows go into the map. "low" and "none" rows are listed
for a decision by hand. Two rows that map one PAL address to different
addresses stop the merge. The port file holds addresses and the .text hash
only: nothing copied from the game.
"""
import json
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).parent))
from port import text_sha256  # noqa: E402

KEEP = ("high", "medium")


def merge(rows):
    """Returns (map {PAL: build}, rows left for a decision). Raises on a conflict."""
    out, left = {}, []
    for r in rows:
        if r.get("confidence") not in KEEP or not r.get("ntsc"):
            left.append(r)
            continue
        pal, new = f"0x{int(r['pal'], 16):08X}", f"0x{int(r['ntsc'], 16):08X}"
        if out.get(pal, new) != new:
            raise SystemExit(f"conflict for {pal}: {out[pal]} and {new}")
        out[pal] = new
    return dict(sorted(out.items(), key=lambda kv: int(kv[0], 16))), left


def main(xbe, variant, out_path, *checked):
    rows = [r for f in checked for r in json.loads(pathlib.Path(f).read_text())]
    m, left = merge(rows)
    port = {"variant": variant,
            "text_sha256": text_sha256(pathlib.Path(xbe).read_bytes()),
            "map": m}
    pathlib.Path(out_path).write_text(json.dumps(port, indent=1) + "\n")
    print(f"{len(m)} addresses mapped into {out_path}")
    for r in left:
        print(f"  decide: {r['pal']} -> {r.get('ntsc')} ({r.get('confidence')}): {r.get('evidence', '')[:120]}")


if __name__ == "__main__":
    main(*sys.argv[1:])
