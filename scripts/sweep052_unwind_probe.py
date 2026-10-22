#!/usr/bin/env python3
"""SW-1262 probe: parse mergedlo.dll .pdata entries for the frames seen in the
rc=99 walk (RVAs 0x1cb3880, 0x3129ee0, 0x1cd9480, and the odd handler RVA
0x533fb15). Decode UNWIND_INFO flags / handler RVA / chained info."""
import struct, sys

DLL = "/home/z/my-project/workspace/miniwin-engineering/artifacts/sweep051/guests/soffice/mergedlo.dll"

f = open(DLL, "rb")
dos = f.read(64)
e_lfanew = struct.unpack_from("<I", dos, 0x3C)[0]
f.seek(e_lfanew)
nt = f.read(24 + 112 + 240)
sig = nt[:4]
assert sig == b"PE\0\0", sig
machine = struct.unpack_from("<H", nt, 4)[0]
numsec = struct.unpack_from("<H", nt, 6)[0]
opt_size = struct.unpack_from("<H", nt, 20)[0]
opt = nt[24:24+opt_size]
magic = struct.unpack_from("<H", opt, 0)[0]
image_base = struct.unpack_from("<Q", opt, 24)[0] if magic == 0x20b else struct.unpack_from("<I", opt, 28)[0]
print(f"machine=0x{machine:x} magic=0x{magic:x} image_base=0x{image_base:x} nsec={numsec}")

secs = []
sec_off = e_lfanew + 24 + opt_size
f.seek(sec_off)
for i in range(numsec):
    s = f.read(40)
    name = s[:8].rstrip(b"\0").decode(errors="replace")
    vsize, vaddr, rsize, raddr = struct.unpack_from("<IIII", s, 8)
    chars = struct.unpack_from("<I", s, 36)[0]
    secs.append((name, vaddr, vsize, raddr, rsize, chars))
    print(f"  sec {name:8s} VA 0x{vaddr:09x} VS 0x{vsize:09x} raw 0x{raddr:09x} RS 0x{rsize:09x} ch 0x{chars:08x}")

def rva2off(rva):
    for name, vaddr, vsize, raddr, rsize, ch in secs:
        if vaddr <= rva < vaddr + max(vsize, rsize):
            d = rva - vaddr
            if d < rsize:
                return raddr + d, name
            return None, name
    return None, None

def read(rva, n):
    off, sec = rva2off(rva)
    if off is None:
        return None, sec
    f.seek(off)
    return f.read(n), sec

def parse_unwind(ui_rva, label):
    data, sec = read(ui_rva, 32)
    if not data:
        print(f"{label}: UNWIND_INFO at RVA 0x{ui_rva:x} unmapped (sec={sec})")
        return
    ver_flags = data[0]
    ver = ver_flags & 7
    flags = (ver_flags >> 3) & 0xF  # UNW_FLAG_NHANDLER=0x? actually bits 3..: E(3) U(4) C(5) F(6)? mapping: UNW_FLAG_EHANDLER=0x1<<3? No:
    # UNWIND_INFO: bit0-2 version, bit3 E (handler), bit4 U (UOP), bit5 C (chaininfo)
    e = (ver_flags >> 3) & 1
    u = (ver_flags >> 4) & 1
    c = (ver_flags >> 5) & 1
    prolog = data[1]
    cnt = data[2]
    frame = data[3]
    print(f"{label}: UNWIND_INFO RVA 0x{ui_rva:x} sec={sec} ver={ver} E={e} U={u} CHAIN={c} prolog=0x{prolog:x} codes={cnt} frame={frame:#x}")
    codes_end = 4 + cnt * 2
    if cnt % 2: codes_end += 2  # padding
    if e or c:
        if c:
            # chained: after codes+pad, a RUNTIME_FUNCTION {begin,end,unwind} RVAs
            rf = struct.unpack_from("<III", data, codes_end) if len(data) >= codes_end + 12 else None
            print(f"   CHAININFO RF = {rf and [hex(x) for x in rf]}")
        else:
            h = struct.unpack_from("<I", data, codes_end)[0]
            print(f"   handler RVA = 0x{h:x} {'<== ODD!' if h & 1 else ''}")
    return data

# Frames from the walk
for frva, label in [(0x1cb3880, "Frame[4]"), (0x3129ee0, "Frame[5]"), (0x1cd9480, "Frame[7]"),
                    (0x1cbef60, "Frame[3]"), (0x3129cf0, "Frame[6]")]:
    # find .pdata entry: .pdata section
    pd = None
    for name, vaddr, vsize, raddr, rsize, ch in secs:
        if name == ".pdata":
            pd = (vaddr, raddr, rsize)
    lo, hi = frva - 0x100, frva + 0x100
    found = None
    # binary search
    vaddr, raddr, rsize = pd
    n = rsize // 12
    a, b = 0, n - 1
    while a <= b:
        m = (a + b) // 2
        f.seek(raddr + m * 12)
        beg, end, ui = struct.unpack("<III", f.read(12))
        if beg <= frva < end:
            found = (beg, end, ui, m)
            break
        if frva < beg: b = m - 1
        else: a = m + 1
    if found:
        beg, end, ui, idx = found
        print(f"\n{label}: .pdata[{idx}] begin=0x{beg:x} end=0x{end:x} unwind=0x{ui:x}")
        parse_unwind(ui, f"{label}")
    else:
        print(f"\n{label}: NO .pdata entry containing RVA 0x{frva:x}")

# What is at the odd handler RVA 0x533fb15?
data, sec = read(0x533fb15, 32)
print(f"\nOdd handler target RVA 0x533fb15: sec={sec} bytes={data.hex() if data else 'unmapped'}")
# and 4-aligned around it
data, sec = read(0x533fb14, 32)
print(f"  RVA 0x533fb14: sec={sec} bytes={data.hex() if data else 'unmapped'}")
