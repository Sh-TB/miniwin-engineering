#!/usr/bin/env python3
"""SWEEP-056 Train 3 (SW-1278) part 2: decode the callees + string constants
inside check-a 0x1400d6df0 / check-b 0x1400d6f30."""
import pefile, struct
from capstone import *

PE = "artifacts/sweep056/guests/everything/everything.exe"
pe = pefile.PE(PE)
base = pe.OPTIONAL_HEADER.ImageBase

def read(va, n):
    return pe.get_data(va - base, n)

def disas(va, n=120, maxn=40):
    md = Cs(CS_ARCH_X86, CS_MODE_64)
    out = []
    for i in md.disasm(read(va, n), va):
        out.append(f"  0x{i.address:x}: {i.mnemonic} {i.op_str}")
        if len(out) >= maxn:
            break
    return "\n".join(out)

def cstr(va, n=80):
    b = read(va, n)
    z = b.find(b"\x00")
    return b[:z if z >= 0 else n].decode("utf-8", "replace")

def wcstr(va, n=160):
    b = read(va, n)
    s = b""
    for i in range(0, len(b) - 1, 2):
        if b[i] == 0 and b[i+1] == 0:
            break
        s += b[i:i+2]
    return s.decode("utf-16-le", "replace")

# check-b: lea rdx,[rip+0xf94fd] at 0x1400d6f34, next insv addr 0x1400d6f3b
tgt = 0x1400d6f3b + 0xf94fd
print(f"check-b string at 0x{tgt:x}: {cstr(tgt)!r}")

# check-b callee 0x14016efe0
print(f"\n=== callee 0x14016efe0 (arg ecx=0, rdx=str) ===")
print(disas(0x14016efe0, 200, 30))

# check-a callees
print(f"\n=== check-a callee 0x1400012c0 ===")
print(disas(0x1400012c0, 120, 25))
print(f"\n=== check-a callee 0x14017e5c0 (arg rdx=rbx=this) ===")
print(disas(0x14017e5c0, 160, 30))

# what follows the xor eax test in check-b
print(f"\n=== check-b continued (0x1400d6f40) ===")
print(disas(0x1400d6f40, 160, 30))
