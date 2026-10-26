#!/usr/bin/env python3
"""SWEEP-081: dump notepad2-mod x64 import table (names + ordinals per DLL)."""
import struct, sys

def u16(b, o): return struct.unpack_from("<H", b, o)[0]
def u32(b, o): return struct.unpack_from("<I", b, o)[0]
def u64(b, o): return struct.unpack_from("<Q", b, o)[0]

path = sys.argv[1]
b = open(path, "rb").read()
pe = u32(b, 0x3c)
opt = pe + 24
magic = u16(b, opt)
assert magic == 0x20b, "not PE32+"
dd = opt + 112  # data dirs for PE32+
imp_rva, imp_sz = u32(b, dd + 8), u32(b, dd + 12)
# build RVA->file offset map from sections
nsec = u16(b, pe + 6)
optsz = u16(b, pe + 20)
sec0 = opt + optsz
secs = []
for i in range(nsec):
    s = sec0 + i * 40
    secs.append((u32(b, s + 12), u32(b, s + 8), u32(b, s + 20), u32(b, s + 16)))  # va, vsz, raw_ptr, raw_sz
def r2o(rva):
    for va, vsz, raw, rawsz in secs:
        if va <= rva < va + max(vsz, rawsz):
            return raw + (rva - va)
    return None

def cstr(off, wide=False):
    out = b""
    while True:
        c = b[off] if not wide else b[off] | (b[off+1] << 8)
        if c == 0: break
        out += bytes([c]) if not wide else c.to_bytes(2, "little")
        off += 2 if wide else 1
    return out.decode("latin1")

o = r2o(imp_rva)
while o and True:
    oft, tds, fwd, name_rva, ft = struct.unpack_from("<IIIII", b, o)
    if name_rva == 0: break
    name_off = r2o(name_rva)
    if name_off is None:
        print(f"?? name RVA {name_rva:#x} unmapped (imp dir misparsed?)")
        break
    dll = cstr(name_off)
    thunks = r2o(oft) if oft else r2o(ft)
    names = []
    while True:
        t = u64(b, thunks)
        if t == 0: break
        if t & (1 << 63):
            names.append(f"ordinal {t & 0xffff}")
        else:
            names.append(cstr(r2o(t) + 2))
        thunks += 8
    print(f"== {dll} ({len(names)})")
    for n in names: print("   ", n)
    o += 20
