#!/usr/bin/env python3
"""SWEEP-040 SW-1206 part 2: wider disasm of adexplorer crash function +
IAT name resolution for the call at 0x14001da97 -> [0x1400636e0]."""
import struct, subprocess

EXE = 'artifacts/sweep50/dl/adexplorer64/adexplorer64.exe'
IB = 0x140000000
data = open(EXE, 'rb').read()

e_lfanew = struct.unpack_from('<I', data, 0x3c)[0]
opt_size = struct.unpack_from('<H', data, e_lfanew + 20)[0]
nsec = struct.unpack_from('<H', data, e_lfanew + 6)[0]
sec0 = e_lfanew + 24 + opt_size
secs = []
for i in range(nsec):
    off = sec0 + 40 * i
    name = data[off:off+8].rstrip(b'\0').decode()
    va, vsz, rsz, rp = (struct.unpack_from('<I', data, off+12)[0],
                        struct.unpack_from('<I', data, off+8)[0],
                        struct.unpack_from('<I', data, off+16)[0],
                        struct.unpack_from('<I', data, off+20)[0])
    secs.append((name, va, vsz, rp, rsz))

def rva2off(rva):
    for name, va, vsz, rp, rsz in secs:
        if va <= rva < va + max(vsz, rsz):
            return rp + (rva - va)
    return None

# --- IAT directory: build addr->name map ---
opt_off = e_lfanew + 24
imp_rva, imp_sz = struct.unpack_from('<II', data, opt_off + 112 + 8)  # DataDirectory[1] = import
print(f"import dir rva={imp_rva:#x} size={imp_sz:#x}")
iat_names = {}
off = rva2off(imp_rva)
while True:
    oft, tds, fwd, name_rva, ft = struct.unpack_from('<IIIII', data, off)
    if oft == 0 and name_rva == 0:
        break
    noff = rva2off(name_rva)
    dll = data[noff:data.index(b'\0', noff)].decode()
    # walk thunk table (use FirstThunk ft)
    toff = rva2off(ft)
    while True:
        v = struct.unpack_from('<Q', data, toff)[0]
        if v == 0:
            break
        va = ft + (toff - rva2off(ft))
        if v & (1 << 63):
            iat_names[IB + va] = f"{dll}!ord{v & 0xffff}"
        else:
            hn = rva2off(v & 0x7fffffff)
            fn = data[hn+2:data.index(b'\0', hn+2)].decode()
            iat_names[IB + va] = f"{dll}!{fn}"
        toff += 8
    off += 20

print(f"IAT entries: {len(iat_names)}")
for a in (0x1400636e0,):
    print(f"  IAT {a:#x} = {iat_names.get(a, '??')}")

# unresolved list from run1 stderr -> check which are NOT in loader
print("\n--- disasm: function containing crash, from 0x1d8f0 to 0x1db40 ---")
out = subprocess.run(['objdump', '-d', '-M', 'intel',
                      '--start-address', hex(IB + 0x1d8f0),
                      '--stop-address', hex(IB + 0x1db40), EXE],
                     capture_output=True, text=True).stdout
for line in out.splitlines():
    print(line)
