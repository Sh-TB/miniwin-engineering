#!/usr/bin/env python3
"""SWEEP-50 final: master 100-program stage table.
Merges: census.tsv (100 exes) + STAGES.tsv (72 ran) -> STAGE_TABLE_100.tsv
Programs never run (x86/ARM64) get stage ARCH-OUT-OF-SCOPE (loader is
x64-only by design; path B is research-only per mission).
"""
import os, csv

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
A = os.path.join(ROOT, "artifacts", "sweep50")
OUT = os.path.join(A, "STAGE_TABLE_100.tsv")

# map run-dir name -> census rel name
runs = {}
for d in os.listdir(os.path.join(A, "runs")):
    m = os.path.join(A, "runs", d, "meta.txt")
    if os.path.exists(m):
        for ln in open(m):
            if ln.startswith("exe="):
                runs[ln.strip()[4:]] = d

stages = {}
for row in csv.DictReader(open(os.path.join(A, "STAGES.tsv")), delimiter="\t"):
    stages[row["name"]] = row

rows = []
for r in csv.DictReader(open(os.path.join(A, "census.tsv")), delimiter="\t"):
    name = r["name"]
    rundir = runs.get(name)
    if rundir and rundir in stages:
        s = stages[rundir]
        rows.append([name, r["machine"], r["subsystem"], s["stage"],
                     s["exit"], s["n_unresolved"], r["crt"], rundir])
    else:
        rows.append([name, r["machine"], r["subsystem"], "ARCH-OUT-OF-SCOPE",
                     "-", "-", r["crt"], "-"])

with open(OUT, "w", newline="") as f:
    w = csv.writer(f, delimiter="\t")
    w.writerow(["program", "arch", "subsys", "stage", "exit", "n_unresolved", "crt", "run_dir"])
    w.writerows(rows)

from collections import Counter
c = Counter(r[3] for r in rows)
print(f"100-program stage table written: {OUT}")
for k, v in c.most_common():
    print(f"  {v:3d}  {k}")
