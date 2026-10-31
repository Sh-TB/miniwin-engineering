#!/usr/bin/env python3
"""SWEEP-058 Train 1 (SW-1239): decode pslist64's PERF_DATA_BLOCK parse contract.
v2: correct VA mapping (objdump -h VMA already includes image base)."""
import subprocess, re, sys

EXE = "artifacts/sweep047/bin/pslist64.exe"
BASE = 0x140000000

def sections():
    out = subprocess.run(["objdump", "-h", EXE], capture_output=True, text=True).stdout
    secs = []
    for m in re.finditer(r"^\s*\d+\s+(\S+)\s+([0-9a-f]+)\s+([0-9a-f]+)\s+([0-9a-f]+)\s+([0-9a-f]+)\s", out, re.M):
        name, size, vma, lma, fileoff = m.groups()
        secs.append((name, int(vma, 16), int(size, 16), int(fileoff, 16)))
    return secs

def file2va(secs, off):
    for name, vma, size, fo in secs:
        if fo <= off < fo + size:
            return vma + (off - fo)
    return None

def find_all(data, b, limit=8):
    hits, i = [], 0
    while len(hits) < limit:
        i = data.find(b, i)
        if i < 0: break
        hits.append(i); i += 1
    return hits

def main():
    data = open(EXE, "rb").read()
    secs = sections()
    pats = {}
    for label, s in [
        ("Processor_perf", "Processor"),
        ("processor_low", "processor "),
        ("notfound", "performance object not found"),
        ("exctrlst", "Exctrlst"),
        ("pct_cpu", "% Processor Time"),
        ("system_uptime", "System Up Time"),
        ("PROCESS_1", "PROCESS 1"),
    ]:
        for off in find_all(data, s.encode()) + find_all(data, s.encode("utf-16-le")):
            va = file2va(secs, off)
            if va: pats.setdefault(label, []).append(va)

    dis = subprocess.run(["objdump", "-d", "--no-show-raw-insn", "-M", "intel", EXE],
                         capture_output=True, text=True).stdout
    # index lines by address for context printing
    lines = dis.splitlines()
    addr_re = re.compile(r"^\s*([0-9a-f]+):")
    addrs = []
    for i, l in enumerate(lines):
        m = addr_re.match(l)
        if m: addrs.append((int(m.group(1), 16), i))

    def ctx_for(va, back=25, fwd=40):
        # find nearest instruction at/below va
        target = None
        for a, i in addrs:
            if a <= va: target = (a, i)
            else: break
        if not target: return [f"(no insn at 0x{va:x})"]
        a, i = target
        return [l for l in lines[max(0, i - back): i + fwd]]

    for label, vas in sorted(pats.items()):
        for va in vas:
            print(f"\n######## XREFS to {label} VA 0x{va:x} ########")
            n = 0
            for i, l in enumerate(lines):
                if f"0x{va:x}" in l:
                    n += 1
                    print(f"--- ref {n}: {l.strip()}")
                    m = addr_re.match(l)
                    if m:
                        a, i0 = int(m.group(1), 16), i
                        for cl in lines[max(0, i0 - 6): i0 + 12]:
                            print("   ", cl.strip())
                    if n >= 6: break
            if n == 0:
                print("(no direct rip-rel refs)")

if __name__ == "__main__":
    main()
