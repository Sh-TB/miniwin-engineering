#!/usr/bin/env python3
"""SWEEP-055 (SW-1273): compute the STATIC import closure of soffice.bin from
the CAB extract — the SWEEP-051 staging recipe (soffice.bin + sal3 + mergedlo
+ transitive static imports + VC runtime). Dynamic LoadLibrary targets (nss3
family etc.) are deliberately absent: their CRT init is the SW-1276 frontier
and the census baseline (rc=99, 8 stubs) was measured without them."""
import struct, os, shutil, sys

SRC = "artifacts/sweep055/lo_extract"
DST = "artifacts/sweep055/guests/soffice_closure"

def pe_imports(path):
    """Return list of imported DLL names (static import table only)."""
    try:
        data = open(path, "rb").read()
    except OSError:
        return []
    if len(data) < 0x200 or data[:2] != b"MZ":
        return []
    e_lfanew = struct.unpack_from("<I", data, 0x3C)[0]
    if data[e_lfanew:e_lfanew + 4] != b"PE\0\0":
        return []
    coff = e_lfanew + 4
    optsz = struct.unpack_from("<H", data, coff + 16)[0]
    magic = struct.unpack_from("<H", data, coff + 20)[0]
    ddoff = coff + 20 + (112 if magic == 0x20b else 96)
    imp_rva, imp_sz = struct.unpack_from("<II", data, ddoff + 8)
    nsec = struct.unpack_from("<H", data, coff + 2)[0]
    so = coff + 16 + optsz
    secs = []
    for i in range(nsec):
        vsz, va, rsz, raw = struct.unpack_from("<IIII", data, so + 8 + 40 * i)
        secs.append((va, max(vsz, rsz), raw))
    def r2o(rva):
        for va, sz, raw in secs:
            if va <= rva < va + sz:
                return raw + (rva - va)
        return None
    out = []
    o = r2o(imp_rva)
    if o is None:
        return []
    while True:
        ent = data[o:o + 20] if o + 20 <= len(data) else b""
        if len(ent) < 20 or ent == b"\0" * 20:
            break
        name_rva = struct.unpack_from("<I", ent, 12)[0]
        no = r2o(name_rva)
        if no is None:
            break
        nm = data[no:data.index(b"\0", no)].decode("ascii", "replace")
        out.append(nm)
        o += 20
    return out

# BFS closure
core = ["soffice.bin"]
seen = set()
queue = list(core)
while queue:
    dll = queue.pop(0)
    key = dll.lower()
    if key in seen:
        continue
    seen.add(key)
    p = os.path.join(SRC, dll)
    if not os.path.exists(p):
        continue
    for imp in pe_imports(p):
        ikey = imp.lower()
        if ikey not in seen and os.path.exists(os.path.join(SRC, imp)):
            queue.append(imp)
closure = sorted(seen)
present = [d for d in closure if os.path.exists(os.path.join(SRC, d))]
missing = [d for d in closure if not os.path.exists(os.path.join(SRC, d))]
print(f"static closure: {len(present)} present, {len(missing)} missing (host/system)")
print("missing:", missing)
os.makedirs(DST, exist_ok=True)
for f in present:
    shutil.copy2(os.path.join(SRC, f), os.path.join(DST, f))
# config payloads the bootstrap reads
for extra in ("soffice.ini", "version.ini", "sofficerc"):
    if os.path.exists(os.path.join(SRC, extra)):
        shutil.copy2(os.path.join(SRC, extra), os.path.join(DST, extra))
print(f"STAGED {len(present)} files -> {DST}")
