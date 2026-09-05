"""
resolve_tt.py - Resolve Tourist Trophy hook addresses from the known
Gran Turismo 4 Online (SCUS_974.36) addresses used by GT4Hooks.

Method
------
GT4O and Tourist Trophy are built from the same Polyphony Digital codebase, so
individual functions compile to identical machine code and differ only in
placement and in the immediates they embed.

1. Build a candidate function inventory for each image:
   every `jal`/`bal` target (normal calls) UNION every 32-bit word in the image
   that points into .text (virtual functions, which are only reached through
   vtables and are therefore never jal targets).

2. Fingerprint each candidate with `normalize()`: the mnemonic sequence up to
   the first `jr $ra`, with every operand and immediate stripped. Same source
   -> same fingerprint, regardless of address.

3. For each known GT4O address, look its fingerprint up in the TT index.
   A single hit is an unambiguous match.

4. Verification: matches are cross-checked for *delta consistency*. Functions
   from one translation unit relocate as a block, so a correct mapping shows a
   constant GT4O->TT delta across the whole unit. A match whose delta differs
   from its neighbours is reported as suspect.

Data addresses (vtable slots, arena globals) are resolved by locating the word
that points at an already-resolved function.

Usage:  python tools/resolve_tt.py [--json out.json]
"""

import argparse
import json
import os
import pickle
import struct
import sys
from collections import defaultdict

# ttanalyze lives one level up, in tools/
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from ttanalyze import Image, normalize, disasm, call_targets, similarity  # noqa: E402

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
    "SCUS-97502": os.path.join(IMAGES, "TTexe_bin/extracted/SCUS-97502.elf"),  # US
    "SCES-53372": os.path.join(IMAGES, "TTexe_bin/extracted/SCES-53372.elf"),  # EU
    "SCPS-15105": os.path.join(IMAGES, "TTexe_bin/extracted/SCPS-15105.elf"),  # JP
}

# ---------------------------------------------------------------------------
# Known GT4 Online addresses, taken from GT4Hooks source.
# group = translation unit; matches within a group must share one delta.
# ---------------------------------------------------------------------------
FUNCS = {
    # --- PlayStation2::FileDeviceRo / Ro2 (virtual, vtable-only) ---
    "PlayStation2_FileDeviceRo_closeStream":        (0x466040, "filedev"),
    "PlayStation2_FileDeviceRo_openStream":         (0x4660D8, "filedev"),
    "PlayStation2_FileDeviceRo_rawReadStream":      (0x466288, "filedev"),
    "PlayStation2_FileDeviceRo2_IOControlStream":   (0x4665C0, "filedev"),
    "PlayStation2_FileDeviceRo2_readStat":          (0x466AB0, "filedev"),
    "PlayStation2_FileDeviceRo2_getCdOffset":       (0x466C00, "filedev"),
    "PlayStation2_FileDeviceRo_pipe":               (0x466CB0, "filedev"),
    "PlayStation2_FileDeviceRo2_GetFileEntry":      (0x45FF60, "filedev"),
    # --- PDISTD::FileDevice / FileInternalStream ---
    "PDISTD_FileDevice_readStream":                 (0x462F18, "filedev"),
    "PDISTD_FileDevice_setExpander":                (0x462FD0, "filedev"),
    "PDISTD_FileDevice_removeExpander":             (0x463020, "filedev"),
    "PDISTD_FileInternalStream_ioctl":              (0x464CD0, "filedev"),
    "PDISTD_FileInternalStream_getDevice":          (0x464D28, "filedev"),
    "PDISTD_FileStatus_FileStatus":                 (0x464968, "filedev"),
    "PageManager_GetEntryForFile":                  (0x4613C0, "filedev"),
    "PlaystationX_LockFileName":                    (0x467060, "filedev"),
    "PlayStationX_UnlockFileName":                  (0x467080, "filedev"),
    # --- PDISTD::UnitArenaBase ---
    "UnitArenaBase_allocate":                       (0x528580, "arena"),
    "UnitArenaBase_free":                           (0x5285B0, "arena"),
    # --- libc-ish ---
    "sprintf":                                      (0x50F1D8, "libc"),
    "print":                                        (0x50F120, "libc"),
    # --- sce / ps2 kernel stubs ---
    "sceOpen":                                      (0x5DF338, "sce"),
    "sceClose":                                     (0x5DF5C8, "sce"),
    "sceRead":                                      (0x5DF980, "sce"),
    "sceLSeek":                                     (0x5DF740, "sce"),
    "sceGetStat":                                   (0x5E0F20, "sce"),
    "sceStdioConvertError":                         (0x5E5AF0, "sce"),
    "sceMcGetDir":                                  (0x5B9620, "sce"),
    "sceMcSync":                                    (0x5B9310, "sce"),
    # --- functions called directly by HostFs closeStream ---
    "closeStream_helper_51B478":                    (0x51B478, "misc"),
    "closeStream_helper_51B4A8":                    (0x51B4A8, "misc"),
}

