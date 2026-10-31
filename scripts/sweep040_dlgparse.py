#!/usr/bin/env python3
"""SWEEP-040 SW-1206: enumerate adexplorer64 RT_DIALOG resources and parse the
DLGTEMPLATE that contains control IDs 1002 (0x3ea) / 1070 (0x42e) — the crash
sequence needs GetDlgItem(hDlg,1002) then (hDlg,1070) to resolve."""
import struct

EXE = 'artifacts/sweep50/dl/adexplorer64/adexplorer64.exe'
IB = 0x140000000
data = open(EXE, 'rb').read()

e = struct.unpack_from('<I', data, 0x3c)[0]
optsz = struct.unpack_from('<H', data, e + 20)[0]
nsec = struct.unpack_from('<H', data, e + 6)[0]
sec0 = e + 24 + optsz
secs = []
for i in range(nsec):
    off = sec0 + 40 * i
    name = data[off:off+8].rstrip(b'\0').decode()
    vsz, va, rsz, rp = struct.unpack_from('<IIII', data, off + 8)
    secs.append((name, va, vsz, rp, rsz))

def rva2off(rva):
    for name, va, vsz, rp, rsz in secs:
        if va <= rva < va + max(vsz, rsz):
            return rp + (rva - va)
    return None

# .rsrc section
rsrc = [s for s in secs if s[0] == '.rsrc'][0]
_, rva, vsz, rp, rsz = rsrc
base = rp  # file offset of resource dir (rva 0x9a000)

def read_u16(o): return struct.unpack_from('<H', data, o)[0]
def read_u32(o): return struct.unpack_from('<I', data, o)[0]

def walk_dir(off, level, path):
    charac, tstamp, mj, mn, nnamed, nid = struct.unpack_from('<IIHHHH', data, off)
    entries = off + 16
    for i in range(nnamed + nid):
        eo = entries + 8 * i
        nameid, offo = struct.unpack_from('<II', data, eo)
        if nameid & 0x80000000:
            so = base + (nameid & 0x7FFFFFFF)
            ln = read_u16(so)
            nm = data[so+2:so+2+2*ln].decode('utf-16-le', 'replace')
        else:
            nm = str(nameid)
        if offo & 0x80000000:
            yield from walk_dir(base + (offo & 0x7FFFFFFF), level + 1, path + [nm])
        else:
            # leaf: IMAGE_RESOURCE_DATA_ENTRY
            drva, dsz, cp, rv = struct.unpack_from('<IIII', data, base + offo)
            yield path + [nm], drva, dsz

def wstr(o):
    ln = 0
    while read_u16(o + 2*ln):
        ln += 1
    return data[o:o+2*ln+2].decode('utf-16-le', 'replace'), o + 2*ln + 2

CLASS_ATOMS = {0x80: 'BUTTON', 0x81: 'EDIT', 0x82: 'STATIC', 0x83: 'LISTBOX',
               0x84: 'SCROLLBAR', 0x85: 'COMBOBOX'}

def parse_dlg(buf_off, size, label):
    o = buf_off
    w0, w1 = read_u16(o), read_u16(o+2)
    items = []
    if w0 == 1 and w1 == 0xFFFF:
        # DLGTEMPLATEEX
        helpid, exsty, sty, cdit = read_u32(o+4), read_u32(o+8), read_u32(o+12), read_u16(o+16)
        x, y, cx, cy = struct.unpack_from('<hhhh', data, o+18)
        o2 = o + 26
        menu, o2 = wstr(o2) if read_u16(o2) != 0 else ('', o2+2)
        if read_u16(o2) == 0:
            o2 += 2
        else:
            if read_u16(o2) == 0xFFFF:
                o2 += 4
            else:
                _, o2 = wstr(o2)
        title, o2 = wstr(o2)
        if sty & 0x40 or sty & 0x10:  # DS_SETFONT or DS_SHELLFONT
            o2 += 2  # pointsize
            o2 += 4  # weight(2)+italic(1)+charset(1)
            _, o2 = wstr(o2)
        for i in range(cdit):
            o2 = (o2 + 3) & ~3
            hhelp, hexs, hsty = read_u32(o2), read_u32(o2+4), read_u32(o2+8)
            ix, iy, icx, icy = struct.unpack_from('<hhhh', data, o2+12)
            iid = read_u32(o2+20)
            o2 += 24
            if read_u16(o2) == 0xFFFF:
                cls = CLASS_ATOMS.get(read_u16(o2+2), f'atom{read_u16(o2+2):#x}')
                o2 += 4
            else:
                cls, o2 = wstr(o2)
            if read_u16(o2) == 0xFFFF:
                ttl = f'ord{read_u16(o2+2)}'
                o2 += 4
            else:
                ttl, o2 = wstr(o2)
            o2 += 2 + read_u16(o2)  # cbExtra + data (read_u16(o2) is cbExtra)
            items.append((iid, cls, ttl, hsty))
        fmt = 'EX'
    else:
        sty, exsty, cdit = read_u32(o), read_u32(o+4), read_u16(o+8)
        o2 = o + 12
        if read_u16(o2) == 0:
            o2 += 2
        elif read_u16(o2) == 0xFFFF:
            o2 += 4
        else:
            _, o2 = wstr(o2)
        if read_u16(o2) == 0:
            o2 += 2
        else:
            if read_u16(o2) == 0xFFFF:
                o2 += 4
            else:
                _, o2 = wstr(o2)
        title, o2 = wstr(o2)
        if sty & 0x40:
            o2 += 2
            _, o2 = wstr(o2)
        for i in range(cdit):
            o2 = (o2 + 3) & ~3
            hsty, hexs = read_u32(o2), read_u32(o2+4)
            ix, iy, icx, icy = struct.unpack_from('<hhhh', data, o2+8)
            iid = read_u16(o2+16)
            o2 += 18
            if read_u16(o2) == 0xFFFF:
                cls = CLASS_ATOMS.get(read_u16(o2+2), f'atom{read_u16(o2+2):#x}')
                o2 += 4
            else:
                cls, o2 = wstr(o2)
            if read_u16(o2) == 0xFFFF:
                ttl = f'ord{read_u16(o2+2)}'
                o2 += 4
            else:
                ttl, o2 = wstr(o2)
            o2 += 2 + read_u16(o2)
            items.append((iid, cls, ttl, hsty))
        fmt = 'STD'
    ids = [it[0] for it in items]
    hit = 1002 in ids or 1070 in ids
    print(f"{label} [{fmt}] items={len(items)} title={title!r} "
          f"{'<<<< CONTAINS 1002/1070' if hit else ''}")
    if hit or len(items) < 40:
        for iid, cls, ttl, hsty in items[:60]:
            print(f"    id={iid:<6} {cls:12s} style={hsty:#010x} title={ttl[:40]!r}")
    return hit

found = 0
for path, drva, dsz in walk_dir(rp, 0, []):
    if len(path) >= 2 and path[0] == '5':  # RT_DIALOG
        off = rva2off(drva)
        label = f"dlg {path[1:]} rva={drva:#x} size={dsz}"
        if parse_dlg(off, dsz, label):
            found += 1
print(f"\ndialog resources containing the crash IDs: {found}")
