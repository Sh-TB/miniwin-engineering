#!/usr/bin/env python3
"""SWEEP-056 (SW-1278) part 3: xref scan for the check functions + decode the
DB-entry struct producer. Scan .text for E8/E9 rel32 calls targeting the two
checks and the walker, and rip-relative leas."""
import pefile, struct
from capstone import *

PE = "artifacts/sweep056/guests/everything/everything.exe"
pe = pefile.PE(PE)
base = pe.OPTIONAL_HEADER.ImageBase
text = None
for s in pe.sections:
    if b".text" in s.Name:
        text = s
data = text.get_data()
tva = base + text.VirtualAddress
tsz = len(data)

TARGETS = {0x1400d24e0: "walker", 0x1400d6df0: "check-a", 0x1400d6f30: "check-b"}
print("scanning", hex(tva), "size", hex(tsz))

# scan for rel32 call/jmp
for i in range(tsz - 5):
    op = data[i]
    if op not in (0xE8, 0xE9):
        continue
    rel = struct.unpack_from("<i", data, i + 1)[0]
    tgt = tva + i + 5 + rel
    if tgt in TARGETS:
        print(f"  {TARGETS[tgt]} <- {'call' if op==0xE8 else 'jmp'} at 0x{tva+i:x}")

# scan rip-relative lea reg,[rip+d] referencing the targets
md = Cs(CS_ARCH_X86, CS_MODE_64)
for i in range(0, tsz - 7, 1):
    if data[i] != 0x48 and data[i] != 0x4C:
        continue
    ins = next(md.disasm(bytes(data[i:i+7]), tva + i), None)
    if ins is None or ins.mnemonic != "lea":
        continue
    if "[rip" in ins.op_str:
        # compute target
        import re
        m = re.search(r"\[rip \+ (0x[0-9a-f]+)\]|\[rip - (0x[0-9a-f]+)\]", ins.op_str)
        if m:
            d = int(m.group(1), 16) if m.group(1) else -int(m.group(2), 16)
            t = ins.address + ins.size + d
            if t in TARGETS:
                print(f"  {TARGETS[t]} <- lea at 0x{ins.address:x} ({ins.op_str})")
