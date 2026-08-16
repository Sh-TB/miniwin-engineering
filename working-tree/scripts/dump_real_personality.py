#!/usr/bin/env python3
"""Dump the REAL personality functions with correct targets."""
import struct

PATH = 'samples/upx_decompressed.exe'
sections = []
with open(PATH, 'rb') as f:
    f.seek(0x3C)
    lfanew = struct.unpack('<I', f.read(4))[0]
    f.seek(lfanew + 4)
    coff = f.read(20)
    num_sections = struct.unpack('<H', coff[2:4])[0]
    opt_size = struct.unpack('<H', coff[16:18])[0]
    sec_start = lfanew + 24 + opt_size
    for i in range(num_sections):
        f.seek(sec_start + i * 40)
        sec = f.read(40)
        vsize = struct.unpack('<I', sec[8:12])[0]
        vaddr = struct.unpack('<I', sec[12:16])[0]
        rawptr = struct.unpack('<I', sec[20:24])[0]
        sections.append((vaddr, vsize, rawptr))

def rva_to_offset(rva):
    for va, vs, rp in sections:
        if va <= rva < va + vs:
            return rp + (rva - va)
    return None

# Wrapper at 0xe0220:
#   0xe0230: e8 0b d1 fb ff  call target
#   Target = 0xe0235 + (int32)0xfffffb0b = 0xe0235 - 0x4f5 = 0xddf40
# LEA loads 0xafdb0 (stored as param)

# So the SEH wrapper is at 0xddf40, real GCC personality at 0xafdb0
for rva, size, label in [
    (0xe0220, 16, 'SEH handler reg (0xe0220)'),
    (0xddf40, 256, 'SEH personality wrapper (0xddf40)'),
    (0xafdb0, 256, 'real GCC __gxx_personality_v0 (0xafdb0)'),
]:
    off = rva_to_offset(rva)
    if off:
        with open(PATH, 'rb') as f:
            f.seek(off)
            data = f.read(size)
        print(f'=== {label} ===')
        for i in range(0, len(data), 16):
            chunk = data[i:i+16]
            hex_str = ' '.join(f'{b:02x}' for b in chunk)
            print(f'  {rva+i:06x}: {hex_str}')
        print()
    else:
        print(f'=== {label} @ 0x{rva:x} — NOT MAPPED ===')
        print()