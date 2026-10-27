"""One row per build-specific address, for checking the port by hand.

    python3 worksheet.py INVENTORY.json ALIGN.json OUT.json

Joins the inventory (every use, comments included: they often say what the
code at the address does) with the aligner's suggestion and its run length.
"needed" marks addresses the build depends on: sub_ names and literals in code,
and icall seeds. Comment-only addresses help as evidence but need no port.
"""
import collections
import json
import re
import sys

COMMENT = re.compile(r"^(\*|/\*|//)")


def main(inv_path, align_path, out_path):
    inv = json.load(open(inv_path))
    align = {r["old"]: r for r in json.load(open(align_path))}
    by = collections.defaultdict(list)
    for r in inv:
        by[r["addr"]].append(r)
    rows = []
    for addr in sorted(by, key=lambda a: int(a, 16)):
        uses = by[addr]
        needed = any(u["kind"] in ("sub", "seed") or
                     (not COMMENT.match(u["text"]) and "XBOX_PART" not in u["text"]) for u in uses)
        a = align.get(addr, {})
        rows.append({"pal": addr, "needed": needed,
                     "suggested": a.get("new"), "drift": a.get("drift"), "run_len": a.get("run_len", 0),
                     "uses": [f"{u['file']}:{u['line']} | {u['text'][:160]}" for u in uses]})
    json.dump(rows, open(out_path, "w"), indent=1)
    print(len(rows), "addresses,", sum(r["needed"] for r in rows), "needed")


if __name__ == "__main__":
    main(*sys.argv[1:4])
