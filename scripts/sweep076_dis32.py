#!/usr/bin/env python3
"""Disassemble a window of an x86 PE around a given RVA with import annotations."""
import pefile, capstone, sys

path, rva, back, fwd = sys.argv[1], int(sys.argv[2], 0), int(sys.argv[3]) if len(sys.argv) > 3 else 32, int(sys.argv[4]) if len(sys.argv) > 4 else 32
pe = pefile.PE(path, fast_load=True)
pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_IMPORT']])
ib = pe.OPTIONAL_HEADER.ImageBase
iat = {}
for d in getattr(pe, 'DIRECTORY_ENTRY_IMPORT', []):
    for imp in d.imports:
        if imp.address:
            iat[imp.address - ib] = (d.dll.decode(), imp.name.decode() if imp.name else f"ord{imp.ordinal}")
data = pe.get_data(rva - back, back + fwd)
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
for ins in md.disasm(data, ib + rva - back):
    cur = ins.address - ib
    note = ""
    if ins.mnemonic in ('call', 'jmp') and ins.operands and ins.operands[0].type == capstone.x86.X86_OP_MEM:
        m = ins.operands[0].mem
        if m.base == 0 and m.index == 0:
            t = m.disp & 0xffffffff
            if t in iat: note = f"  ; [{t:#x}] -> {iat[t][0]}!{iat[t][1]}"
    if ins.mnemonic in ('call', 'jmp') and ins.operands and ins.operands[0].type == capstone.x86.X86_OP_IMM:
        t = ins.operands[0].imm - ib
        note = f"  ; -> {t:#x}"
    tag = " <<<<" if abs(cur - rva) < 2 else ""
    print(f"{cur:#010x}  {ins.mnemonic:8s} {ins.op_str}{note}{tag}")
