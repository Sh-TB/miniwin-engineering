#!/usr/bin/env python3
"""SWEEP-048 v2: CAB reader (compression NONE or per-block MSZIP-skip) per MSBZ spec."""
import struct, os, sys

path = sys.argv[1] if len(sys.argv) > 1 else "msi_cab/_______.cab"
out = sys.argv[2] if len(sys.argv) > 2 else "msi_files"
os.makedirs(out, exist_ok=True)
d = open(path, "rb").read()
assert d[:4] == b"MSCF"
cbCabinet, = struct.unpack_from("<I", d, 8)
coffFiles, = struct.unpack_from("<I", d, 16)
cFiles, cFolders = struct.unpack_from("<HH", d, 20)
flags, = struct.unpack_from("<H", d, 30)
print(f"size={cbCabinet} files={cFiles} folders={cFolders} flags={hex(flags)}")

hdr_end = 36
if flags & 0x0004:
    hdr_end += 4 + struct.unpack_from("<I", d, hdr_end)[0] + 8  # cbReserve + abReserve + prev/next

folders = []
p = hdr_end
for _ in range(cFolders):
    coffCFDATA, cCFDATA, tCompress = struct.unpack_from("<IHH", d, p)
    p += 8
    if flags & 0x0100:  # reservCFFOLDER
        cb = d[p]; p += 1 + cb
    # two ASCIIZ
    p += 1 + d[p]
    p += 1 + d[p]
    folders.append((coffCFDATA, cCFDATA, tCompress))
print("folders:", folders[:3], "method:", [f[2] for f in folders])

# CFFILE entries at coffFiles
ents = []
p = coffFiles
for _ in range(cFiles):
    usize, uoffset, iFolder, fdate, ftime, fattrib = struct.unpack_from("<IIHHHH", d, p)
    p += 16
    e = d.index(b"\x00", p)
    name = d[p:e].decode("utf-8", "replace")
    p = e + 1
    ents.append((name, usize, uoffset, iFolder))
print("first entries:", ents[:3])

# blocks: CFDATA = csum(4) cbData(2) cbUncomp(2) then data
def folder_data(idx):
    coffCFDATA, cCFDATA, tCompress = folders[idx]
    p = hdr_end  # CFDATA blocks are laid sequentially starting at hdr_end? No — coffCFDATA is absolute offset
    p = coffCFDATA
    data = bytearray()
    for _ in range(cCFDATA):
        cbData, cbUncomp = struct.unpack_from("<HH", d, p + 4)
        raw = d[p + 8: p + 8 + cbData]
        if tCompress == 0:
            data += raw
        else:
            raise SystemExit(f"compressed folder {tCompress} unsupported")
        p += 8 + cbData
    return bytes(data)

ok = 0
for (name, usize, uoffset, iFolder) in ents:
    idx = iFolder if iFolder < 0xFFFE else 0
    data = folder_data(idx)
    blob = data[uoffset:uoffset + usize]
    safe = "".join(ch if ch.isalnum() or ch in "._- " else "_" for ch in name)
    with open(f"{out}/{safe}", "wb") as f:
        f.write(blob)
    ok += 1
print(f"extracted {ok}/{cFiles} -> {out}/")
