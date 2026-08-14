#!/usr/bin/env python3
"""Analyze UPX exception handling - dump key functions and LSDA data."""
import struct

def read_pe_sections(path):
    with open(path, 'rb') as f:
        f.seek(0x3C)
        lfanew = struct.unpack('<I', f.read(4))[0]
        f.seek(lfanew + 4)
        coff = f.read(20)
        num_sections = struct.unpack('<H', coff[2:4])[0]
        opt_size = struct.unpack('<H', coff[16:18])[0]
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
        return sections

def rva_to_offset(sections, rva):
    for name, va, vs, rp, rs in sections:
        if va <= rva < va + max(vs, rs):
            return rp + (rva - va)
    return None

def read_bytes(path, sections, rva, size):
    off = rva_to_offset(sections, rva)
    if off is None: return None
    with open(path, 'rb') as f:
        f.seek(off)
        return f.read(size)

def read_u8(data, pos): return data[pos], pos + 1
def read_u16(data, pos): return struct.unpack_from('<H', data, pos)[0], pos + 2
def read_u32(data, pos): return struct.unpack('<I', data, pos)[0], pos + 4

def read_uleb128(data, pos):
    result = 0; shift = 0
    while pos < len(data):
        b = data[pos]; pos += 1
        result |= (b & 0x7f) << shift; shift += 7
        if (b & 0x80) == 0: break
    return result, pos

PATH = 'samples/upx_decompressed.exe'
sections = read_pe_sections(PATH)

# Print section map
print("=== Section Map ===")
for name, va, vs, rp, rs in sections:
    print(f"  {name:8s} VA=0x{va:06x} VS=0x{vs:06x} Raw=0x{rp:06x} RS=0x{rs:06x}")
print()

# 1. Check what's at the personality's expected exception class address
# Personality at 0xe0220: lea rax, [rip-0x3e7b]
# Next instruction at 0xe0227, so target = 0xe0227 - 0x3e7b = 0xa1aac
class_rva = 0xe0227 - 0x3e7b
cls_data = read_bytes(PATH, sections, class_rva, 16)
if cls_data:
    cls_u64 = struct.unpack('<Q', cls_data[:8])[0]
    print(f"=== Personality expected class @ RVA 0x{class_rva:x} ===")
    print(f"  bytes: {' '.join(f'{b:02x}' for b in cls_data[:16])}")
    print(f"  u64: 0x{cls_u64:016x}")
    print(f"  str: {cls_data[:8]}")
    print()

# 2. Parse LSDA for Frame 4 (function 0x32c0, LSDA RVA 0x201200)
print("=== LSDA for func 0x32c0 (Frame 4) @ RVA 0x201200 ===")
lsda = read_bytes(PATH, sections, 0x201200, 128)
if lsda:
    pos = 0
    lpstart_enc, pos = read_u8(lsda, pos)
    ttype_enc, pos = read_u8(lsda, pos)
    callsite_enc, pos = read_u8(lsda, pos)
    
    print(f"  LPStart encoding: 0x{lpstart_enc:02x} {'(omit)' if lpstart_enc == 0xff else ''}")
    print(f"  TType encoding:   0x{ttype_enc:02x}")
    print(f"  CallSite encoding: 0x{callsite_enc:02x}")
    
    ttype_base = 0
    if ttype_enc != 0xff:
        # Read TType base using the encoding
        if ttype_enc & 0x0f == 0x04:  # udata4
            raw = struct.unpack('<I', lsda[pos:pos+4])[0]
            ttype_base = pos + 4 + raw  # absolute = current_pos + 4 + offset (for pcrel)
            # But encoding high nibble determines interpretation
            hi = (ttype_enc >> 4) & 0x0f
            if hi & 0x01:  # pcrel
                ttype_base = 0x201200 + pos + 4 + struct.unpack('<i', lsda[pos:pos+4])[0]
            pos += 4
        elif ttype_enc & 0x0f == 0x0b:  # sdata4
            raw = struct.unpack('<i', lsda[pos:pos+4])[0]
            ttype_base = 0x201200 + pos + 4 + raw  # pcrel
            pos += 4
        else:
            ttype_base, pos = read_uleb128(lsda, pos)
        print(f"  TType base offset: 0x{ttype_base:x} (from section start)")
    
    cs_length, pos = read_uleb128(lsda, pos)
    print(f"  Call site table length: {cs_length}")
    
    cs_table_start = pos
    entry = 0
    while pos < cs_table_start + cs_length:
        if callsite_enc == 0x01:  # uleb128
            start, pos = read_uleb128(lsda, pos)
            length, pos = read_uleb128(lsda, pos)
            lp, pos = read_uleb128(lsda, pos)
            action, pos = read_uleb128(lsda, pos)
        else:
            print(f"  Entry {entry}: (encoding 0x{callsite_enc:02x} not supported for dump)")
            break
        lp_str = f"0x{lp:x}" if lp else "none"
        act_str = f"action[{action}]" if action else "cleanup"
        print(f"  Entry {entry}: start=0x{start:x} len=0x{length:x} lp={lp_str} {act_str}")
        entry += 1
    print()

