import sys
with open('samples/upx_decompressed.exe','rb') as f:
    data = f.read()

def xdata_off(rva):
    return 0x1ff400 + (rva - 0x201000)

# Check all frames' UNWIND_INFO
frames = [
    (0x49d5b1, 0x9d560, 0x2098b4, 'Frame[0]'),
    (0x4e0203, 0xe0190, 0x20931c, 'Frame[1]'),
    (0x4e02d9, 0xe0290, 0x201270, 'Frame[2]'),
    (0x401593, 0x1570,  0x20107c, 'Frame[3]'),
    (0x4032f2, 0x32c0,  0x2011f0, 'Frame[4]'),
]
for rip_rva, begin, ui_rva, label in frames:
    off = xdata_off(ui_rva)
    ui = data[off:off+32]
    flags = (ui[0] >> 3) & 0x07
    count = ui[2]
    frame_reg = (ui[3] >> 4) & 0x0f
    frame_off = ui[3] & 0x0f
    # Quick total alloc
    total = 0
    slot = 0
    for i in range(count):
        cw = ui[4 + slot*2] | (ui[4 + slot*2 + 1] << 8)
        op = (cw >> 8) & 0x0f
        info = (cw >> 12) & 0x0f
        if op == 0: total += 8; slot += 1
        elif op == 1:
            slot += 1
            if info == 0:
                val = ui[4 + slot*2] | (ui[4 + slot*2 + 1] << 8)
                total += val; slot += 1
            else:
                slot += 3; total += 0  # skip for now
        elif op == 2: total += (info+1)*8; slot += 1
        elif op == 3: pass  # SET_FPREG
        elif op == 4: slot += 2
        elif op == 5: slot += 3
        else: slot += 1
    fpreg = 'YES' if flags & 4 == 0 and frame_reg != 0 and frame_off != 0 else 'NO'
    print(f'{label}: RIP=0x{rip_rva:x} begin=0x{begin:x} ui=0x{ui_rva:x}')
    print(f'  Flags=0x{flags:x} Codes={count} FpReg=R{frame_reg} FpOff={frame_off} ALLOC=0x{total:x}')
    print(f'  SET_FPREG: {fpreg}  EHANDLER: {bool(flags&1)}  CHAININFO: {bool(flags&4)}')
    print(f'  Est frame = RSP + 0x{total:x}  (fpreg: {fpreg})')
    print()