# GT4O vtable slots (address of the function-pointer word inside the vtable).
# Resolved in TT by finding the word that points at the matched function.
VTABLE_SLOTS = {
    "ADDR_PlayStation2_FileDeviceRo_closeStream":   (0x6D968C, "PlayStation2_FileDeviceRo_closeStream"),
    "ADDR_PlayStation2_FileDeviceRo_openStream":    (0x6D9694, "PlayStation2_FileDeviceRo_openStream"),
    "ADDR_PlayStation2_FileDeviceRo_rawReadStream_slot": (0x6D969C, "PlayStation2_FileDeviceRo_rawReadStream"),
    "ADDR_PDISTD_FileDevice_readStream":            (0x6D96A4, "PDISTD_FileDevice_readStream"),
    "ADDR_PlayStation2_FileDeviceRo2_IOControlStream_slot": (0x6D96C4, "PlayStation2_FileDeviceRo2_IOControlStream"),
    "ADDR_PlayStation2_FileDeviceRo2_readStat":     (0x6D96CC, "PlayStation2_FileDeviceRo2_readStat"),
    "ADDR_PlayStation2_FileDeviceRo2_getCdOffset":  (0x6D96D4, "PlayStation2_FileDeviceRo2_getCdOffset"),
    "ADDR_PlayStation2_FileDeviceRo_pipe_slot":     (0x6D96DC, "PlayStation2_FileDeviceRo_pipe"),
    "ADDR_PlayStation2_FileDeviceRo2_GetFileEntry": (0x6D96E4, "PlayStation2_FileDeviceRo2_GetFileEntry"),
}

# Global data GT4Hooks pokes directly. Resolved heuristically / by xref.
GLOBALS = {
    "UnitArena_stream_state": 0x885340,   # UnitArenaBase for FileInternalStream::State
    "UnitArena_state_field0C": 0x885360,  # secondary arena
}


def build_index(img, cache):
    """fingerprint -> [addr]; cached to disk since it costs ~40s per image."""
    if os.path.exists(cache):
        with open(cache, "rb") as f:
            return pickle.load(f)
    tb, tblob = img.text
    lo, hi = tb, tb + len(tblob)
    cands = set(call_targets(img))
    for base, blob in img.segments:
        n = len(blob) // 4
        for w in struct.unpack_from("<%dI" % n, blob, 0):
            if lo <= w < hi and (w & 3) == 0:
                cands.add(w)
    idx = defaultdict(list)
    for a in sorted(cands):
        fp = normalize(img, a)
        if fp and len(fp) >= 4:
            idx[fp].append(a)
    idx = dict(idx)
    with open(cache, "wb") as f:
        pickle.dump(idx, f)
    return idx


