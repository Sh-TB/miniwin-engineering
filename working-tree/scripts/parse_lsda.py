#!/usr/bin/env python3
"""Parse DWARF LSDA for Frame[3] of upx_decompressed.exe"""
import struct, sys

PE_PATH = "samples/upx_decompressed.exe"

# Section mapping for RVA -> file offset
SECTIONS = [
    (0x1000, 0x0dfbb0, 0x400, 0x0dfc00),   # .text
    (0xe1000, 0xa50, 0xe0000, 0xc00),        # .data
    (0xe2000, 0x115800, 0xe0c00, 0x115800),   # .rdata
    (0x1f8000, 0x8e08, 0x1f6400, 0x9000),     # .pdata
    (0x201000, 0x937c, 0x1ff400, 0x9400),     # .xdata
]

def rva_to_file_offset(rva):
    for va, vs, raw_off, raw_sz in SECTIONS:
        if va <= rva < va + vs and raw_sz > 0:
            return raw_off + (rva - va)
    return rva  # fallback

def read_uleb128(data, off):
    result = 0; shift = 0
    while off < len(data):
        b = data[off]; off += 1
        result |= (b & 0x7f) << shift
        if not (b & 0x80): break
        shift += 7
    return result, off

def read_sleb128(data, off):
    result = 0; shift = 0; byte = 0
    while off < len(data):
        b = data[off]; off += 1
        result |= (b & 0x7f) << shift
        shift += 7; byte = b
        if not (b & 0x80): break
    if shift < 64 and (byte & 0x40):
        result |= -(1 << shift)
    return result, off

with open(PE_PATH, "rb") as f:
    pe_data = f.read()

# Find the LSDA for Frame[3] (function begin=0x1570, UI at RVA 0x20107c)
# The UNWIND_INFO handler data offset points to the LSDA
# From trace: LSDA at VA 0x601088 = RVA 0x201088
lsda_rva = 0x201088
lsda_file_off = rva_to_file_offset(lsda_rva)
lsda = pe_data[lsda_file_off:lsda_file_off+256]

print(f"LSDA at RVA 0x{lsda_rva:x}")
print(f"Raw (first 128 bytes): {lsda[:128].hex()}")
print()

# Parse header
lpstart_enc = lsda[0]
ttype_enc = lsda[1]
cs_enc = lsda[2]
off = 3

print(f"LPStart encoding: 0x{lpstart_enc:02x} ({'omit' if lpstart_enc == 0xff else 'present'})")
print(f"TType encoding:   0x{ttype_enc:02x} ({'no type table' if ttype_enc == 0xff else 'has type table'})")
print(f"CS encoding:      0x{cs_enc:02x}")
print()

# Parse call site table (uleb128 encoded fields)
print("=== Call Site Table ===")
cs_entries = []
cs_num = 0
while off < len(lsda):
    start, off = read_uleb128(lsda, off)
    length, off = read_uleb128(lsda, off)
    lp, off = read_uleb128(lsda, off)
    action, off = read_uleb128(lsda, off)
    
    if start == 0:
        print(f"  [TERMINATOR] start=0 len={length} lp={lp} action={action}")
        break
    
    cs_entries.append((start, length, lp, action))
    print(f"  CS[{cs_num}]: start={start} len={length} lp={lp} action={action}")
    print(f"    Range: [{start}, {start+length})")
    cs_num += 1

action_table_start = off
print(f"\nAction table starts at LSDA byte {action_table_start}")

# Parse action records for each call site
for i, (start, length, lp, action) in enumerate(cs_entries):
    print(f"\n=== CS[{i}] action analysis (action_offset={action}) ===")
    if action == 0:
        print("  No action (cleanup only or no handler)")
        continue
    
    # Walk action chain
    act_off = action_table_start + action
    chain = 0
    while act_off < len(lsda):
        filter_val, new_off = read_sleb128(lsda, act_off)
        next_disp, new_off = read_sleb128(lsda, new_off)
        
        if filter_val == 0:
            print(f"  Action[{chain}]: filter=0 (CLEANUP), next={next_disp}")
        elif filter_val > 0:
            print(f"  Action[{chain}]: filter={filter_val} (CATCH type index {filter_val}), next={next_disp}")
        else:
            print(f"  Action[{chain}]: filter={filter_val} (CATCH ALL / catch(...)), next={next_disp}")
        
        chain += 1
        if next_disp == 0:
            break
        act_off = new_off + next_disp

# Also parse Frame[4] LSDA for comparison
print("\n" + "="*60)
print("=== Frame[4] LSDA (RVA 0x201200) for comparison ===")
print("="*60)
lsda4_rva = 0x201200
lsda4_file_off = rva_to_file_offset(lsda4_rva)
lsda4 = pe_data[lsda4_file_off:lsda4_file_off+256]

lpstart_enc4 = lsda4[0]
ttype_enc4 = lsda4[1]
cs_enc4 = lsda4[2]
off4 = 3

print(f"LPStart: 0x{lpstart_enc4:02x}  TType: 0x{ttype_enc4:02x}  CS: 0x{cs_enc4:02x}")

if ttype_enc4 != 0xff:
    # Read TType base
    tt_base_enc = ttype_enc4 & 0x0f
    if tt_base_enc == 0x0b:  # udata8
        tt_base = struct.unpack_from('<Q', lsda4, off4)[0]
        off4 += 8
        print(f"TType base: 0x{tt_base:x}")
    elif tt_base_enc == 0x09:  # udata4
        tt_base = struct.unpack_from('<I', lsda4, off4)[0]
        off4 += 4
        print(f"TType base: 0x{tt_base:x}")
    elif tt_base_enc == 0x01:  # uleb128
        tt_base, off4 = read_uleb128(lsda4, off4)
        print(f"TType base (uleb): 0x{tt_base:x}")

# For Frame[4], CS encoding is 0x15 = DW_EH_PE_uleb128 | DW_EH_PE_pcrel
# This means call site values are PC-relative!
cs_is_pcrel = (cs_enc4 & 0x70) != 0
print(f"CS is PC-relative: {cs_is_pcrel}")

# Parse call sites for Frame[4]
cs4_entries = []
while off4 < len(lsda4):
    start, off4 = read_uleb128(lsda4, off4)
    length, off4 = read_uleb128(lsda4, off4)
    lp, off4 = read_uleb128(lsda4, off4)
    action, off4 = read_uleb128(lsda4, off4)
    
    if start == 0:
        print(f"  [TERMINATOR]")
        break
    
    cs4_entries.append((start, length, lp, action))
    print(f"  CS: start={start} len={length} lp={lp} action={action}")

print(f"\nAction table at byte {off4}")
for i, (start, length, lp, action) in enumerate(cs4_entries):
    print(f"  CS[{i}] action={action}:")
    if action == 0: continue
    act_off = off4 + action
    chain = 0
    while act_off < len(lsda4) and chain < 5:
        filter_val, new_off = read_sleb128(lsda4, act_off)
        next_disp, new_off = read_sleb128(lsda4, new_off)
        if filter_val == 0:
            ftype = "CLEANUP"
        elif filter_val > 0:
            ftype = f"CATCH type#{filter_val}"
        else:
            ftype = "CATCH_ALL"
        print(f"    Action: filter={filter_val} ({ftype}), next={next_disp}")
        chain += 1
        if next_disp == 0: break
        act_off = new_off + next_disp
