#!/usr/bin/env python3
"""SWEEP-039 SW-1102 static decode: find the embedded .NET CLR counter-name
strings in procexp64.exe, then xref code that references them (rip-relative
lea/mov) to locate the copy loop and its enclosing frame size."""
import struct, subprocess

EXE = 'artifacts/sweep50/dl/procexp64/procexp64.exe'
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
    vsize, vaddr, rsize, raddr = struct.unpack_from('<IIII', data, off + 8)
    secs.append((name, vaddr, vsize, raddr, rsize))

def off2va(off):
    for name, va, vs, ra, rs in secs:
        if ra <= off < ra + rs:
            return IB + va + (off - ra)
    return None

def off2sec(off):
    for name, va, vs, ra, rs in secs:
        if ra <= off < ra + rs:
            return name
    return '?'

def rva2off(rva):
    for name, va, vs, ra, rs in secs:
        if va <= rva < va + max(vs, rs):
            return ra + (rva - va)
    return None

# --- 1. locate UTF-16LE strings ---
probes = ['# bytes in all heaps', '# Gen 0 collections', 'Total # of contentions']
locs = {}
for s in probes:
    pat = s.encode('utf-16-le')
    off = data.find(pat)
    while off != -1:
        va = off2va(off)
        locs.setdefault(s, []).append((off, va))
        off = data.find(pat, off + 1)
for s, ls in locs.items():
    for off, va in ls:
        print(f'{s!r}: file 0x{off:x} sec {off2sec(off)} VA {va:#x}')

# --- 2. xref: scan .text for rip-relative refs to those VAs ---
# lea/mov rip-rel: opcode ... ModRM(mod=00,rm=101) + disp32. Scan every 4-byte
# window in .text; candidate if target VA (next_insn_va + disp32) hits a string.
if '# bytes in all heaps' in locs:
    targets = {va: s for s, ls in locs.items() for off, va in ls}
    tname, tva, tvs, tra, trs = [(n, v, vs, ra, rs) for n, v, vs, ra, rs in secs if n == '.text'][0]
    text = data[tra:tra + tvs]
    print(f'\n--- xref scan over .text ({tvs} bytes) ---')
    hits = 0
    for i in range(len(text) - 4):
        disp = struct.unpack_from('<i', text, i)[0]
        # rip-relative: next instruction address unknown; approximate by trying
        # next-insn = VA(i+4 in section) .. we flag windows where a plausible
        # next-insn (disp end + k for k in 0..7) lands on a target.
        base = IB + tva + i + 4
        for k in range(0, 8):
            if base + k in targets:
                insn_off = i - 2  # rough: disp32 usually ends 0-3 into insn
                if hits < 12:
                    print(f'  .text 0x{IB+tva+i-3:#x} disp {disp:#x} -> {targets[base+k]!r}')
                hits += 1
    print('total candidate windows:', hits)
