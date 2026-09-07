"""
irxdis.py - Disassemble a range of an IOP module, annotating calls to imports.

IOP code is MIPS R3000A - plain MIPS-I, little-endian, no FPU - which is a
different decode from the R5900 EE code the rest of the tools handle, so this
uses capstone in MIPS32 mode rather than MIPS64.

The useful part is the annotation: every `jal` that lands on an import stub is
labelled with the library and function it reaches, which is what makes an IOP
module readable without symbols.

Usage:  python tools/irxdis.py <file.irx> <start> [end]
        python tools/irxdis.py irx/tt/pdistr.irx 0x2790 0x2B78
"""

import struct
import sys

from capstone import Cs, CS_ARCH_MIPS, CS_MODE_MIPS32, CS_MODE_LITTLE_ENDIAN

IMPORT_MAGIC = 0x41E00000

IOMAN_NAMES = {
    4: "open", 5: "close", 6: "read", 7: "write", 8: "lseek",
    10: "remove", 11: "mkdir", 12: "rmdir", 13: "dopen", 14: "dclose",
    15: "dread", 16: "getStat",
}
# Only the handful that matter here; everything else prints as an index.
KNOWN = {
    ("sysclib", 4): "memcpy?", ("sysclib", 14): "strlen?",
    ("thbase", 20): "CreateThread?", ("thbase", 33): "SleepThread?",
    ("sifcmd", 17): "SifRegisterRpc?", ("sifcmd", 19): "SifSetRpcQueue?",
    ("sifcmd", 22): "SifRpcLoop?",
    ("intrman", 17): "CpuSuspendIntr?", ("intrman", 18): "CpuResumeIntr?",
    ("loadcore", 6): "RegisterLibraryEntries?",
}


def load(path):
    with open(path, "rb") as f:
        d = f.read()
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


def off_of(segs, va):
    for so, sva, sz in segs:
        if sva <= va < sva + sz:
            return so + (va - sva)
    return None


def va_of(segs, off):
    for so, sva, sz in segs:
        if so <= off < so + sz:
            return sva + (off - so)
    return None


def import_stubs(d, segs):
    stubs = {}
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
            va = va_of(segs, p)
            if va is not None:
                idx = b & 0xFFFF
                if name in ("ioman", "iomanx"):
                    label = "%s::%s" % (name, IOMAN_NAMES.get(idx, str(idx)))
                else:
                    label = "%s::%s" % (name, KNOWN.get((name, idx), "fn%d" % idx))
                stubs[va] = label
            p += 8
    return stubs


def main():
    if len(sys.argv) < 3:
        print("usage: python tools/irxdis.py <file.irx> <start> [end]")
        return 1
    path = sys.argv[1]
    start = int(sys.argv[2], 0)
    end = int(sys.argv[3], 0) if len(sys.argv) > 3 else start + 0x100

    d, segs = load(path)
    stubs = import_stubs(d, segs)

    off = off_of(segs, start)
    if off is None:
        print("address 0x%X is not in a loadable segment" % start)
        return 1

    md = Cs(CS_ARCH_MIPS, CS_MODE_MIPS32 | CS_MODE_LITTLE_ENDIAN)
    md.detail = False

    blob = d[off:off + (end - start)]
    print("%s  0x%X..0x%X   (MIPS R3000A)" % (path, start, end))
    print("-" * 72)

    for i in md.disasm(blob, start):
        note = ""
        if i.mnemonic in ("jal", "j"):
            try:
                tgt = int(i.op_str.strip(), 0)
                if tgt in stubs:
                    note = "   <<< %s" % stubs[tgt]
            except ValueError:
                pass
        print("  0x%04X  %-8s %-34s%s" % (i.address, i.mnemonic, i.op_str, note))
    return 0


if __name__ == "__main__":
    sys.exit(main())
