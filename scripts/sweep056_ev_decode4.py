#!/usr/bin/env python3
"""SWEEP-056 (SW-1278) part 4: decode the FindFirstVolumeW caller function
(0x1400d2a00..0x1400d2c40) to see what it passes to the walker as arg3
(the fs-name that check-b stricmps against 'ntfs')."""
import pefile
from capstone import *

pe = pefile.PE("artifacts/sweep056/guests/everything/everything.exe")
base = pe.OPTIONAL_HEADER.ImageBase

def read(va, n):
    return pe.get_data(va - base, n)

md = Cs(CS_ARCH_X86, CS_MODE_64)
start = 0x1400d2a00
n = 0x2c40 - 0x2a00 + 0x40
for i in md.disasm(read(start, n), start):
    print(f"0x{i.address:x}: {i.mnemonic} {i.op_str}")
