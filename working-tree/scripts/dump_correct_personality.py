#!/usr/bin/env python3
"""Find the REAL personality functions by tracing call targets.""""
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

def read_bytes(rva, n):
    off = rva_to_offset(rva)
    if off is None: return None
    with open(PATH, 'rb') as f:
        f.seek(off)
        return f.read(n)

# The wrapper at 0xe0220 has:
#   0xe0224: 48 8d 05 85 fb fc ff  lea rax, [rip+disp]
#   0xe0230: e8 0b d1 fb ff       call [rip+disp]
wrapper_bytes = read_bytes(0xe0220, 0x20)

# Parse LEA target: instruction at 0xe0224 (7 bytes), next at 0xe022b
# 48 8d 05 85 fb fc ff
lea_disp = struct.unpack('<i', wrapper_bytes[7:11])[0]  # bytes 7-10 = 85 fb fc ff
lea_target = 0xe022b + lea_disp
print(f'LEA loads: 0x{lea_target:x}')

# Parse CALL target: instruction at 0xe0230 (5 bytes), next at 0xe0235
# e8 0b d1 fb ff
call_disp = struct.unpack('<i', wrapper_bytes[16:20])[0]  # bytes 16-19 = 0b d1 fb ff
call_target = 0xe0235 + call_disp
print(f'CALL targets: 0x{call_target:x}')
print()

# Dump both functions
for rva, size, label in [
    (0xe0220, 16, f'SEH handler (0xe0220) calls 0x{call_target:x}'),
    (call_target, 128, f'SEH personality wrapper (0x{call_target:x})'),
    (lea_target, 128, f'GCC personality (0x{lea_target:x}'),
]:
    data = read_bytes(rva, size)
    if data:
        print(f'=== {label} ===')
        for i in range(0, len(data), 16):
            chunk = data[i:i+16]
            hex_str = ' '.join(f'{b:02x}' for b in chunk)
            print(f'  {rva+i:06x}: {hex_str}')
        print()
    else:
        print(f'=== {label} @ 0x{rva:x} — NOT MAPPED ===')
        print()
