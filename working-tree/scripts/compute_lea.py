#!/usr/bin/env python3
"""Compute the LEA target address for the personality function."""
import struct

# Personality at 0xe0220, LEA instruction at offset 4:
# 48 8d 05 85 fb fc ff
# REX.W LEA rax, [rip + disp32]
# Instruction bytes at 0xe0224 (4 bytes into the function)
# Next instruction at 0xe0224 + 7 = 0xe022b

instr_rva = 0xe0224  # LEA instruction start
next_rva = instr_rva + 7  # 0xe022b

# Displacement bytes (LE): 85 fb fc ff
disp_bytes = bytes([0x85, 0xfb, 0xfc, 0xff])
disp = struct.unpack('<i', disp_bytes)[0]  # signed 32-bit

target_rva = next_rva + disp
print(f"LEA instruction at RVA: 0x{instr_rva:x}")
print(f"Next instruction at RVA: 0x{next_rva:x}")
print(f"Displacement (signed): {disp} (0x{disp & 0xffffffff:x})")
print(f"Target RVA: 0x{target_rva:x}")
print(f"Target is in .rdata: {0xe2000 <= target_rva < 0x1f8000}")

# Also check: the personality function loads this value to compare against the exception class
# The exception class observed was: 0x474e5543432b2b00 = 'GNUC\\0C++\\0'
# Let's see what 8 bytes are at the target

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
        name = sec[0:8].rstrip(b'\x00').decode('ascii', errors='replace')
        vsize = struct.unpack('<I', sec[8:12])[0]
        vaddr = struct.unpack('<I', sec[12:16])[0]
        rawsize = struct.unpack('<I', sec[16:20])[0]
        rawptr = struct.unpack('<I', sec[20:24])[0]
        sections.append((name, vaddr, vsize, rawptr, rawsize))

def rva_to_offset(sections, rva):
    for name, va, vs, rp, rs in sections:
        if va <= rva < va + max(vs, rs):
            return rp + (rva - va)
    return None

off = rva_to_offset(sections, target_rva)
if off:
    with open(PATH, 'rb') as f:
        f.seek(off)
        data = f.read(16)
        u64 = struct.unpack('<Q', data[:8])[0]
        print(f"\nData at target RVA 0x{target_rva:x} (file_off=0x{off:x}):")
        print(f"  bytes: {' '.join(f'{b:02x}' for b in data)}")
        print(f"  u64: 0x{u64:016x}")
        print(f"  as string: {data[:8]}")
        print(f"  Matches GCC class: {u64 == 0x474e5543432b2b00}")
else:
    print(f"Cannot map RVA 0x{target_rva:x} to file offset")