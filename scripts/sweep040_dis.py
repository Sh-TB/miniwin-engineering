#!/usr/bin/env python3
"""SWEEP-040 SW-1206: disassemble adexplorer64.exe around crash RVA 0x1da85
(RAX=0 write: mov [rax+8],esi) and find the enclosing function + callers."""
import struct, subprocess

EXE = 'artifacts/sweep50/dl/adexplorer64/adexplorer64.exe'
IB = 0x140000000
CRASH = IB + 0x1DA85
data = open(EXE, 'rb').read()

e_lfanew = struct.unpack_from('<I', data, 0x3c)[0]
nsec = struct.unpack_from('<H', data, e_lfanew + 6)[0]
opt_size = struct.unpack_from('<H', data, e_lfanew + 20)[0]
sec0 = e_lfanew + 24 + opt_size
secs = []
for i in range(nsec):
    off = sec0 + 40 * i
    name = data[off:off+8].rstrip(b'\0').decode()
    vaddr = struct.unpack_from('<I', data, off+12)[0]
    vsize = struct.unpack_from('<I', data, off+8)[0]
    rawsz = struct.unpack_from('<I', data, off+16)[0]
    rawptr = struct.unpack_from('<I', data, off+20)[0]
    secs.append((name, vaddr, vsize, rawptr, rawsz))
    print(f"sec {name:8s} va={vaddr:#x} vsz={vsize:#x} raw={rawptr:#x} rsz={rawsz:#x}")

def rva2off(rva):
    for name, va, vsz, rp, rsz in secs:
        if va <= rva < va + max(vsz, rsz):
            return rp + (rva - va)
    return None

def dis(lo_rva, hi_rva, mark=None):
    out = subprocess.run(
        ['objdump', '-d', '-M', 'intel',
         '--start-address', hex(IB + lo_rva), '--stop-address', hex(IB + hi_rva), EXE],
        capture_output=True, text=True).stdout
    for line in out.splitlines():
        if line.strip().startswith(IB + lo_rva and f'{IB+lo_rva:x}' or '') or True:
            if mark and f'{mark:x}:' in line:
                line += '   <<<< CRASH (mov [rax+8],esi with RAX=0)'
            print(line)

print(f"\n--- disasm around crash {CRASH:#x} (RVA {CRASH-IB:#x}) ---")
dis(0x1da20, 0x1dae0, mark=CRASH)
