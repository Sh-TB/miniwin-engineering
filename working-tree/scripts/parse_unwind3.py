import sys
with open('samples/upx_decompressed.exe','rb') as f:
    data = f.read()

def xdata_off(rva):
    return 0x1ff400 + (rva - 0x201000)

# Frame[0]: begin=0x9d560, UI=0x2098b4, ALLOC=0x28, 1 code
ui_rva = 0x2098b4
off = xdata_off(ui_rva)
ui = data[off:off+32]
count = ui[2]
flags = (ui[0] >> 3) & 0x07
print(f'Frame[0] UI at 0x{ui_rva:x}:')
print(f'  Version={ui[0]&7} Flags=0x{flags:x} PrologSize={ui[1]} Codes={count}')
for i in range(count):
    cw = ui[4 + i*2] | (ui[4 + i*2 + 1] << 8)
    op = (cw >> 8) & 0x0f
    info = (cw >> 12) & 0x0f
    code_off = cw & 0xff
    print(f'  Code[{i}]: word=0x{cw:04x} op={op} info={info} offset={code_off}')
    if op == 0:
        print(f'    PUSH_NONVOL R{info}')
    elif op == 1:
        if info == 0:
            slot = i + 1
            val = ui[4 + slot*2] | (ui[4 + slot*2 + 1] << 8)
            print(f'    ALLOC_LARGE(16) size={val}')
        else:
            slot = i + 1
            lo = ui[4 + slot*2] | (ui[4 + slot*2 + 1] << 8)
            hi = ui[4 + (slot+1)*2] | (ui[4 + (slot+1)*2 + 1] << 8)
            print(f'    ALLOC_LARGE(32) size={(hi<<16)|lo}')
    elif op == 2:
        print(f'    ALLOC_SMALL size={(info+1)*8}')
    elif op == 8:
        print(f'    SAVE_XMM128')
    elif op == 9:
        print(f'    SAVE_XMM128_FAR')
    else:
        print(f'    UNKNOWN op={op}')

print(f'Expected ALLOC=0x28 (40 bytes)')
print(f'Actual function prolog bytes:')
func_off = 0x400 + (0x9d560 - 0x1000)  # file offset of function
for i in range(16):
    print(f'  +{i:02x}: {data[func_off+i]:02x}', end=' ')
    print()

# Check if RtlVirtualUnwind is being called correctly
# by looking at what code words are actually read
print('Raw UNWIND_INFO bytes:')
for i in range(32):
    print(f' {data[off+i]:02x}', end='')
    if i % 16 == 15: print()
