# SWEEP-073 baseline — audited state before any change

## Repository / build state (Phase 0)
- Repo: Sh-TB/miniwin-engineering, local branch `main` tracking `origin/main`.
- HEAD at session start: `800e1f0` (SWEEP-072 landed: PEB UTF-16 cmd buffer,
  issue #1 closed, English-only GitHub content, portable wave).
- Remote HEAD verified in sync (`git fetch` then `git status -sb`: 0/0).
- Working tree: only untracked guest-sandbox dir `Windows/` (PUTTY.RND etc.),
  never committed.
- Build: `make` (gcc -O2 -g -Wall -no-pie -Ttext-segment=0x2000000, single TU
  src/loader/loader.c 41.5k lines + include/pe.h).
- Baseline binary BEFORE this sweep: 4,296,104 bytes, sha256
  08f85c713556039e097b906d1d3a035e44356fe48e5998cde324a9400aaf3e4e (the
  SWEEP-072-a build recorded in the worklog).
- G1 at baseline: PIN 12/12 valid for that sha (evidence/master/g1/20261025_191451).

## Supplied-patch provenance
`miniwin_fixes.patch` / `MINIWIN_CHECKLIST.md` were NOT found in the sandbox,
in any git tree, or attached anywhere reachable. The audit therefore acted on
the two fix descriptions in the task text and validated each claim directly
against the current source, per the task's own instruction ("Do not assume the
supplied report is correct").

## Pre-fix reproductions (the report's own failure stories)
1. 7zr.exe (7-Zip 26.04 x86, artifacts/sweep065/dl/7zr.exe):
   `rc=139`, `[CRASH] Signal 11 at RIP=0x40159800000000`, guest steps=74,
   `[PE32] guest EIP=0x00a00001` — reproduced 3/3 BEFORE any change.
   Note: the report attributed this to `_onexit` -> host atexit. The trace
   shows a DIFFERENT first divergence (see fix_audit.md): the value
   0x40159800000000 is a 64-bit merge of two adjacent 32-bit pointer-table
   entries {0x00000000, 0x00401598} read by mw__initterm.
2. notepadpp64.exe (Notepad++ 8.9.8.1 x64): with this sweep's pre-SW-1322
   interim builds the guest died at RVA 0x1d04 with si_addr=0xdead0048
   (`movsxd rax,[rcx+0x3c]`, RCX=0xdead000c = comctl32.dll pseudo-handle) —
   exactly the mechanism the report described. At the 800e1f0 baseline the
   guest reached its 2 MessageBoxes and died at the older 0x3b94 frontier;
   the sweep's startup fixes advanced the guest deeper into init, exposing
   the header-introspection crash, which SW-1322 then fixed.
3. GetAsyncKeyState: `src/loader/loader.c:25720` (pre-fix line) literally
   `return 0` — confirmed at source level; x64 corpus guests (ZoomIt64,
   Bginfo64) resolve and call it.
4. Accelerator store: `g_accel_slots[8]` with `uint32_t[2]` (8 bytes) while
   LoadAcceleratorsA/W memcpy'd `n*6` bytes, n up to 512 — confirmed at
   source level with live-corpus tables: calc.exe id=0x69 = 760 bytes,
   spider id=0x66 = 96 bytes, winmine id=0x1f5 = 16 bytes copied into an
   8-byte field on every run.
