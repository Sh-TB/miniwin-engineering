# SWEEP-069 — 29 NEW guests + tool-impact audit (better and faster ... how much did the auxiliary tools help ... add the same number of programs and games again)

Date: 2026-10-25 · Base: 48e468b (origin/main, post SWEEP-068) · Binary rebuilt from source
4,295,920 bytes (41,579-line `src/loader/loader.c`, gcc -O2, one translation unit) ·
winmine smoke rc=0 / 82 frames EXACT before battery.

## Part 1 — 29 NEW guests (same count as SWEEP-068, none ran before)

Harness: `scripts/sweep069_battery.sh` (xargs -P4, fresh per-exe hive). Table: `battery.tsv`.
Full tree per guest under `miniwin-results/` (regenerable), raw: `battery_raw.txt`.

**Verdict: 29/29 executed. Headline finds:**

1. **NirSoft family SPLIT — the SWEEP-065 blanket "13x rc=139" is now stratified**
   (SWEEP-066/067 engine fixes measurably paid off):
   - **2 NEW CLEAN EXITS**: faview, recentfilesview (rc=0; were rc=139 in SWEEP-065 era)
   - **3 DEEP divers** past the UPX stub into real OEP code: heapmemview **794,277** steps,
     iconsext **603,019**, htmlastext **460,750** (driverview-class depth, new SW-1302 kin)
   - 5 early-crash at 26 steps (devmanview/dnsquerysniffer/installedappview/multimonitortool/
     simpleprogramdebugger — the usbdeview-27-step family)
   - 2 PE32-FATAL rc=86 (resourcesextract, urlprotocolview — SW-1297 x86 frontier family)
2. **gup.exe (Notepad++ updater) hits the EXACT 2450-step signature** of notepad++.exe —
   same codebase, same frontier; a free A/B repro for SW-1297-x86 at 8.5x smaller binary.
3. **Sysinternals x64: 8/13 clean or honest exits** — autologon64/listdlls64/pipelist64/
   psloggedon64/psservice64 rc=0; curl printed its REAL usage error («try 'curl --help'»)
   and exited 2; strings64/du64/psinfo64/sdelete64 printed real banners then self-exited 255
   (no-console-device path = real program decisions).
4. **XP trio clean**: drwtsn32, regedt32 rc=0; sndvol32 rendered its real
   «This program will now close.» MessageBox (no-audio-device behavior).
5. **logonsessions64 rc=133** = the winmergeu EH-chain terminate→int3 signature (SW-1201
   family) — first x64 Sysinternals data point in that class.

New worklist cars: **SW-1306** (NirSoft deep-diver trio: heapmemview 794,277 —
driver the common frontier), **SW-1307** (gup 2450 A/B for SW-1297-x86),
**SW-1308** (logonsessions64 exit-133 x64 EH chain).

## Part 2 — Tool-impact audit (measured, not vibes) (how much did the auxiliary tools help you)

| Tool | Measured impact (from git history + this session) |
|---|---|
| **Ghidra 12.2 headless** | hours → **minutes**: procexp64 5.2MB = 129s → 2,939 fns/767 imports (SWEEP-063); batch x20 = **30,289 fns / 3,778 imports / 163,774 resources in ~726s** (36s/prog, SWEEP-065). Directly decoded SW-1292 paint chain, closed SW-1298 by audit, validated SW-1204b ordinals |
| **Parallel recon subagents** | SWEEP-067: 4 engine fixes in one sweep; SW-1300 (the driverview root cause) agent-decoded — the fix that took 13 NirSoft tools from blanket-crash to today's stratified runs |
| **Parallel battery harness (xargs -P4)** | SWEEP-068: 24 guests one wave; SWEEP-069: **29 guests one wave (~7 min wall)** vs ~35+ min serial → ~5x throughput; fresh-hive-per-run killed the stale-registry artifact class |
| **Wine .spec cross-check (sweep066_arity_check.py)** | 8,345 names × 23 DLLs machine-checked → **226 arity bugs fixed in one shot** (SWEEP-066) — impossible by hand at that scale |
| **G1 PIN + 3x discipline gate** | 12/12 every build; caught a mid-round UPX regression (SWEEP-030) before push; zero-regression claims in SWEEP-068/069 are machine-verified, not asserted |
| **gcc single-TU build + make** | after today's sandbox reset: full engine rebuild **~2 min** (41,579 lines), winmine smoke 82-frames EXACT immediately — recovery ≈ 5 min total including corpus pull |
| **Git fetch-check + no-force pushes** | today's reset proved it: local sandbox rolled back to SWEEP-030 era, but origin/main @ 48e468b was intact; `git pull --ff-only` restored 60 commits + artifacts instantly |
| **python struct/PE sniffing** | machine-type checks in seconds (npp x86 vs x64 disambiguation, UPX detection) |
| **py7zr + curl downloads** | corpus 48: Notepad++ 8.9.8.1, WinRAR 7.12, Sysinternals, 13 NirSoft — all fetched/extracted headless |

**Net effect**: SWEEP-065→069 = five sweeps in one day, 100+ program-runs, ~10 engine
root-cause fixes, each push machine-verified. The tool flywheel is the reason the train
moves at this speed — user's standing rule (always let the tools help you) is load-bearing.

## Method notes

- Sandbox was found reset to SWEEP-030 era (242af84, 2.7MB stale binary, missing
  branch/artifacts) — Rule #1 caught it; recovery = move stale untracked files aside,
  `git pull --ff-only` → 48e468b, `make`, smoke-test, proceed. No data lost.
- 29 guest exe filenames all distinct → parallel-safe result dirs (SWEEP-068 pattern).
- No engine changes this sweep — measure-and-ship round; G1 pin 049876c0 still valid.