# 3. Parse LSDA for Frame 3 (function 0x1570, LSDA RVA 0x201088)
print("=== LSDA for func 0x1570 (Frame 3) @ RVA 0x201088 ===")
lsda3 = read_bytes(PATH, sections, 0x201088, 128)
if lsda3:
    pos = 0
    lpstart_enc, pos = read_u8(lsda3, pos)
    ttype_enc, pos = read_u8(lsda3, pos)
    callsite_enc, pos = read_u8(lsda3, pos)
    
    print(f"  LPStart encoding: 0x{lpstart_enc:02x} {'(omit)' if lpstart_enc == 0xff else ''}")
    print(f"  TType encoding:   0x{ttype_enc:02x}")
    print(f"  CallSite encoding: 0x{callsite_enc:02x}")
    
    if ttype_enc != 0xff:
        pos += 4  # skip TType base for now
    
    cs_length, pos = read_uleb128(lsda3, pos)
    print(f"  Call site table length: {cs_length}")
    
    cs_table_start = pos
    entry = 0
    while pos < cs_table_start + cs_length:
        start, pos = read_uleb128(lsda3, pos)
        length, pos = read_uleb128(lsda3, pos)
        lp, pos = read_uleb128(lsda3, pos)
        action, pos = read_uleb128(lsda3, pos)
        lp_str = f"0x{lp:x}" if lp else "none"
        act_str = f"action[{action}]" if action else "cleanup"
        print(f"  Entry {entry}: start=0x{start:x} len=0x{length:x} lp={lp_str} {act_str}")
        entry += 1
    print()

# 4. Dump the full function at 0x1570
print("=== Full function 0x1570 (0x4b bytes) ===")
func_data = read_bytes(PATH, sections, 0x1570, 0x4b)
if func_data:
    for i in range(0, len(func_data), 16):
        chunk = func_data[i:i+16]
        hex_str = ' '.join(f'{b:02x}' for b in chunk)
        print(f"  {0x1570+i:06x}: {hex_str}")
    print()

# 5. Check what function 0x32c0 calls before the exception
# The call at 0x32eb: e8 7e e2 ff ff -> target = 0x32f0 + (-0x1D82) = 0x156E
print("=== Call targets from 0x32c0 ===")
calls = [
    (0x32eb, "e8 7e e2 ff ff", "first call"),
    (0x32f5, "e8 54 e2 ff ff", "second call"),
    (0x3310, "e8 b4 ec 00 00", "third call"),
    (0x3329, "e8 24 e2 ff ff", "fourth call"),
]
for rva, expected_bytes, desc in calls:
    data = read_bytes(PATH, sections, rva, 5)
    if data:
        actual = ' '.join(f'{b:02x}' for b in data)
        disp = struct.unpack('<i', data[1:5])[0]
        target = rva + 5 + disp
        match = "MATCH" if actual == expected_bytes else f"MISMATCH (got {actual})"
        print(f"  0x{rva:x} ({desc}): target RVA = 0x{target:x} {match}")
print()

# 6. Check the UnhandledExceptionFilter function at 0x49c9c0
print("=== UnhandledExceptionFilter @ 0x49c9c0 ===")
uef_data = read_bytes(PATH, sections, 0x49c9c0, 32)
if uef_data:
    print(f"  bytes: {' '.join(f'{b:02x}' for b in uef_data)}")
