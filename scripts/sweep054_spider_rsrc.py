#!/usr/bin/env python3
"""SWEEP-054: dump spider.exe resource tree (types/ids/sizes) + RT_ACCELERATOR entries."""
import struct

path = "artifacts/sweep054/guests/spider/spider.exe"
data = open(path, "rb").read()
e_lfanew = struct.unpack_from("<I", data, 0x3C)[0]
nsec = struct.unpack_from("<H", data, e_lfanew + 6)[0]
optsz = struct.unpack_from("<H", data, e_lfanew + 20)[0]
opt = e_lfanew + 24
rsrc_rva, rsrc_sz = struct.unpack_from("<II", data, opt + 112 + 16)
so = opt + optsz
secs = []
for i in range(nsec):
    name = data[so:so+8].rstrip(b"\0").decode()
    vsz, va, rsz, raw = struct.unpack_from("<IIII", data, so + 8)
    secs.append((name, va, vsz, raw)); so += 40

def r2o(rva):
    for name, va, vsz, raw in secs:
        if va <= rva < va + max(vsz, 0x1000):
            return raw + (rva - va)
    return None

base = r2o(rsrc_rva)
TYPES = {1:"CURSOR",2:"BITMAP",3:"ICON",4:"MENU",5:"DIALOG",6:"STRING",7:"FONTDIR",
         8:"FONT",9:"ACCELERATOR",10:"RCDATA",11:"MESSAGETABLE",12:"GROUP_CURSOR",
         14:"GROUP_ICON",16:"VERSION",24:"MANIFEST"}

def label_of(node):
    nameid = node
    if nameid & 0x80000000:
        no = base + (nameid & 0x7FFFFFFF)
        ln = struct.unpack_from("<H", data, no)[0]
        return data[no+2:no+2+ln*2].decode("utf-16le")
    return str(nameid)

def walk(off, level, tname):
    nnamed, nid = struct.unpack_from("<HH", data, off + 12)
    for i in range(nnamed + nid):
        nameid, ofs = struct.unpack_from("<II", data, off + 16 + 8*i)
        lab = label_of(nameid)
        if ofs & 0x80000000:
            walk(base + (ofs & 0x7FFFFFFF), level+1, tname if tname else lab)
        else:
            drva, dsz = struct.unpack_from("<II", data, base + ofs)
            extra = ""
            if tname == "ACCELERATOR":
                o = r2o(drva)
                accs = []
                for k in range(min(dsz // 8, 16)):
                    fv, ans, kid, pad = struct.unpack_from("<BBHI", data, o + 8*k)
                    accs.append((hex(fv), ans, kid))
                extra = f" entries={dsz//8} {accs}"
            tdisp = tname if tname else "?"
            print(f"{'  '*level}{tdisp:14s} id={lab:>6s} size={dsz}{extra}")

walk(base, 0, None)
