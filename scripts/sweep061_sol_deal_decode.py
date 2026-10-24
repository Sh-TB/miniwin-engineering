#!/usr/bin/env python3
"""SW-1285: decode sol.exe deal chain — why the card-laying loop never draws.
Trace the writers of [game+0x70] (slot count) and decode fn(8) 0x100007CF0."""
import pefile, struct, sys
from capstone import *

pe = pefile.PE("artifacts/sweep061/guests/sol/sol.exe")
base = pe.OPTIONAL_HEADER.ImageBase
text = None
for s in pe.sections:
    if b".text" in s.Name:
        text = s
data = text.get_data()
tva = base + text.VirtualAddress

md = Cs(CS_ARCH_X86, CS_MODE_64)
md.detail = False

def dis(va, n=24, span=0x80):
    off = va - base - text.VirtualAddress
    code = data[off:off+span]
    out = []
    for ins in md.disasm(code, va):
        out.append(f"  0x{ins.address:x}: {ins.mnemonic} {ins.op_str}")
        if len(out) >= n:
            break
    return "\n".join(out)

print("=== fn(8) 0x100007CF0 (deal dispatcher) ===")
print(dis(0x100007CF0, 30, 0x120))

# find writers of offset 0x70 with game obj in a register: "mov QWORD [r??+0x70], ..."
# and the game object global [0x10000c298] readers
print("\n=== scan: stores to +0x70 (C7 /r or 48 C7) ===")
import re
for m in re.finditer(rb"\x48\xc7\x40\x70|\x48\xc7\x41\x70|\x48\xc7\x42\x70|\x48\xc7\x43\x70|\x48\xc7\x46\x70|\x48\xc7\x47\x70|\xc7\x40\x70|\xc7\x41\x70|\xc7\x42\x70|\xc7\x43\x70|\xc7\x46\x70|\xc7\x47\x70", data):
    va = tva + m.start()
    print(f"hit @ 0x{va:x}:")
    print(dis(va - 0x20, 6, 0x60))
    print()

print("=== game obj global [0x10000c298] refs (48 8b 05/0d/15/1d with rel to c298) ===")
# rip-relative lea/mov reaching 0x10000c298: compute for each '48 8b 05/0d/15/1d/35/3d' site
for m in re.finditer(rb"\x48\x8b[\x05\x0d\x15\x1d\x2d\x35\x3d]", data):
    i = m.start()
    if i + 6 > len(data):
        continue
    rel = struct.unpack("<i", data[i+2:i+6])[0]
    target = tva + i + 6 + rel
    if target == 0x10000c298:
        va = tva + i
        print(f"read of game-ptr @ 0x{va:x}")
