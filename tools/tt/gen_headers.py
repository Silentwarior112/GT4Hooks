"""
gen_headers.py - Emit the per-region address headers for the Tourist Trophy game.

Reads the resolver output and writes one header per game build into
source/games/tt/Targets/. Those headers are the ONLY place where a
Tourist Trophy address literal appears; every other file in the game is
region-independent.

Usage:  python tools/gen_headers.py
"""

import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))    # tools/tt
TOOLS = os.path.dirname(HERE)                        # tools
ROOT = os.path.dirname(TOOLS)                        # the repo
OUTDIR = os.path.join(ROOT, "source", "games", "tt", "Targets")

REGIONS = {
    "SCUS-97502": {
        "region": "US",
        "elf": "SCUS_975.02",
        "title": "Tourist Trophy (USA)",
        "exponent": 90301,
        "date": "2006-02-28",
    },
    "SCES-53372": {
        "region": "EU",
        "elf": "SCES_533.72",
        "title": "Tourist Trophy (Europe)",
        "exponent": 90401,
        "date": "2006-04-12",
    },
    "SCPS-15105": {
        "region": "JP",
        "elf": "SCPS_151.05",
        "title": "Tourist Trophy (Japan)",
        "exponent": 90001,
        "date": "2005-12-28",
    },
}

# Order and grouping of the emitted defines.
LAYOUT = [
    ("Virtual function table slots - patched with HOOK_FUNC_ADDR.\n"
     "   These are DATA addresses: the function-pointer word inside\n"
     "   PlayStation2::FileDeviceRo2's vtable, not the function itself.", [
        "ADDR_PlayStation2_FileDeviceRo_openStream",
        "ADDR_PlayStation2_FileDeviceRo_closeStream",
        "ADDR_PlayStation2_FileDeviceRo2_GetFileEntry",
        "ADDR_PlayStation2_FileDeviceRo2_readStat",
        "ADDR_PDISTD_FileDevice_readStream",
     ]),
    ("Real function addresses - patched in place with HOOK, or called.", [
        "ADDR_PlayStation2_FileDeviceRo_rawReadStream",
        "ADDR_PlayStation2_FileDeviceRo2_IOControlStream",
        "ADDR_PlayStation2_FileDeviceRo_pipe",
        "ADDR_PDISTD_FileInternalStream_ioctl",
        "ADDR_PDISTD_FileStatus_FileStatus",
        "ADDR_PageManager_GetEntryForFile",
        "ADDR_PDISTD_FileDevice_setExpander",
        "ADDR_PDISTD_FileDevice_removeExpander",
        "ADDR_UnitArenaBase_allocate",
        "ADDR_UnitArenaBase_free",
        "ADDR_PlaystationX_LockFileName",
        "ADDR_PlayStationX_UnlockFileName",
     ]),
    ("Stream helpers called by FileDeviceRo::openStream / ::closeStream.", [
        "ADDR_FileStream_acquire",
        "ADDR_FileStream_flushWrite",
        "ADDR_FileStream_release",
     ]),
    ("UnitArenaBase globals that back FileInternalStream::State.", [
        "ADDR_UnitArena_StreamState",
        "ADDR_UnitArena_StateField0C",
     ]),
    ("HOutput - the debug output class stubbed out in release builds.\n"
     "   ADDR_HOutput_Handler is a global function POINTER holding a jr-$ra\n"
     "   stub, not a function. Resolved by tools/resolve_houtput.py.", [
        "ADDR_HOutput_Handler",
        "ADDR_ADHOC_printf",
        "ADDR_ADHOC_debug",
     ]),
    ("libc.", [
        "ADDR_sprintf",
        "ADDR_print",
        "ADDR_strlen",
        "ADDR_strcmp",
        "ADDR_strstr",
        "ADDR_strcpy",
     ]),
    ("sce / PS2 kernel.", [
        "ADDR_sceOpen",
        "ADDR_sceClose",
        "ADDR_sceRead",
        "ADDR_sceLSeek",
        "ADDR_sceGetStat",
        "ADDR_sceStdioConvertError",
        "ADDR_sceMcGetDir",
        "ADDR_sceMcSync",
     ]),
]

STR_MAP = {
    "ADDR_strlen": "strlen",
    "ADDR_strcmp": "strcmp",
    "ADDR_strstr": "strstr",
    "ADDR_strcpy": "strcpy",
}

# Injection parameters come from tt_injection.json, emitted alongside the
# address block below.


