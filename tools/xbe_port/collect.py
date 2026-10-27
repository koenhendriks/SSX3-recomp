"""List the PAL code addresses this project depends on, and where each comes from.

    python3 collect.py REPO OUT.json

Sources: sub_XXXXXXXX names in the hand-written C, function names in
analysis/ghidra_names.json (string labels, s_*, are data and left out), and the
XBE entry point recorded in src/main.c.
"""
import json
import pathlib
import re
import sys

HAND = ["src/*.c", "src/*.h", "src/vk/*.c", "src/web/*.c"]


def collect(repo):
    repo = pathlib.Path(repo)
    out = []
    for pat in HAND:
        for f in sorted(repo.glob(pat)):
            for m in set(re.findall(r"\bsub_([0-9A-F]{8})", f.read_text(errors="replace"))):
                out.append({"addr": "0x" + m, "src": f"{f.relative_to(repo)}"})
    for k, name in json.load(open(repo / "analysis/ghidra_names.json")).items():
        if not name.startswith("s_"):
            out.append({"addr": k, "src": f"ghidra_names:{name}"})
    m = re.search(r"#define\s+YOUR_GAME_ENTRY_POINT\s+(0x[0-9A-Fa-f]+)", (repo / "src/main.c").read_text())
    out.append({"addr": m.group(1), "src": "entry point"})
    return out


if __name__ == "__main__":
    rows = collect(sys.argv[1])
    json.dump(rows, open(sys.argv[2], "w"), indent=1)
    print(len(rows), "references,", len({r["addr"].upper() for r in rows}), "addresses")
