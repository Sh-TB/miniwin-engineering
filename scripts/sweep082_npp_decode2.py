#!/usr/bin/env python3
"""SWEEP-082 SW-1381 decode: npp64 new crash RVA 0x1eb48c + IAT slot resolution."""
import struct, sys
from capstone import Cs, CS_ARCH_X86, CS_MODE_64

PATH = sys.argv[1] if len(sys.argv) > 1 else "artifacts/sweep080/dl/npp.portable.x64.x/notepad++.exe"
b = open(PATH, "rb").read()
pe = struct.unpack_from("<I", b, 0x3C)[0]
nsec = struct.unpack_from("<H", b, pe + 6)[0]
optsz = struct.unpack_from("<H", b, pe + 20)[0]
imgbase = struct.unpack_from("<Q", b, pe + 24 + 24)[0]
sec0 = pe + 24 + optsz
def r2o(rva):
    for i in range(nsec):
        s = sec0 + i * 40
        vsz, va = struct.unpack_from("<II", b, s + 8)
        rawsz, raw = struct.unpack_from("<II", b, s + 16)
        if va <= rva < va + max(vsz, rawsz):
            return raw + (rva - va)
    return None

# ---- import table walk (manual, no pefile) ----
dir_imp_rva, dir_imp_sz = struct.unpack_from("<II", b, pe + 24 + 120)  # data dir 1 (import)
iat_map = {}
o = r2o(dir_imp_rva)
while True:
    oft, ts, fc, name_rva, first_thunk = struct.unpack_from("<IIIII", b, o)
    if name_rva == 0 and first_thunk == 0: break
    dll = b[r2o(name_rva):r2o(name_rva)+64].split(b"\0")[0].decode(errors="replace")
    idx = 0
    thunk_rva = oft or first_thunk
    while True:
        t = struct.unpack_from("<Q", b, r2o(thunk_rva + idx*8))[0]
        if t == 0: break
        if t & (1 << 63):
            nm = "#%d" % (t & 0xFFFF)
        else:
            hn = r2o(t & 0x7FFFFFFF)
            nm = b[r2o(hn)+2:r2o(hn)+2+64].split(b"\0")[0].decode(errors="replace")
        iat_map[imgbase + first_thunk + idx*8] = f"{dll}!{nm}"
        idx += 1
    o += 20

CRASH = 0x1eb48c
start = CRASH - 0x60
o2 = r2o(start)
md = Cs(CS_ARCH_X86, CS_MODE_64)
code = b[o2:o2 + 0xB0]
for ins in md.disasm(code, start):
    mark = " <<<< CRASH" if ins.address == CRASH else ""
    ann = ""
    if "rip" in ins.op_str:
        # compute rip-relative target
        try:
            tgt = int(ins.op_str.split("# ")[-1].replace("0x", ""), 16) if "#" in ins.op_str else None
        except Exception:
            tgt = None
        if tgt is None:
            # parse 'QWORD PTR [rip+0x...]' comment-less forms
            import re
            m = re.search(r"rip (?:\+|)(0x[0-9a-f]+|\- 0x[0-9a-f]+|\-0x[0-9a-f]+)", ins.op_str)
            if m:
                off = int(m.group(1).replace(" ", "").replace("-", "-"), 16)
                tgt = ins.address + ins.size + off
        if tgt and tgt in iat_map:
            ann = f"  ; {iat_map[tgt]}"
    print(f"{ins.address:#x}  {ins.mnemonic} {ins.op_str}{mark}{ann}")
print("\nimage base:", hex(imgbase))
