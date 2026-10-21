#!/usr/bin/env python3
"""Round 3: disassemble around the six xref sites; find enclosing function
prologues (sub rsp, N) to size the frames that hold the name-copy buffer."""
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
    nm = data[off:off+8].rstrip(b'\0').decode()
    vsize, vaddr, rsize, raddr = struct.unpack_from('<IIII', data, off + 8)
    secs.append((nm, vaddr, vsize, raddr, rsize))
TEXT = [(n, v, vs, ra, rs) for n, v, vs, ra, rs in secs if n == '.text'][0]
_, tva, tvs, tra, trs = TEXT

def dis(va_start, va_end):
    off = tra + (va_start - IB - tva)
    n = va_end - va_start
    open('/tmp/chunk.bin', 'wb').write(data[off:off+n])
    r = subprocess.run(['objdump', '-D', '-b', 'binary', '-m', 'i386:x86-64',
                        '--adjust-vma=%#x' % va_start, '/tmp/chunk.bin'],
                       capture_output=True, text=True)
    out = []
    for ln in r.stdout.splitlines():
        if '\t' in ln and ':' in ln:
            addr, ins = ln.split('\t', 1)
            a = addr.strip()[:-1]
            out.append((a, ins.strip()))
    return out

SITES = [0x1400a042b, 0x1400a0486, 0x1400a06b8, 0x1400a0716, 0x1400a0807, 0x1400a0866]
for s in SITES:
    print(f'===== site {s:#x} =====')
    for a, ins in dis(s - 32, s + 48):
        mark = ' <<<' if a == f'{s:x}' else ''
        print(f'  {a}: {ins[:78]}{mark}')
