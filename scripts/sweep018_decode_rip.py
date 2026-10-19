#!/usr/bin/env python3
"""SWEEP-018: decode guest_rip stub-call sites for any program.
Usage: sweep018_decode_rip.py <stderr> <exe> [import-map-json]
FF15 decode: slot_va = guest_rip + disp32 (disp32 read at rip-4 in file).
Import name resolved via pefile IAT directory (works without candidate JSON).
"""
import struct, sys
import pefile

stderr_path, exe_path = sys.argv[1], sys.argv[2]
pe = pefile.PE(exe_path)
base = pe.OPTIONAL_HEADER.ImageBase

# IAT slot -> import name, straight from the import directory
slots = {}
for entry in pe.DIRECTORY_ENTRY_IMPORT:
    dll = entry.dll.decode()
    for i, imp in enumerate(entry.imports):
        if imp.address:
            slots[imp.address] = f"{dll}!{imp.name.decode() if imp.name else '#%d' % imp.ordinal}"

rips = {}
for line in open(stderr_path):
    if "guest_rip=" not in line:
        continue
    rip = int(line.split("guest_rip=")[1].split()[0], 16)
    rips[rip] = rips.get(rip, 0) + 1

raw = open(exe_path, "rb").read()
print(f"=== {exe_path.split('/')[-1]}: {sum(rips.values())} stub calls, "
      f"{len(rips)} distinct sites ===")
for rip, cnt in sorted(rips.items()):
    rva = rip - base
    try:
        off = pe.get_offset_from_rva(rva)
        disp = struct.unpack_from("<i", raw, off - 4)[0]
        opcode = raw[off - 6:off - 4]
        slot = rip + disp
        imp = slots.get(slot, "??UNMAPPED??")
        ok = "OK " if opcode == b"\xff\x15" else "BAD"
        print(f"  {ok} x{cnt:3d} rip={hex(rip)} slot={hex(slot)} -> {imp}")
    except Exception as e:
        print(f"  ERR x{cnt:3d} rip={hex(rip)}: {e}")
