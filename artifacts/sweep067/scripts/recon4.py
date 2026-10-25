#!/usr/bin/env python3.13
"""SW-1296 TASK3: 4-exe static recon (imports, IAT call sites, UPX class check)."""
import re, pefile, capstone
from capstone import CS_ARCH_X86, CS_MODE_32
from capstone.x86 import *

EXES = {
    "driverview":   "/home/z/my-project/workspace/miniwin-engineering/artifacts/sweep062/x/driverview/DriverView.exe",
    "heapmemview":  "/home/z/my-project/workspace/miniwin-engineering/artifacts/sweep062/x/heapmemview/HeapMemView.exe",
    "htmlastext":   "/home/z/my-project/workspace/miniwin-engineering/artifacts/sweep062/x/htmlastext/HtmlAsText.exe",
    "faview":       "/home/z/my-project/workspace/miniwin-engineering/artifacts/sweep062/x/faview/faview.exe",
}

md = capstone.Cs(CS_ARCH_X86, CS_MODE_32); md.detail = True

def recon(tag, path):
    pe = pefile.PE(path, fast_load=False)
    base = pe.OPTIONAL_HEADER.ImageBase
    ep = base + pe.OPTIONAL_HEADER.AddressOfEntryPoint
    secs = [(s.Name.decode(errors='replace').rstrip(chr(0)), base + s.VirtualAddress,
             s.Misc_VirtualSize, s.SizeOfRawData) for s in pe.sections]
    packed = any(n.startswith("UPX") for n, *_ in secs)

    iat_map, names = {}, []
    for entry in pe.DIRECTORY_ENTRY_IMPORT:
        dll = entry.dll.decode(errors="replace")
        for imp in entry.imports:
            nm = imp.name.decode(errors="replace") if imp.name else f"Ordinal_{imp.ordinal}"
            iat_map[imp.address] = (dll, nm)
            names.append(f"{dll}!{nm}")

    # ff 15 (call dword ptr [imm32]) scan across ALL executable sections
    sites = []
    for s in pe.sections:
        if not (s.Characteristics & 0x20000000):
            continue
        data = s.get_data(); sva = base + s.VirtualAddress
        for m in re.finditer(rb"\xff\x15", data):
            off = m.start()
            if off + 6 > len(data): continue
            va = int.from_bytes(data[off+2:off+6], "little")
            tgt = iat_map.get(va)
            sites.append((sva + off, va, tgt))

    # stub-tail register-relative calls (call dword [reg+disp32] = ff /2 with mod=2)
    stub_calls = []
    if packed:
        for s in pe.sections:
            if s.Name.decode(errors='replace').startswith("UPX1"):
                data = s.get_data(); sva = base + s.VirtualAddress
                tail = data[-0x70:]
                for insn in md.disasm(tail, sva + len(data) - len(tail)):
                    if insn.mnemonic in ("call", "jmp") and insn.op_str.startswith("dword ptr ["):
                        mm = re.search(r"\[(?:e(?:si|di|bp|ax|bx|cx|dx|sp)) \+ (0x[0-9a-f]+)\]", insn.op_str)
                        if mm:
                            eff = 0x401000 + int(mm.group(1), 16)   # UPX stub base after unpack
                            tgt = iat_map.get(eff)
                            stub_calls.append((insn.address, insn.mnemonic, insn.op_str, tgt))

    print(f"### {tag}")
    print(f"file: {path}")
    print(f"size={len(open(path,'rb').read())} ImageBase=0x{base:08X} EP=0x{ep:08X} sections={[n for n,*_ in secs]}")
    print(f"UPX-packed: {packed}")
    print(f"(a) total imports: {len(names)} from {len(pe.DIRECTORY_ENTRY_IMPORT)} DLLs")
    for e in pe.DIRECTORY_ENTRY_IMPORT:
        print(f"    {e.dll.decode(errors='replace'):16s} {len(e.imports):3d}")
    print(f"(b) ff 15 indirect call sites in exec sections: {len(sites)}; resolved-to-import: {sum(1 for _,_,t in sites if t)}")
    seen = set()
    for a, va, t in sites:
        if t: seen.add(f"{t[0]}!{t[1]}")
    if seen:
        print("    distinct IAT targets hit by ff 15:", ", ".join(sorted(seen)[:14]) + (" ..." if len(seen) > 14 else ""))
    if stub_calls:
        print(f"(b2) UPX stub-tail reg-relative API calls:")
        for a, mn, ops, t in stub_calls:
            print(f"    0x{a:08X}: {mn} {ops}" + (f"   ; = {t[0]}!{t[1]}" if t else "   ; IAT? (unresolved)"))
    print()
    return {"tag": tag, "packed": packed, "imports": len(names), "dlls": len(pe.DIRECTORY_ENTRY_IMPORT),
            "ff15": len(sites), "ff15_resolved": sum(1 for _,_,t in sites if t)}

if __name__ == "__main__":
    rows = [recon(t, p) for t, p in EXES.items()]
    print("=== summary table ===")
    print(f"{'exe':14s} {'UPX':5s} {'imports':8s} {'DLLs':5s} {'ff15 sites':11s} {'ff15->import'}")
    for r in rows:
        print(f"{r['tag']:14s} {str(r['packed']):5s} {r['imports']:<8d} {r['dlls']:<5d} {r['ff15']:<11d} {r['ff15_resolved']}")
