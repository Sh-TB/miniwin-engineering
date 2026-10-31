#!/usr/bin/env python3
"""SW-1349: faithful NRV2B + UPX filter 0x49 unfilter model (RegScanner x86).
Every branch mirrors the stub asm at 0x41d4a0..0x41d630."""
import sys

PATH = sys.argv[1] if len(sys.argv) > 1 else \
    'artifacts/sweep078/dl/x/regscanner-x86/RegScanner.exe'
data = open(PATH, 'rb').read()
SRC = 0x400
DEST = 0x401000
SPAN = 0x1d000
out = bytearray(SPAN)
di = 0
esi = SRC
ebx = 0
M32 = 0xFFFFFFFF

def feeder():
    """add ebx,ebx; jne ok; mov ebx,[esi]; esi+=4; adc ebx,ebx.
    Returns (CF, ebx_zero_after_full_seq)."""
    global ebx, esi
    cf = (ebx >> 31) & 1
    ebx = (ebx << 1) & M32
    if ebx == 0:                       # jne not taken -> reload
        lo = int.from_bytes(data[esi:esi+4], 'little'); esi += 4
        ebx = ((lo << 1) | 1) & M32
        cf = (lo >> 31) & 1            # CF out of the adc
    return cf

def stopbit():
    """add ebx,ebx; jae(loop when CF=0); jne(done when ebx!=0); reload; jae."""
    global ebx, esi
    cf = (ebx >> 31) & 1
    ebx = (ebx << 1) & M32
    if cf == 0: return False           # jae -> loop
    if ebx != 0: return True           # jne -> done
    lo = int.from_bytes(data[esi:esi+4], 'little'); esi += 4
    ebx = ((lo << 1) | 1) & M32
    return (lo >> 31) & 1 == 1         # reloaded adc CF=1 -> done

def decompress():
    global di, esi
    ebp = 0
    steps = 0
    while True:
        steps += 1
        if steps > 50_000_000: raise Exception('runaway')
        if feeder() == 1:                          # jb 0x10: literal
            if di >= SPAN: raise Exception('dst overflow 0x%x' % di)
            out[di] = data[esi]; esi += 1; di += 1
            continue
        # ---- match: first length loop (0x23..0x44) ----
        eax = 1
        while True:
            cf = feeder()                          # 0x28/0x2c
            eax = ((eax << 1) | cf) & M32          # 0x33 adc eax,eax
            if stopbit():                          # 0x35..0x42
                break
        # ---- 0x44: xor ecx,ecx ; 0x46 sub eax,3 ----
        if eax >= 3:                               # jb not taken
            eax = (((eax - 3) << 8) | data[esi]) & M32   # 0x4b shl; 0x4e mov al
            esi += 1
            eax ^= M32                             # 0x51 xor eax,-1
            if eax == 0:                           # 0x54 je 0xca END
                break
            ebp = eax                              # 0x56 mov ebp,eax
        # ---- length (0x58..0x94) ----
        ecx = 0
        cf = feeder(); ecx = ((ecx << 1) | cf) & M32     # 0x58..0x63
        cf = feeder(); ecx = ((ecx << 1) | cf) & M32     # 0x65..0x70
        if ecx == 0:                                    # 0x72 jne 0x94
            ecx = 1                                     # 0x74 inc ecx
            while True:                                 # 0x75 extended
                cf = feeder(); ecx = ((ecx << 1) | cf) & M32   # 0x80
                if stopbit(): break                     # 0x82..0x8f
            ecx = (ecx + 2) & M32                       # 0x91 add ecx,2
        # 0x94: cmp ebp,0xfffff300 ; adc ecx,1
        cf = 1 if ebp < 0xFFFFF300 else 0
        ecx = (ecx + 1 + cf) & M32
        # ---- copy (0x9d..0xc5) ----
        srcp = (di + ebp) & M32
        if ebp >= 0xFFFFFFFC:                           # 0xa0 jbe -> dword
            while True:
                if srcp + 4 > SPAN: raise Exception('src overrun dword')
                if di + 4 > SPAN: raise Exception('dst overrun dword')
                v = int.from_bytes(out[srcp:srcp+4], 'little')
                out[di:di+4] = v.to_bytes(4, 'little')
                di += 4; srcp += 4
                ecx = (ecx - 4) & M32
                if ecx != 0 and ecx < 0x80000000: continue   # 0xc1 ja
                break
            # 0xc3 add edi,ecx (ecx is negative-wrapped remainder)
            di += (ecx - (1 << 32)) if ecx > 0x7FFFFFFF else ecx
        else:
            if di + ecx > SPAN: raise Exception('dst overrun byte')
            for _ in range(ecx):
                if srcp >= SPAN: raise Exception('src overrun 0x%x di=0x%x' % (srcp, di))
                out[di] = out[srcp]; di += 1; srcp += 1
    return steps

n = decompress()
print(f'decompressed {di} bytes (0x{di:x}), consumed 0x{esi-SRC:x} src, steps={n}')
head = bytes(out[:48])
print('head:', head[:16].hex(), ''.join(chr(c) if 32 <= c < 127 else '.' for c in head[:32]))
i = bytes(out).find(b'This program cannot be run in DOS mode')
print('dos-stub anchor:', 'OK at 0x%x' % (DEST + i) if i >= 0 else 'MISSING')

def unfilter():
    edi = 0
    ecx = 0x7c2
    def patch():
        nonlocal edi
        eax = int.from_bytes(out[edi:edi+4], 'little')
        bl = out[edi + 4]
        eax = (eax & 0xFFFF0000) | ((eax & 0xFFFF) >> 8)          # shr ax,8
        eax = ((eax >> 16) | (eax << 16)) & M32                   # rol eax,16
        eax = (eax & 0xFFFF0000) | ((eax & 0xFF00) >> 8) | ((eax & 0xFF) << 8)  # xchg al,ah
        eax = (eax - (DEST + edi)) & M32                          # sub eax,edi
        bl = (bl - 0xe8) & 0xFF
        eax = (eax + DEST) & M32                                  # add eax,esi
        out[edi:edi+4] = eax.to_bytes(4, 'little')
        edi += 5
        return bl
    while ecx:
        while True:
            al = (out[edi] - 0xe8) & 0xFF
            edi += 1
            if al <= 1: break
        if out[edi] != 7: continue
        al = patch()
        ecx -= 1
        while ecx and al <= 1:                 # loop 0xd7 chain re-entry
            if out[edi] != 7: break
            al = patch()
            ecx -= 1
unfilter()
print('unfilter done')
open('/tmp/sw78_truth.bin', 'wb').write(bytes(out))
print('wrote /tmp/sw78_truth.bin')
