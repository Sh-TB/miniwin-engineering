#!/usr/bin/env python3
"""Dump bytes at specific RVAs in a PE file."""
import struct, sys

def rva_to_offset(sections, rva):
    for name, va, vs, rp, rs in sections:
        if va <= rva < va + vs:
            return rp + (rva - va)
    return None

with open('samples/upx_decompressed.exe', 'rb') as f:
    f.seek(0x3C)
    lfanew = struct.unpack('<I', f.read(4))[0]
    f.seek(lfanew + 4)
    coff = f.read(20)
    num_sections = struct.unpack('<H', coff[2:4])[0]
    opt_size = struct.unpack('<H', coff[16:18])[0]
    f.seek(lfanew + 24)
    opt = f.read(opt_size)
    
    sections = []
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
    
    targets = [
        (0x9d560, 0x60, 'RaiseException caller (0x9d560)'),
        (0xe0190, 0x80, 'libgcc func A (0xe0190)'),
        (0xe0290, 0x50, 'libgcc func B (0xe0290)'),
        (0xe0220, 0x80, '__gxx_personality_v0 (0xe0220)'),
        (0x1593 - 0x10, 0x80, 'frame3 caller func (0x1570)'),
        (0x32c0, 0xb0, 'frame4 caller func (0x32c0)'),
    ]
    
    for rva, size, label in targets:
        off = rva_to_offset(sections, rva)
        if off is None:
            print(f'=== {label} === RVA NOT MAPPED')
            continue
        f.seek(off)
        data = f.read(size)
        # Print as hex dump
        print(f'=== {label} (file_off=0x{off:05x}) ===')
        for i in range(0, len(data), 16):
            chunk = data[i:i+16]
            hex_part = ' '.join(f'{b:02x}' for b in chunk)
            ascii_part = ''.join(chr(b) if 32 <= b < 127 else '.' for b in chunk)
            print(f'  {rva+i:06x}: {hex_part:<48s}  {ascii_part}')
        print()
