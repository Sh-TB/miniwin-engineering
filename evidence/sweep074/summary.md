# SWEEP-074 — 48-target campaign summary

Numbering note: the supplied spec named this "SWEEP-072" with
evidence/sweep072/, but that directory was already landed (issue-completion
wave, commit 800e1f0). The campaign evidence therefore lives in
sweep074/, and the fix-validation work in sweep073/, keeping directories
unique. No other renumbering was applied.

## Matrix state (final battery, current engine)
- 50 rows in results.tsv: 39 with real binaries, 11 BLOCKED_DOWNLOAD
  (Explorer++ x86/x64, SciTE x32/x64, KeePass, WinSCP, HxD, Resource
  Hacker, Ditto, FastCopy, FreeCommander — hosts 404/refused/HTML-gated;
  per the task rule these are recorded, not silently substituted).
- Verdict counts: FAIL_CRASH 27, PARTIAL 3, PASS_EXPECTED_CLI_ERROR 2,
  FAIL_RUNTIME_API 1, PASS_LAUNCH_ONLY 6, BLOCKED_DOWNLOAD 11.
- Every run: 3 cold starts, per-target fresh hive, raw logs in raw/.

## The campaign's engineering result (shared roots, not per-program hacks)
1. SW-1323 (UCRT _get_narrow_winmain_command_line): moved the ENTIRE
   20-puzzle family + Notepad3 + AHK-x86 from pre-main SIGSEGV to a full
   window/message lifecycle (one fix, 27 guests touched).
2. SW-1324 (vfprintf stream fallback): removed the loader-fault class on
   guest-constructed stdio blocks.
3. SW-1320 (named late-bind thunks + SetDefaultDllDirectories arity):
   fixed two distinct 7zr crash signatures.
4. SW-1322 (real pseudo-module bases with synthesized PE headers):
   fixed the notepadpp64 header-introspection crash at its true root.
5. SW-1316/1316b/1319 + __p___initenv: 7zr from rc=139 (74 steps) to a
   complete CRT init, banner+usage output, rc=0 3/3.
6. SW-1317b (8-byte accel records) + SW-1318 (key state): latent corpus
   corruption removed; input semantics contract established.

## Honest remaining frontier (highest leverage first)
1. Puzzle GDI paint path (20 targets): windows create, pump runs,
   PostQuitMessage handled; frames still 0 (32x32 paint surface, nil
   brush snapshot) and teardown crashes (midend free walk). Exact
   reproduction: MW_GUI_EVENTS=1 MW_GUI_EVENTS_BUDGET=60 ./minwin_loader
   artifacts/sweep074/dl/blackbox.exe
2. Dialog-template engine SW-1313 (gate for PuTTY config, N++ find).
3. NirSoft x86 width-specific crash (RegScanner x86 vs x64 divergence).
4. 7zr arg-parse of `a` (SW-1321) — archive create/extract regression
   pending that car.
5. .NET/utility clean-exits need frame capture + scripted probes before
   any PASS_BEHAVIOR claim.

## G1 / regression delta
- G1 PIN 12/12 (evidence/master/g1/20261025_210649, sha b7b86431...;
  final re-pin recorded in sweep073/summary.md).
- Corpus parity EXACT: winmine 82 / sol 38 / freecell 10 / calc 1 /
  breakout 48 / snake 30 / spider in 593-631 band / notepad, wordpad,
  mspaint rc=0. Zero regressions attributable to the eight engine changes.
