#!/usr/bin/env python3
"""SW-1277 part 2: wide scan for ANY rip-relative ref to cdtInit IAT slot,
plus FF 25 thunks, then decode around each hit."""
import pefile, struct
from capstone import *

pe = pefile.PE("artifacts/sweep056/guests/sol/sol.exe")
base = pe.OPTIONAL_HEADER.ImageBase
slot = 0x100001048

sec = None
for s in pe.sections:
    if b".text" in s.Name:
        sec = s
data = sec.get_data()
tva = base + sec.VirtualAddress

hits = []
# any 4-byte window that resolves as rip-rel disp to slot: check every position
# with the two most common encodings and also brute-force: for i, treat
# data[i+2:i+6] (FF15) or data[i+3:i+7] (48 8B 05) as disp.
for i in range(len(data) - 6):
    if data[i] == 0xFF and data[i+1] in (0x15, 0x25):
        rel = struct.unpack_from("<i", data, i + 2)[0]
        if tva + i + 6 + rel == slot:
            hits.append((tva + i, "FF15/25"))
    if data[i] == 0xFF and data[i+1] == 0x14 and data[i+2] == 0x25:
        # absolute indirect call [abs32]
        abs32 = struct.unpack_from("<I", data, i + 3)[0]
        if abs32 == slot:
            hits.append((tva + i, "call [abs32]"))
    # 64-bit mov with REX
    if data[i] in (0x48, 0x4C) and data[i+1] == 0x8B and (data[i+2] & 0xC7) == 0x05:
        rel = struct.unpack_from("<i", data, i + 3)[0]
        if tva + i + 7 + rel == slot:
            hits.append((tva + i, "mov riprel"))
    # lea
    if data[i] in (0x48, 0x4C) and data[i+1] == 0x8D and (data[i+2] & 0xC7) == 0x05:
        rel = struct.unpack_from("<i", data, i + 3)[0]
        if tva + i + 7 + rel == slot:
            hits.append((tva + i, "lea riprel"))

print("hits:", [(hex(a), k) for a, k in hits])
md = Cs(CS_ARCH_X86, CS_MODE_64)
for va, kind in hits:
    print(f"\n=== 0x{va:x} ({kind}) ===")
    start = max(0, va - 0x40)
    for ins in md.disasm(pe.get_data(start - base, 0xA0), start):
        print(f"  0x{ins.address:x}: {ins.mnemonic} {ins.op_str}")
        if ins.address >= va + 0x60:
            break
