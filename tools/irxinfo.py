"""
irxinfo.py - List the libraries an IOP module imports and exports.

IRX modules declare their imports as tables embedded in .text. Each table is:

    u32  magic    0x41E00000
    u32  0
    u32  version  (major << 8) | minor
    char name[8]  space-padded library name
    ... then one 8-byte stub per imported function:
            jr $ra                       0x03E00008
            addiu $zero, $zero, index    0x24000000 | index
    ... terminated by a zero word.

Export tables use magic 0x41C00000 with the same header shape.

This matters for the HostFS work: PCSX2 substitutes its own host-capable
implementation for `ioman` / `iomanx` exports at import-resolution time, so a
module can only reach host: paths if it imports one of those libraries. See
pcsx2/IopBios.cpp, irxImport() / irxImportHLE().

Usage:  python tools/irxinfo.py <file.irx> [...]
"""

import struct
import sys

IMPORT_MAGIC = 0x41E00000
EXPORT_MAGIC = 0x41C00000

# ioman / iomanx export indices PCSX2 replaces with its host-aware versions.
IOMAN_HLE = {
    4: "open", 5: "close", 6: "read", 7: "write", 8: "lseek",
    10: "remove", 11: "mkdir", 12: "rmdir", 13: "dopen", 14: "dclose",
    15: "dread", 16: "getStat",
}


def tables(data, magic):
    out = []
    for off in range(0, len(data) - 20, 4):
        if struct.unpack_from("<I", data, off)[0] != magic:
            continue
        if struct.unpack_from("<I", data, off + 4)[0] != 0:
            continue
        ver = struct.unpack_from("<I", data, off + 8)[0]
        name = data[off + 12:off + 20].rstrip(b"\x00").rstrip().decode("ascii", "replace")
        if not name or not all(32 <= c < 127 for c in name.encode()):
            continue
        # walk the stubs
        idx, p = [], off + 20
        while p + 8 <= len(data):
            a, b = struct.unpack_from("<II", data, p)
            if a == 0:
                break
            if a != 0x03E00008 or (b & 0xFFFF0000) != 0x24000000:
                break
            idx.append(b & 0xFFFF)
            p += 8
        out.append((name, ver, sorted(set(idx))))
    return out


def main():
    if len(sys.argv) < 2:
        print(__doc__.strip().splitlines()[-1])
        return 1

    for path in sys.argv[1:]:
        with open(path, "rb") as f:
            data = f.read()
        print("=" * 68)
        print("%s   (%d bytes)" % (path, len(data)))
        print("=" * 68)

        exp = tables(data, EXPORT_MAGIC)
        if exp:
            print("  exports:")
            for name, ver, _ in exp:
                print("    %-10s v%d.%d" % (name, ver >> 8, ver & 0xFF))

        imp = tables(data, IMPORT_MAGIC)
        print("  imports (%d libraries):" % len(imp))
        hostable = False
        for name, ver, idx in sorted(imp):
            mark = ""
            if name in ("ioman", "iomanx"):
                hostable = True
                named = [IOMAN_HLE.get(i, str(i)) for i in idx]
                mark = "   <<< PCSX2 HLE-hooks this: %s" % ", ".join(named)
            print("    %-10s v%-6s %2d funcs  %s%s"
                  % (name, "%d.%d" % (ver >> 8, ver & 0xFF), len(idx),
                     idx if len(idx) <= 12 else "%s..." % idx[:12], mark))

        print()
        print("  => can reach host: paths under PCSX2: %s" % ("YES" if hostable else "NO"))
        print()
    return 0


if __name__ == "__main__":
    sys.exit(main())
