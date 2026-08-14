#!/usr/bin/env python3
"""Generate a minimal valid PE32+ x64 test binary."""
import struct

pe = bytearray()

# === DOS Header (64 bytes) ===
pe += struct.pack('<H', 0x5A4D)           # e_magic = MZ
pe += b'\x00' * 58                        # padding
pe += struct.pack('<I', 64)               # e_lfanew
assert len(pe) == 64

# === PE Signature (4 bytes) at offset 64 ===
pe += b'PE\x00\x00'
assert len(pe) == 68

# === COFF Header (20 bytes) at offset 68 ===
num_sections = 2
size_opt_header = 240  # PE32+ standard
pe += struct.pack('<H', 0x8664)           # machine = AMD64
pe += struct.pack('<H', num_sections)     # number_of_sections
pe += struct.pack('<I', 0)                # time_date_stamp
pe += struct.pack('<I', 0)                # pointer_to_symbol_table
pe += struct.pack('<I', 0)                # number_of_symbols
pe += struct.pack('<H', size_opt_header)  # size_of_optional_header
pe += struct.pack('<H', 0x0022)           # characteristics
assert len(pe) == 88

# === Optional Header PE32+ (240 bytes) at offset 88 ===
opt_start = len(pe)
pe += struct.pack('<H', 0x020B)           # magic = PE32+
pe += struct.pack('<BB', 14, 0)           # linker version
pe += struct.pack('<I', 0x200)            # size_of_code
pe += struct.pack('<I', 0x200)            # size_of_initialized_data
pe += struct.pack('<I', 0)                # size_of_uninitialized_data
pe += struct.pack('<I', 0x1000)           # address_of_entry_point
pe += struct.pack('<I', 0x1000)           # base_of_code
pe += struct.pack('<Q', 0x140000000)      # image_base
pe += struct.pack('<I', 0x1000)           # section_alignment
pe += struct.pack('<I', 0x200)            # file_alignment
pe += struct.pack('<HH', 6, 0)           # OS version
pe += struct.pack('<HH', 0, 0)           # image version
pe += struct.pack('<HH', 6, 0)           # subsystem version
pe += struct.pack('<I', 0)                # win32_version_value
pe += struct.pack('<I', 0x4000)           # size_of_image
pe += struct.pack('<I', 0x400)            # size_of_headers
pe += struct.pack('<I', 0)                # checksum
pe += struct.pack('<H', 3)                # subsystem = CUI
pe += struct.pack('<H', 0x8160)           # dll_characteristics
pe += struct.pack('<Q', 0x100000)         # size_of_stack_reserve
pe += struct.pack('<Q', 0x1000)           # size_of_stack_commit
pe += struct.pack('<Q', 0x100000)         # size_of_heap_reserve
pe += struct.pack('<Q', 0x1000)           # size_of_heap_commit
pe += struct.pack('<I', 0)                # loader_flags
pe += struct.pack('<I', 16)               # number_of_rva_and_sizes

# Data directory offset should be at opt_start + 112 = 200
data_dir_start = opt_start + 112
assert len(pe) == data_dir_start, f"Expected {data_dir_start}, got {len(pe)}"

# 16 data directories (128 bytes)
data_dirs = [0] * 16 * 2  # rva + size pairs (as u32)
# Import directory: RVA 0x2000, size 0x3C (enough for 1 descriptor + null)
data_dirs[1*2] = 0x2000
data_dirs[1*2 + 1] = 0x3C
for rva, size in [(data_dirs[i*2], data_dirs[i*2+1]) for i in range(16)]:
    pe += struct.pack('<II', rva, size)

# End of optional header at opt_start + 240 = 328
assert len(pe) == 328, f"Expected 328, got {len(pe)}"

# === Section Headers at offset 328 ===
# .text section
pe += b'.text\x00\x00\x00'                # name (8 bytes)
pe += struct.pack('<I', 0x200)            # virtual_size
pe += struct.pack('<I', 0x1000)           # virtual_address (RVA)
pe += struct.pack('<I', 0x200)            # size_of_raw_data
pe += struct.pack('<I', 0x400)            # pointer_to_raw_data
pe += struct.pack('<II', 0, 0)            # relocs, linenumbers
pe += struct.pack('<HH', 0, 0)            # num_relocs, num_linenumbers
pe += struct.pack('<I', 0x60000020)       # characteristics (code|exec|read)