def main():
    with open(os.path.join(HERE, "tt_final.json")) as f:
        final = json.load(f)
    with open(os.path.join(HERE, "tt_addresses_str.json")) as f:
        strs = json.load(f)

    with open(os.path.join(HERE, "tt_prologue.json")) as f:
        prologue = json.load(f)

    inj_path = os.path.join(HERE, "tt_injection.json")
    injection = {}
    if os.path.exists(inj_path):
        with open(inj_path) as f:
            injection = json.load(f)

    # HOutput needs its own resolver - see tools/resolve_houtput.py for why.
    ho_path = os.path.join(HERE, "tt_houtput.json")
    houtput = {}
    if os.path.exists(ho_path):
        with open(ho_path) as f:
            houtput = json.load(f)

    os.makedirs(OUTDIR, exist_ok=True)

    for tag, meta in REGIONS.items():
        table = dict(final[tag])
        for sym, key in STR_MAP.items():
            table[sym] = strs[tag][key]
        for sym in ("ADDR_HOutput_Handler", "ADDR_ADHOC_printf", "ADDR_ADHOC_debug"):
            v = houtput.get(tag, {}).get(sym)
            if v is not None:
                table[sym] = v

        inj = injection.get(tag, {})

        lines = []
        lines.append("#pragma once")
        lines.append("")
        lines.append("/*")
        lines.append("    %s" % meta["title"])
        lines.append("    Boot ELF : %s" % meta["elf"])
        lines.append("    Build ID : %s   (GTImageLoader RSA exponent %d)"
                     % (tag, meta["exponent"]))
        lines.append("    Executable date: %s" % meta["date"])
        lines.append("")
        lines.append("    GENERATED FILE - do not edit by hand.")
        lines.append("    Produced by tools/gen_headers.py from the resolver output in")
        lines.append("    tools/tt_final.json. Addresses were derived from the known")
        lines.append("    Gran Turismo 4 Online (SCUS_974.36) addresses used by GT4Hooks by")
        lines.append("    matching operand-stripped instruction fingerprints, then confirmed")
        lines.append("    against call-graph and constant-pool context. See tools/resolve_tt.py.")
        lines.append("*/")
        lines.append("")
        lines.append("#define GAME_REGION_%s 1" % meta["region"])
        lines.append('#define REGION_NAME "%s"' % meta["region"])
        lines.append('#define BUILD_NAME "%s"' % tag)
        lines.append('#define ELF_NAME "%s"' % meta["elf"])
        lines.append("")

        for comment, syms in LAYOUT:
            lines.append("/* %s */" % comment)
            width = max(len(s) for s in syms)
            for s in syms:
                v = table.get(s)
                if v is None:
                    lines.append("/* #define %-*s  UNRESOLVED */" % (width, s))
                else:
                    lines.append("#define %-*s 0x%06X" % (width, s, v))
            lines.append("")

        lines.append("/* The first two instruction words of FileDeviceRo::rawReadStream.")
        lines.append("   readStream restores these to briefly un-hook the function so that")
        lines.append("   streamed files still reach the original implementation. Read out of")
        lines.append("   this build's own code by tools/gen_headers.py. */")
        pro = prologue.get(tag, {})
        lines.append("#define %-28s %s" % ("RAWREADSTREAM_INSN_0", pro.get("insn0", "/* UNRESOLVED */")))
        lines.append("#define %-28s %s" % ("RAWREADSTREAM_INSN_1", pro.get("insn1", "/* UNRESOLVED */")))
        lines.append("")

        lines.append("/*")
        lines.append("    Injection parameters.")
        lines.append("")
        lines.append("    Tourist Trophy has no dead wnn/fep region to borrow the way the GT4")
        lines.append("    game does, so the plugin takes its memory from the game's generic")
        lines.append("    pool instead. The pool's base is a link-time constant sitting in")
        lines.append("    initialised data; init() raises it past the plugin before the pool is")
        lines.append("    ever built, so the allocator can never hand out the plugin's range.")
        lines.append("    See tools/make_injection.py for the checks behind these values.")
        lines.append("*/")
        for key, sym, note in (
            ("base_address", "PLUGIN_BASE_ADDRESS",
             "where the plugin is linked - keep in sync with BASE_<REGION> in game.mk"),
            ("base_address_size", "PLUGIN_RESERVE_BYTES",
             "bytes reserved for the plugin at PLUGIN_BASE_ADDRESS"),
            ("pool_base_global", "POOL_BASE_GLOBAL",
             "holds the pool's base address; raised by PLUGIN_RESERVE_BYTES in init()"),
            ("pool_base_value", "POOL_BASE_VALUE",
             "that global's shipped value (PLUGIN_BASE_ADDRESS is this, aligned up)"),
            ("new_pool_base", "POOL_BASE_NEW",
             "what init() writes to POOL_BASE_GLOBAL: past the reserved block"),
            ("sbrk_break_global", "SBRK_BREAK_GLOBAL",
             "newlib sbrk's break pointer, initialised to the same address; sbrk is\n   dead code in these builds but is raised too so nothing disagrees"),
            ("memory_size_offset", "MEMORY_SIZE_OFFSET",
             "holds the top of RAM; the pool runs from the base up to here"),
            ("memset_call_site", "GLOBAL_MEMSET_OFFSET",
             "jal that zero-fills the pool. NOT patched: raising the pool base\n   already moves the clear above the plugin, and suppressing it would only\n   rob the game of its zeroed pool. Emitted for reference - see tt/main.c"),
            ("pool_setup_func", "POOL_SETUP_FUNC",
             "the function containing that call (informational)"),
            ("invoker_hook_site", "INVOKER_HOOK_SITE",
             "first `ei`, where the injector patches in jal INVOKER (informational)"),
        ):
            v = inj.get(key)
            lines.append("/* %s */" % note)
            if v in (None, "", "unknown"):
                lines.append("/* #define %-28s UNRESOLVED */" % sym)
            else:
                val = v if isinstance(v, str) else "0x%X" % v
                lines.append("#define %-28s %s" % (sym, val))
        lines.append("")

        path = os.path.join(OUTDIR, "%s.h" % tag)
        with open(path, "w", newline="\n") as f:
            f.write("\n".join(lines))
        n = sum(1 for _, syms in LAYOUT for s in syms if table.get(s) is not None)
        tot = sum(len(syms) for _, syms in LAYOUT)
        print("wrote %-40s %d/%d addresses" % (os.path.relpath(path, ROOT), n, tot))

    # The per-game selector this used to emit is gone: source/core/Target.h is
    # now the single shared seam, and the makefile points it at the right build
    # with -DTARGET_HEADER. Adding a region touches no header at all.


if __name__ == "__main__":
    main()
