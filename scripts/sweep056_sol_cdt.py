#!/usr/bin/env python3
"""SWEEP-056 Train 4 (SW-1277): decode sol.exe's cdtInit metric contract.
Worklist: sol's CreateWindow w/h are COMPUTED from game layout globals fed by
cards.dll!cdtInit; our window renders 152x64. Find the call site and the math."""
import pefile
from capstone import *

pe = pefile.PE("artifacts/sweep056/guests/sol/sol.exe")
base = pe.OPTIONAL_HEADER.ImageBase
print(f"sol base=0x{base:x}")

# find cdtInit IAT slot
slot = None
for d in pe.DIRECTORY_ENTRY_IMPORT:
    if b"CARDS" in d.dll.upper():
        for imp in d.imports:
            if imp.name and imp.name.decode() == "cdtInit":
                slot = imp.address
                print(f"cdtInit IAT slot 0x{slot:x}")
if slot is None:
    raise SystemExit("no cdtInit import")

# scan .text for calls/rip-rel refs to the slot
import struct
for s in pe.sections:
    if b".text" in s.Name:
        text = s
data = text.get_data()
tva = base + text.VirtualAddress
md = Cs(CS_ARCH_X86, CS_MODE_64)
hits = []
for i in range(len(data) - 6):
    op = data[i]
    if op in (0xE8, 0xE9):
        continue  # direct call unlikely (IAT indirect)
    # FF 15 disp32 = call qword [rip+d]
    if op == 0xFF and data[i+1] == 0x15:
        rel = struct.unpack_from("<i", data, i + 2)[0]
        if tva + i + 6 + rel == slot:
            hits.append(("call", tva + i))
    if op in (0x48, 0x4C) and data[i+1] == 0x8B:
        modrm = data[i+2]
        if (modrm & 0xC7) == 0x05:  # mov r64, [rip+d]
            rel = struct.unpack_from("<i", data, i + 3)[0]
            if tva + i + 7 + rel == slot:
                hits.append(("mov", tva + i))
for kind, va in hits:
    print(f"\n=== {kind} at 0x{va:x} ===")
    for ins in md.disasm(read := pe.get_data(va - base, 90), va):
        print(f"  0x{ins.address:x}: {ins.mnemonic} {ins.op_str}")
        if ins.address > va + 70:
            break
