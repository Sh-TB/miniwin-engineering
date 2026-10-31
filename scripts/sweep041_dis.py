#!/usr/bin/env python3
"""SWEEP-041 SW-1102d: static hunt for the 32-byte-stride UTF-16 name-record
writer loop (procexp64 BUG-078 — crash RIP = 'ntio' of 'contentions').

Facts carried from SWEEP-039/040:
  - embedded CLR perf-counter names at .rdata 0x14015ad10/40/70
  - name-table search loop at 0x1400a0486 (r15={count,array} via call 0x1401025d8)
  - crash stack: 32-byte name records with 8-byte gaps, RBP=0
This script disassembles backwards from each plausible return address on the
crash stack (found in the SWEEP-041 auto-decoded crash dump) to identify the
enclosing functions, then scans each for stack-store loops with a 0x20 stride
or wide-char stores ([rsp+N] with 2-byte granules).
"""
import struct, subprocess, tempfile, os

EXE = 'artifacts/sweep50/dl/procexp64/procexp64.exe'
IB = 0x140000000
RAS = [0x140066367, 0x1400a13fd, 0x1400c36a6]  # .text-resident only

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
        if va <= rva < va + max(vsz, rsz):
            return rp + (rva - va)
    return None

def disasm(va_start, nbytes, label):
    off = rva2off(va_start - IB)
    if off is None:
        print(f'{label}: no file backing'); return
    blob = data[off:off+nbytes]
    with tempfile.NamedTemporaryFile(delete=False) as f:
        f.write(blob); tmp = f.name
    out = subprocess.run(['objdump', '-D', '-b', 'binary', '-m', 'i386:x86-64',
                          '--adjust-vma=%#x' % va_start, tmp],
                         capture_output=True, text=True).stdout
    os.unlink(tmp)
    print(f'--- {label} ---')
    print(out)

# 1. Walk back from each RA to find the preceding function start (int3 padding
#    or CC alignment runs), then disassemble the tail of that function.
for ra in RAS:
    rva = ra - IB
    off = rva2off(rva)
    if off is None:
        continue
    back = data[max(0, off-0x400):off]
    # find last run of >=4 CC/00 bytes (alignment padding) = function boundary
    start_off = max(0, off-0x400)
    boundary = None
    i = len(back) - 2
    while i > 0:
        if back[i] == 0xCC and back[i+1] == 0xCC and i+2 < len(back) and back[i+2] != 0xCC:
            # padding ends at i+2 -> function containing RA starts there
            boundary = start_off + i + 2
            break
        i -= 1
    fstart = (start_off + (boundary - start_off)) if boundary else off - 0x200
    fva = IB + fstart - (off - rva) + rva  # careful: fstart is a file offset
    # convert file offset back to VA
    fva = None
    for name, va, vsz, rp, rsz in secs:
        if rp <= fstart < rp + rsz:
            fva = IB + va + (fstart - rp)
            break
    print(f'RA {ra:#x}: enclosing func candidate start ~{fva:#x}'
          if fva else f'RA {ra:#x}: boundary not mapped')
    if fva:
        disasm(fva, (ra - fva) + 16, f'func tail up to RA {ra:#x}')
