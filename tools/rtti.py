#!/usr/bin/env python3
"""Find MSVC x86 vtables by RTTI in a PE (e.g. HL2MP's server.dll) and show each slot's first instructions.

  python -I rtti.py <server.dll> <ClassName> [first-slot] [last-slot] [ninsn]
  python -I rtti.py <server.dll> --disasm <rva-hex> [n]

Slots are numbered from 0 (the primary vtable, i.e. the COL with offset 0). Needs mingw objdump
(set OBJDUMP or put objdump on PATH).
"""
import os, re, struct, subprocess, sys

OBJDUMP = os.environ.get("OBJDUMP", "objdump")


class PE:
    def __init__(self, path):
        self.path = path
        d = self.d = open(path, "rb").read()
        pe = struct.unpack_from("<I", d, 0x3C)[0]
        nsec = struct.unpack_from("<H", d, pe + 6)[0]
        optsz = struct.unpack_from("<H", d, pe + 20)[0]
        self.base = struct.unpack_from("<I", d, pe + 24 + 28)[0]
        self.secs = []
        o = pe + 24 + optsz
        for i in range(nsec):
            name = d[o:o + 8].rstrip(b"\0").decode()
            vsz, va, rsz, raw = struct.unpack_from("<IIII", d, o + 8)
            self.secs.append((name, va, max(vsz, rsz), raw, rsz))
            o += 40

    def sec(self, name):
        return next(s for s in self.secs if s[0] == name)

    def off(self, rva):
        for n, va, sz, raw, rsz in self.secs:
            if va <= rva < va + sz:
                return raw + rva - va if rva - va < rsz else None
        return None

    def u32(self, rva):
        return struct.unpack_from("<I", self.d, self.off(rva))[0]

    def find_all(self, needle, sec=None):
        out, start = [], 0
        while True:
            i = self.d.find(needle, start)
            if i < 0:
                return out
            for n, va, sz, raw, rsz in self.secs:
                if raw <= i < raw + rsz and (sec is None or n == sec):
                    out.append(va + i - raw)
            start = i + 1


def vtables(pe, cls):
    """Return [(col_offset, vtable_rva)] for class `cls` (MSVC mangled .?AV<cls>@@)."""
    name = (".?AV%s@@" % cls).encode() + b"\0"
    hits = pe.find_all(name, ".data")
    if not hits:
        raise SystemExit("type descriptor not found for " + cls)
    td = hits[0] - 8                               # TypeDescriptor: pVFTable, spare, name
    td_va = pe.base + td
    res = []
    r0, rsz = pe.sec(".rdata")[1], pe.sec(".rdata")[2]
    for col in pe.find_all(struct.pack("<I", td_va), ".rdata"):
        col -= 12                                  # COL: signature, offset, cdOffset, pTypeDescriptor, pClassDescriptor
        sig, offset, cd = struct.unpack_from("<III", pe.d, pe.off(col))
        if sig != 0:
            continue
        for ref in pe.find_all(struct.pack("<I", pe.base + col), ".rdata"):
            res.append((offset, ref + 4))
    return sorted(res)


def disasm(pe, rva, n=6):
    start = pe.base + rva
    out = subprocess.run([OBJDUMP, "-d", "-M", "intel", "--start-address=0x%x" % start,
                          "--stop-address=0x%x" % (start + 16 * n), pe.path], capture_output=True, text=True).stdout
    lines = [l for l in out.splitlines() if re.match(r"\s*[0-9a-f]+:\t", l)]
    return [re.sub(r"\s+", " ", l.split("\t")[-1]).strip() for l in lines[:n]]


def main():
    pe = PE(sys.argv[1])
    if sys.argv[2] == "--disasm":
        rva = int(sys.argv[3], 16)
        for l in disasm(pe, rva, int(sys.argv[4]) if len(sys.argv) > 4 else 20):
            print(l)
        return
    vts = vtables(pe, sys.argv[2])
    first = int(sys.argv[3]) if len(sys.argv) > 3 else 0
    last = int(sys.argv[4]) if len(sys.argv) > 4 else 400
    ninsn = int(sys.argv[5]) if len(sys.argv) > 5 else 3
    text = pe.sec(".text")
    for off, vt in vts:
        print("vtable rva 0x%x (this-offset %d)" % (vt, off))
        if off:
            continue
        for i in range(first, last + 1):
            fn = pe.u32(vt + 4 * i) - pe.base
            if not (text[1] <= fn < text[1] + text[2]):
                break
            print("  [%3d] rva 0x%06x  %s" % (i, fn, " ; ".join(disasm(pe, fn, ninsn))))


if __name__ == "__main__":
    main()
