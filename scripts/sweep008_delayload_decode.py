#!/usr/bin/env python3
"""Decode PE delay-load import directory; find which dll!import owns a given IAT slot VA."""
import struct, sys

exe, target = sys.argv[1], int(sys.argv[2], 16)
f = open(exe, 'rb').read()
pe = struct.unpack_from('<I', f, 0x3c)[0]
opt = pe + 24
ddoff = opt + 112
rva, sz = struct.unpack_from('<II', f, ddoff + 13 * 8)
imgbase = struct.unpack_from('<Q', f, opt + 24)[0]
nsec = struct.unpack_from('<H', f, pe + 6)[0]
so = opt + struct.unpack_from('<H', f, pe + 20)[0]
secs = []
for i in range(nsec):
    b = so + i * 40
    vsz, va, rsz, ra = struct.unpack_from('<IIII', f, b + 8)
    secs.append((va, vsz, ra))

def r2o(r):
    if r == 0: return None
    for va, vsz, ra in secs:
        if va <= r < va + vsz: return ra + (r - va)
    return None

print(f'delay-load dir rva={rva:#x} size={sz:#x} imagebase={imgbase:#x}')
o = r2o(rva)
if o is None:
    print('no delay-load dir'); sys.exit(0)
i = 0
found = False
while i < 64:
    b = o + i * 64
    grattrs, = struct.unpack_from('<I', f, b)
    szname, pmod, piat, pint = struct.unpack_from('<QQQQ', f, b + 8)
    if szname == 0 and piat == 0:
        break
    if not (grattrs & 1):  # RVA-based
        no = r2o(szname)
        dllname = f[no:].split(b'\x00')[0].decode(errors='replace') if no else '?'
        io = r2o(piat); to = r2o(pint)
        k = 0
        while io and to and k < 512:
            tv, = struct.unpack_from('<Q', f, to + k * 8)
            if tv == 0: break
            if tv & 0x8000000000000000:
                nm = f'Ordinal{tv & 0xffff}'
            else:
                fo = r2o(tv & 0x7fffffffffffffff)
                nm = f[fo + 2:].split(b'\x00')[0].decode(errors='replace') if fo else '?'
            slotva = imgbase + piat + k * 8
            mark = '  <<< TARGET' if slotva == target else ''
            if slotva == target:
                print(f'TARGET: {dllname}!{nm} (delay-load, slot {slotva:#x})')
                found = True
            if k < 6 or slotva == target:
                print(f'  {dllname}!{nm} slot={slotva:#x}{mark}')
            k += 1
        print(f'{dllname}: {k} delay-load imports, IAT@{imgbase+piat:#x}')
    else:
        print(f'(VA-based delay descriptor grattrs={grattrs:#x} szname={szname:#x})')
    i += 1
if not found:
    print('target slot NOT found in delay-load descriptors')
