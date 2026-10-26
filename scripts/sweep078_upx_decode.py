#!/usr/bin/env python3
"""SWEEP-078: decode UPX x86 stub crash sites (RegScanner 0x4110e4-0x411120)."""
import struct, sys

def sections(data):
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    nsec = struct.unpack_from('<H', data, pe + 6)[0]
    opt = struct.unpack_from('<H', data, pe + 20)[0]
    out = []
    for i in range(nsec):
        off = pe + 24 + opt + 40 * i
        name = data[off:off+8].rstrip(b'\0').decode(errors='replace')
        vsize, va, rsize, raw = struct.unpack_from('<IIII', data, off + 8)
        out.append((name, va, vsize, raw, rsize))
    return out

def va2off(data, va):
    for name, sva, vsz, raw, rsz in sections(data):
        if sva <= va < sva + max(vsz, rsz):
            if raw == 0: return None
            return raw + (va - sva)
    return None

path = sys.argv[1]
data = open(path, 'rb').read()
print("== sections ==")
for s in sections(data): print(f"  {s[0]:8} va=0x{s[1]:x} vsize=0x{s[2]:x} raw=0x{s[3]:x}")
for va in range(0x4110c0, 0x411140):
    off = va2off(data, va)
    b = data[off] if off is not None and off < len(data) else None
    if va % 16 == 0: print(f"\n{va:08x}: ", end="")
    print(f"{b:02x} " if b is not None else "?? ", end="")
print()
