#!/usr/bin/env python3
"""SWEEP-054: decode XP spider.exe's message loop — find the IAT slots of
GetMessageW / DispatchMessageW / PeekMessageW in USER32 imports and locate the
call sites in .text via objdump, to understand why no dispatch happens."""
import struct, sys, subprocess, re

path = sys.argv[1] if len(sys.argv) > 1 else "artifacts/sweep054/guests/spider/spider.exe"
data = open(path, "rb").read()

# PE headers
e_lfanew = struct.unpack_from("<I", data, 0x3C)[0]
nsec = struct.unpack_from("<H", data, e_lfanew + 6)[0]
optsz = struct.unpack_from("<H", data, e_lfanew + 20)[0]
opt = e_lfanew + 24
magic = struct.unpack_from("<H", data, opt)[0]
assert magic == 0x20B, "PE32+ only"
imp_rva, impsz = struct.unpack_from("<II", data, opt + 120 + 8)  # data dir 1
imgbase = struct.unpack_from("<Q", data, opt + 24)[0]

secs = []
so = opt + optsz
for i in range(nsec):
    name = data[so:so+8].rstrip(b"\0").decode()
    vsz, va, rsz, raw = struct.unpack_from("<IIII", data, so + 8)
    secs.append((name, va, vsz, raw, rsz))
    so += 40

def rva2off(rva):
    for name, va, vsz, raw, rsz in secs:
        if va <= rva < va + max(vsz, rsz):
            return raw + (rva - va)
    return None

def cstr(off):
    end = data.index(b"\0", off)
    return data[off:end].decode("utf-8", "replace")

# walk import descriptors
slots = {}   # (dll, func) -> iat_rva
d = imp_rva
while True:
    off = rva2off(d)
    oft, tds, fwd, name_rva, ft = struct.unpack_from("<IIIII", data, off)
    if oft == 0 and name_rva == 0:
        break
    dll = cstr(rva2off(name_rva))
    n = 0
    while True:
        fo = None
        for src in (oft, ft):
            if src:
                fo = rva2off(src + 8 * n)
                if fo is not None:
                    break
        if fo is None:
            break
        t = struct.unpack_from("<Q", data, fo)[0]
        if t == 0:
            break
        if not (t & (1 << 63)):
            fname = cstr(rva2off(t & 0x7FFFFFFF) + 2)
            slots[(dll.upper(), fname)] = ft + 8 * n
        n += 1
    d += 20

TARGETS = ["GetMessageW", "DispatchMessageW", "PeekMessageW", "TranslateMessage",
           "BeginPaint", "EndPaint", "UpdateWindow", "CallWindowProcW", "ShowWindow"]
want = {}
for (dll, fn), rva in slots.items():
    if dll == "USER32.DLL" and fn in TARGETS:
        want[fn] = rva
for fn in TARGETS:
    rva = want.get(fn)
    print(f"{fn:20s} IAT RVA {hex(rva) if rva else 'NOT-IMPORTED'}")

# disassemble and find call sites: call qword [rip+X] where X resolves to IAT rva
out = subprocess.run(["objdump", "-d", "--no-show-raw-insn", path],
                     capture_output=True, text=True).stdout
# map rip-relative: 'call ... rip+0x...'  need image base + rva math
# objdump shows: call QWORD PTR [rip+0xNNNN]  # addr 0xIMAGEBASE+RVA
pat = re.compile(r"call.*\[rip\+(0x[0-9a-f]+)\]\s*#\s*(0x[0-9a-f]+)")
calls = {}
for line in out.splitlines():
    m = pat.search(line)
    if m:
        disp, absaddr = int(m.group(1), 16), int(m.group(2), 16)
        iat_va = absaddr + disp  # rip after instr == absaddr; target = rip + disp
        rva = iat_va - imgbase
        for fn, wrva in want.items():
            if rva == wrva:
                calls.setdefault(fn, []).append(absaddr - imgbase)
for fn in TARGETS:
    sites = calls.get(fn, [])
    print(f"{fn:20s} call sites: {len(sites)} {[hex(s) for s in sites[:6]]}")
