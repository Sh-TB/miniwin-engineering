#!/usr/bin/env python3
import struct, sys

p = sys.argv[1] if len(sys.argv) > 1 else \
    '/home/z/my-project/workspace/miniwin-engineering/artifacts/sweep50/dl/coreinfoex64/Coreinfo64.exe'
d = open(p, 'rb').read()
pe = struct.unpack_from('<I', d, 0x3c)[0]
nsec = struct.unpack_from('<H', d, pe + 6)[0]
optsz = struct.unpack_from('<H', d, pe + 20)[0]
rsrc = None
for i in range(nsec):
    off = pe + 24 + optsz + i * 40
    name = d[off:off + 8].rstrip(b'\x00').decode('latin-1')
    vs, va, rs, ra = struct.unpack_from('<IIII', d, off + 8)
    if name == '.rsrc':
        rsrc = (va, rs, ra)
if not rsrc:
    print('NO .rsrc'); sys.exit(1)
va, rs, ra = rsrc
print(f'.rsrc va={va:#x} size={rs:#x} raw={ra:#x}')

TYPES = {1:'CURSOR',2:'BITMAP',3:'ICON',4:'MENU',5:'DIALOG',6:'STRING',9:'ACCEL',
         10:'RCDATA',11:'MESSAGETABLE',12:'GROUP_CURSOR',14:'GROUP_ICON',16:'VERSION',
         23:'HTML',24:'MANIFEST'}

def read_name(nameid):
    if nameid & 0x80000000:
        so = ra + (nameid & 0x7fffffff)
        ln = struct.unpack_from('<H', d, so)[0]
        return d[so+2:so+2+ln*2].decode('utf-16-le', 'replace')
    return str(nameid)

def walk(off, level=0, path=()):
    chars, _, _, _, nnamed, nid = struct.unpack_from('<IIHHHH', d, off)
    for k in range(nnamed + nid):
        nameid, valoff = struct.unpack_from('<II', d, off + 16 + k * 8)
        label = read_name(nameid)
        if valoff & 0x80000000:
            print('  ' * level + label)
            walk(ra + (valoff & 0x7fffffff), level + 1, path + (label,))
        else:
            entry_off = ra + (valoff & 0x7fffffff)
            rva, size = struct.unpack_from('<II', d, entry_off)
            print('  ' * level + f'{label} -> data RVA={rva:#x} size={size}')
walk(ra)
