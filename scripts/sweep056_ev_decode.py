#!/usr/bin/env python3
"""SWEEP-056 Train 3 (SW-1278): decode Everything 1.4.1's internal volume-DB
walker chain. SWEEP-055 left the gate: volume walker 0x1400d24e0 matches the
volume against its OWN database via user-mode checks 0x1400d6df0 / 0x1400d6f30
that fail BEFORE any API call. Decode those two checks to find the engine
contract they need."""
import pefile
from capstone import *

PE = "artifacts/sweep056/guests/everything/everything.exe"
pe = pefile.PE(PE)
base = pe.OPTIONAL_HEADER.ImageBase
print(f"base=0x{base:x} size=0x{pe.OPTIONAL_HEADER.SizeOfImage:x}")

def rva_of(va):
    return va - base

def read(va, n):
    return pe.get_data(rva_of(va), n)

def disas(va, n=90):
    code = read(va, n)
    md = Cs(CS_ARCH_X86, CS_MODE_64)
    md.detail = False
    out = []
    for i in md.disasm(code, va):
        out.append(f"  0x{i.address:x}: {i.mnemonic} {i.op_str}")
        if i.mnemonic in ("ret", "jmp") and len(out) > 4:
            break
    return "\n".join(out)

for name, va in [("walker 0x1400d24e0", 0x1400d24e0),
                 ("check-a 0x1400d6df0", 0x1400d6df0),
                 ("check-b 0x1400d6f30", 0x1400d6f30)]:
    print(f"\n=== {name} ===")
    print(disas(va))
