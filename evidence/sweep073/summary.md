# SWEEP-073 — fix-validation + legacy-base summary

## 1. Source HEAD and executable
- All work on top of remote HEAD `800e1f0` (SWEEP-072), branch main.
- Final engine build: sha256 in engine_sha256_final.txt, size 4,319,752
  bytes (make, gcc -O2). G1 PIN 12/12 (evidence/master/g1/20261025_214251).

## 2. Patch verdicts
- Fix 1 (_onexit via guest registry): ACCEPTED WITH MODIFICATION — the
  routing is right, the int return type was wrong (now full `_onexit_t`).
  The report's causal story for 7zr was WRONG (evidence: steps=74 crash is
  mw__initterm 64-bit stride over a 32-bit table, not host atexit) — the
  real 7zr fix required SW-1316b + SW-1319 + __p___initenv + SW-1320.
- Fix 2 (accelerator storage): ACCEPTED THEN CORRECTED — PE records are
  8 bytes, not 6 (calc.exe 760B = 95x8; Wine resource.c PE_ACCEL;
  ReactOS accel.c). Storage fixed at 512 normalized entries.
- Fix 3 (GetAsyncKeyState): ACCEPTED AND GENERALIZED (canonical table +
  latch + KEYUP pair delivery; Pong demo blocked by no binary/no compiler —
  BLOCKED_TEST_HARNESS, model proven via BgInfo64 pair trace + game parity).
- Fix 4 (Notepad++ pseudo-handle): REPORT CONFIRMED at root; fixed
  generically with synthesized module image bases (no fake-PE-header hack).

## 3. Reproductions and regressions
- 7zr: pre rc=139 (RIP=0x40159800000000, steps=74, 3/3) -> post rc=0 3/3
  with 41 real initializers + _onexit registration + banner/usage output.
  Archive create/extract: BLOCKED by the remaining 7zr arg-parse frontier
  (SW-1321) — honestly recorded, not faked.
- Calculator: parity EXACT (1 frame, rc=0), accel table now parsed at the
  correct 95x8 framing; spider n=12, winmine n=2 verified from file bytes.
- Pong: binary unavailable — see fix 3 note.
- Games + G1: all EXACT parity; G1 PIN 12/12 after every batch.

## 4. Newly observed unimplemented APIs (api_report.tsv + traces)
- _get_narrow_winmain_command_line (was gate-NULL; now implemented) —
  exercised by 20 puzzles + Notepad3 + AHK x86.
- CreateDirectoryA / RemoveDirectoryA (7zr import-time unresolved, gates).
- 7zr's msvcrt `a`-command path exercises deeper stdio than the engine
  bridges today (SW-1321).

## 5. Width split of findings
- PE32/x86: _initterm width, 0F BA + shld/shrd, late-bind thunks, __p___initenv
  (all 7zr family); RegScanner x86 crash delta vs x64.
- PE32+/x64: pseudo-module headers (N++), key-state model (BgInfo/ZoomIt
  pollers), UCRT winmain-command-line (puzzles/Notepad3 are x64 UCRT builds).
- DOS: none in corpus; backend recommendation in legacy_architecture.md.
- VB6-native: none in corpus; detection contract documented, no execution claim.
- Managed: ShareX (SWEEP-070) remains the only .NET-path data point.

## 6. Legacy minimum architecture
See legacy_architecture.md (research-grounded; Wine/ReactOS/DOSBox/
FreeDOS/VB6/WinNC identity + licenses; 5-contract priority ladder;
MiniDOS clean-room recommendation; extension-manifest design postponed).

## 7. Regression deltas and next root cause
- Zero corpus regressions; G1 12/12; puzzle family lifecycle unlocked.
- Next highest-leverage: the puzzles' GDI paint/teardown path (exact
  repro command in ../sweep074/summary.md), then SW-1313 dialogs.
