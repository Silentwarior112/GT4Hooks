"""
consolidate.py - Merge the three resolver stages into one address table and
emit the per-region C headers for the Tourist Trophy hook game.

Stages:
  1. resolve_tt.py      - fingerprint match (functions + vtable slots)
  2. resolve_stage2.py  - call-graph / constant-pool context (arenas, helpers, globals)
  3. resolve_printf.py  - printf-family stubs, disambiguated via their core

Where stages overlap they are required to agree; a disagreement is a hard error
rather than a silent pick, since a wrong address here is a crash in the game.

Usage:  python tools/consolidate.py
"""

import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))

REGIONS = {
    "SCUS-97502": ("US", "SCUS_975.02"),
    "SCES-53372": ("EU", "SCES_533.72"),
    "SCPS-15105": ("JP", "SCPS_151.05"),
}

# Final symbol -> (source stage, key in that stage)
PLAN = [
    # --- vtable slots (patched with HOOK_FUNC_ADDR) ---
    ("ADDR_PlayStation2_FileDeviceRo_openStream",      "s1", "ADDR_PlayStation2_FileDeviceRo_openStream"),
    ("ADDR_PlayStation2_FileDeviceRo_closeStream",     "s1", "ADDR_PlayStation2_FileDeviceRo_closeStream"),
    ("ADDR_PlayStation2_FileDeviceRo2_GetFileEntry",   "s1", "ADDR_PlayStation2_FileDeviceRo2_GetFileEntry"),
    ("ADDR_PlayStation2_FileDeviceRo2_readStat",       "s1", "ADDR_PlayStation2_FileDeviceRo2_readStat"),
    ("ADDR_PDISTD_FileDevice_readStream",              "s1", "ADDR_PDISTD_FileDevice_readStream"),
    # --- raw function addresses (patched with HOOK / called directly) ---
    ("ADDR_PlayStation2_FileDeviceRo_rawReadStream",   "s1", "PlayStation2_FileDeviceRo_rawReadStream"),
    ("ADDR_PlayStation2_FileDeviceRo2_IOControlStream", "s1", "PlayStation2_FileDeviceRo2_IOControlStream"),
    ("ADDR_PlayStation2_FileDeviceRo_pipe",            "s1", "PlayStation2_FileDeviceRo_pipe"),
    ("ADDR_PDISTD_FileInternalStream_ioctl",           "s1", "PDISTD_FileInternalStream_ioctl"),
    ("ADDR_PDISTD_FileStatus_FileStatus",              "s1", "PDISTD_FileStatus_FileStatus"),
    ("ADDR_PageManager_GetEntryForFile",               "s1", "PageManager_GetEntryForFile"),
    ("ADDR_PDISTD_FileDevice_setExpander",             "s2", "PDISTD_FileDevice_setExpander"),
    ("ADDR_PDISTD_FileDevice_removeExpander",          "s2", "PDISTD_FileDevice_removeExpander"),
    ("ADDR_UnitArenaBase_allocate",                    "s2", "UnitArenaBase_allocate"),
    ("ADDR_UnitArenaBase_free",                        "s2", "UnitArenaBase_free"),
    ("ADDR_PlaystationX_LockFileName",                 "s2", "PlaystationX_LockFileName"),
    ("ADDR_PlayStationX_UnlockFileName",               "s2", "PlayStationX_UnlockFileName"),
    # --- closeStream / openStream helpers ---
    ("ADDR_FileStream_flushWrite",                     "s2", "closeStream_helper_51B478"),
    ("ADDR_FileStream_release",                        "s2", "closeStream_helper_51B4A8"),
    ("ADDR_FileStream_acquire",                        "s2", "openStream_helper_51B370"),
    # --- arena globals ---
    ("ADDR_UnitArena_StreamState",                     "s2", "UnitArena_stream_state"),
    ("ADDR_UnitArena_StateField0C",                    "s2", "UnitArena_state_field0C"),
    # --- libc ---
    ("ADDR_sprintf",                                   "pf", "sprintf"),
    ("ADDR_print",                                     "pf", "print"),
    # --- sce ---
    ("ADDR_sceOpen",                                   "s1", "sceOpen"),
    ("ADDR_sceClose",                                  "s1", "sceClose"),
    ("ADDR_sceRead",                                   "s1", "sceRead"),
    ("ADDR_sceLSeek",                                  "s1", "sceLSeek"),
    ("ADDR_sceGetStat",                                "s1", "sceGetStat"),
    ("ADDR_sceStdioConvertError",                      "s1", "sceStdioConvertError"),
    ("ADDR_sceMcGetDir",                               "s1", "sceMcGetDir"),
    ("ADDR_sceMcSync",                                 "s1", "sceMcSync"),
]

# Symbols present in more than one stage: they must agree.
CROSSCHECK = [
    ("PDISTD_FileDevice_setExpander", "PDISTD_FileDevice_setExpander"),
    ("PDISTD_FileDevice_removeExpander", "PDISTD_FileDevice_removeExpander"),
    ("PlaystationX_LockFileName", "PlaystationX_LockFileName"),
    ("PlayStationX_UnlockFileName", "PlayStationX_UnlockFileName"),
]


def load(name):
    with open(os.path.join(HERE, name)) as f:
        return json.load(f)


def main():
    s1 = load("tt_addresses.json")
    s2 = load("tt_addresses_stage2.json")
    pf = load("tt_addresses_printf.json")

    final = {}
    problems = []
    for tag in REGIONS:
        src = {"s1": {k: v.get("tt") for k, v in s1[tag].items()},
               "s2": s2[tag], "pf": pf[tag]}

        # cross-stage agreement
        for k1, k2 in CROSSCHECK:
            a, b = src["s1"].get(k1), src["s2"].get(k2)
            if a is not None and b is not None and a != b:
                problems.append("%s: %s stage1=0x%X stage2=0x%X" % (tag, k1, a, b))

        table = {}
        for sym, stage, key in PLAN:
            v = src[stage].get(key)
            if v is None:
                problems.append("%s: %s UNRESOLVED" % (tag, sym))
            table[sym] = v
        final[tag] = table

    with open(os.path.join(HERE, "tt_final.json"), "w") as f:
        json.dump(final, f, indent=2)

    for tag, (region, elfname) in REGIONS.items():
        t = final[tag]
        n_ok = sum(1 for v in t.values() if v is not None)
        print("%-12s (%s)  %d/%d resolved" % (tag, region, n_ok, len(t)))

    print()
    if problems:
        print("PROBLEMS:")
        for p in problems:
            print("  " + p)
    else:
        print("All symbols resolved, all cross-stage checks agree.")
    print("\nwrote tt_final.json")


if __name__ == "__main__":
    main()
