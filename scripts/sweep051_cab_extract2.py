#!/usr/bin/env python3
"""SWEEP-051 Train 1 (SW-1261): expand LibreOffice payload CAB — final layout:
sig@0 cbCabinet@8 | mystery@0x10(=0xb0=176 CFFOLDER off) | ver@0x18 cFolders@0x1a
cFiles@0x1c flags@0x1e setID@0x20 iCab@0x22 | reserveDecl@0x24 (128,0,0)
reserveData@0x28..168 | CFFOLDER@168 | CFFILE | CFDATA (all compression=0)
Files addressed DIRECTLY inside CAB: uncomp stream == raw concatenation of
block payloads. Selective extract by name pattern.
"""
import struct, sys, os

CAB = "/home/z/my-project/workspace/miniwin-engineering/artifacts/sweep051/lo_payload.cab"
OUT = "/home/z/my-project/workspace/miniwin-engineering/artifacts/sweep051/lo_extracted"

f = open(CAB, "rb")

cFolders = 1
folders = []
o = 168
for i in range(cFolders):
    f.seek(o)
    coffCabStart, cCFData, typeComp = struct.unpack("<IHH", f.read(8))
    folders.append((coffCabStart, cCFData, typeComp))
    print(f"folder {i}: coffCabStart={coffCabStart} blocks={cCFData} comp={typeComp:#x}")
    assert (typeComp & 0xF) == 0, "non-stored compression"
    o += 8

# CFFILE entries — count from header
f.seek(28)
cFiles = struct.unpack_from("<H", f.read(2), 0)[0]
print("cFiles =", cFiles)

files = []
for i in range(cFiles):
    f.seek(o)
    rec = f.read(16)
    if len(rec) < 16:
        break
    cbFile, uoff, iFolder, fdate, ftime, attribs = struct.unpack_from("<IIHHHH", rec, 0)
    f.seek(o + 16)
    name = f.read(512)
    name = name.split(b"\x00", 1)[0]
    o += 16 + len(name) + 1
    files.append((cbFile, uoff, iFolder, name.decode("utf-8", "replace")))
print(f"parsed {len(files)} CFFILE entries; CFDATA starts at {o}")

# CFDATA blocks per folder
def folder_spans(fi):
    coffCabStart, cCFData, typeComp = folders[fi]
    spans = []  # (uncomp_start, payload_off, payload_len)
    p = coffCabStart
    u = 0
    for b in range(cCFData):
        f.seek(p)
        cbData, cbUncomp = struct.unpack("<HH", f.read(4))
        p += 4
        spans.append((u, p, cbData))
        p += cbData
        u += cbUncomp
    return spans

all_spans = {fi: folder_spans(fi) for fi in range(cFolders)}
for fi, sp in all_spans.items():
    print(f"folder {fi}: {len(sp)} blocks, uncomp total {sp[-1][0] + sp[-1][2]}")

mode = sys.argv[1] if len(sys.argv) > 1 else "list"
if mode == "list":
    import re
    pat = re.compile(r"^(soffice|sal3|vcl|skialo|mergedlo|ucp|utl|tl|basegfx|i18n|fwk|svt|tk|svl|comphelper|cppu|cppuhelper|uwinapi|gdiplus|msvcp140|vcruntime140|ucrtbase)", re.I)
    hits = [x for x in files if pat.match(x[3])]
    print(f"pattern hits: {len(hits)}")
    for cb, uo, ifo, nm in hits[:40]:
        print(f"  {cb:>10}  {nm}")
    print("--- all exe/bin:")
    for cb, uo, ifo, nm in files:
        if nm.lower().endswith((".exe", ".bin")):
            print(f"  {cb:>10}  {nm}")
    sys.exit(0)

os.makedirs(OUT, exist_ok=True)
PATTERNS = (".exe", ".bin", ".dll", ".ini", ".xml", ".rc", ".xcs", ".xcu", ".dat")
MAXSZ = 60 * 1024 * 1024
n = 0
total_bytes = 0
for (cbFile, uoff, iFolder, nm) in files:
    if not nm.lower().endswith(PATTERNS) or cbFile > MAXSZ:
        continue
    sp = all_spans.get(iFolder)
    if not sp:
        continue
    buf = bytearray()
    need_start, need_end = uoff, uoff + cbFile
    for (ustart, poff, plen) in sp:
        uend = ustart + plen
        if uend <= need_start or ustart >= need_end:
            continue
        s = max(need_start, ustart)
        e = min(need_end, uend)
        f.seek(poff + (s - ustart))
        buf.extend(f.read(e - s))
    dest = os.path.join(OUT, nm.replace("\\", "/"))
    os.makedirs(os.path.dirname(dest), exist_ok=True)
    with open(dest, "wb") as g:
        g.write(buf)
    n += 1
    total_bytes += len(buf)
print(f"extracted {n} files, {total_bytes} bytes -> {OUT}")
