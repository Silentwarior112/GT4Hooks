"""
resolve_injection.py - Resolve the Tourist Trophy injection parameters:
the .bss-clearing memset call to NOP, and the RAM-size global.

Background
----------
Before main(), the game hands its whole .bss to a pool allocator and zero-fills
it. GT4Hooks links its injected code inside that range, so it NOPs the clearing
call first (GT4O: the `jal` at 0x5225F0 inside the setup function at 0x5225B0).

The GT4O setup function decodes as:

    lui  $v0, 0x68
    lw   $a0, 0x196c($v0)        ; heap base global
    jal  <aligned alloc>          ; a1 = 0x10
    lui  $v0, 0x68
    lw   $s0, 0x1970($v0)        ; RAM size global  == MEMORY_SIZE_OFFSET
    subu $s0, $s0, $s1           ; size = end - base
    and  $s0, $s0, -0x10
    jal  <memset>                 ; a0 = base, a1 = 0, a2 = size   <-- NOP this
    ...

So both parameters fall out of the same function once it is located: the second
`jal` is the call to NOP, and the second materialised global is the RAM size.

Usage:  python tools/resolve_injection.py
"""

import json
import os
import pickle
import sys

# ttanalyze lives one level up, in tools/
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from ttanalyze import (  # noqa: E402
    Image, normalize, similarity, calls_from, addr_consts_in, disasm, dis_text,
)

HERE = os.path.dirname(os.path.abspath(__file__))    # tools/tt
TOOLS = os.path.dirname(HERE)                        # tools
ROOT = os.path.dirname(TOOLS)                        # the repo
IMAGES = os.path.dirname(ROOT)        # game images live beside the repo
# Fingerprint caches are large binaries, so they live outside the repo too.
CACHE = os.environ.get("GT4HOOKS_WORK") or os.path.join(IMAGES, "GT4Hooks-work")
CACHE = os.path.join(CACHE, "cache")
os.makedirs(CACHE, exist_ok=True)

GT4O_ELF = os.path.join(IMAGES, "GT4exe_bin/extracted/SCUS-97436.elf")
TT_ELFS = {
    "SCUS-97502": os.path.join(IMAGES, "TTexe_bin/extracted/SCUS-97502.elf"),
    "SCES-53372": os.path.join(IMAGES, "TTexe_bin/extracted/SCES-53372.elf"),
    "SCPS-15105": os.path.join(IMAGES, "TTexe_bin/extracted/SCPS-15105.elf"),
}

GT4O_SETUP = 0x5225B0        # the .bss pool setup function
GT4O_MEMSET_SITE = 0x5225F0  # the jal inside it that clears .bss
GT4O_MEMSET_FN = 0x5D5E98    # memset itself
GT4O_RAMSIZE = 0x681970      # MEMORY_SIZE_OFFSET


def find_setup(tt, idx, gt4):
    """Locate TT's .bss pool setup function by fingerprint."""
    fp = normalize(gt4, GT4O_SETUP)
    hits = idx.get(fp, [])
    if len(hits) == 1:
        return hits[0], "unique fingerprint"
    # fall back: score every candidate with the same call/const shape
    best = []
    gcalls = calls_from(gt4, GT4O_SETUP)
    gconsts = [a for a, _ in addr_consts_in(gt4, GT4O_SETUP)]
    for cands in idx.values():
        for c in cands:
            if len(calls_from(tt, c)) == len(gcalls) and len(addr_consts_in(tt, c)) == len(gconsts):
                s = similarity(fp, normalize(tt, c))
                if s > 0.95:
                    best.append((s, c))
    best.sort(reverse=True)
    if best and (len(best) == 1 or best[0][0] - best[1][0] > 0.02):
        return best[0][1], "best shape+similarity match (%.3f)" % best[0][0]
    return None, "ambiguous (%d candidates)" % len(hits or best)


def main():
    gt4 = Image(GT4O_ELF)

    # --- self-check the reference decode ---
    gcalls = calls_from(gt4, GT4O_SETUP)
    gconsts = [a for a, _ in addr_consts_in(gt4, GT4O_SETUP)]
    memset_idx = gcalls.index(GT4O_MEMSET_FN)
    ram_idx = gconsts.index(GT4O_RAMSIZE)
    print("GT4O setup 0x%06X: calls=%s consts=%s" %
          (GT4O_SETUP, [hex(c) for c in gcalls], [hex(c) for c in gconsts]))
    print("  memset is call #%d, RAM size is const #%d" % (memset_idx, ram_idx))

    # offset of the memset jal from the function start
    site_off = GT4O_MEMSET_SITE - GT4O_SETUP
    print("  memset jal sits at +0x%X from the function start\n" % site_off)

    out = {}
    for tag, path in TT_ELFS.items():
        tt = Image(path)
        with open(os.path.join(CACHE, "_fpx_%s.pkl" % tag), "rb") as f:
            idx = pickle.load(f)

        setup, how = find_setup(tt, idx, gt4)
        print("=" * 70)
        print("%s  setup func: %s   (%s)" %
              (tag, ("0x%06X" % setup) if setup else "NOT FOUND", how))
        if setup is None:
            out[tag] = {}
            continue

        calls = calls_from(tt, setup)
        consts = [a for a, _ in addr_consts_in(tt, setup)]
        print("  calls  : %s" % [hex(c) for c in calls])
        print("  consts : %s" % [hex(c) for c in consts])

        # locate the actual jal instruction address for call #memset_idx
        site = None
        seen = 0
        for i in disasm(tt, setup, 200):
            if i.mnemonic in ("jal", "bal"):
                if seen == memset_idx:
                    site = i.address
                    break
                seen += 1
            if i.mnemonic == "jr" and i.op_str.strip() == "$ra":
                break

        memset_fn = calls[memset_idx] if len(calls) > memset_idx else None
        ramsize = consts[ram_idx] if len(consts) > ram_idx else None

        # verify: the memset callee should fingerprint-match GT4O's memset
        ver = ""
        if memset_fn is not None:
            s = similarity(normalize(gt4, GT4O_MEMSET_FN), normalize(tt, memset_fn))
            ver = "memset fingerprint similarity %.3f %s" % (s, "OK" if s > 0.95 else "<<< SUSPECT")

        print("  memset call site : %s  (offset +0x%X %s)" %
              (("0x%06X" % site) if site else "?", (site - setup) if site else 0,
               "matches GT4O" if site and (site - setup) == site_off else "DIFFERS from GT4O"))
        print("  memset function  : %s   %s" %
              (("0x%06X" % memset_fn) if memset_fn else "?", ver))
        print("  RAM size global  : %s" % (("0x%06X" % ramsize) if ramsize else "?"))
        print()
        print(dis_text(tt, setup, 20))
        print()

        out[tag] = {
            "pool_setup_func": "0x%X" % setup,
            "memset_call_site": "0x%X" % site if site else None,
            "memset_func": "0x%X" % memset_fn if memset_fn else None,
            "memory_size_offset": "0x%X" % ramsize if ramsize else None,
        }

    with open(os.path.join(HERE, "tt_injection_partial.json"), "w") as f:
        json.dump(out, f, indent=2)
    print("wrote tt_injection_partial.json")


if __name__ == "__main__":
    main()