# .rdata section
pe += b'.rdata\x00\x00'                   # name (8 bytes)
pe += struct.pack('<I', 0x200)            # virtual_size
pe += struct.pack('<I', 0x2000)           # virtual_address (RVA)
pe += struct.pack('<I', 0x200)            # size_of_raw_data
pe += struct.pack('<I', 0x600)            # pointer_to_raw_data
pe += struct.pack('<II', 0, 0)            # relocs, linenumbers
pe += struct.pack('<HH', 0, 0)            # num_relocs, num_linenumbers
pe += struct.pack('<I', 0x40000040)       # characteristics (init_data|read)

# Pad to file_alignment (0x200 = 512) — section headers end at 408, pad to 0x400
while len(pe) < 0x400:
    pe += b'\x00'

# === .text section raw data at offset 0x400 ===
# Entry point: simple RET instruction (0xC3)
pe.append(0xC3)
# Pad rest of .text section (0x200 bytes total)
while len(pe) < 0x600:
    pe += b'\x00'

# === .rdata section raw data at offset 0x600 ===
# Import Directory at RVA 0x2000
# .rdata: VA=0x2000, raw=0x600, so RVA 0x2000 → file offset 0x600

# Import descriptor for KERNEL32.DLL
# Using ILT (original_first_thunk) at RVA 0x2040
# IAT (first_thunk) at RVA 0x2060
# DLL name at RVA 0x2080
# Hint/Name at RVA 0x20A0
ilt_rva = 0x2040
iat_rva = 0x2060
dll_name_rva = 0x2080
hint_name_rva = 0x20A0

pe += struct.pack('<II', ilt_rva, 0)      # original_first_thunk, time_date_stamp
pe += struct.pack('<II', 0, dll_name_rva) # forwarder_chain, name_rva
pe += struct.pack('<I', iat_rva)          # first_thunk
pe += struct.pack('<I', 0)                # padding to 20 bytes

# Null terminator descriptor
pe += struct.pack('<IIIIII', 0, 0, 0, 0, 0, 0)  # 24 bytes

# ILT (at RVA 0x2040 → file offset 0x600 + (0x2040-0x2000) = 0x640)
while len(pe) < 0x640:
    pe += b'\x00'
pe += struct.pack('<Q', hint_name_rva)   # 8-byte entry for PE32+ (no ordinal flag)
pe += struct.pack('<Q', 0)                # null terminator

# IAT (at RVA 0x2060 → file offset 0x600 + (0x2060-0x2000) = 0x660)
while len(pe) < 0x660:
    pe += b'\x00'
pe += struct.pack('<Q', hint_name_rva)   # same entry
pe += struct.pack('<Q', 0)                # null terminator

# DLL name "KERNEL32.DLL\0" at RVA 0x2080 → file offset 0x600 + 0x80 = 0x680
while len(pe) < 0x680:
    pe += b'\x00'
pe += b'KERNEL32.DLL\x00'

# Hint/Name at RVA 0x20A0 → file offset 0x600 + 0xA0 = 0x6A0
while len(pe) < 0x6A0:
    pe += b'\x00'
pe += struct.pack('<H', 1)                # hint
pe += b'GetProcAddress\x00'

# Pad to fill .rdata section
while len(pe) < 0x800:
    pe += b'\x00'

output_path = '/home/z/my-project/tests/fixtures/minimal_pe64.exe'
with open(output_path, 'wb') as f:
    f.write(pe)

print(f"Generated minimal PE64: {len(pe)} bytes at {output_path}")
print(f"DOS header: 0-63")
print(f"PE signature: 64-67")
print(f"COFF header: 68-87")
print(f"Optional header: 88-327")
print(f"Data directories: 200-327")
print(f"Section headers: 328-407")
print(f".text raw: 0x400-0x5FF")
print(f".rdata raw: 0x600-0x7FF")
