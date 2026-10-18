#!/usr/bin/env python3
"""SWEEP-008/015 broad sweep — every x64 exe under artifacts/sweep50/dl_extract
plus the loose putty family. 3 runs each; records exit codes, named stub
calls, dialog images produced. Output: artifacts/sweep50/sweep015_sweep.tsv

Harness note: stdout/stderr must be FILES, not pipes — Sysinternals GUI
binaries check GetFileType(GetStdHandle(STD_OUTPUT_HANDLE)) and switch to
their console path (print EULA, exit 1) when stdout is a pipe."""
import glob
import os
import re
import struct
import subprocess
import sys

REPO = "/home/z/my-project/workspace/miniwin-engineering"
RUNS = f"{REPO}/artifacts/sweep50/runs_sweep015"
OUT = f"{REPO}/artifacts/sweep50/sweep015_sweep.tsv"

def is_x64(path):
    try:
        with open(path, "rb") as f:
            d = f.read(0x400)
        pe_off = struct.unpack_from("<I", d, 0x3C)[0]
        if d[pe_off:pe_off + 4] != b"PE\x00\x00":
            return False
        machine = struct.unpack_from("<H", d, pe_off + 4)[0]
        return machine == 0x8664
    except Exception:
        return False

def main():
    exes = sorted(glob.glob(f"{REPO}/artifacts/sweep50/dl_extract/**/*.exe", recursive=True))
    exes += sorted(glob.glob(f"{REPO}/artifacts/sweep50/dl/putty/*.exe"))
    exes += sorted(glob.glob(f"{REPO}/artifacts/sweep50/dl/puttygen/*.exe"))
    exes += sorted(glob.glob(f"{REPO}/artifacts/sweep50/dl/pageant/*.exe"))
    exes += [f"{REPO}/artifacts/sweep008/calc.exe"]
    exes = [e for e in exes if is_x64(e)]
    print(f"{len(exes)} x64 exes", file=sys.stderr)
    os.makedirs(RUNS, exist_ok=True)
    out = open(OUT, "w")
    out.write("exe\texit1\texit2\texit3\tstub_calls\tstub_names\timages\n")
    for exe in exes:
        rel = os.path.relpath(exe, f"{REPO}/artifacts")
        tag = re.sub(r"[^A-Za-z0-9_.-]", "_", rel)
        rd = f"{RUNS}/{tag}"
        os.makedirs(rd, exist_ok=True)
        exits, stubs, images = [], [], 0
        for i in (1, 2, 3):
            so_p = f"{rd}/stdout_{i}.txt"
            se_p = f"{rd}/stderr_{i}.txt"
            try:
                with open(so_p, "wb") as fo, open(se_p, "wb") as fe:
                    cp = subprocess.run([f"{REPO}/minwin_loader", exe],
                                        stdout=fo, stderr=fe, timeout=40, cwd=REPO)
                ec = cp.returncode
                se = open(se_p, encoding="utf-8", errors="replace").read()
            except subprocess.TimeoutExpired:
                ec = "TIMEOUT"
                se = open(se_p, encoding="utf-8", errors="replace").read()
            exits.append(ec)
            if i == 1:
                stubs = [l for l in se.splitlines() if l.startswith("[STUB]")]
                names = []
                for l in stubs:
                    m = re.search(r"\(call #\d+\) (\S+) guest_rip", l)
                    if m:
                        names.append(m.group(1))
                with open(f"{rd}/stderr_1.txt", "w") as g:
                    g.write(se)
        exe_base = os.path.basename(exe)
        shots = f"{REPO}/miniwin-results/{exe_base}/screenshots"
        if os.path.isdir(shots):
            images = len([f for f in os.listdir(shots) if f.endswith(".bmp")])
        out.write(f"{rel}\t{exits[0]}\t{exits[1]}\t{exits[2]}\t{len(stubs)}\t{';'.join(names[:12])}\t{images}\n")
        out.flush()
        print(f"{rel}: {exits} stubs={len(stubs)} imgs={images}", file=sys.stderr)
    out.close()
    print(f"wrote {OUT}", file=sys.stderr)

if __name__ == "__main__":
    main()
