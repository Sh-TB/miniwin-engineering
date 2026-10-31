#!/usr/bin/env python3
"""SWEEP-076: decode crash sites in the 8 failing B-group binaries.
For each: disassemble around crash RVA, identify nearest function start,
list import calls nearby to name the failing context."""
import pefile, capstone, sys, struct

CASES = [
    ("U25", "artifacts/sweep074/dl/utils/tcpview64.exe", 0x25b75),
    ("U03", "artifacts/sweep074/dl/x/notepad3-x86/Notepad3.exe", None),
    ("U04", "artifacts/sweep074/dl/x/notepad3-x64/Notepad3.exe", None),
    ("U07", "artifacts/sweep074/dl/x/ahk-v1/AutoHotkeyU32.exe", 0x12b92),  # guest EIP x86
    ("U18", "artifacts/sweep074/dl/x/regscanner-x86/RegScanner.exe", None),
    ("U27", "artifacts/sweep074/dl/x/winlister-x64/winlister.exe", None),
    ("R3",  "artifacts/sweep074/dl/x/lastactivityview/LastActivityView.exe", 0x12b92),
]

def load(path):
    pe = pefile.PE(path, fast_load=True)
    pe.parse_data_directories(
        directories=[pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_IMPORT'],
                     pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_IAT']])
    ib = pe.OPTIONAL_HEADER.ImageBase
    return pe, ib

def iat_map(pe):
    """RVA of IAT slot -> (dll, func)"""
    m = {}
    for d in getattr(pe, 'DIRECTORY_ENTRY_IMPORT', []):
        for imp in d.imports:
            if imp.address:
                m[imp.address - pe.OPTIONAL_HEADER.ImageBase] = (d.dll.decode(), imp.name.decode() if imp.name else f"ord{imp.ordinal}")
    return m

def func_start(pe, rva):
    """nearest function boundary viapdata or 16-align heuristic"""
    sec = pe.get_rva_profile = None
    for s in pe.sections:
        if s.VirtualAddress <= rva < s.VirtualAddress + max(s.Misc_VirtualSize, s.SizeOfRawData):
            return s.Name.decode().rstrip('\0')
    return "?"

def disasm_win(pe, path, rva, back=96, fwd=80, arch64=True):
    data_rva = max(0, rva - back)
    try:
        data = pe.get_data(data_rva, back + fwd)
    except Exception as e:
        print(f"  [no data at RVA {rva:#x}: {e}]")
        return
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64 if arch64 else capstone.CS_MODE_32)
    md.detail = True
    iat = iat_map(pe)
    ib = pe.OPTIONAL_HEADER.ImageBase
    for ins in md.disasm(data, ib + data_rva):
        cur = ins.address - ib
        tag = " <<<< CRASH" if abs(cur - rva) < 4 else ""
        note = ""
        # rip-relative IAT resolution
        if ins.mnemonic == 'call' and 'rip' in ins.op_str and len(ins.operands) == 1 and ins.operands[0].type == capstone.x86.X86_OP_MEM:
            m = ins.operands[0].mem
            tgt = ins.address + ins.size + m.disp
            trva = tgt - ib
            if trva in iat:
                note = f"  ; -> {iat[trva][0]}!{iat[trva][1]}"
            else:
                note = f"  ; -> RVA {trva:#x}"
        elif ins.mnemonic == 'call':
            try:
                t = int(ins.op_str, 16) - ib
                note = f"  ; -> RVA {t:#x}" + (f" {iat[t][0]}!{iat[t][1]}" if t in iat else "")
            except ValueError:
                pass
        print(f"  {cur:#010x}  {ins.mnemonic:8s} {ins.op_str}{note}{tag}")

for tid, path, rva in CASES:
    print(f"\n===== {tid} {path} =====")
    pe, ib = load(path)
    m64 = pe.FILE_HEADER.Machine == 0x8664
    print(f"  machine={'x64' if m64 else 'x86'} base={ib:#x} ep={pe.OPTIONAL_HEADER.AddressOfEntryPoint:#x} sec={func_start(pe, rva or pe.OPTIONAL_HEADER.AddressOfEntryPoint)}")
    if rva:
        disasm_win(pe, path, rva, arch64=m64)
