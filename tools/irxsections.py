"""
irxsections.py - Dump an IOP module's ELF structure: segments, sections and
relocations.

IRX modules are usually ET_REL, so addresses embedded in data - jump tables in
particular - are stored as relocatable values and are meaningless read raw. This
prints the section table and any relocation entries so you can tell whether a
word you are reading needs fixing up before it means anything.

Usage:  python tools/irxsections.py <file.irx> [reloc-target-addr]
"""

import struct
import sys

SHT = {0: "NULL", 1: "PROGBITS", 2: "SYMTAB", 3: "STRTAB", 8: "NOBITS",
       9: "REL", 0x70000080: "IOPMOD"}

R_MIPS = {0: "NONE", 2: "32", 4: "26", 5: "HI16", 6: "LO16"}


def cstr(b, off):
    end = b.index(0, off)
    return b[off:end].decode("ascii", "replace")


def main():
    if len(sys.argv) < 2:
        print("usage: python tools/irxsections.py <file.irx> [addr]")
        return 1
    path = sys.argv[1]
    want = int(sys.argv[2], 0) if len(sys.argv) > 2 else None

    d = open(path, "rb").read()
    etype = struct.unpack_from("<H", d, 0x10)[0]
    print("%s" % path)
    print("  e_type = %d (%s)" % (etype, {1: "ET_REL", 2: "ET_EXEC"}.get(etype, "?")))

    shoff = struct.unpack_from("<I", d, 0x20)[0]
    shes = struct.unpack_from("<H", d, 0x2E)[0]
    shn = struct.unpack_from("<H", d, 0x30)[0]
    stridx = struct.unpack_from("<H", d, 0x32)[0]
    if not shn:
        print("  no section headers")
        return 0

    so, ssz = struct.unpack_from("<II", d, shoff + stridx * shes + 0x10)
    strtab = d[so:so + ssz]

    secs = []
    print("  sections:")
    for i in range(shn):
        nm, ty, fl, va, off, sz, link, info, align, ent = struct.unpack_from(
            "<10I", d, shoff + i * shes)
        name = cstr(strtab, nm)
        secs.append((name, ty, va, off, sz, link, info, ent))
        print("    %-2d %-14s %-9s va=0x%-6X off=0x%-6X size=0x%-6X" %
              (i, name, SHT.get(ty, str(ty)), va, off, sz))

    # ---- relocations ----
    for name, ty, va, off, sz, link, info, ent in secs:
        if ty != 9:
            continue
        n = sz // 8
        print("  %s: %d entries" % (name, n))
        hits = 0
        for k in range(n):
            r_off, r_info = struct.unpack_from("<II", d, off + k * 8)
            rtype = r_info & 0xFF
            rsym = r_info >> 8
            if want is not None and r_off != want:
                continue
            hits += 1
            print("     offset=0x%-6X type=R_MIPS_%-5s sym=%d" %
                  (r_off, R_MIPS.get(rtype, str(rtype)), rsym))
            if want is not None and hits > 12:
                break
        if want is None:
            # summarise types
            counts = {}
            for k in range(n):
                _o, ri = struct.unpack_from("<II", d, off + k * 8)
                t = R_MIPS.get(ri & 0xFF, str(ri & 0xFF))
                counts[t] = counts.get(t, 0) + 1
            print("     types: %s" % counts)
    return 0


if __name__ == "__main__":
    sys.exit(main())
