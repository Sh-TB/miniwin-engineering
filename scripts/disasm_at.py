#!/usr/bin/env python3
"""disasm_at.py <pe> <rva_hex_start> <len> [imgbase_hex] — capstone disasm of a PE64 file at RVA.

Resolves RVA -> file offset via the section table (pefile-free minimal parser).
"""
import struct, sys, capstone

def main():
    path, rva = sys.argv[1], int(sys.argv[2], 16)
    ln = int(sys.argv[3], 0)
    pe = open(path, 'rb').read()
    e_lfanew = struct.unpack_from('<I', pe, 0x3C)[0]
    nsec = struct.unpack_from('<H', pe, e_lfanew + 6)[0]
    opt = e_lfanew + 24
    sec0 = opt + struct.unpack_from('<H', pe, e_lfanew + 20)[0]
    off = None
    for i in range(nsec):
        s = sec0 + i * 40
        name = pe[s:s+8].rstrip(b'\0').decode(errors='replace')
        vsz, va = struct.unpack_from('<II', pe, s + 8)
        rsz, ro = struct.unpack_from('<II', pe, s + 16)
        vsz2 = max(vsz, rsz)
        if va <= rva < va + vsz2:
            off = ro + (rva - va)
            print(f"; section {name} rva={rva:#x} off={off:#x}")
            break
    if off is None:
        print("RVA not in any section"); return 2
    code = pe[off:off+ln]
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    md.detail = False
    for ins in md.disasm(code, rva):
        print(f"{ins.address:08x}  {ins.bytes.hex():<20} {ins.mnemonic} {ins.op_str}")
    return 0

if __name__ == '__main__':
    sys.exit(main() or 0)
