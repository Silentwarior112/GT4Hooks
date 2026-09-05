"""
make_injection.py - Decide where the injected Tourist Trophy code lives, and
emit tt_injection.json for gen_headers.py.

The problem
-----------
GT4Hooks links its code at 0x68A480, inside the static globals of the Omron
wnn/fep Japanese IME that Gran Turismo 4 Online links but never calls. Tourist
Trophy does not ship that code at all - InitWnn, InitFep and LoadFep are absent
from all three retail builds - so there is no equivalent dead region to borrow.

Searching .bss for an unreferenced hole is not a safe substitute: a large static
array only ever has its base address materialised in code, so the interior of a
live buffer looks exactly like free space to a cross-reference scan.

The approach used instead
-------------------------
Take the memory from the game rather than guess at it.

Before main(), the game gives its generic pool everything from a statically
initialised base pointer up to the top of RAM, and zero-fills the lot:

    base = *POOL_BASE_GLOBAL          ; a link-time constant, the end of .bss
    size = *RAM_END_GLOBAL - base
    memset(base, 0, size)

The plugin is linked at that base address, and init() raises the global by the
reserved amount before the pool is ever built. The pool then starts above the
plugin, so the game's own allocator can never handt out the region, and the
memset only clears from the new base upwards.

This works because the injector's INVOKER hook fires from the first `ei` in the
executable, at 0x001001F8 in all three builds - the earliest startup code, long
before the pool setup function runs. That ordering is what makes the patch safe,
so it is asserted rather than assumed: see check_ordering() below.

Usage:  python tools/make_injection.py
"""

import json
import os
import struct
import sys

# ttanalyze lives one level up, in tools/
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from ttanalyze import Image  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))    # tools/tt
TOOLS = os.path.dirname(HERE)                        # tools
ROOT = os.path.dirname(TOOLS)                        # the repo
IMAGES = os.path.dirname(ROOT)        # game images live beside the repo

# How much room to reserve for injected code. The HostFS plugin is a few KB;
# this leaves plenty of headroom for further hooks without being wasteful.
RESERVE = 0x20000  # 128 KiB

# The linkfile aligns sections to 128, so the link base is aligned to match.
ALIGN = 128

BUILDS = {
    "SCUS-97502": {
        "elf": "TTexe_bin/extracted/SCUS-97502.elf",
        "pool_base_global": 0x5981C0,
        "ram_end_global": 0x5981C4,
        "pool_setup": 0x4A44A0,
        "memset_call_site": 0x4A44E0,
        "sbrk_break_global": 0x5A30C0,
    },
    "SCES-53372": {
        "elf": "TTexe_bin/extracted/SCES-53372.elf",
        "pool_base_global": 0x596980,
        "ram_end_global": 0x596984,
        "pool_setup": 0x4764E8,
        "memset_call_site": 0x476528,
        "sbrk_break_global": 0x5A6A30,
    },
    "SCPS-15105": {
        "elf": "TTexe_bin/extracted/SCPS-15105.elf",
        "pool_base_global": 0x593468,
        "ram_end_global": 0x59346C,
        "pool_setup": 0x4BB160,
        "memset_call_site": 0x4BB1A0,
        "sbrk_break_global": 0x595AF0,
    },
}

EI = bytes([0x38, 0x00, 0x00, 0x42])  # the `ei` instruction, little-endian


def first_ei_vaddr(path):
    """Where the injector will place its jal INVOKER: the first `ei` in the file."""
    with open(path, "rb") as f:
        d = f.read()
    off = d.find(EI)
    if off < 0:
        return None
    phoff = struct.unpack_from("<I", d, 0x1C)[0]
    es = struct.unpack_from("<H", d, 0x2A)[0]
    n = struct.unpack_from("<H", d, 0x2C)[0]
    for i in range(n):
        t, o, v, _pa, fs, _ms = struct.unpack_from("<IIIIII", d, phoff + i * es)
        if t == 1 and o <= off < o + fs:
            return v + (off - o)
    return None


