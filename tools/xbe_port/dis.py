"""Disassemble an XBE at a virtual address, for checking a ported address by eye.

    python dis.py XBE ADDR [COUNT]              # COUNT instructions (default 40)
    python dis.py XBE ADDR --func FUNCTIONS     # the whole detected function

Needs capstone (the toolkit's .venv has it). Calls and jumps to a detected
function start are shown with that function's name.
"""
import argparse
import json
import struct

import capstone


def sections(d):
    base = struct.unpack_from("<I", d, 0x104)[0]
    n, at = struct.unpack_from("<II", d, 0x11C)
    out = []
    for i in range(n):
        _, va, vs, raw, rs, _ = struct.unpack_from("<IIIIII", d, at - base + i * 56)
        out.append((va, raw, rs))
    return out


def read(d, secs, va, size):
    for sva, raw, rs in secs:
        if sva <= va < sva + rs:
            n = min(size, sva + rs - va)
            return d[raw + va - sva: raw + va - sva + n]
    raise SystemExit(f"0x{va:08X} is not in a section with file data")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("xbe")
    ap.add_argument("addr", type=lambda s: int(s, 16))
    ap.add_argument("count", nargs="?", type=int, default=40)
    ap.add_argument("--func", metavar="FUNCTIONS_JSON")
    a = ap.parse_args()
    d = open(a.xbe, "rb").read()
    secs = sections(d)
    names, end = {}, None
    if a.func:
        for f in json.load(open(a.func)):
            s = int(f["start"], 16)
            names[s] = f.get("name", f"sub_{s:08X}")
            if s == a.addr:
                end = int(f["end"], 16)
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    code = read(d, secs, a.addr, (end - a.addr) if end else a.count * 15)
    for i, ins in enumerate(md.disasm(code, a.addr)):
        if end is None and i >= a.count:
            break
        note = ""
        if ins.mnemonic in ("call", "jmp") or ins.mnemonic.startswith("j"):
            try:
                t = int(ins.op_str, 16)
                note = f"  ; {names[t]}" if t in names else ""
            except ValueError:
                pass
        print(f"{ins.address:08X}  {ins.bytes.hex():<20} {ins.mnemonic} {ins.op_str}{note}")


if __name__ == "__main__":
    main()
