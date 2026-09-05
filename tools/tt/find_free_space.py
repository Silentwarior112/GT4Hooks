"""
find_free_space.py - Find a region of a Tourist Trophy executable that is safe
to link injected plugin code into.

Why this is not the same problem as in GT4
------------------------------------------
GT4Hooks links its code at 0x68A480: the static globals of the Omron wnn/fep
Japanese IME, which GT4 Online links in but never calls. Tourist Trophy does not
ship that code at all (InitWnn / InitFep / LoadFep are absent from all three
retail builds), so an equivalent region has to be found from scratch.

Constraints a candidate must satisfy
------------------------------------
1. Below the generic pool's base address. Before main(), the game gives the pool
   everything from a base pointer up to the top of RAM and zero-fills it. Code
   placed inside that range can be handed out to the game as an allocation.
      US 0x7A69CC   EU 0x7AA7FC   JP 0x79831C
2. Above the end of .text, so patching it cannot corrupt code.
3. Nothing may reference any address inside the span:
     - no lui/addiu or lui/lw-style materialised address,
     - no 32-bit pointer word anywhere in the image.
4. Big enough for the plugin, with headroom.

The scan below reports every qualifying gap, largest first.

Usage:  python tools/find_free_space.py [build] [--min 0x4000]
"""

import argparse
import os
import struct
import sys
from collections import defaultdict

# ttanalyze lives one level up, in tools/
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from ttanalyze import Image, disasm_all, _md  # noqa: E402
from capstone.mips import MIPS_OP_IMM, MIPS_OP_MEM  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))    # tools/tt
TOOLS = os.path.dirname(HERE)                        # tools
ROOT = os.path.dirname(TOOLS)                        # the repo
IMAGES = os.path.dirname(ROOT)        # game images live beside the repo

BUILDS = {
    "SCUS-97502": {"elf": "TTexe_bin/extracted/SCUS-97502.elf", "pool": 0x7A69CC},
    "SCES-53372": {"elf": "TTexe_bin/extracted/SCES-53372.elf", "pool": 0x7AA7FC},
    "SCPS-15105": {"elf": "TTexe_bin/extracted/SCPS-15105.elf", "pool": 0x79831C},
}


def referenced_addresses(img):
    """
    Every address the image can plausibly touch:
      - the target of any lui/addiu or lui/<mem op> pair in .text
      - every 32-bit word in the image that looks like an in-image address
    Returned as a sorted list.
    """
    hits = set()

    # --- instruction-materialised addresses ---
    hi = {}
    for insn in disasm_all(img):
        m = insn.mnemonic
        ops = insn.operands
        if m == "lui" and len(ops) == 2 and ops[1].type == MIPS_OP_IMM:
            hi[ops[0].reg] = (ops[1].imm & 0xFFFF) << 16
            continue
        if m in ("addiu", "ori") and len(ops) == 3 and ops[2].type == MIPS_OP_IMM:
            base = hi.get(ops[1].reg)
            if base is not None:
                imm = ops[2].imm if m == "addiu" else (ops[2].imm & 0xFFFF)
                hits.add((base + imm) & 0xFFFFFFFF)
            continue
        if len(ops) >= 2 and ops[-1].type == MIPS_OP_MEM:
            mem = ops[-1].mem
            base = hi.get(mem.base)
            if base is not None:
                hits.add((base + mem.disp) & 0xFFFFFFFF)

    # --- raw pointer words ---
    lo_img = min(b for b, _ in img.segments)
    hi_img = max(b + len(d) for b, d in img.segments)
    for base, blob in img.segments:
        n = len(blob) // 4
        for w in struct.unpack_from("<%dI" % n, blob, 0):
            if lo_img <= w < hi_img:
                hits.add(w)

    return sorted(hits)


def find_gaps(refs, lo, hi, guard=0x400):
    """Contiguous [start,end) ranges in [lo,hi) with no referenced address,
    shrunk by `guard` at each end so a near-miss reference cannot reach in."""
    gaps = []
    prev = lo
    for a in refs:
        if a < lo:
            continue
        if a >= hi:
            break
        if a - prev > 2 * guard:
            gaps.append((prev + guard, a - guard))
        prev = max(prev, a + 4)
    if hi - prev > 2 * guard:
        gaps.append((prev + guard, hi - guard))
    return gaps


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("build", nargs="?", default=None)
    ap.add_argument("--min", default="0x4000")
    ap.add_argument("--top", default=None, help="override the upper bound")
    args = ap.parse_args()
    minsz = int(args.min, 0)

    builds = {args.build: BUILDS[args.build]} if args.build else BUILDS

    for tag, info in builds.items():
        img = Image(os.path.join(ROOT, info["elf"]))
        text_base, text_blob = img.text
        text_end = text_base + len(text_blob)
        pool = info["pool"]
        top = int(args.top, 0) if args.top else pool

        print("=" * 72)
        print("%s   text 0x%08X..0x%08X   pool base 0x%08X" % (tag, text_base, text_end, pool))
        print("  scanning 0x%08X..0x%08X for unreferenced gaps >= 0x%X" % (text_end, top, minsz))

        refs = referenced_addresses(img)
        print("  %d distinct referenced addresses in image" % len(refs))

        gaps = [(a, b) for a, b in find_gaps(refs, text_end, top) if b - a >= minsz]
        gaps.sort(key=lambda g: g[1] - g[0], reverse=True)

        if not gaps:
            print("  no qualifying gap found")
            print()
            continue

        # is the gap file-backed (initialised data) or .bss?
        def backing(a):
            for base, blob in img.segments:
                if base <= a < base + len(blob):
                    return "file-backed data"
            return ".bss (zero-filled)"

        print("  %-11s %-11s %-10s %-18s" % ("start", "end", "size", "backing"))
        for a, b in gaps[:12]:
            print("  0x%08X  0x%08X  0x%-8X %s" % (a, b, b - a, backing(a)))
        print()


if __name__ == "__main__":
    main()
