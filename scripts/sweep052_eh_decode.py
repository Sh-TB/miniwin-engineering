#!/usr/bin/env python3
"""SW-1262 decisive probe: decode the C++ throw (ThrowInfo/CatchableTypes) and
the catch tables (FuncInfo4/HandlerType4) of the frames that were walked in the
soffice rc=99 run, from the on-disk DLLs.
Throw side: cppuhelper3msc RVA 0x145bd8 (ThrowInfo @ runtime 0x7f0133405bd8).
Handler side LSDA RVAs (engine HandlerData -> 4-byte FuncInfo4 RVA):
  frame1 base 0x7f01332c0000 lsda 0x7f01333e8958  -> RVA 0x128958
  frame4 base 0x7f0133a54000 lsda 0x7f013b322fe4  -> RVA 0x82cefe4 (mergedlo)
  frame5 base 0x7f0133a54000 lsda 0x7f013b6b2de4  -> RVA 0x865ede4 (mergedlo)
"""
import struct, sys

SO = "/home/z/my-project/workspace/miniwin-engineering/artifacts/sweep051/guests/soffice/"
IMGS = {
    "cppuhelper3MSC": (SO + "cppuhelper3MSC.dll", 0x7f01332c0000),
    "mergedlo":       (SO + "mergedlo.dll",       0x7f0133a54000),
    "soffice.bin":    (SO + "soffice.bin",        0x140000000),
}
HANDLE_IMG = {}  # image_name -> (file obj, sections, preferred_base, runtime_base)

for nm, (path, rtbase) in IMGS.items():
    f = open(path, "rb")
    dos = f.read(64)
    e_lfanew = struct.unpack_from("<I", dos, 0x3C)[0]
    f.seek(e_lfanew)
    nt = f.read(24 + 240)
    numsec = struct.unpack_from("<H", nt, 6)[0]
    opt_size = struct.unpack_from("<H", nt, 20)[0]
    opt = nt[24:24+opt_size]
    magic = struct.unpack_from("<H", opt, 0)[0]
    pbase = struct.unpack_from("<Q", opt, 24)[0] if magic == 0x20b else struct.unpack_from("<I", opt, 28)[0]
    secs = []
    f.seek(e_lfanew + 24 + opt_size)
    for i in range(numsec):
        s = f.read(40)
        name = s[:8].rstrip(b"\0").decode(errors="replace")
        vsize, vaddr, rsize, raddr = struct.unpack_from("<IIII", s, 8)
        secs.append((name, vaddr, vsize, raddr, rsize))
    HANDLE_IMG[nm] = (f, secs, pbase, rtbase)
    print(f"{nm}: preferred=0x{pbase:x} runtime=0x{rtbase:x} delta=0x{rtbase-pbase:x} nsec={numsec}")

def rd(img, rva, n):
    f, secs, pbase, rtbase = HANDLE_IMG[img]
    for name, vaddr, vsize, raddr, rsize in secs:
        if vaddr <= rva < vaddr + max(vsize, rsize):
            d = rva - vaddr
            avail = max(0, rsize - d)
            take = min(n, avail) if avail else n
            f.seek(raddr + d)
            b = f.read(take)
            return b + b"\0" * (n - len(b))
    return None

def u32(img, rva):
    b = rd(img, rva, 4)
    return struct.unpack("<I", b)[0] if b else None

def cstr(img, rva, maxlen=96):
    b = rd(img, rva, maxlen)
    if not b: return None
    z = b.find(b"\0")
    return b[:z if z >= 0 else maxlen].decode(errors="replace")

def ptr2rva(img, va):
    f, secs, pbase, rtbase = HANDLE_IMG[img]
    if va >= pbase and va - pbase < 0x80000000:
        return va - pbase
    return None

print("\n=== THROW SIDE (cppuhelper3MSC, FH4 image-relative format: EI[0]=0x01994000) ===")
ti_rva = 0x145bd8
raw = rd("cppuhelper3MSC", ti_rva, 16)
attrs, pmfn, pfc, pcta = struct.unpack_from("<IIII", raw)
print(f"ThrowInfo4@0x{ti_rva:x}: attributes=0x{attrs:x} pmfnUnwind(RVA)=0x{pmfn:x} fwd(RVA)=0x{pfc:x} CTArray(RVA)=0x{pcta:x}")
cta_rva = pcta
n = u32("cppuhelper3MSC", cta_rva)
print(f"CatchableTypeArray@0x{cta_rva:x}: nCatchableTypes={n}")
raw = rd("cppuhelper3MSC", cta_rva + 4, 4 * n)
for i in range(n):
    ct_rva = struct.unpack_from("<I", raw, 4 * i)[0]
    c = rd("cppuhelper3MSC", ct_rva, 40)
    props, dispType = struct.unpack_from("<II", c, 0)
    mdisp, pdisp, vdisp = struct.unpack_from("<iii", c, 8)
    sz, dispCopy = struct.unpack_from("<II", c, 20)
    name = cstr("cppuhelper3MSC", dispType + 16) if dispType else "<null>"
    print(f"  CT[{i}]@0x{ct_rva:x} props=0x{props:x} size={sz} mdisp={mdisp} pdisp={pdisp} vdisp={vdisp} type='{name}'")

print("\n=== HANDLER SIDE (FuncInfo4 catch tables) ===")
def funcinfo4(img, lsda_rva, label):
    fi_rva = u32(img, lsda_rva)
    print(f"{label}: HandlerData@0x{lsda_rva:x} -> FuncInfo4 RVA=0x{fi_rva:x}" if fi_rva else f"{label}: HandlerData@0x{lsda_rva:x} NULL")
    if not fi_rva: return
    raw = rd(img, fi_rva, 40)
    magic_bbt, maxState, dispUnw, nTry, dispTry, nIP, dispIP, dispUH, dispES, eFlags = struct.unpack("<IiiIIIIIII", raw)
    print(f"  magic=0x{magic_bbt & 0xFF:x} bbt=0x{magic_bbt>>8:x} maxState={maxState} nTryBlocks={nTry}")
    if nTry == 0:
        print("  (no try blocks — ContinueSearch is CORRECT)")
        return
    for t in range(nTry):
        tb_rva = dispTry + t * 20
        tb = rd(img, tb_rva, 20)
        tryLow, tryHigh, catchHigh, nCat, dispHA = struct.unpack("<IIIII", tb)
        print(f"  try[{t}]: states {tryLow}..{tryHigh} catchHigh={catchHigh} nCatches={nCat}")
        for h in range(nCat):
            ht_rva = dispHA + h * 16
            ht = rd(img, ht_rva, 16)
            adj, dispType, dispCatchObj, dispHandler = struct.unpack("<IIII", ht)
            tname = cstr(img, dispType + 16) if dispType else "<catch-all>"
            print(f"    catch[{h}]: adjectives=0x{adj:x} type='{tname}' catchObjDisp={dispCatchObj} handlerRVA=0x{dispHandler:x}")

funcinfo4("cppuhelper3MSC", 0x128958, "Frame[1] cppuhelper3MSC")
funcinfo4("mergedlo", 0x82cefe4, "Frame[4] mergedlo")
funcinfo4("mergedlo", 0x865ede4, "Frame[5] mergedlo")
