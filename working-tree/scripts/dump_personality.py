#!/usr/bin/env python3
"""Dump the real personality function implementation."""
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

# Dump personality wrapper (0xe0220) full
for rva, size, label in [
    (0xe0220, 16, 'wrapper __gxx_pers_v0'),
    (0xb42f0, 128, 'real personality impl'),
    (0xafdb0, 128, 'inner personality (loaded by wrapper)'),
]:
    off = rva_to_offset(rva)
    if off:
        with open(PATH, 'rb') as f:
            f.seek(off)
            data = f.read(size)
        print(f'=== {label} @ RVA 0x{rva:x} (file_off=0x{off:x}) ===')
        for i in range(0, len(data), 16):
            chunk = data[i:i+16]
            hex_str = ' '.join(f'{b:02x}' for b in chunk)
            print(f'  {rva+i:06x}: {hex_str}')
        print()

# Also check what's at the __cxa_throw cleanup targets
for rva, label in [
    (0xdbce2, '__cxa_throw cleanup1'),
    (0xdc2c2, '__cxa_throw cleanup2'),
]:
    off = rva_to_offset(rva)
    if off:
        with open(PATH, 'rb') as f:
            f.seek(off)
            data = f.read(32)
        print(f'=== {label} @ RVA 0x{rva:x} ===')
        print(f'  bytes: {" ".join(f"{b:02x}" for b in data[:16])}')
        print()
