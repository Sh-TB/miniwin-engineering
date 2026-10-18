# SWEEP-011 — REA-powered speed: program wall time + debugger speed (2026-10-18)

User directive: «پس از ابزارهای rea نهایت استفاده رو بکن برای کمتر شدن زمان اجرا
برنامه‌ها، سرعت دیباگر بهتر بشه» — maximize the use of the REA toolchain to
reduce program execution time and make the debugger faster.

## Measured starting point (13-program battery, 3x each, engine df28de0f)

Guest code runs NATIVELY — every program except the two frame-paced games
finishes in 2–8 ms. The real costs were elsewhere:

1. **Per-run I/O tax**: `MW_TRACE` did `fprintf + fflush` PER API line into
   api_trace.json (breakout: 21,608 lines/run), plus a BMP disk write per GUI
   frame (breakout: 381 x ~900 KB). sys time on breakout: 292 ms of 1,228 ms.
2. **Per-query debug tax**: every `rea` address question paid a fresh Ghidra
   auto-analysis (40–90 s; ephemeral project — evidence/sweep008_rea/README).
   The post-home-reset environment had also wiped the whole toolchain.

## What landed (all env-gated; defaults = previous behavior)

### 1. In-engine function-map symbolizer (the debugger-speed core)
- `MINWIN_FUNC_MAP=<map.json>[,<map2.json>]` — O(log n) RIP→function lookups,
  merged maps, zero JSON dependencies, loaded eagerly at startup.
- Now auto-symbolizes: **[STUB]** lines (`caller=FUN_14005c860+0x50b`),
  **[CRASH]** `[FUNC]` line, crash **[SCAN]** stack values, backtrace frames +
  return addresses, and the profiler output.
- Map builders:
  - `scripts/sweep011_funcmap_export.py` — persistent Ghidra headless project
    (`/home/z/ghidra-dl/mwproj`): each PE auto-analyzed ONCE (Java postScript,
    because Ghidra 12 needs PyGhidra for .py), then `-process -noanalysis`
    re-exports in seconds. 10 guest maps built (2938 functions for procexp64,
    4958 for ZoomIt64, ...).
  - `scripts/sweep011_engine_funcmap.sh` — nm-based map of the engine itself
    (1070 functions) so profiler samples inside MiniWin resolve by name.
- The home-reset toolchain was rebuilt: rea 6.7.0 (npm rea-agents), Ghidra
  12.1.4 PUBLIC, Temurin JDK 21.0.12.1 (exact prior versions).

### 2. REA deep-dive loop: --snapshot
`rea function <pe> <addr> --snapshot <name>` — cold 2m29s, warm **2.0s**
(74x). Snapshot lives next to the working dir (e.g. /tmp/rea_snap_test/mw11).
Policy: funcmap for "which function", rea --snapshot for decompiles, cold rea
only for first-touch questions.

### 3. MINWIN_PROFILE=1 poor-man's guest profiler
ITIMER_PROF/SIGPROF at 1 ms CPU; sampled RIPs land in
`miniwin-results/<base>/profile.json` (top-16, symbolized via funcmap),
dumped by the single shutdown choke point (atexit / ExitProcess / handled
crashes). One breakout run answered "where does the time go": **mwb_fill
(+0x65/+0x57/+0x4e/+0x5e) and mw_BitBlt+0x173** — the engine's own pixel
loops, not guest logic (breakout_profile.json).

### 4. Program wall-time wins (engine fast paths)
- api_trace.json: 64 KiB stdio buffer instead of per-line fflush (content
  identical; flushed at every shutdown path; unhandled-signal loss bounded to
  64 KiB). `MINWIN_TRACE=0` disables the file entirely.
- `MINWIN_SCREENSHOT=0` skips per-frame BMP writes (frame numbering kept).
- SIGTRAP (guest int3) now gets the full crash report — zoomit64's silent
  TRAP-133 death (SW-553) is now diagnosed: Signal 5 at RIP=0x140054c56,
  registers/bytes/backtrace, exit stays 133.