def resolve_image(gt4, tt, tag):
    idx = build_index(tt, os.path.join(CACHE, "_fpx_%s.pkl" % tag))
    out = {}
    for name, (addr, group) in FUNCS.items():
        fp = normalize(gt4, addr)
        hits = idx.get(fp, [])
        out[name] = {
            "gt4o": addr,
            "group": group,
            "candidates": hits,
            "tt": hits[0] if len(hits) == 1 else None,
            "status": "unique" if len(hits) == 1 else ("ambiguous" if hits else "missing"),
            "fp_len": len(fp),
        }

    # --- delta-consistency pass: the majority delta per group is authoritative
    for group in {g for _, g in FUNCS.values()}:
        deltas = defaultdict(list)
        for name, r in out.items():
            if r["group"] == group and r["tt"] is not None:
                deltas[r["tt"] - r["gt4o"]].append(name)
        if not deltas:
            continue
        best = max(deltas, key=lambda d: len(deltas[d]))
        for name, r in out.items():
            if r["group"] != group:
                continue
            r["group_delta"] = best
            if r["tt"] is not None and r["tt"] - r["gt4o"] != best:
                r["status"] = "unique-offdelta"
            # disambiguate multi-candidate entries using the group delta
            if r["status"] == "ambiguous":
                pick = [c for c in r["candidates"] if c - r["gt4o"] == best]
                if len(pick) == 1:
                    r["tt"] = pick[0]
                    r["status"] = "resolved-by-delta"

    # --- vtable slots: find the word pointing at the resolved function ---
    for slot, (gslot, fname) in VTABLE_SLOTS.items():
        fn = out.get(fname, {}).get("tt")
        if fn is None:
            out[slot] = {"gt4o": gslot, "tt": None, "status": "no-func"}
            continue
        ptrs = tt.find_bytes(fn.to_bytes(4, "little"))
        ptrs = [p for p in ptrs if (p & 3) == 0 and not tt.text[0] <= p < tt.text[0] + len(tt.text[1])]
        out[slot] = {
            "gt4o": gslot,
            "candidates": ptrs,
            "tt": ptrs[0] if ptrs else None,
            "status": "unique" if len(ptrs) == 1 else ("multi-vtable" if ptrs else "missing"),
        }

    # vtable delta consistency
    vd = defaultdict(list)
    for slot in VTABLE_SLOTS:
        r = out[slot]
        if r.get("tt") is not None:
            vd[r["tt"] - r["gt4o"]].append(slot)
    if vd:
        best = max(vd, key=lambda d: len(vd[d]))
        for slot in VTABLE_SLOTS:
            r = out[slot]
            r["group_delta"] = best
            cands = r.get("candidates") or []
            pick = [c for c in cands if c - r["gt4o"] == best]
            if len(pick) == 1:
                r["tt"] = pick[0]
                r["status"] = "unique" if len(cands) == 1 else "resolved-by-delta"
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--json", help="write results to this JSON file")
    args = ap.parse_args()

    gt4 = Image(GT4O_ELF)
    all_results = {}
    for tag, path in TT_ELFS.items():
        tt = Image(path)
        res = resolve_image(gt4, tt, tag)
        all_results[tag] = res

        print("=" * 78)
        print("%s   text 0x%08X..0x%08X" % (tag, tt.text[0], tt.text[0] + len(tt.text[1])))
        print("=" * 78)
        for name in list(FUNCS) + list(VTABLE_SLOTS):
            r = res[name]
            tt_s = ("0x%08X" % r["tt"]) if r.get("tt") is not None else "?" * 10
            d = r.get("group_delta")
            d_s = ("%+#x" % d) if d is not None else ""
            flag = "" if r["status"].startswith(("unique", "resolved")) else "   <<< " + r["status"]
            print("  %-48s 0x%08X -> %s  %-10s%s" % (name, r["gt4o"], tt_s, d_s, flag))
        bad = [n for n, r in res.items() if r.get("tt") is None]
        print("  unresolved: %d" % len(bad))
        print()

    if args.json:
        with open(args.json, "w") as f:
            json.dump(all_results, f, indent=2)
        print("wrote", args.json)


if __name__ == "__main__":
    main()
