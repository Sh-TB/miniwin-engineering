#!/usr/bin/env python3
"""guest_profile.py — PE guest profiler: compiler, language/runtime, prerequisites.

Answers "what was this program written with, what language, what does it need"
from the PE import table + optional markers, without executing the guest.

Usage: python3 scripts/guest_profile.py <file.exe> [more.exe ...]
Output: TSV rows on stdout: file<TAB>arch<TAB>compiler<TAB>language<TAB>crt<TAB>
        managed<TAB>frameworks<TAB>prerequisites<TAB>installer_markers
"""
import struct, sys, os, re

API_MS_RE = re.compile(r"^api-ms-win-crt")

def pe_info(path):
    d = open(path, "rb").read()
    if d[:2] != b"MZ":
        return None
    pe = struct.unpack_from("<I", d, 0x3C)[0]
    if d[pe:pe+4] != b"PE\0\0":
        return None
    machine = struct.unpack_from("<H", d, pe+4)[0]
    nsec = struct.unpack_from("<H", d, pe+6)[0]
    optsz = struct.unpack_from("<H", d, pe+20)[0]
    opt = pe + 24
    magic = struct.unpack_from("<H", d, opt)[0]
    arch = "x64" if machine == 0x8664 else ("x86" if machine == 0x14c else hex(machine))
    ddir = opt + (0x70 if magic == 0x20b else 0x60)
    ndirs = struct.unpack_from("<I", d, opt + (108 if magic == 0x20b else 96))[0]
    imp_rva = imp_sz = 0
    iat_dir = 0
    com_rva = com_sz = 0
    if ndirs > 0:  imp_rva, imp_sz = struct.unpack_from("<II", d, ddir + 8)
    if ndirs > 12: com_rva, com_sz = struct.unpack_from("<II", d, ddir + 14*8)
    secs = []
    so = opt + optsz
    for i in range(nsec):
        b = d[so+i*40: so+(i+1)*40]
        name = b[:8].rstrip(b"\0").decode("ascii", "replace")
        vsz, va, rsz, praw = struct.unpack_from("<IIII", b, 8)
        secs.append((name, va, vsz, praw, rsz))
    def r2o(rva):
        for name, va, vsz, praw, rsz in secs:
            if va <= rva < va + max(vsz, rsz):
                return praw + (rva - va)
        return None
    # imports
    dlls = []
    if imp_rva:
        o = r2o(imp_rva)
        while o and o + 20 <= len(d):
            oft, ts, fc, nameR, ft = struct.unpack_from("<IIIII", d, o)
            if not nameR:
                break
            no = r2o(nameR)
            dll = d[no:d.find(b"\0", no)].decode("ascii", "replace") if no else "?"
            dlls.append(dll)
            o += 20
    # markers
    markers = []
    low = d.lower()
    for sig, label in [
        (b"Nullsoft", "NSIS installer"), (b"Inno Setup", "InnoSetup installer"),
        (b"RAR!", "RAR archive/SFX payload"), (b"InstallShield", "InstallShield"),
        (b"Wise Installation", "Wise installer"), (b"MSIF", "MSI package"),
        (b"go1.", "Go binary"), (b"python3", "Python embedded"),
        (b"\x00_thisprogram cannot be run in dos mode", None),
    ]:
        if sig.lower() in low:
            markers.append(label)
    # compiler / runtime inference
    dllset = set(x.lower() for x in dlls)
    ucrt = any(API_MS_RE.match(x) for x in dllset)
    compiler, crt, managed, frameworks = "?", "?", "no", ""
    langs = []
    if com_rva and com_sz:
        managed = "yes (IL assembly, runs on .NET CLR)"
        frameworks = ".NET Framework/CLR"
        compiler = ".NET compiler (csc/IL)"
        crt = "none (managed)"
    else:
        if ucrt:
            crt = "UCRT (api-ms-win-crt-*)"
            if "vcruntime140.dll" in dllset:
                compiler = "Microsoft Visual C++ 2015+ (MSVC)"
            else:
                compiler = "MSVC-style toolchain (UCRT)"
        elif "msvcrt.dll" in dllset:
            crt = "msvcrt.dll"
            compiler = "MSVC (legacy msvcrt) or MinGW"
        if any(x.startswith(("libgcc", "libstdc++", "libmingw")) for x in dllset) or "mingwm10.dll" in dllset:
            compiler = "MinGW/GCC"
            crt = "msvcrt + libgcc/libstdc++"
        if "mfc140u.dll" in dllset or any(x.startswith("mfc") for x in dllset):
            frameworks += " MFC"
        if "atl" in " ".join(dllset):
            frameworks += " ATL"
        if any(x.startswith(("rtl", "vcl", "borlndmm")) for x in dllset) or "borlndmm" in " ".join(dllset):
            compiler = "Delphi/Borland (import-based detection)"
        if "dart.dll" in dllset or b"flutter" in low:
            frameworks += " Flutter"
        if b"Qt5" in low or b"Qt6" in low:
            frameworks += " Qt"
    langs = []
    if compiler == "?" and crt == "?" and len(dlls) >= 5 and not managed.startswith("yes"):
            # no CRT imports at all: CRT statically linked (typical Delphi,
            # or MSVC static /MT builds like WinRAR)
            compiler = "C/C++ (CRT statically linked; toolchain not externally visible)"
            crt = "static"
            langs.append("C/C++ (static CRT)")
    if "mscoree" in dllset or managed.startswith("yes"):
        langs.append("C#/IL")
    if ucrt or "msvcrt.dll" in dllset:
        langs.append("C/C++")
    if "delphi" in compiler.lower():
        langs.append("Object Pascal")
    if "mingw" in compiler.lower():
        langs.append("C/C++ (GCC)")
    prereqs = sorted(x for x in dllset if not API_MS_RE.match(x))
    return {
        "file": os.path.basename(path), "arch": arch,
        "compiler": compiler or "?", "language": "+".join(langs) or "?",
        "crt": crt, "managed": managed, "frameworks": frameworks.strip() or "none",
        "prerequisites": ";".join(prereqs) if prereqs else "none beyond system DLLs",
        "installer_markers": ";".join(m for m in markers if m) or "none",
        "import_count": len(dlls),
    }

def main():
    hdr = ["file", "arch", "compiler", "language", "crt", "managed",
           "frameworks", "prerequisites", "installer_markers", "import_count"]
    print("\t".join(hdr))
    for p in sys.argv[1:]:
        info = pe_info(p)
        if info:
            print("\t".join(str(info[k]) for k in hdr))
        else:
            print(f"{os.path.basename(p)}\tNOT-A-PE")

if __name__ == "__main__":
    main()
