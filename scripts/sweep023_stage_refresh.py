#!/usr/bin/env python3
"""SWEEP-023 SW-998: refresh the 100-software stage table on the SWEEP-023
binary. Re-runs every program that previously ran (artifacts/sweep50/runs/*,
74 dirs) once with MW_GUI_EVENTS=0 + 12s timeout, records
exit / stub calls / unresolved / first unresolved import, and writes
artifacts/sweep023/STAGE_TABLE_100_REFRESH.tsv.

Chunked usage (3 GB box contract — foreground chunks):
    python3 scripts/sweep023_stage_refresh.py <start_idx> <end_idx>
"""
import os, subprocess, sys

REPO = "/home/z/my-project/workspace/miniwin-engineering"
A50 = f"{REPO}/artifacts/sweep50"
OUT = f"{REPO}/artifacts/sweep023"
os.makedirs(OUT, exist_ok=True)
TSV = f"{OUT}/STAGE_TABLE_100_REFRESH.tsv"

run_dirs = sorted(d for d in os.listdir(f"{A50}/runs")
                  if os.path.isdir(f"{A50}/runs/{d}"))
lo, hi = int(sys.argv[1]), int(sys.argv[2])
todo = run_dirs[lo:hi]
if lo == 0:
    with open(TSV, "w") as f:
        f.write("rundir\texe\texit\tstub_calls\tunresolved\tfirst_unresolved\n")

for d in todo:
    exe = None
    m = f"{A50}/runs/{d}/meta.txt"
    if os.path.exists(m):
        for ln in open(m):
            if ln.startswith("exe="):
                exe = ln.strip()[4:]
    if not exe:
        with open(TSV, "a") as f:
            f.write(f"{d}\tNO-EXE\t-\t-\t-\t-\n")
        continue
    exe_path = f"{REPO}/artifacts/sweep50/{exe}"
    if not os.path.exists(exe_path):
        with open(TSV, "a") as f:
            f.write(f"{d}\t{exe}\tMISSING\t-\t-\t-\n")
        continue
    wd = f"{OUT}/refresh/{d}"
    os.makedirs(wd, exist_ok=True)
    errf = os.path.join(wd, "stderr.txt")
    code = 0
    try:
        with open(errf, "wb") as ef:
            r = subprocess.run([f"{REPO}/minwin_loader", exe_path], cwd=wd,
                               env={**os.environ, "MW_GUI_EVENTS": "0"},
                               timeout=12, stdout=subprocess.DEVNULL,
                               stderr=ef)
        code = r.returncode
    except subprocess.TimeoutExpired:
        code = 124
    err = ""
    try:
        err = open(errf, errors="replace").read()
    except OSError:
        pass
    stubs = err.count("[STUB")
    unres = err.count("unresolved:")
    first = ""
    mm = re.search(r"unresolved import '([^']+)'", err) \
        if (re := __import__("re")) else None
    if mm:
        first = mm.group(1)
    with open(TSV, "a") as f:
        f.write(f"{d}\t{exe}\t{code}\t{stubs}\t{unres}\t{first}\n")
print(f"refresh chunk {lo}..{hi} done")
