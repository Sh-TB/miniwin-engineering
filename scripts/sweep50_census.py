#!/usr/bin/env python3
"""SWEEP-50 phase 2: pefile census of all collected exes -> census.tsv
Columns: name, sha256(first8), machine, subsystem, size_kb, linker, crt_dlls
"""
import os, sys, hashlib, csv
import pefile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DL = os.path.join(ROOT, "artifacts", "sweep50", "dl")
OUT = os.path.join(ROOT, "artifacts", "sweep50", "census.tsv")

MACHINE = {0x8664: "x64", 0x14C: "x86", 0xAA64: "ARM64"}
SUBSYS = {2: "GUI", 3: "console"}

def census(path):
    name = os.path.relpath(path, DL)
    h = hashlib.sha256(open(path, "rb").read()).hexdigest()[:12]
    size_kb = os.path.getsize(path) // 1024
    try:
        pe = pefile.PE(path, fast_load=True)
        pe.parse_data_directories(directories=[
            pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_IMPORT"],
            pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_EXPORT"]])
        mach = MACHINE.get(pe.FILE_HEADER.Machine, hex(pe.FILE_HEADER.Machine))
        subsys = SUBSYS.get(pe.OPTIONAL_HEADER.Subsystem, str(pe.OPTIONAL_HEADER.Subsystem))
        linker = f"{pe.OPTIONAL_HEADER.MajorLinkerVersion}.{pe.OPTIONAL_HEADER.MinorLinkerVersion}"
        dlls = []
        if hasattr(pe, "DIRECTORY_ENTRY_IMPORT"):
            dlls = [e.dll.decode(errors="replace").lower() for e in pe.DIRECTORY_ENTRY_IMPORT]
        crt = [d for d in dlls if any(k in d for k in
               ("msvcp", "vcruntime", "ucrtbase", "msvcrt", "msvcr", "libgcc", "winpthread"))]
        return (name, h, mach, subsys, size_kb, linker, ";".join(crt) or "-",
                ";".join(dlls[:14]) + ("..." if len(dlls) > 14 else ""))
    except Exception as e:
        return (name, h, "BADPE", "-", size_kb, "-", f"err:{type(e).__name__}", "-")

def main():
    exes = []
    for dirpath, _, files in os.walk(DL):
        for fn in files:
            if fn.lower().endswith(".exe"):
                exes.append(os.path.join(dirpath, fn))
    exes.sort()
    rows = [census(p) for p in exes]
    with open(OUT, "w", newline="") as f:
        w = csv.writer(f, delimiter="\t")
        w.writerow(["name", "sha256", "machine", "subsystem", "size_kb", "linker", "crt", "imports"])
        w.writerows(rows)
    # summary
    from collections import Counter
    c = Counter((r[2], r[3]) for r in rows)
    print(f"total exes: {len(rows)}")
    for k, v in sorted(c.items()):
        print(f"  {k[0]}/{k[1]}: {v}")
    x64 = [r for r in rows if r[2] == "x64"]
    print(f"x64 total: {len(x64)}")

if __name__ == "__main__":
    main()
