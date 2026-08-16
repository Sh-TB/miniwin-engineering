import sys
with open('samples/upx_decompressed.exe','rb') as f:
    data = f.read()

def xdata_off(rva):
    return 0x1ff400 + (rva - 0x201000)
ui_rva = 0x2011f0
off = xdata_off(ui_rva)
print(f'UNWIND_INFO for function at RVA 0x32c0 (UI RVA 0x{ui_rva:x}):')
ui = data[off:off+64]
flags3 = ui[0]
version = flags3 & 0x07
flags = (flags3 >> 3) & 0x07
prolog_size = ui[1]
count_codes = ui[2]
frame_reg = (ui[3] >> 4) & 0x0f
frame_off = ui[3] & 0x0f
print(f'Version: {version}, Flags: 0x{flags:x}, PrologSize: {prolog_size}')
print(f'CountCodes: {count_codes}, FrameReg: {frame_reg}, FrameOff: {frame_off}')
print(f'EHANDLER={bool(flags & 1)}, UHANDLER={bool(flags & 2)}, CHAININFO={bool(flags & 4)}')
print('Unwind codes:')
total = 0
slot = 0
for i in range(count_codes):
    cw = ui[4 + slot*2] | (ui[4 + slot*2 + 1] << 8)
    op = (cw >> 8) & 0x0f
    info = (cw >> 12) & 0x0f
    code_off = cw & 0xff
    print(f'  [{i}] off={code_off:3d} op={op} info={info} 0x{cw:04x}', end='')
    if op == 0: print(f' PUSH_NONVOL R{info}'); total += 8; slot += 1
    elif op == 1:
        slot += 1
        if info == 0:
            val = ui[4 + slot*2] | (ui[4 + slot*2 + 1] << 8)
            print(f' ALLOC_LARGE(16) size={val}'); total += val; slot += 1
        else:
            slot += 2
            lo = ui[4 + slot*2] | (ui[4 + slot*2 + 1] << 8)
            hi = ui[4 + (slot+1)*2] | (ui[4 + (slot+1)*2 + 1] << 8)
            val = (hi << 16) | lo
            print(f' ALLOC_LARGE(32) size={val}'); total += val; slot += 2
    elif op == 2: print(f' ALLOC_SMALL size={(info+1)*8}'); total += (info+1)*8; slot += 1
    elif op == 3: print(f' SET_FPREG R{frame_reg} off={frame_off}')
    elif op == 4: print(f' SAVE_NONVOL R{info}'); slot += 2
    elif op == 5: print(f' SAVE_NONVOL_FAR R{info}'); slot += 3
    else: print(f' op{op}'); slot += 1
h_off = 4 + count_codes * 2
if h_off % 4: h_off += 2
handler_rva = ui[h_off] | (ui[h_off+1]<<8) | (ui[h_off+2]<<16) | (ui[h_off+3]<<24)
print(f'\nHandler RVA: 0x{handler_rva:x}')
lsda_rva = ui[h_off+4] | (ui[h_off+5]<<8) | (ui[h_off+6]<<16) | (ui[h_off+7]<<24)
print(f'LSDA RVA: 0x{lsda_rva:x}')
print(f'\nTotal stack alloc from unwind codes: 0x{total:x} ({total} bytes)')
print(f'Actual prolog: push r12(8) + push rbx(8) + sub 0x58(88) = 104 (0x68)')
print(f'With return address: 112 (0x70)')
print(f'\nIMPORTANT: If total != 104, the unwind codes dont match the prolog!')
print(f'This means the establisher frame will be WRONG.')
