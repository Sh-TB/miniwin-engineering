#!/usr/bin/env python3
"""Round 2: find the counter-name pointer table in .rdata, then xref code to
the TABLE (and per-string direct refs with a wider rip-rel window model)."""
import struct

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
    for n, va, vs, ra, rs in secs:
        if ra <= off < ra + rs:
            return IB + va + (off - ra)
    return None

STRS = [0x14015ad10, 0x14015ad40, 0x14015ad70]  # Total contentions / bytes heaps / Gen 0

# --- 1. pointer table: qwords equal to a string VA ---
print('--- pointer hits (8-byte LE) ---')
tbl_hits = []
for off in range(0, len(data) - 8, 4):
    q = struct.unpack_from('<Q', data, off)[0]
    if q in STRS:
        va = off2va(off)
        tbl_hits.append((off, va, q))
        if len(tbl_hits) <= 10:
            print(f'  file 0x{off:x} VA {va:#x} -> {q:#x}')
# table VA = VA of the first hit that lands in .rdata right before/after others
tva_set = sorted(set(v for _, v, _ in tbl_hits if v))
print('table candidate VAs:', [hex(v) for v in tva_set[:6]])

# --- 2. rip-rel xref to table/string VAs, proper model ---
def rip_xref(target_set, label):
    tname, tva0, tvs, tra, trs = [(n, v, vs, ra, rs) for n, v, vs, ra, rs in secs if n == '.text'][0]
    text = data[tra:tra + tvs]
    print(f'--- rip-rel xref [{label}] over .text ---')
    found = []
    for i in range(len(text) - 4):
        disp = struct.unpack_from('<i', text, i)[0]
        if disp == 0:
            continue
        # next-insn VA = VA of byte i+4 in section; rip-rel target = next + disp.
        # The disp field ENDS at i+4; instruction start unknown, but rip target
        # computed from END of the disp = VA(i+4). Exact for lea/mov with disp32
        # as the LAST displacement.
        tgt = IB + tva0 + (i + 4) + disp
        if tgt in target_set:
            found.append((IB + tva0 + i + 4, tgt, disp))
    for ni, tgt, disp in found[:12]:
        print(f'  insn ends VA {ni:#x} disp {disp:+#x} -> {tgt:#x}')
    print('  hits:', len(found))
    return found

str_set = set(STRS)
tset = set(tva_set)
f1 = rip_xref(str_set, 'string VAs')
f2 = rip_xref(tset, 'table VAs') if tset else []
