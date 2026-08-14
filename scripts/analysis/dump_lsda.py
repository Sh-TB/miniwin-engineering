import struct, sys

with open('samples/upx_decompressed.exe', 'rb') as f:
    data = f.read()

sections = [
    (0x1000, 0xdfbb0, 0x400, 0),
    (0x4e1000, 0xa50, 0xe0000, 0),
    (0x4e2000, 0x115800, 0xe0c00, 0),
    (0x5f8000, 0x8e08, 0x1f6400, 0),
    (0x601000, 0x937c, 0x1ff400, 0),
    (0x60b000, 0x2ec0, 0, 0),
    (0x60e000, 0x16a8, 0x208800, 0),
]

def rva_to_off(rva):
    for name, vma, vs, rawoff, rsz in sections:
        if vma <= rva < vma + vs:
            return rawoff + (rva - vma)
    return None

def dump_ui(ui_rva, label):
    off = rva_to_off(ui_rva)
    if off is None:
        print(f'{label}: RVA not found in any section'); return
    ui = data[off:off+64]
    ver = ui[0] & 0x07
    flags = (ui[0] >> 3) & 0x03
    prolog = ui[1]
    count = ui[2]
    print(f'{label}: ver={ver} flags=0x{flags:x} prolog={prolog} codes={count}')
    slot = 0
    for i in range(count):
        cw = struct.unpack_from('<H', ui, 4 + i*2)[0]
        op = (cw >> 8) & 0x0f
        info = (cw >> 12) & 0x0f
        off = cw & 0xff
        names = ['PUSH_NONVOL','ALLOC_LARGE','ALLOC_SMALL','SET_FPREG','SAVE_NONVOL','SAVE_NONVOL_FAR','SAVE_XMM','SAVE_XMM_FAR','PUSH_MACHFRAME']
        n = names[op] if op < 9 else f'UNK({op})'
        extra = ''
        if op in [1,5,7]: slot += 1
        if op == 1: extra = f' alloc={struct.unpack_from("<H", ui, 4 + slot*2)[0]}'; slot += 1
        print(f'  Code[{i}]: {n}(info={info}, off={off}){extra}')
        slot += 1
    h_off = 4 + slot * 2
    if h_off % 4: h_off += 2
    if flags & 1:
        hr = struct.unpack_from('<I', ui, h_off)[0]
        lo = h_off + 4
        print(f'  Handler RVA: 0x{hr:x}')
        lsda = data[lo:lo+min(64, len(data)-lo)]
        print(f'  LSDA ({len(lsda)} bytes):')
        for j in range(0, min(64, len(lsda))):
            hex_str = ' '.join(f'{b:02x}' for b in lsda[j:j+16])
            print(f'    {j:3d}: {hex_str}')
    elif flags & 2:
        h_off = 4 + slot * 2
        if h_off % 4: h_off += 2
        hr = struct.unpack_from('<I', ui, h_off)[0]
        print(f'  UHANDLER RVA: 0x{hr:x}')
    elif flags & 4:
        h_off = 4 + slot * 2
        if h_off % 4: h_off += 2
        print(f'  CHAININFO -> chained RF')

for arg in sys.argv[1:]:
    rva = int(arg, 16)
    dump_ui(rva, f'0x{rva:x}')
