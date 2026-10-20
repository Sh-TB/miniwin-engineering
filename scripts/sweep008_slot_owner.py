#!/usr/bin/env python3
"""Find which standard import slot owns a given VA, and whether it's a stub.
Usage: sweep008_slot_owner.py <exe> <slotVA> [loader_stderr]"""
import struct, sys

exe, target = sys.argv[1], int(sys.argv[2], 16)
f = open(exe, 'rb').read()
pe = struct.unpack_from('<I', f, 0x3c)[0]
opt = pe + 24
imgbase = struct.unpack_from('<Q', f, opt + 24)[0]
ddoff = opt + 112
irva, isz = struct.unpack_from('<II', f, ddoff + 1 * 8)
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

print(f'imagebase={imgbase:#x} import dir rva={irva:#x}')
d = r2o(irva)
i = 0
while True:
    oft, tds, fwc, namer, fthunk = struct.unpack_from('<IIIII', f, d + i * 20)
    if namer == 0 and fthunk == 0: break
    no = r2o(namer)
    dll = f[no:].split(b'\x00')[0].decode(errors='replace') if no else '?'
    thunk = oft if oft else fthunk
    to = r2o(thunk)
    k = 0
    while to and k < 4096:
        tv, = struct.unpack_from('<Q', f, to + k * 8)
        if tv == 0: break
        if tv & 0x8000000000000000:
            nm = f'Ordinal{tv & 0xffff}'
        else:
            fo = r2o(tv & 0x7fffffffffffffff)
            nm = f[fo + 2:].split(b'\x00')[0].decode(errors='replace') if fo else '?'
        slotva = imgbase + fthunk + k * 8
        if slotva == target:
            print(f'>>> OWNER: {dll}!{nm}  slot={slotva:#x} (FirstThunk idx {k})')
            sys.exit(0)
        k += 1
    # range report
    if r2o(fthunk):
        print(f'{dll}: slots {imgbase+fthunk:#x}..{imgbase+fthunk+k*8:#x} ({k})')
    i += 1
print('target not inside any standard import IAT range')
