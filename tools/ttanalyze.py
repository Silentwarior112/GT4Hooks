"""
ttanalyze - MIPS R5900 static analysis helpers for the GT4 / Tourist Trophy family.

Used to port GT4Hooks (Gran Turismo 4 Online, SCUS_974.36) hook addresses to the
Tourist Trophy executables, which are built from the same Polyphony Digital
codebase (PDISTD + PlayStation2 file device layer).

Everything here works on the *decompressed* ELF produced by
PDTools.GT4ElfBuilderTool.
"""

import bisect
import re
import struct
from collections import defaultdict

from capstone import Cs, CS_ARCH_MIPS, CS_MODE_MIPS64, CS_MODE_LITTLE_ENDIAN
from capstone.mips import MIPS_OP_IMM, MIPS_OP_REG, MIPS_OP_MEM


class Image:
    """A flat, vaddr-addressable view of a PS2 EE ELF."""

    def __init__(self, path):
        self.path = path
        with open(path, "rb") as f:
            self.raw = f.read()
        self.segments = []  # (vaddr, bytes)
        self._parse_elf()
        self.segments.sort(key=lambda s: s[0])
        self._starts = [s[0] for s in self.segments]

    def _parse_elf(self):
        d = self.raw
        assert d[:4] == b"\x7fELF", f"{self.path}: not an ELF"
        e_phoff = struct.unpack_from("<I", d, 0x1C)[0]
        e_phentsize = struct.unpack_from("<H", d, 0x2A)[0]
        e_phnum = struct.unpack_from("<H", d, 0x2C)[0]
        self.entry = struct.unpack_from("<I", d, 0x18)[0]
        for i in range(e_phnum):
            off = e_phoff + i * e_phentsize
            p_type, p_offset, p_vaddr, _p_paddr, p_filesz, p_memsz = struct.unpack_from(
                "<IIIIII", d, off
            )
            if p_type != 1:  # PT_LOAD
                continue
            blob = bytearray(d[p_offset : p_offset + p_filesz])
            if p_memsz > p_filesz:  # .bss tail
                blob.extend(b"\x00" * (p_memsz - p_filesz))
            self.segments.append((p_vaddr, bytes(blob)))

    # ---------- address helpers ----------

    def _seg_for(self, vaddr):
        i = bisect.bisect_right(self._starts, vaddr) - 1
        if i < 0:
            return None
        base, blob = self.segments[i]
        if vaddr < base + len(blob):
            return base, blob
        return None

    def valid(self, vaddr):
        return self._seg_for(vaddr) is not None

    def read(self, vaddr, size):
        seg = self._seg_for(vaddr)
        if seg is None:
            return None
        base, blob = seg
        off = vaddr - base
        if off + size > len(blob):
            return None
        return blob[off : off + size]

    def u32(self, vaddr):
        b = self.read(vaddr, 4)
        return None if b is None else struct.unpack("<I", b)[0]

    def i32(self, vaddr):
        b = self.read(vaddr, 4)
        return None if b is None else struct.unpack("<i", b)[0]

    def cstr(self, vaddr, maxlen=512):
        seg = self._seg_for(vaddr)
        if seg is None:
            return None
        base, blob = seg
        off = vaddr - base
        end = blob.find(b"\x00", off, off + maxlen)
        if end < 0:
            return None
        try:
            return blob[off:end].decode("ascii")
        except UnicodeDecodeError:
            return None

    @property
    def text(self):
        """The executable code segment: the one containing the ELF entry point."""
        seg = self._seg_for(self.entry)
        if seg is not None:
            return seg
        return max(self.segments, key=lambda s: len(s[1]))

    # ---------- strings ----------

    _STR_RE = re.compile(rb"[\x20-\x7e]{4,}")

    def strings(self, minlen=4):
        """Yield (vaddr, text) for every printable NUL-terminated run."""
        out = []
        for base, blob in self.segments:
            for m in self._STR_RE.finditer(blob):
                s = m.group()
                if len(s) < minlen:
                    continue
                # only keep genuinely NUL-terminated literals
                end = m.end()
                if end < len(blob) and blob[end] != 0:
                    continue
                out.append((base + m.start(), s.decode("ascii")))
        return out

    def find_string(self, needle, exact=False):
        """Addresses of string literals matching `needle`."""
        res = []
        for addr, s in self.strings():
            if (s == needle) if exact else (needle in s):
                res.append((addr, s))
        return res

    def find_bytes(self, pattern):
        """All vaddrs where the raw byte pattern occurs."""
        hits = []
        for base, blob in self.segments:
            start = 0
            while True:
                i = blob.find(pattern, start)
                if i < 0:
                    break
                hits.append(base + i)
                start = i + 1
        return hits


# ---------- disassembly ----------

_md = Cs(CS_ARCH_MIPS, CS_MODE_MIPS64 | CS_MODE_LITTLE_ENDIAN)
_md.detail = True


