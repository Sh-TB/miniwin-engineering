#!/usr/bin/env python3
"""SWEEP-011: build the before/after/fast comparison table.

BEFORE  = /tmp/sweep011_baseline.log (captured while running the G1 binary
          df28de0f..., i.e. pre-SWEEP-011 engine)
AFTER   = artifacts/sweep011_perf/baseline.tsv       (parity run, new engine)
FAST    = artifacts/sweep011_perf/baseline_fast.tsv  (MINWIN_TRACE=0 + MINWIN_SCREENSHOT=0)
"""
import statistics, os

REPO = "/home/z/my-project/workspace/miniwin-engineering"
PERF = os.path.join(REPO, "artifacts", "sweep011_perf")

def parse_log(path):
    rows = {}
    for L in open(path, errors="replace"):
        if not L.startswith("name="):
            continue
        kv = dict(p.split("=", 1) for p in L.strip().split(" | "))
        rows.setdefault(kv["name"], []).append(kv)
    return rows

def parse_tsv(path):
    rows = {}
    with open(path) as f:
        hdr = f.readline().rstrip("\n").split("\t")
        for L in f:
            kv = dict(zip(hdr, L.rstrip("\n").split("\t")))
            rows.setdefault(kv["name"], []).append(kv)
    return rows

def med(rows, key):
    return statistics.median(float(r[key]) for r in rows)

before = parse_log("/tmp/sweep011_baseline.log")
after = parse_tsv(os.path.join(PERF, "baseline.tsv"))
fast = parse_tsv(os.path.join(PERF, "baseline_fast.tsv"))

order = ["autoruns64", "rammap64", "procexp64", "snake", "breakout",
         "t12_libc_io", "t08_big_cpp", "t10_float", "upx_sample",
         "sqlite3", "7za", "curl", "puttygen"]

print(f"{'program':<13} {'exitB':>5} {'exitA':>5} | "
      f"{'wall B->A':>15} {'wall A->F':>13} | {'sys B->A':>14} | {'traceLn B->F':>13}")
for name in order:
    if name not in before:
        continue
    b, a, f_ = before[name], after.get(name, []), fast.get(name, [])
    if not a or not f_:
        continue
    wb, wa, wf = med(b, "wall"), med(a, "wall"), med(f_, "wall")
    sb, sa, sf = med(b, "sys"), med(a, "sys"), med(f_, "sys")
    tb, tf = med(b, "trace_lines"), med(f_, "trace_lines")
    print(f"{name:<13} {b[0]['exit']:>5} {a[0]['exit']:>5} | "
          f"{wb*1000:>6.1f}->{wa*1000:<6.1f}ms | {wa*1000:>5.1f}->{wf*1000:<5.1f}ms | "
          f"{sb*1000:>5.1f}->{sa*1000:<5.1f}ms | {tb:>6.0f}->{tf:<6.0f}")

out = os.path.join(PERF, "COMPARISON.md")
with open(out, "w") as f:
    f.write("# SWEEP-011 before/after/fast (median of 3 runs)\n\n")
    f.write("BEFORE = engine df28de0f (pre-SWEEP-011); AFTER = 937cdc0d parity "
            "(defaults, behavior-identical); FAST = AFTER + MINWIN_TRACE=0 + "
            "MINWIN_SCREENSHOT=0.\n\n")
    f.write("| program | exit B/A | wall BEFORE ms | wall AFTER ms | wall FAST ms | sys B ms | sys FAST ms | trace lines B->F |\n")
    f.write("|---|---|---|---|---|---|---|---|\n")
    for name in order:
        if name not in before:
            continue
        b, a, f_ = before[name], after.get(name, []), fast.get(name, [])
        if not a or not f_:
            continue
        f.write("| %s | %s/%s | %.1f | %.1f | %.1f | %.1f | %.1f | %.0f->%.0f |\n" % (
            name, b[0]["exit"], a[0]["exit"],
            med(b, "wall") * 1000, med(a, "wall") * 1000, med(f_, "wall") * 1000,
            med(b, "sys") * 1000, med(f_, "sys") * 1000,
            med(b, "trace_lines"), med(f_, "trace_lines")))
print("\nwritten:", out)
