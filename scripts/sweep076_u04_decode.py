#!/usr/bin/env python3
"""SWEEP-076 U04: decode comctl32!#381 call sites + the 0x1401b2d50 vcall crash."""
import pefile, capstone

path = 'artifacts/sweep074/dl/x/notepad3-x64/Notepad3.exe'
pe = pefile.PE(path)
ib = pe.OPTIONAL_HEADER.ImageBase
iat = {}
slot_to = {}
for d in getattr(pe, 'DIRECTORY_ENTRY_IMPORT', []):
    for imp in d.imports:
        if imp.address:
            rva = imp.address - ib
            nm = imp.name.decode() if imp.name else f"ORD{imp.ordinal}"
            iat[rva] = (d.dll.decode(), nm)
            slot_to[(d.dll.decode(), nm)] = rva

raw = None
def sec_read(rva, n):
    global raw
    for s in pe.sections:
        if s.VirtualAddress <= rva < s.VirtualAddress + max(s.Misc_VirtualSize, s.SizeOfRawData):
            fo = s.PointerToRawData + (rva - s.VirtualAddress)
            return open(path,'rb').read()[fo:fo+n] if raw is None else raw[fo:fo+n]
    return b''

md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)

def dis(rva, back=40, fwd=30, note_crash=False):
    data = sec_read(rva-back, back+fwd)
    out = []
    for ins in md.disasm(data, ib+rva-back):
        cur = ins.address - ib
        note = ""
        if ins.mnemonic in ('call','jmp'):
            try:
                t = int(ins.op_str, 16) - ib
                if t in iat: note = f"  ; {iat[t][0]}!{iat[t][1]}"
            except ValueError: pass
        tag = " <<<<" if (note_crash and abs(cur-rva)<2) else ""
        out.append(f"{cur:#010x}  {ins.mnemonic:8s} {ins.op_str}{note}{tag}")
    return out

# find callers of comctl32 #381
slot = slot_to.get(('COMCTL32.dll', 'ORD381'))
print(f"#381 IAT slot RVA = {slot:#x}")
for rip in (0x170ea9, 0x170f00, 0x170f58, 0x1b2d50):
    print(f"\n--- around {rip:#x} {'(CRASH)' if rip==0x1b2d50 else ''} ---")
    for l in dis(rip): print(l)
