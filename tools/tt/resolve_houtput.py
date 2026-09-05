"""
resolve_houtput.py - Resolve the HOutput addresses for the Tourist Trophy
builds from the known Gran Turismo 4 Online ones.

Why this needs its own resolver
-------------------------------
The three HOutput addresses cannot be found the way the file-device ones were:

  ADDR_HOutput_Handler is a global function POINTER, not a function. In a
  release build it holds a one-instruction no-op stub (`jr $ra`), which is the
  whole reason the hook exists. A one-instruction fingerprint matches thousands
  of functions, so the pointer has to be reached indirectly: find the function
  that materialises it - a distinctive 46-instruction routine in the HOutput
  implementation - and read the constant back out of the matched copy.

  ADDR_ADHOC_printf and ADDR_ADHOC_debug are two functions with IDENTICAL
  instruction sequences, so a fingerprint cannot tell them apart; both match the
  same pair of candidates. They are separated instead by the fact that the pair
  sits exactly 0x6B0 apart in every build, printf first.

Both results are checked rather than assumed: the anchor must match closely, it
must materialise exactly one constant, and the slot that constant names must
actually hold a `jr $ra` stub.

Usage:  python tools/resolve_houtput.py [--json out.json]
"""

import argparse
import json
import os
import pickle
import sys

# ttanalyze lives one level up, in tools/
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from ttanalyze import (  # noqa: E402
    Image, normalize, similarity, addr_consts_in, disasm,
)

HERE = os.path.dirname(os.path.abspath(__file__))    # tools/tt
TOOLS = os.path.dirname(HERE)                        # tools
ROOT = os.path.dirname(TOOLS)                        # the repo
IMAGES = os.path.dirname(ROOT)        # game images live beside the repo
CACHE = os.environ.get("GT4HOOKS_WORK") or os.path.join(IMAGES, "GT4Hooks-work")
CACHE = os.path.join(CACHE, "cache")

GT4O_ELF = os.path.join(IMAGES, "GT4exe_bin/extracted/SCUS-97436.elf")
TT_ELFS = {
    "SCUS-97502": os.path.join(IMAGES, "TTexe_bin/extracted/SCUS-97502.elf"),
    "SCES-53372": os.path.join(IMAGES, "TTexe_bin/extracted/SCES-53372.elf"),
    "SCPS-15105": os.path.join(IMAGES, "TTexe_bin/extracted/SCPS-15105.elf"),
}

# Gran Turismo 4 Online reference points.
GT4O_ANCHOR = 0x4F4FF4        # materialises the handler slot exactly once
GT4O_SLOT = 0x67A784          # ADDR_HOutput_Handler
GT4O_ADHOC_PRINTF = 0x6259D8
GT4O_ADHOC_DEBUG = 0x626088
PAIR_DELTA = GT4O_ADHOC_DEBUG - GT4O_ADHOC_PRINTF   # 0x6B0

MIN_SIMILARITY = 0.95


def is_stub(img, addr):
    """A release-build HOutput handler is `jr $ra` followed by a delay slot."""
    ins = disasm(img, addr, 2)
    return len(ins) >= 1 and ins[0].mnemonic == "jr" and ins[0].op_str.strip() == "$ra"


def resolve(gt4, tt, idx):
    out = {}

    # ---- the handler slot, via the function that materialises it ----
    gfp = normalize(gt4, GT4O_ANCHOR)
    best = []
    for fp, addrs in idx.items():
        if abs(len(fp) - len(gfp)) > 6:
            continue
        s = similarity(gfp, fp)
        if s >= MIN_SIMILARITY:
            best.append((s, addrs[0], len(fp)))
    best.sort(reverse=True)

    if not best:
        out["_error"] = "no match for the HOutput anchor function"
        return out
    if len(best) > 1 and best[0][0] - best[1][0] < 0.02:
        out["_error"] = "ambiguous anchor: %s" % [("0x%X" % a, round(s, 3)) for s, a, _ in best[:3]]
        return out

    score, anchor, _n = best[0]
    consts = [c for c, _ in addr_consts_in(tt, anchor)]
    if len(consts) != 1:
        out["_error"] = "anchor 0x%X materialises %d constants, expected 1" % (anchor, len(consts))
        return out

    slot = consts[0]
    stub = tt.u32(slot)
    if stub is None or not is_stub(tt, stub):
        out["_error"] = "slot 0x%X does not hold a jr-$ra stub (holds 0x%X)" % (slot, stub or 0)
        return out

    out["anchor"] = anchor
    out["anchor_similarity"] = round(score, 4)
    out["ADDR_HOutput_Handler"] = slot
    out["handler_stub"] = stub

    # ---- the ADHOC pair, separated by their fixed spacing ----
    cands = sorted(idx.get(normalize(gt4, GT4O_ADHOC_PRINTF), []))
    pairs = [(a, b) for a in cands for b in cands if b - a == PAIR_DELTA]
    if len(pairs) != 1:
        out["_error"] = "ADHOC pair not uniquely separated by 0x%X: %s" % (
            PAIR_DELTA, ["0x%X" % c for c in cands])
        return out

    out["ADDR_ADHOC_printf"], out["ADDR_ADHOC_debug"] = pairs[0]
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--json", help="write results to this JSON file")
    args = ap.parse_args()

    gt4 = Image(GT4O_ELF)
    print("Gran Turismo 4 Online reference:")
    print("  anchor        0x%06X  (%d instructions)" % (GT4O_ANCHOR, len(normalize(gt4, GT4O_ANCHOR))))
    print("  handler slot  0x%06X  -> stub 0x%06X" % (GT4O_SLOT, gt4.u32(GT4O_SLOT)))
    print("  ADHOC printf  0x%06X / debug 0x%06X  (0x%X apart)\n"
          % (GT4O_ADHOC_PRINTF, GT4O_ADHOC_DEBUG, PAIR_DELTA))

    results = {}
    ok = True
    for tag, path in TT_ELFS.items():
        cache = os.path.join(CACHE, "_fpx_%s.pkl" % tag)
        if not os.path.exists(cache):
            print("%s: no fingerprint cache at %s - run resolve_tt.py first" % (tag, cache))
            ok = False
            continue
        with open(cache, "rb") as f:
            idx = pickle.load(f)
        r = resolve(gt4, Image(path), idx)
        results[tag] = r

        print("%s" % tag)
        if "_error" in r:
            print("   FAILED: %s" % r["_error"])
            ok = False
            continue
        print("   anchor                0x%06X  (similarity %.3f)" % (r["anchor"], r["anchor_similarity"]))
        print("   ADDR_HOutput_Handler  0x%06X  -> stub 0x%06X (jr $ra confirmed)"
              % (r["ADDR_HOutput_Handler"], r["handler_stub"]))
        print("   ADDR_ADHOC_printf     0x%06X" % r["ADDR_ADHOC_printf"])
        print("   ADDR_ADHOC_debug      0x%06X  (0x%X after printf)"
              % (r["ADDR_ADHOC_debug"], r["ADDR_ADHOC_debug"] - r["ADDR_ADHOC_printf"]))

    if args.json and ok:
        with open(args.json, "w") as f:
            json.dump(results, f, indent=2)
        print("\nwrote %s" % args.json)

    print("\n%s" % ("all builds resolved" if ok else "SOME BUILDS FAILED"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
