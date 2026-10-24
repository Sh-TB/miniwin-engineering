#!/usr/bin/env python3
"""SWEEP-059: restage every expanded .exe as a guest (with sibling DLLs),
correct subsystem detection this time (SUBSYSTEM field, not CHARACTERISTICS)."""
import os, struct, shutil

root = 'artifacts/sweep059/expanded'
guests = 'artifacts/sweep059/guests'
os.makedirs(guests, exist_ok=True)

def pe_info(path):
    try:
        with open(path,'rb') as f: d = f.read(4096)
        if d[:2] != b'MZ': return None
        pe = struct.unpack_from('<I', d, 0x3c)[0]
        with open(path,'rb') as f:
            f.seek(pe); sig = f.read(4)
            if sig != b'PE\x00\x00': return None
            f.seek(pe+4); mach = struct.unpack('<H', f.read(2))[0]
            f.seek(pe+24); optmagic = struct.unpack('<H', f.read(2))[0]  # magic is at optional-header start (pe+24)
            off = pe + 24 + (68 if optmagic == 0x20b else 68 if optmagic==0x10b else None)
            f.seek(pe + 24 + (68 if optmagic == 0x20b else 68))
            subsys = struct.unpack('<H', f.read(2))[0]
        arch = 'x64' if mach == 0x8664 else ('x86' if mach == 0x14c else hex(mach))
        sub = {2:'GUI',3:'CLI'}.get(subsys, f'sub{subsys}')
        return arch, sub
    except Exception:
        return None

rows = []
for dirpath, _, files in os.walk(root):
    for fn in files:
        if not fn.lower().endswith('.exe'): continue
        p = os.path.join(dirpath, fn)
        info = pe_info(p)
        if not info: continue
        arch, sub = info
        # skip ARM64 builds (0xaa64) — engine targets amd64
        if arch not in ('x64','x86'): continue
        gname = os.path.splitext(fn)[0]
        # unique guest name: dir prefix when collision-prone
        gd = os.path.join(guests, gname)
        if os.path.exists(gd):
            pref = os.path.basename(dirpath).split('-')[0]
            gd = os.path.join(guests, f"{gname}_{pref}")
        os.makedirs(gd, exist_ok=True)
        shutil.copy2(p, os.path.join(gd, fn))
        # sibling DLLs travel with the exe
        for sib in os.listdir(dirpath):
            if sib.lower().endswith('.dll'):
                sp = os.path.join(dirpath, sib)
                dp = os.path.join(gd, sib)
                if not os.path.exists(dp):
                    shutil.copy2(sp, dp)
        rows.append((gname, arch, sub, os.path.getsize(p), os.path.relpath(dirpath, root)))

rows.sort()
with open('artifacts/sweep059/guest_inventory.tsv','w') as f:
    f.write("guest\tarch\tsubsystem\tbytes\tdir\n")
    for r in rows:
        f.write('\t'.join(map(str,r)) + '\n')
print(f"staged {len(rows)} guests")
from collections import Counter
print(Counter((r[1], r[2]) for r in rows))