- Backtrace walker hardening: follows the return chain + bounded to the used
  stack (the old unbounded walk killed the handler on zoomit64).

## Results (median of 3; full table artifacts/sweep011_perf/COMPARISON.md)

| program | exit B→A | wall BEFORE | wall FAST | trace lines B→F |
|---|---|---|---|---|
| breakout | 0→0 | 1228.1 ms | **896.1 ms (−27%)** | 21608→2 |
| snake | 0→0 | 101.6 ms | **86.3 ms (−15%)** | 1175→2 |
| 7za | 0→0 | 4.7 ms | **2.7 ms (−43%)** | 4550→2 |
| sqlite3 | 0→0 | 7.0 ms | **3.9 ms (−44%)** | 2177→2 |
| t08_big_cpp_stl | 0→0 | 3.4 ms | **1.7 ms (−50%)** | 2644→2 |
| t12_libc_io | 0→0 | 1.8 ms | 1.5 ms | 373→2 |
| procexp64 | 1→1 | 6.0 ms | 4.7 ms | 1348→2 |
| autoruns64 | 1→1 | 5.3 ms | 4.1 ms | 960→2 |
| rammap64 | 1→1 | 3.0 ms | 2.3 ms | 864→2 |
| upx_sample | 1→1 | 3.4 ms | 2.5 ms | 455→2 |
| curl | −11→−11 | 4.9 ms | 4.2 ms | 787→2 |
| puttygen | 139→139 | 2.8 ms | 2.5 ms | 430→2 |

All 13 exit codes identical BEFORE→AFTER (parity proven). Sub-10 ms rows carry
±1–2 ms scheduler noise; the ≥100 ms rows are the meaningful deltas.

## Debugger-speed scorecard

| question | BEFORE | NOW |
|---|---|---|
| "which function is RIP X?" | 40–90 s rea query | **<1 ms** in-engine (auto on [STUB]/[CRASH]/[PROF]) |
| deep decompile of a function | 40–90 s per query | **2.0 s** warm `--snapshot` (74x) |
| "why is program X slow?" | manual multi-session hunt | **1 profiled run** → profile.json |
| zoomit64 int3 | silent death | full crash report, exit 133 |
| puttygen stack (SW-552) | raw addrs + manual REA | `FUN_1400028b0+0x32` (WinMain), `FUN_140041140+0x106`, `entry+0x9` on the crash lines |

## Verification
- G1 pin: binary sha256 5e81f847e897c7239eb8e1c5893109fce32c267b767168eb3f43eb4939c9e47e,
  UPX banner exact, regression **12/12** (evidence/master/g1/20261018_153005).
- Parity battery 13/13 exit-code identical (runs_baseline vs BEFORE capture).
- Live diagnostics on the pinned binary: procexp64 stub caller named,
  puttygen stack named, zoomit64 exit 133 with report.

## Worklist deltas
- SW-553 (zoomit64 TRAP133): int3 site now diagnosed — RIP=0x140054c56 with
  full context; next step is the rea --snapshot decompile of the containing
  region to identify the anti-debug/limit check.
- SW-552 (puttygen RIP=0): crash stack machine-named (WinMain
  FUN_1400028b0+0x32 return; caller chain FUN_140041140); the .data fn-ptr
  hunt continues with instant symbolization.
- Tooling: funcmap rebuild after every engine rebuild
  (`scripts/sweep011_engine_funcmap.sh`).

## Files
- artifacts/sweep011_perf/: baseline.tsv, baseline_fast.tsv, COMPARISON.md,
  funcmaps/*.json (11 maps)
- evidence/sweep011_perf/: zoomit64_trap_diag.txt, puttygen_symbolized_stack.txt,
  procexp64_stub_named.txt, breakout_profile.json
- Raw runs (git-ignored): artifacts/sweep011_perf/runs_baseline/ (654 MB incl.
  parity BMPs), runs_fast/
