"""
resolve_stage2.py - Resolve the addresses that fingerprint matching alone
leaves ambiguous, using call-graph and constant-pool context.

Stage 1 (resolve_tt.py) pins down every function that has a distinctive
instruction fingerprint. What it cannot pin down are:

  * tiny leaf helpers whose fingerprint is shared by many functions
    (UnitArenaBase::allocate/free, sprintf, the closeStream helpers)
  * globals, which are not functions at all

Both fall out of context. FileDeviceRo::openStream and ::closeStream are
already matched, and because both builds come from the same source their
`jal` sequence and their lui/addiu constant sequence are positionally
identical. So call site N in GT4O corresponds to call site N in TT, and the
same holds for materialised global addresses.

Usage:  python tools/resolve_stage2.py
"""

import json
import os
import sys

# ttanalyze lives one level up, in tools/
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from ttanalyze import Image, calls_from, addr_consts_in, dis_text, normalize  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))    # tools/tt
TOOLS = os.path.dirname(HERE)                        # tools
ROOT = os.path.dirname(TOOLS)                        # the repo
IMAGES = os.path.dirname(ROOT)        # game images live beside the repo

GT4O_ELF = os.path.join(IMAGES, "GT4exe_bin/extracted/SCUS-97436.elf")
TT_ELFS = {
    "SCUS-97502": os.path.join(IMAGES, "TTexe_bin/extracted/SCUS-97502.elf"),
    "SCES-53372": os.path.join(IMAGES, "TTexe_bin/extracted/SCES-53372.elf"),
    "SCPS-15105": os.path.join(IMAGES, "TTexe_bin/extracted/SCPS-15105.elf"),
}

# Anchor functions, already resolved by stage 1 (GT4O address -> per-region TT).
ANCHORS = {
    "closeStream": 0x466040,
    "openStream": 0x4660D8,
}

# What we want out of each anchor, as (anchor, kind, index_in_gt4o_sequence).
# Indices are read off the GT4O disassembly and verified below.
WANTED = {
    # from closeStream: calls are [removeExpander, 51B478, 51B4A8, free, free]
    "PDISTD_FileDevice_removeExpander": ("closeStream", "call", 0),
    "closeStream_helper_51B478":        ("closeStream", "call", 1),
    "closeStream_helper_51B4A8":        ("closeStream", "call", 2),
    "UnitArenaBase_free":               ("closeStream", "call", 3),
    # from openStream: calls are [LockFileName, 51B370, setExpander, alloc, alloc, UnlockFileName]
    "PlaystationX_LockFileName":        ("openStream", "call", 0),
    "openStream_helper_51B370":         ("openStream", "call", 1),
    "PDISTD_FileDevice_setExpander":    ("openStream", "call", 2),
    "UnitArenaBase_allocate":           ("openStream", "call", 3),
    "PlayStationX_UnlockFileName":      ("openStream", "call", 5),
}

# Globals: which lui/addiu constant, by its position in the GT4O sequence.
WANTED_GLOBALS = {
    "UnitArena_state_field0C": ("closeStream", 0x885360),
    "UnitArena_stream_state":  ("closeStream", 0x885340),
}


def seq(img, addr):
    return calls_from(img, addr), [a for a, _ in addr_consts_in(img, addr)]


def main():
    gt4 = Image(GT4O_ELF)
    gt4_seqs = {k: seq(gt4, a) for k, a in ANCHORS.items()}

    print("GT4O reference sequences")
    for k, (calls, consts) in gt4_seqs.items():
        print("  %-12s calls  : %s" % (k, " ".join("0x%06X" % c for c in calls)))
        print("  %-12s consts : %s" % ("", " ".join("0x%06X" % c for c in consts)))
    print()

    # sanity: the indices in WANTED must line up with the GT4O addresses we expect
    EXPECT = {
        "PDISTD_FileDevice_removeExpander": 0x463020,
        "closeStream_helper_51B478": 0x51B478,
        "closeStream_helper_51B4A8": 0x51B4A8,
        "UnitArenaBase_free": 0x5285B0,
        "PlaystationX_LockFileName": 0x467060,
        "PDISTD_FileDevice_setExpander": 0x462FD0,
        "UnitArenaBase_allocate": 0x528580,
        "PlayStationX_UnlockFileName": 0x467080,
    }
    ok = True
    for name, (anchor, kind, idx) in WANTED.items():
        got = gt4_seqs[anchor][0][idx]
        exp = EXPECT.get(name)
        if exp is not None and got != exp:
            print("  !! index check FAILED for %s: idx %d -> 0x%06X, expected 0x%06X"
                  % (name, idx, got, exp))
            ok = False
    print("index self-check: %s\n" % ("PASS" if ok else "FAIL"))

    # stage 1 results give us the per-region anchor addresses
    with open(os.path.join(HERE, "tt_addresses.json")) as f:
        stage1 = json.load(f)

    out = {}
    for tag, path in TT_ELFS.items():
        tt = Image(path)
        s1 = stage1[tag]
        res = {}
        anchor_addr = {
            "closeStream": s1["PlayStation2_FileDeviceRo_closeStream"]["tt"],
            "openStream": s1["PlayStation2_FileDeviceRo_openStream"]["tt"],
        }
        tt_seqs = {k: seq(tt, a) for k, a in anchor_addr.items()}

        print("=" * 72)
        print("%s  (closeStream=0x%06X openStream=0x%06X)"
              % (tag, anchor_addr["closeStream"], anchor_addr["openStream"]))
        print("=" * 72)
        for k, (calls, consts) in tt_seqs.items():
            gcalls, gconsts = gt4_seqs[k]
            match = "OK" if (len(calls) == len(gcalls) and len(consts) == len(gconsts)) else "SHAPE MISMATCH"
            print("  %-12s calls %d/%d  consts %d/%d   %s"
                  % (k, len(calls), len(gcalls), len(consts), len(gconsts), match))

        for name, (anchor, kind, idx) in WANTED.items():
            calls = tt_seqs[anchor][0]
            gcalls = gt4_seqs[anchor][0]
            if len(calls) != len(gcalls):
                res[name] = None
                print("  %-36s SKIPPED (call count differs)" % name)
                continue
            res[name] = calls[idx]

        for name, (anchor, gval) in WANTED_GLOBALS.items():
            gconsts = gt4_seqs[anchor][1]
            consts = tt_seqs[anchor][1]
            if gval not in gconsts or len(consts) != len(gconsts):
                res[name] = None
                print("  %-36s SKIPPED (const layout differs)" % name)
                continue
            res[name] = consts[gconsts.index(gval)]

        for name in list(WANTED) + list(WANTED_GLOBALS):
            v = res.get(name)
            print("  %-36s -> %s" % (name, ("0x%08X" % v) if v else "UNRESOLVED"))

        # cross-check: the two arena frees/allocs should agree with themselves
        print()
        out[tag] = res

    with open(os.path.join(HERE, "tt_addresses_stage2.json"), "w") as f:
        json.dump(out, f, indent=2)
    print("wrote tt_addresses_stage2.json")


if __name__ == "__main__":
    main()