def disasm(img, vaddr, count=40):
    """Disassemble `count` instructions at vaddr. Returns capstone insns."""
    data = img.read(vaddr, count * 4)
    if data is None:
        return []
    return list(_md.disasm(data, vaddr, count))


def dis_text(img, vaddr, count=40):
    """Human-readable disassembly listing."""
    lines = []
    for i in disasm(img, vaddr, count):
        lines.append(f"0x{i.address:08X}  {i.mnemonic:<10} {i.op_str}")
    return "\n".join(lines)


def disasm_all(img, start=None, end=None):
    """
    Disassemble the whole text segment, resuming past undecodable words.

    capstone's generator stops at the first invalid instruction, and PD's .text
    has data (jump tables, float pools) interleaved, so we restart after each
    stall and skip one word.
    """
    base, blob = img.text
    if start is None:
        start = base
    if end is None:
        end = base + len(blob)
    pos = start
    while pos < end:
        off = pos - base
        chunk = blob[off : end - base]
        progressed = False
        for insn in _md.disasm(chunk, pos):
            yield insn
            progressed = True
            pos = insn.address + 4
        if not progressed:
            pos += 4


def call_targets(img):
    """Every jal/bal target inside .text - a good function-start inventory."""
    base, blob = img.text
    lo, hi = base, base + len(blob)
    out = set()
    for i in disasm_all(img):
        if i.mnemonic in ("jal", "bal"):
            try:
                t = int(i.op_str.strip(), 0)
            except ValueError:
                continue
            if lo <= t < hi:
                out.add(t)
    return out


def func_end(img, vaddr, limit=4000):
    """
    Find the end of a function: the delay slot after the first `jr $ra`
    that is not inside a branch-forward region. Good enough for PD code.
    """
    for i in disasm(img, vaddr, limit):
        if i.mnemonic == "jr" and i.op_str.strip() == "$ra":
            return i.address + 8  # include delay slot
    return None


def func_start(img, vaddr, limit=6000):
    """
    Walk backwards to find the containing function's entry: the instruction
    right after the previous function's `jr $ra` delay slot.
    """
    probe = vaddr - limit * 4
    if probe < 0:
        probe = 0
    seg = img._seg_for(vaddr)
    if seg is None:
        return None
    base, _ = seg
    if probe < base:
        probe = base
    # scan forward from probe, tracking last jr $ra
    last_end = base
    for i in disasm(img, probe, limit * 2):
        if i.address >= vaddr:
            break
        if i.mnemonic == "jr" and i.op_str.strip() == "$ra":
            last_end = i.address + 8
    return last_end


# ---------- xrefs ----------

class Xrefs:
    """
    Whole-image cross-reference index.

    - calls[target]  -> [call sites]   (jal / bal)
    - datarefs[addr] -> [code sites]   (lui/addiu, lui/lw, lui/sw pairs)
    - ptrs[value]    -> [addresses]    (raw 32-bit words pointing at `value`)
    """

    def __init__(self, img, text_only=True):
        self.img = img
        self.calls = defaultdict(list)
        self.datarefs = defaultdict(list)
        self.ptrs = defaultdict(list)
        self._build(text_only)

    def _build(self, text_only):
        img = self.img
        base, blob = img.text

        # --- pass 1: raw pointer table (finds vtables, string tables, ...) ---
        for seg_base, seg_blob in img.segments:
            n = len(seg_blob) // 4
            words = struct.unpack_from(f"<{n}I", seg_blob, 0)
            for idx, w in enumerate(words):
                if w and img.valid(w):
                    self.ptrs[w].append(seg_base + idx * 4)

        # --- pass 2: instruction scan for calls and hi/lo address pairs ---
        # track per-register the pending `lui` upper half
        hi = {}
        for insn in _md.disasm(blob, base):
            m = insn.mnemonic
            if m in ("jal", "bal"):
                try:
                    tgt = int(insn.op_str.strip(), 0)
                    self.calls[tgt].append(insn.address)
                except ValueError:
                    pass
                continue

            ops = insn.operands
            if m == "lui" and len(ops) == 2 and ops[1].type == MIPS_OP_IMM:
                hi[ops[0].reg] = (ops[1].imm & 0xFFFF) << 16
                continue

            # lo half: addiu rd, rs, imm  /  lw rt, imm(rs)  /  sw ...
            if m in ("addiu", "ori") and len(ops) == 3 and ops[2].type == MIPS_OP_IMM:
                src = ops[1].reg
                if src in hi:
                    val = (hi[src] + (ops[2].imm if m == "addiu" else (ops[2].imm & 0xFFFF))) & 0xFFFFFFFF
                    if img.valid(val):
                        self.datarefs[val].append(insn.address)
                    # result register now holds a full address
                    hi[ops[0].reg] = None if ops[0].reg != src else hi.get(src)
                continue

            if len(ops) >= 2 and ops[-1].type == MIPS_OP_MEM:
                mem = ops[-1].mem
                if mem.base in hi and hi[mem.base] is not None:
                    val = (hi[mem.base] + mem.disp) & 0xFFFFFFFF
                    if img.valid(val):
                        self.datarefs[val].append(insn.address)

    def callers_of(self, addr):
        return sorted(set(self.calls.get(addr, [])))

    def refs_to(self, addr):
        return sorted(set(self.datarefs.get(addr, [])))

    def pointers_to(self, addr):
        return sorted(set(self.ptrs.get(addr, [])))


