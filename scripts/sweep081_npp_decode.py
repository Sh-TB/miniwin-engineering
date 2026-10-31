#!/usr/bin/env python3
"""SWEEP-081 SW-1298-x64 decode: disassemble npp64 around crash RVA 0x1099b3."""
import struct
from capstone import Cs, CS_ARCH_X86, CS_MODE_64

b = open("artifacts/sweep080/npp64/notepad++.exe", "rb").read()
pe = struct.unpack_from("<I", b, 0x3C)[0]
nsec = struct.unpack_from("<H", b, pe + 6)[0]
optsz = struct.unpack_from("<H", b, pe + 20)[0]
sec0 = pe + 24 + optsz
def r2o(rva):
    for i in range(nsec):
        s = sec0 + i * 40
        vsz, va = struct.unpack_from("<II", b, s + 8)
        rawsz, raw = struct.unpack_from("<II", b, s + 16)
        if va <= rva < va + max(vsz, rawsz):
            return raw + (rva - va)
    return None

CRASH = 0x1099b3
start = CRASH - 0x60
o = r2o(start)
md = Cs(CS_ARCH_X86, CS_MODE_64)
code = b[o:o + 0xA0]
for ins in md.disasm(code, start):
    mark = " <<<< CRASH" if ins.address == CRASH else ""
    print(f"{ins.address:#x}  {ins.mnemonic} {ins.op_str}{mark}")
