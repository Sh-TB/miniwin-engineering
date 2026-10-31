#!/usr/bin/env python3
"""SW-1279: find sol.exe's WndProc switch. Scan .text for imm32 compares of
WM_COMMAND (0x111), the menu-deal id 0x3e8 (1000), and 0x65/0x66 menu ids;
decode around the strongest hit."""
import pefile, struct
from capstone import *

pe = pefile.PE("artifacts/sweep056/guests/sol/sol.exe")
base = pe.OPTIONAL_HEADER.ImageBase
for s in pe.sections:
    if b".text" in s.Name:
        sec = s
data = sec.get_data()
tva = base + sec.VirtualAddress

def scan_imm32(val):
    b = struct.pack("<I", val)
    hits = []
    idx = 0
    while len(hits) < 12:
        i = data.find(b, idx)
        if i < 0:
            break
        op = data[i - 1]
        if op in (0x81, 0x3d, 0x25, 0xf7, 0xc7):
            hits.append(i - 1)
        idx = i + 1
    return hits

md = Cs(CS_ARCH_X86, CS_MODE_64)
for val, label in [(0x111, "WM_COMMAND"), (0x3e8, "ID 1000"), (0x3ec, "ID 1004")]:
    hits = scan_imm32(val)
    print(f"=== {label} (0x{val:x}) hits: {[hex(tva+h) for h in hits]}")
    for h in hits[:2]:
        va = tva + h
        start = va - 0x20
        print(f"--- context @ 0x{va:x} ---")
        for ins in md.disasm(pe.get_data(start - base, 0x50), start):
            print(f"  0x{ins.address:x}: {ins.mnemonic} {ins.op_str}")
            if ins.address > va + 0x30:
                break
