#!/usr/bin/env python3
"""SWEEP-011 baseline timing battery — where does program execution time go?

Runs each program 3x under the current engine binary. For every run records:
  exit code, wall / user / sys seconds (getrusage RUSAGE_CHILDREN deltas),
  stderr line counts by prefix, api_trace.json size + line count.
Output: artifacts/sweep011_perf/baseline.tsv (+ raw stderr per run)
"""
import os, subprocess, time, resource, sys

REPO = "/home/z/my-project/workspace/miniwin-engineering"
ENGINE = os.path.join(REPO, "minwin_loader")
PERF = os.path.join(REPO, "artifacts", "sweep011_perf")
# SWEEP-011: SWEEP011_FAST=1 -> engine fast mode (trace file off, BMP dumps
# off) and results land in runs_fast/baseline_fast.tsv; default = parity run.
FAST = os.environ.get("SWEEP011_FAST") == "1"
RUNS = os.path.join(PERF, "runs_fast" if FAST else "runs_baseline")
EXTRA_ENV = {"MINWIN_TRACE": "0", "MINWIN_SCREENSHOT": "0"} if FAST else {}
TSVNAME = "baseline_fast.tsv" if FAST else "baseline.tsv"
DL = os.path.join(REPO, "artifacts", "sweep50", "dl")

PROGS = [
    dict(name="autoruns64",  exe=f"{DL}/autoruns64/Autoruns64.exe", env={"MW_GUI_EVENTS": "1"}, timeout=60,  args=[]),
    dict(name="rammap64",    exe=f"{DL}/rammap64/RAMMap64.exe",     env={"MW_GUI_EVENTS": "1"}, timeout=60,  args=[]),
    dict(name="procexp64",   exe=f"{DL}/procexp64/procexp64.exe",   env={"MW_GUI_EVENTS": "1"}, timeout=60,  args=[]),
    dict(name="snake",       exe=f"{REPO}/artifacts/sweep005/snake.exe",
         env={"MW_GUI_EVENTS": "1", "MW_GUI_KEYS": "40,39,38,39,40,39,38,39,40,39,27"}, timeout=60, args=[]),
    dict(name="breakout",    exe=f"{REPO}/artifacts/sweep006/breakout.exe",
         env={"MW_GUI_EVENTS": "1", "MW_GUI_KEYS": "32,68,68,68,37,37,32,65,27",
              "MW_GUI_EVENTS_BUDGET": "2400", "MW_GUI_KEY_EVERY": "240"}, timeout=180, args=[]),
    dict(name="t12_libc_io", exe=f"{REPO}/tests/corpus/bin/t12_libc_io.exe", env={}, timeout=60, args=[]),
    dict(name="t08_big_cpp", exe=f"{REPO}/tests/corpus/bin/t08_big_cpp_stl.exe", env={}, timeout=60, args=[]),
    dict(name="t10_float",   exe=f"{REPO}/tests/corpus/bin/t10_float_math.exe", env={}, timeout=60, args=[]),
    dict(name="upx_sample",  exe=f"{REPO}/samples/upx_decompressed.exe", env={}, timeout=60, args=[]),
    dict(name="sqlite3",     exe=f"{REPO}/artifacts/opensource/sqlite/sqlite3.exe", env={}, timeout=60,
         args=[":memory:", "SELECT 1+1;"]),
    dict(name="7za",         exe=f"{REPO}/artifacts/opensource/7zip/x64/7za.exe", env={}, timeout=60, args=["i"]),
    dict(name="curl",        exe=f"{REPO}/artifacts/opensource/_dl/curl/curl-8.17.0_2-win64-mingw/bin/curl.exe",
         env={}, timeout=60, args=["--version"]),
    dict(name="puttygen",    exe=f"{DL}/puttygen/puttygen.exe", env={}, timeout=60, args=[]),
]

PREFIXES = ("[TRACE]", "[WARN]", "[STUB]", "[CRASH]", "[GUI]", "[MiniWin]")

def run_once(p, r):
    rundir = os.path.join(RUNS, p["name"], f"run{r}")
    os.makedirs(rundir, exist_ok=True)
    env = dict(os.environ)
    env.update(p["env"])
    env.update(EXTRA_ENV)
    before = resource.getrusage(resource.RUSAGE_CHILDREN)
    t0 = time.monotonic()
    timed_out = False
    try:
        proc = subprocess.run([ENGINE, p["exe"]] + p["args"], env=env, cwd=rundir,
                              timeout=p["timeout"], capture_output=True, text=True, errors="replace")
        out, err, code = proc.stdout or "", proc.stderr or "", proc.returncode
    except subprocess.TimeoutExpired as e:
        timed_out = True
        out, err, code = (e.stdout or b"").decode("utf-8", "replace"), (e.stderr or b"").decode("utf-8", "replace"), "TIMEOUT"
    wall = time.monotonic() - t0
    after = resource.getrusage(resource.RUSAGE_CHILDREN)
    user = after.ru_utime - before.ru_utime
    sysd = after.ru_stime - before.ru_stime

    with open(os.path.join(rundir, "stderr.txt"), "w") as f:
        f.write(err)
    with open(os.path.join(rundir, "stdout.txt"), "w") as f:
        f.write(out)

    lines = err.splitlines()
    counts = {pref: sum(1 for L in lines if L.startswith(pref)) for pref in PREFIXES}
    tpath = os.path.join(rundir, "miniwin-results", os.path.basename(p["exe"]), "api_trace.json")
    tbytes, tlines = 0, 0
    if os.path.exists(tpath):
        tbytes = os.path.getsize(tpath)
        with open(tpath, "r", errors="replace") as f:
            tlines = sum(1 for _ in f)
    crash = next((L for L in lines if "[CRASH]" in L or "[STUB]" in L), "")
    row = dict(name=p["name"], run=r, exit=code, timed_out=int(timed_out),
               wall=round(wall, 4), user=round(user, 4), sys=round(sysd, 4),
               stderr_lines=len(lines), trace_lines=tlines, trace_bytes=tbytes,
               **{pref.strip("[]").lower(): counts[pref] for pref in PREFIXES})
    row["note"] = crash[:120]
    return row

def main():
    os.makedirs(PERF, exist_ok=True)
    tsv = os.path.join(PERF, TSVNAME)
    cols = ["name", "run", "exit", "wall", "user", "sys", "stderr_lines",
            "trace_lines", "trace_bytes", "trace", "warn", "stub", "crash",
            "gui", "miniwin", "timed_out", "note"]
    with open(tsv, "w") as f:
        f.write("\t".join(cols) + "\n")
        for p in PROGS:
            if not os.path.exists(p["exe"]):
                print(f"MISSING: {p['name']} {p['exe']}", flush=True)
                continue
            for r in (1, 2, 3):
                row = run_once(p, r)
                f.write("\t".join(str(row[c]) for c in cols) + "\n")
                f.flush()
                print(" | ".join(f"{c}={row[c]}" for c in ("name", "run", "exit", "wall", "user", "sys", "trace_lines")), flush=True)
    print("DONE ->", tsv, flush=True)

if __name__ == "__main__":
    main()
