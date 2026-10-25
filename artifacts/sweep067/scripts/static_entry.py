#!/usr/bin/env python3.13
"""SW-1296 TASK1: static decode of DriverView.exe entry with capstone."""
import re
import pefile, capstone
from capstone import CS_ARCH_X86, CS_MODE_32
from capstone.x86 import *

EXE = "/home/z/my-project/workspace/miniwin-engineering/artifacts/sweep062/x/driverview/DriverView.exe"
pe = pefile.PE(EXE)
base = pe.OPTIONAL_HEADER.ImageBase
ep = base + pe.OPTIONAL_HEADER.AddressOfEntryPoint
print(f"ImageBase=0x{base:08X} EntryVA=0x{ep:08X} EntryRVA=0x{pe.OPTIONAL_HEADER.AddressOfEntryPoint:X} SizeOfImage=0x{pe.OPTIONAL_HEADER.SizeOfImage:X}")

iat_map = {}
for entry in pe.DIRECTORY_ENTRY_IMPORT:
    dll = entry.dll.decode(errors="replace")
    for imp in entry.imports:
        nm = imp.name.decode(errors="replace") if imp.name else f"ord{imp.ordinal}"
        iat_map[imp.address] = (dll, nm)

print(f"total imports: {sum(len(e.imports) for e in pe.DIRECTORY_ENTRY_IMPORT)} from {len(pe.DIRECTORY_ENTRY_IMPORT)} DLLs")
for entry in pe.DIRECTORY_ENTRY_IMPORT:
    print(f"  {entry.dll.decode(errors='replace'):20s} {len(entry.imports):3d} imports")

print("sections:")
for s in pe.sections:
    print(f"  {s.Name.decode(errors='replace').rstrip(chr(0)):8s} VA=0x{base+s.VirtualAddress:08X} rawsz=0x{s.SizeOfRawData:X} virtsz=0x{s.Misc_VirtualSize:X} exec={bool(s.Characteristics & 0x20000000)}")

def read_at(va, n):
    try:
        return pe.get_data(va - base, n)
    except Exception:
        return b""

md = capstone.Cs(CS_ARCH_X86, CS_MODE_32)
md.detail = True

ENTRY_BYTES = 399
code = read_at(ep, ENTRY_BYTES)
print(f"\n=== entry disassembly (VA 0x{ep:08X}, {len(code)} bytes) ===")
calls = []
count = 0
for insn in md.disasm(code, ep):
    count += 1
    ann = ""
    for op in insn.operands:
        if op.type == X86_OP_MEM and op.mem.base == 0 and op.mem.index == 0:
            tgt = op.mem.disp & 0xFFFFFFFF
            if tgt in iat_map:
                ann = f"   ; -> {iat_map[tgt][0]}!{iat_map[tgt][1]}"
                if insn.mnemonic.startswith("call"):
                    calls.append((insn.address, *iat_map[tgt]))
    print(f"0x{insn.address:08X}: {insn.mnemonic:8s} {insn.op_str}{ann}")
    if insn.mnemonic in ("ret", "retf", "jmp"):
        break
print(f"(decoded {count} instructions)")

print("\n=== ordered import calls reachable from entry (linear scan) ===")
for i, (a, dll, nm) in enumerate(calls, 1):
    print(f"{i:2d}. 0x{a:08X}  {dll}!{nm}")
if not calls:
    print("(none — entry resolves API via dynamic loading or data IAT)")

loads = {va: (d, n) for va, (d, n) in iat_map.items()
         if n.lower() in ("loadlibrarya","loadlibraryw","loadlibraryexa","loadlibraryexw",
                          "getprocaddress","getmodulehandlea","getmodulehandlew","freelibrary")}
print("\n=== dynamic-loading IAT slots ===")
for va, (d, n) in sorted(loads.items()):
    print(f"  IAT 0x{va:08X} = {d}!{n}")

print("\n=== raw byte references to those slots in .text ===")
text = next(s for s in pe.sections if s.Name.decode(errors="replace").rstrip(chr(0)) == ".text")
data = text.get_data()
tva = base + text.VirtualAddress
for va, (d, n) in sorted(loads.items()):
    pat = va.to_bytes(4, "little")
    idxs = []
    off = 0
    while True:
        idx = data.find(pat, off)
        if idx < 0:
            break
        idxs.append(tva + idx)
        off = idx + 1
    if idxs:
        prev = " ".join(f"0x{i-1:08X}" for i in idxs[:12])
        print(f"  {d}!{n} @ IAT 0x{va:08X}: {len(idxs)} refs; insn-start guesses: {prev}")