def main():
    out = {}
    ok = True
    for tag, info in BUILDS.items():
        path = os.path.join(ROOT, info["elf"])
        img = Image(path)

        pool_base = img.u32(info["pool_base_global"])
        ram_end = img.u32(info["ram_end_global"])
        ei = first_ei_vaddr(path)

        print("=" * 70)
        print(tag)
        print("  pool base global 0x%06X = 0x%08X" % (info["pool_base_global"], pool_base))
        print("  ram end   global 0x%06X = 0x%08X" % (info["ram_end_global"], ram_end))
        print("  pool spans 0x%08X..0x%08X (0x%X bytes)" % (pool_base, ram_end, ram_end - pool_base))
        print("  INVOKER hook (first ei) at 0x%08X" % ei if ei else "  INVOKER hook: NOT FOUND")

        # --- the safety argument, checked rather than assumed ---
        problems = []
        if ei is None:
            problems.append("no `ei` found - the injector would fail")
        elif ei >= info["pool_setup"]:
            problems.append("INVOKER hook 0x%08X is NOT before the pool setup 0x%08X; "
                            "patching the pool base would be too late"
                            % (ei, info["pool_setup"]))

        # the base global must be statically initialised, i.e. file-backed
        filebacked = False
        with open(path, "rb") as f:
            d = f.read()
        phoff = struct.unpack_from("<I", d, 0x1C)[0]
        es = struct.unpack_from("<H", d, 0x2A)[0]
        n = struct.unpack_from("<H", d, 0x2C)[0]
        for i in range(n):
            t, o, v, _pa, fs, _ms = struct.unpack_from("<IIIIII", d, phoff + i * es)
            if t == 1 and v <= info["pool_base_global"] < v + fs:
                filebacked = True
        if not filebacked:
            problems.append("pool base global is not file-backed, so its value is not a link-time constant")

        if pool_base == 0 or ram_end <= pool_base:
            problems.append("pool bounds look wrong")
        if ram_end - pool_base < RESERVE * 4:
            problems.append("pool too small to give up 0x%X" % RESERVE)

        text_end = img.text[0] + len(img.text[1])
        if pool_base <= text_end:
            problems.append("pool base is inside .text")

        for p in problems:
            print("  !! %s" % p)
            ok = False

        # The plugin is linked at the aligned base; the few bytes between the
        # pool's shipped base and that alignment boundary are simply unused.
        link_base = (pool_base + ALIGN - 1) & ~(ALIGN - 1)
        new_pool_base = link_base + RESERVE

        # sbrk's break pointer is initialised to the same address. sbrk is dead
        # code in these builds, but it costs one store to keep it consistent.
        sbrk = info.get("sbrk_break_global")
        if sbrk is not None:
            sv = img.u32(sbrk)
            if sv != pool_base:
                problems.append("sbrk break global 0x%X holds 0x%X, expected 0x%X"
                                % (sbrk, sv, pool_base))
                ok = False
            else:
                print("  sbrk break global 0x%06X = 0x%08X (also raised)" % (sbrk, sv))

        if not problems:
            print("  OK: link plugin at 0x%08X (aligned %d), raise pool base to 0x%08X (reserve 0x%X)"
                  % (link_base, ALIGN, new_pool_base, RESERVE))

        out[tag] = {
            "base_address": "0x%X" % link_base,
            "base_address_size": "0x%X" % RESERVE,
            "pool_base_global": "0x%X" % info["pool_base_global"],
            "pool_base_value": "0x%X" % pool_base,
            "new_pool_base": "0x%X" % new_pool_base,
            "sbrk_break_global": "0x%X" % sbrk if sbrk else None,
            "ram_end_global": "0x%X" % info["ram_end_global"],
            "ram_end_value": "0x%X" % ram_end,
            "memset_call_site": "0x%X" % info["memset_call_site"],
            "memory_size_offset": "0x%X" % info["ram_end_global"],
            "pool_setup_func": "0x%X" % info["pool_setup"],
            "invoker_hook_site": "0x%X" % ei if ei else None,
        }
        print()

    with open(os.path.join(HERE, "tt_injection.json"), "w") as f:
        json.dump(out, f, indent=2)
    print("wrote tt_injection.json  (%s)" % ("all checks passed" if ok else "WITH PROBLEMS"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
