#!/usr/bin/env python3
"""SWEEP-043 SW-1102e/g consolidated static decode: the procexp64 BUG-078
'ntio' crash chain. Part 1 dumps the .rdata/.data string tables; part 2
decodes the crash-stack call sites; part 3 walks the import directory for the
IAT slots used by the packing frame 0x1400a8a30. Findings (2026-10-22):
  - .data 0x1401887c0/8a08/c50/e98/90e0 = registry root path prefixes (NOT
    CLR names — the SWEEP-041 'CLR perf-counter master init' label on
    0x1400c3340 was wrong; it is registry-path canonicalization)
  - .rdata 0x14015ad10/40/70 = 'Total # of contentions' / '# bytes in all
    heaps' / '# Gen 0 collections' (0x30-stride string pool, continues into
    the Exctrlst error message and the comma-joined category list)
  - crash call chain: 0x1400c3340 -> 0x1400a1380 -> 0x1400a8a30 (3 pushes +
    sub 0x250; RegQueryValueExW two-pass; tokenizer L","; formatter '%u '/'%s ')
  - the RIP='ntio' crash itself = mw_IsBadStringPtrW's own 1024-into-512
    engine-stack overflow (fixed SW-1102g), NOT a guest copy loop
Usage: python3 scripts/sweep043_dis.py [exe]"""
import struct, subprocess, tempfile, os, sys

EXE = sys.argv[1] if len(sys.argv) > 1 else 'artifacts/sweep50/dl/procexp64/procexp64.exe'
IB = 0x140000000
data = open(EXE, 'rb').read()
e_lfanew = struct.unpack_from('<I', data, 0x3c)[0]
nsec = struct.unpack_from('<H', data, e_lfanew + 6)[0]
opt_size = struct.unpack_from('<H', data, e_lfanew + 20)[0]
sec0 = e_lfanew + 24 + opt_size
secs = []
for i in range(nsec):
    off = sec0 + 40 * i
    name = data[off:off+8].rstrip(b'\0').decode()
    vaddr, vsize = struct.unpack_from('<I', data, off+12)[0], struct.unpack_from('<I', data, off+8)[0]
    rawsz, rawptr = struct.unpack_from('<I', data, off+16)[0], struct.unpack_from('<I', data, off+20)[0]
    secs.append((name, vaddr, vsize, rawptr, rawsz))

def rva2off(rva):
    for name, va, vsz, rp, rsz in secs:
        if va <= rva < va + max(vsz, rsz) and rva - va < rsz:
            return rp + (rva - va)
    return None

def wstr(va, maxc=120):
    off = rva2off(va - IB)
    if off is None: return None
    out = []
    for i in range(maxc):
        c = struct.unpack_from('<H', data, off + 2*i)[0]
        if c == 0: break
        out.append(chr(c) if 0x20 <= c < 0x7f else f'\\x{c:04x}')
    return ''.join(out)

def astr(va, maxc=160):
    off = rva2off(va - IB)
    if off is None: return None
    out = []
    for i in range(maxc):
        c = data[off+i]
        if c == 0: break
        out.append(chr(c) if 0x20 <= c < 0x7f else f'\\x{c:02x}')
    return ''.join(out)

print('== A. .data wide-string tables (registry roots) ==')
for t in [0x1401887c0, 0x140188a08, 0x140188c50, 0x140188e98, 0x1401890e0]:
    print(f'  {t:#x} "{wstr(t)}"')

print('\n== A2. .rdata CLR counter name pool (0x30 stride) ==')
for k in range(16):
    t = 0x14015ad10 + 0x30 * k
    s = wstr(t, 48)
    if s is None: break
    print(f'  name[{k:2d}] {t:#x} "{s}"')
print(f'  key 0x14015aa48 "{wstr(0x14015aa48, 32)}"')

print('\n== B. crash-stack call sites ==')
for ra in (0x140066367, 0x1400a13fd, 0x1400c36a6):
    off = rva2off(ra - IB)
    for back in (5, 6):
        b = data[off - back:off]
        if b[0] == 0xE8:
            rel = struct.unpack('<i', b[1:5])[0]
            print(f'  RA {ra:#x}: E8 call @ {ra-back:#x} -> callee {ra+rel:#x}')
            break
        if b[0] == 0xFF and b[1] == 0x15:
            slot = struct.unpack('<I', b[2:6])[0]
            print(f'  RA {ra:#x}: FF15 IAT call @ {ra-back:#x} -> slot {ra+slot:#x}')
            break

print('\n== C. IAT slot decode ==')
opt = e_lfanew + 24
dd = opt + 112
imp_rva = struct.unpack_from('<I', data, dd + 8)[0]
SLOTS = [0x140134018, 0x140134850, 0x140134ae8]
desc = imp_rva
while True:
    do = rva2off(desc)
    if do is None: break
    oft, ts, fc, namerva, ft = struct.unpack_from('<IIIII', data, do)
    if oft == 0 and ft == 0: break
    dll = astr(IB + namerva, 48)
    ta = oft if oft else ft
    idx = 0
    while True:
        to = rva2off(ta + 8 * idx)
        if to is None: break
        th = struct.unpack_from('<Q', data, to)[0]
        if th == 0: break
        sv = ft + 8 * idx + IB
        if sv in SLOTS:
            if th & (1 << 63):
                print(f'  {sv:#x} ({dll}) ordinal {th & 0xffff}')
            else:
                ho = rva2off(th & 0x7fffffff)
                nm = data[ho+2:ho+2+80].split(b'\0')[0].decode(errors='replace')
                print(f'  {sv:#x} ({dll}) {nm}')
        idx += 1
    desc += 20
