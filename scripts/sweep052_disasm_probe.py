#!/usr/bin/env python3
"""SW-1262: disassemble the guest's REAL vcruntime140._CxxThrowException and
vcruntime140_1.__CxxFrameHandler4 to settle how the FH4 throw record is built
and how the throwing image base is obtained (EI[3] vs RtlPcToFileHeader)."""
import struct, subprocess, sys

SO = "/home/z/my-project/workspace/miniwin-engineering/artifacts/sweep051/guests/soffice/"

def exports(path, want):
    f = open(path, "rb")
    dos = f.read(64)
    e = struct.unpack_from("<I", dos, 0x3C)[0]
    f.seek(e); nt = f.read(260)
    nsec = struct.unpack_from("<H", nt, 6)[0]
    osz = struct.unpack_from("<H", nt, 20)[0]
    f.seek(e + 24 + osz)
    secs = []
    for i in range(nsec):
        s = f.read(40)
        nm = s[:8].rstrip(b"\0").decode()
        vs, va, rs, ra = struct.unpack_from("<IIII", s, 8)
        secs.append((va, max(vs, rs), ra))
    f.seek(0)
    raw = f.read()
    exp_rva = struct.unpack_from("<I", nt, 24 + 112)[0]  # DataDirectory[0]
    ed = struct.unpack_from("<IIIIII", raw, exp_rva + 0x14 - 4)  # base..nfn..nnames..aof..aon..aoo
    base = ed[0]; nfn = ed[1]; nnames = ed[2]
    aof = struct.unpack_from("<I", raw, exp_rva + 0x1C)[0]
    aon = struct.unpack_from("<I", raw, exp_rva + 0x20)[0]
    aoo = struct.unpack_from("<I", raw, exp_rva + 0x24)[0]
    out = {}
    for i in range(nnames):
        nrva = struct.unpack_from("<I", raw, aon + 4 * i)[0]
        z = raw.find(b"\0", nrva)
        nm = raw[nrva:z].decode()
        if nm in want:
            fo = struct.unpack_from("<H", raw, aoo + 2 * i)[0]
            frva = struct.unpack_from("<I", raw, aof + 4 * fo)[0]
            out[nm] = frva
    def rva2off(rva):
        for va, sz, ra in secs:
            if va <= rva < va + sz:
                return ra + (rva - va)
        return None
    return [(nm, rva, rva2off(rva)) for nm, rva in out.items()]

for path, want in [
    (SO + "vcruntime140.dll", {"_CxxThrowException"}),
    (SO + "vcruntime140_1.dll", {"__CxxFrameHandler4"}),
]:
    for nm, rva, off in exports(path, want):
        print(f"\n=== {path.split('/')[-1]}!{nm} RVA=0x{rva:x} off=0x{off:x} ===")
        # dump 512 bytes as hex + let objdump disassemble via a tiny raw bin
        f = open(path, "rb"); f.seek(off); code = f.read(384)
        open("/tmp/probe.bin", "wb").write(code)
        r = subprocess.run(["objdump", "-D", "-b", "binary", "-m", "i386:x86-64",
                            "--adjust-vma=" + hex(rva), "/tmp/probe.bin"],
                           capture_output=True, text=True)
        lines = r.stdout.splitlines()
        for ln in lines:
            if ":\t" in ln:
                print(ln)
