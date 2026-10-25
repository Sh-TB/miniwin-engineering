#!/usr/bin/env python3.13
"""SW-1296 TASK1b: disassemble UPX stub tail around 0x414340-0x4143A0 + full imports."""
import pefile, capstone
from capstone import CS_ARCH_X86, CS_MODE_32
from capstone.x86 import *

EXE = "/home/z/my-project/workspace/miniwin-engineering/artifacts/sweep062/x/driverview/DriverView.exe"
pe = pefile.PE(EXE)
base = pe.OPTIONAL_HEADER.ImageBase

iat_map = {}
for entry in pe.DIRECTORY_ENTRY_IMPORT:
    dll = entry.dll.decode(errors="replace")
    for imp in entry.imports:
        nm = imp.name.decode(errors="replace") if imp.name else f"#{imp.ordinal}"
        iat_map[imp.address] = (dll, nm)

print("=== FULL import table (13) ===")
for entry in pe.DIRECTORY_ENTRY_IMPORT:
    for imp in entry.imports:
        nm = imp.name.decode(errors="replace") if imp.name else f"Ordinal_{imp.ordinal}"
        print(f"  IAT 0x{imp.address:08X}  {entry.dll.decode(errors='replace'):14s} {nm}")

md = capstone.Cs(CS_ARCH_X86, CS_MODE_32); md.detail = True

def dis(va, n, label):
    print(f"\n=== {label} (VA 0x{va:08X}, {n} bytes) ===")
    code = pe.get_data(va - base, n)
    for insn in md.disasm(code, va):
        ann = ""
        for op in insn.operands:
            if op.type == X86_OP_MEM and op.mem.base == 0 and op.mem.index == 0:
                t = op.mem.disp & 0xFFFFFFFF
                if t in iat_map:
                    ann = f"   ; IAT -> {iat_map[t][0]}!{iat_map[t][1]}"
        print(f"0x{insn.address:08X}: {' '.join(f'{b:02x}' for b in insn.bytes):20s} {insn.mnemonic:7s} {insn.op_str}{ann}")

dis(0x414330, 0x70, "UPX stub tail (import-resolution loop + handoff)")
dis(0x414250, 0x40, "UPX stub head (EP)")

# strings near 0x4163d1 (the LoadLibraryA arg) and in UPX1 data region
print("\n=== strings @ 0x416380..0x416460 ===")
d = pe.get_data(0x16380, 0xE0)
import re
off = 0
for m in re.finditer(rb"[\x20-\x7e]{3,}", d):
    print(f"  0x{0x416380+m.start():08X}: {m.group().decode()}")

# read raw IAT slot values (as file image) for both dynamic slots
for va, name in ((0x416364, "LoadLibraryA"), (0x41636C, "GetProcAddress")):
    print(f"\nfile-image IAT 0x{va:08X} ({name}) = 0x{int.from_bytes(pe.get_data(va-base,4),'little'):08X}")
