"""
resolve_printf.py - Pin down the printf-family stubs.

PD's varargs wrappers (printf / sprintf / fprintf / ...) all compile to the
same stub shape: spill the register-passed varargs to the stack, then tail into
a v*printf core. They are therefore operand-identical to each other, and no
fingerprint of the stub alone can tell them apart.

They are separated one level down instead: match the *core* each stub calls
against the GT4O core. printf's core and sprintf's core have distinct
fingerprints, so the stub whose callee matches is the right stub.

Usage:  python tools/resolve_printf.py
"""

import json
import os
import sys

# ttanalyze lives one level up, in tools/
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from ttanalyze import Image, normalize, calls_from, similarity  # noqa: E402

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

# GT4O: stub -> the v*printf core it tail-calls
TARGETS = {
    "print":   (0x50F120, 0x50F238),
    "sprintf": (0x50F1D8, 0x50F588),
}


def main():
    import pickle

    gt4 = Image(GT4O_ELF)
    out = {}
    for tag, path in TT_ELFS.items():
        tt = Image(path)
        with open(os.path.join(CACHE, "_fpx_%s.pkl" % tag), "rb") as f:
            idx = pickle.load(f)

        res = {}
        print("=" * 64)
        print(tag)
        print("=" * 64)
        for name, (gstub, gcore) in TARGETS.items():
            gfp = normalize(gt4, gstub)
            gcore_fp = normalize(gt4, gcore)
            cands = idx.get(gfp, [])
            scored = []
            for c in cands:
                callees = calls_from(tt, c)
                if not callees:
                    continue
                s = similarity(gcore_fp, normalize(tt, callees[0]))
                scored.append((s, c, callees[0]))
            scored.sort(reverse=True)
            for s, c, callee in scored:
                print("   %-8s cand 0x%06X core 0x%06X  sim=%.3f" % (name, c, callee, s))
            if scored and scored[0][0] >= 0.99 and (len(scored) == 1 or scored[0][0] - scored[1][0] > 0.05):
                res[name] = scored[0][1]
                print("   %-8s -> 0x%08X  RESOLVED" % (name, scored[0][1]))
            else:
                res[name] = None
                print("   %-8s -> UNRESOLVED (no clear winner)" % name)

        # cross-check: GT4O keeps sprintf 0xB8 after print; the same must hold
        if res.get("print") and res.get("sprintf"):
            d = res["sprintf"] - res["print"]
            gd = TARGETS["sprintf"][0] - TARGETS["print"][0]
            print("   spacing check: TT delta 0x%X vs GT4O 0x%X -> %s"
                  % (d, gd, "OK" if d == gd else "MISMATCH"))
        print()
        out[tag] = res

    with open(os.path.join(HERE, "tt_addresses_printf.json"), "w") as f:
        json.dump(out, f, indent=2)
    print("wrote tt_addresses_printf.json")


if __name__ == "__main__":
    main()