# ---------- function fingerprinting ----------

def normalize(img, vaddr, maxinsn=400):
    """
    Build a version-independent fingerprint of a function: the mnemonic
    sequence with all immediates/addresses stripped. Two builds of the same
    source produce the same normalized form even at different addresses.
    """
    out = []
    for i in disasm(img, vaddr, maxinsn):
        out.append(i.mnemonic)
        if i.mnemonic == "jr" and i.op_str.strip() == "$ra":
            break
    return tuple(out)


def shape(img, vaddr, maxinsn=400):
    """Coarser fingerprint: instruction count + call count + mnemonic histogram."""
    insns = []
    ncalls = 0
    for i in disasm(img, vaddr, maxinsn):
        insns.append(i.mnemonic)
        if i.mnemonic in ("jal", "bal"):
            ncalls += 1
        if i.mnemonic == "jr" and i.op_str.strip() == "$ra":
            break
    hist = defaultdict(int)
    for m in insns:
        hist[m] += 1
    return {"count": len(insns), "calls": ncalls, "hist": dict(hist)}


def similarity(a, b):
    """Jaccard-ish similarity between two normalize() tuples."""
    if not a or not b:
        return 0.0
    if a == b:
        return 1.0
    # longest common subsequence ratio via difflib
    import difflib
    return difflib.SequenceMatcher(None, a, b).ratio()


def string_refs_in(img, xr, vaddr, maxinsn=400):
    """Every string literal referenced by the function at vaddr."""
    end = func_end(img, vaddr) or (vaddr + maxinsn * 4)
    found = []
    hi = {}
    for i in disasm(img, vaddr, (end - vaddr) // 4):
        ops = i.operands
        if i.mnemonic == "lui" and len(ops) == 2:
            hi[ops[0].reg] = (ops[1].imm & 0xFFFF) << 16
        elif i.mnemonic == "addiu" and len(ops) == 3 and ops[1].reg in hi:
            val = (hi[ops[1].reg] + ops[2].imm) & 0xFFFFFFFF
            s = img.cstr(val)
            if s and len(s) >= 3:
                found.append((val, s))
    return found


def addr_consts_in(img, vaddr, maxinsn=400):
    """
    Every full 32-bit address materialised by a lui/addiu (or lui/ori) pair
    inside the function at vaddr, in order of the completing instruction.

    This is how PD code references globals, so it recovers things like the
    UnitArenaBase globals used by FileDeviceRo::openStream/closeStream.
    """
    end = func_end(img, vaddr) or (vaddr + maxinsn * 4)
    hi = {}
    out = []
    for i in disasm(img, vaddr, (end - vaddr) // 4):
        ops = i.operands
        if i.mnemonic == "lui" and len(ops) == 2 and ops[1].type == MIPS_OP_IMM:
            hi[ops[0].reg] = (ops[1].imm & 0xFFFF) << 16
        elif i.mnemonic in ("addiu", "ori") and len(ops) == 3 and ops[1].reg in hi:
            base = hi.get(ops[1].reg)
            if base is None:
                continue
            imm = ops[2].imm if i.mnemonic == "addiu" else (ops[2].imm & 0xFFFF)
            out.append(((base + imm) & 0xFFFFFFFF, i.address))
        elif len(ops) >= 2 and ops[-1].type == MIPS_OP_MEM:
            mem = ops[-1].mem
            if hi.get(mem.base) is not None:
                out.append(((hi[mem.base] + mem.disp) & 0xFFFFFFFF, i.address))
    return out


def calls_from(img, vaddr, maxinsn=400):
    """Every jal target inside the function at vaddr, in order."""
    end = func_end(img, vaddr) or (vaddr + maxinsn * 4)
    out = []
    for i in disasm(img, vaddr, (end - vaddr) // 4):
        if i.mnemonic in ("jal", "bal"):
            try:
                out.append(int(i.op_str.strip(), 0))
            except ValueError:
                pass
    return out


# ---------- vtable helpers ----------

def read_vtable(img, vaddr, n=32):
    """Read a run of code pointers starting at vaddr."""
    base, blob = img.text
    lo, hi = base, base + len(blob)
    out = []
    for k in range(n):
        w = img.u32(vaddr + k * 4)
        if w is None:
            break
        out.append((vaddr + k * 4, w, lo <= w < hi))
    return out


def vtable_span(img, vaddr, maxn=64):
    """How many consecutive valid .text pointers start at vaddr."""
    base, blob = img.text
    lo, hi = base, base + len(blob)
    n = 0
    while n < maxn:
        w = img.u32(vaddr + n * 4)
        if w is None or not (lo <= w < hi):
            break
        n += 1
    return n
