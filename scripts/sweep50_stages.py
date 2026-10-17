#!/usr/bin/env python3
"""SWEEP-50 phase 4: honest per-program stage classification.

Reads each run dir (stdout/stderr/exit) and assigns a stage:
  MISSING-EXE | ARCH-REJECT(x86/ARM64) | CRASH-GUEST | CRASH-LOADER |
  GO-RUNTIME-FAIL | IMPORT-BLOCK (unresolved called -> loader died) |
  RAN-OUTPUT (exit 0 + bytes) | RAN-EXIT<n> (ran, nonzero, output>0) |
  HANG | SILENT
Also extracts: unresolved-import count, last stderr line, first guest line.
Writes artifacts/sweep50/STAGES.tsv
"""
import os, re, glob, csv

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RUNS = os.path.join(ROOT, "artifacts", "sweep50", "runs")
OUT = os.path.join(ROOT, "artifacts", "sweep50", "STAGES.tsv")

def stage_for(d):
    name = os.path.basename(d)
    so = open(os.path.join(d, "stdout.txt"), errors="replace").read() \
        if os.path.exists(os.path.join(d, "stdout.txt")) else ""
    se = open(os.path.join(d, "stderr.txt"), errors="replace").read() \
        if os.path.exists(os.path.join(d, "stderr.txt")) else ""
    meta = open(os.path.join(d, "meta.txt"), errors="replace").read() \
        if os.path.exists(os.path.join(d, "meta.txt")) else ""
    # exit code from summary.tsv / summary_b.tsv
    exit_code = ""
    for sfile in ("summary.tsv", "summary_b.tsv"):
        stg = os.path.join(RUNS, sfile)
        if os.path.exists(stg):
            for line in open(stg, errors="replace"):
                p = line.rstrip("\n").split("\t")
                if p and p[0] == name:
                    exit_code = p[1] if len(p) > 1 else ""
    if not exit_code:
        # fallback: re-derive impossible; mark
        exit_code = ""
    n_unres = len(re.findall(r"unresolved: (\S+)", se))
    unres_names = re.findall(r"unresolved: (\S+)", se)
    crash = re.search(r"\[CRASH\] Signal (\d+) at RIP=(0x[0-9a-f]+)", so + se)
    guest_crash = False
    if crash:
        guest_crash = "inside loaded PE" in (so + se) and "NOT inside" not in (so + se)
    arch = "x86" if "/x86-" in name or name.endswith(("7zr", "x86-si-strings")) else ""
    # stage decision
    if exit_code == "-":
        stage = "MISSING-EXE"
    elif exit_code == "1" and ("Invalid PE" in se or "Unsupported" in se or
                               "not x64" in se or "Not a PE32+" in se or
                               "Failed to load PE" in se or "reject" in se.lower()):
        stage = "LOAD-REJECT"
    elif exit_code == "139":
        stage = "CRASH-GUEST" if guest_crash and crash else "CRASH-LOADER"
    elif exit_code == "124":
        stage = "HANG"
    elif "fatal error:" in so + se and "runtime:" in so + se:
        stage = "GO-RUNTIME-FAIL"
    elif exit_code == "0" and len(so.strip()) > 0:
        stage = "RAN-OUTPUT"
    elif exit_code and int(exit_code) != 0 and (so.strip() or se.strip()):
        stage = f"RAN-EXIT{exit_code}"
    elif exit_code == "0":
        stage = "RAN-SILENT"
    else:
        stage = "OTHER"
    last_se = ""
    for ln in se.strip().splitlines():
        if ln.startswith("[TRACE]") or ln.startswith("[WARN]"):
            continue
        if ln.strip():
            last_se = ln.strip()[:90]
    first_guest = ""
    for ln in (so + se).splitlines():
        if not ln.startswith(("[TRACE]", "[WARN]", "[CRASH]", "[MiniWin]")) and ln.strip():
            first_guest = ln.strip()[:90]
            break
    return (name, exit_code, stage, n_unres,
            ";".join(unres_names[:8]) + ("..." if n_unres > 8 else ""),
            first_guest, last_se)

def main():
    dirs = sorted(glob.glob(os.path.join(RUNS, "*")))
    rows = [stage_for(d) for d in dirs if os.path.isdir(d)]
    with open(OUT, "w", newline="") as f:
        w = csv.writer(f, delimiter="\t")
        w.writerow(["name", "exit", "stage", "n_unresolved", "unresolved_first8",
                    "first_guest_line", "last_stderr_line"])
        w.writerows(rows)
    from collections import Counter
    print("stage counts:")
    for k, v in Counter(r[2] for r in rows).most_common():
        print(f"  {v:3d}  {k}")
    print(f"\nwritten: {OUT}")

if __name__ == "__main__":
    main()
