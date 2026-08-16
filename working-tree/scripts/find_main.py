import struct, subprocess, re

with open('samples/upx_decompressed.exe','rb') as f:
    data = f.read()

base = 0x400000
rdata_rva = 0xe2000
rdata_file = 0xe0c00
version_rva = 0xe7876  # '--version' string
rva_bytes = struct.pack('<I', version_rva)

text_start_rva = 0x1000
text_size = 0xdfbb0
text_file = 0x400

found = 0
for i in range(text_file, text_file + text_size - 4):
    if data[i:i+4] == rva_bytes:
        rva_here = text_start_rva + (i - text_file)
        va = base + rva_here
        print(f'Found ref to version_str at RVA=0x{rva_here:x} (VA=0x{va:x})')
        result = subprocess.run(
            ['/tmp/my-project/tools/llvm-mingw-20240917-ucrt-ubuntu-20.04-x86_64/bin/x86_64-w64-mingw32-objdump',
             '-d', f'--start-address=0x{va-10:x}',
             f'--stop-address=0x{va+25:x}',
             'samples/upx_decompressed.exe'],
            capture_output=True, text=True)
        for line in result.stdout.split('\n'):
            if line.strip() and not line.startswith('upx_') and not line.startswith('Disassembly'):
                print(f'  {line}')
        print()
        found += 1
        if found >= 5:
            break

if found == 0:
    print('No direct references found. Trying RIP-relative search...')
    # Search for lea instructions where disp would target our string
    # lea reg, [rip+disp32] at va X means: target = X+7 + disp32
    # So disp32 = target_va - X - 7 = 0x40e7876 - X - 7
    for text_off in range(0, text_size - 7):
        va = base + text_start_rva + text_off
        instr = data[text_file + text_off:text_file + text_off + 7]
        if len(instr) >= 7 and instr[0] in (0x48, 0x4c):  # REX.W prefix
            if instr[1] == 0x8d and (instr[2] & 0x07) in (0x05, 0x0d, 0x15, 0x1d, 0x25, 0x2d, 0x35, 0x3d):
                disp = struct.unpack('<i', instr[3:7])[0]
                target = va + 7 + disp
                if target == base + version_rva:
                    rva = text_start_rva + text_off
                    print(f'LEA ref at RVA=0x{rva:x}: lea -> 0x{target:x} (version string)')
                    result = subprocess.run(
                        ['/tmp/my-project/tools/llvm-mingw-20240917-ucrt-ubuntu-20.04-x86_64/bin/x86_64-w64-mingw32-objdump',
                         '-d', f'--start-address=0x{va-10:x}',
                         f'--stop-address=0x{va+30:x}',
                         'samples/upx_decompressed.exe'],
                        capture_output=True, text=True)
                    for line in result.stdout.split('\n'):
                        if line.strip() and not line.startswith('upx_') and not line.startswith('Disassembly'):
                            print(f'  {line}')
                    print()
                    found += 1
                    if found >= 10:
                        break

print(f'Total references found: {found}')
