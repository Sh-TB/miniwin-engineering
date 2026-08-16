#!/usr/bin/env python3
"""Dump specific RVAs from UPX binary for analysis"""
import sys

with open('samples/upx_decompressed.exe','rb') as f:
    data = f.read()

# .text: VA=0x1000, Raw=0x400, Size=0xdfbb0
def rva_to_off(rva):
    return 0x400 + (rva - 0x1000)

def dump(rva, n, label):
    off = rva_to_off(rva)
    print(f'{label} (RVA 0x{rva:05x}, file 0x{off:05x}):')
    for i in range(0, n, 16):
        b = ' '.join(f'{data[off+i+j]:02x}' for j in range(min(16, n-i)))
        print(f'  +{i:04x}: {b}')
    print()

# Landing pad RVA 0x331c (from RtlUnwindEx target_ip)
dump(0x331c, 80, 'Landing pad')

# Function called from landing pad at 0x403328 (call 0x401551)
dump(0x1550, 32, 'Crash function 0x401550')

# Global at RVA 0xe2778 (from lea rax, [rip+0xdf4aa] at 0x4032c7)
# .rdata: VA=0xe2000, Raw=0x400+0xe1000-0x1000=0xf400? No...
# Section .rdata: VA=0xe2000, Raw=0x400+(0xe2000-0x1000)*1... 
# Actually need to find right raw offset
print('Section headers for RVA 0xe2778:')
print('  .rdata VA=0xe2000 Raw=0x400+0xe1000=...')

# Check global at 0xe2778 (in .rdata: VA=0xe2000 raw=0xe0400 based on section headers)
# .rdata: VA=0xe2000 VS=0x115800 Raw=0x115800 RawAddr=0x400+0xdfc00? 
# Let me just read the PE headers properly
import struct

# PE header offset
pe_off = struct.unpack_from('<I', data, 0x3c)[0]
# Number of sections
num_sections = struct.unpack_from('<H', data, pe_off + 6)[0]
optional_size = struct.unpack_from('<H', data, pe_off + 20)[0]
section_off = pe_off + 24 + optional_size

print(f'PE offset: 0x{pe_off:x}, Sections: {num_sections}')
for i in range(num_sections):
    s = section_off + i * 40
    name = data[s:s+8].rstrip(b'\x00').decode('ascii','replace')
    vs, va, rawsz, rawaddr = struct.unpack_from('<IIII', data, s+8)
    chars = struct.unpack_from('<I', data, s+36)[0]
    print(f'  {name:8s} VA=0x{va:06x} VS=0x{vs:06x} Raw=0x{rawaddr:06x} RawSz=0x{rawsz:06x} Ch=0x{chars:08x}')
    if va <= 0xe2778 < va + vs:
        file_off = rawaddr + (0xe2778 - va)
        print(f'    RVA 0xe2778 maps to file offset 0x{file_off:x}')
        val = struct.unpack_from('<Q', data, file_off)[0]
        print(f'    u64 at 0xe2778: 0x{val:016x}')
        val2 = struct.unpack_from('<Q', data, file_off+8)[0]
        print(f'    u64 at 0xe2780 (+0x08): 0x{val2:016x}')
        val3 = struct.unpack_from('<Q', data, file_off+16)[0]
        print(f'    u64 at 0xe2788 (+0x10): 0x{val3:016x}')
