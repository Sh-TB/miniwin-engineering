#!/usr/bin/env python3
"""SWEEP-039 SW-1102: disassemble around the procexp64 crash-frame call sites.
PE-tagged stack values from [SCAN]: RAs of surviving frames. The nearest one
above the crash RSP (0x140144bd8 @ rsp+272) is the crashing frame's caller.
Dump bytes around each candidate and find the enclosing function prologue."""
import struct, subprocess, sys

EXE = 'artifacts/sweep50/dl/procexp64/procexp64.exe'
IB = 0x140000000
data = open(EXE, 'rb').read()

# --- PE headers ---
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

def rva2off(rva):
    for name, va, vs, ra, rs in secs:
        if va <= rva < va + max(vs, rs):
            return ra + (rva - va)
    return None

def va2off(va):
    return rva2off(va - IB)

# candidate RAs from [SCAN] (rsp+off, va)
cands = [
    (272,  0x140144bd8),
    (520,  0x140066367),
    (616,  0x1400a13fd),
    (728,  0x1400c36a6),
    (744,  0x1401a1ea0),
    (2544, 0x140191bf0),
]
n = len(data)
for rspoff, va in cands:
    off = va2off(va)
    if off is None or off < 16 or off >= n:
        print(f'== rsp+{rspoff} VA {va:#x}: no file offset')
        continue
    print(f'== rsp+{rspoff} VA {va:#x} (file 0x{off:x}) — call site instruction just before:')
    # the call that produced this RA: disasm 24 bytes ending at VA
    start = off - 24
    end = min(off + 8, n)
    open('/tmp/pe_chunk.bin', 'wb').write(data[start:end])
    base_va = va - 24
    r = subprocess.run(['objdump', '-D', '-b', 'binary', '-m', 'i386:x86-64',
                        '--adjust-vma=%#x' % base_va, '/tmp/pe_chunk.bin'],
                       capture_output=True, text=True)
    lines = r.stdout.splitlines()
    for ln in lines:
        if '\t' in ln and ':' in ln:
            print('   ', ln.split('\t', 1)[1].strip()[:70])
