"""
irxcalls.py - Find which imported IOP functions a module actually calls.

An IRX import table is followed by one 8-byte stub per imported function. Code
in the module reaches an import by `jal`-ing its stub. So counting jal targets
that land on a stub tells you whether an import is live or vestigial - which
is not the same question as whether it is declared.

For the HostFS work the specific question is whether pdistr.irx already opens
files by name through ioman (a path that could simply be pointed at host:), or
whether it only ever reads through pdicdvd by sector.

Usage:  python tools/irxcalls.py <file.irx> [library]
"""

import struct
import sys

IMPORT_MAGIC = 0x41E00000

IOMAN_NAMES = {
    4: "open", 5: "close", 6: "read", 7: "write", 8: "lseek",
    10: "remove", 11: "mkdir", 12: "rmdir", 13: "dopen", 14: "dclose",
    15: "dread", 16: "getStat",
}


def load(path):
    with open(path, "rb") as f:
        d = f.read()
    # IRX is a normal little-endian ELF; map file offsets to vaddrs via phdrs.
    segs = []
    if d[:4] == b"\x7fELF":
        phoff = struct.unpack_from("<I", d, 0x1C)[0]
        phes = struct.unpack_from("<H", d, 0x2A)[0]
        phn = struct.unpack_from("<H", d, 0x2C)[0]
        for i in range(phn):
            t, off, va, _pa, fsz, _msz = struct.unpack_from("<IIIIII", d, phoff + i * phes)
            if t == 1:
                segs.append((off, va, fsz))
    if not segs:
        segs = [(0, 0, len(d))]
    return d, segs


def off_to_va(segs, off):
    for so, va, sz in segs:
        if so <= off < so + sz:
            return va + (off - so)
    return None


def main():
    if len(sys.argv) < 2:
        print("usage: python tools/irxcalls.py <file.irx> [library]")
        return 1
    path = sys.argv[1]
    want = sys.argv[2].lower() if len(sys.argv) > 2 else None

    d, segs = load(path)

    # ---- locate import stubs and give each a vaddr ----
    stubs = {}   # vaddr -> (libname, index)
    for off in range(0, len(d) - 20, 4):
        if struct.unpack_from("<I", d, off)[0] != IMPORT_MAGIC:
            continue
        if struct.unpack_from("<I", d, off + 4)[0] != 0:
            continue
        name = d[off + 12:off + 20].rstrip(b"\x00").rstrip().decode("ascii", "replace")
        if not name or not all(32 <= c < 127 for c in name.encode()):
            continue
        p = off + 20
        while p + 8 <= len(d):
            a, b = struct.unpack_from("<II", d, p)
            if a != 0x03E00008 or (b & 0xFFFF0000) != 0x24000000:
                break
            va = off_to_va(segs, p)
            if va is not None:
                stubs[va] = (name, b & 0xFFFF)
            p += 8

    # ---- scan every word for a jal landing on a stub ----
    calls = {}
    for off in range(0, len(d) - 4, 4):
        w = struct.unpack_from("<I", d, off)[0]
        if (w >> 26) != 0x03:            # jal
            continue
        va = off_to_va(segs, off)
        if va is None:
            continue
        target = ((va + 4) & 0xF0000000) | ((w & 0x03FFFFFF) << 2)
        if target in stubs:
            calls.setdefault(stubs[target], []).append(va)

    print("=" * 66)
    print("%s" % path)
    print("=" * 66)
    print("  %d import stubs found" % len(stubs))
    print()

    libs = sorted({lib for lib, _ in stubs.values()})
    for lib in libs:
        if want and lib.lower() != want:
            continue
        entries = sorted((idx, calls.get((lib, idx), []))
                         for l, idx in stubs.values() if l == lib)
        live = sum(1 for _i, c in entries if c)
        print("  %s  (%d imported, %d actually called)" % (lib, len(entries), live))
        for idx, sites in entries:
            nm = IOMAN_NAMES.get(idx, "") if lib in ("ioman", "iomanx") else ""
            label = "%-3d %-8s" % (idx, nm)
            if sites:
                shown = " ".join("0x%X" % s for s in sites[:6])
                more = "" if len(sites) <= 6 else " (+%d more)" % (len(sites) - 6)
                print("     %s CALLED %2d x  from %s%s" % (label, len(sites), shown, more))
            else:
                print("     %s -- not called --" % label)
        print()
    return 0


if __name__ == "__main__":
    sys.exit(main())
