# SWEEP-016 — elephant batch (2026-10-19)

One batch, five landings, two program gates closed, three root causes fixed.

## Landings
1. **SW-801a-e resource/version port batch** applied (patch was queued in
   SWEEP-015; 3-way apply, conflicts resolved, UXTHEME dedup, stray brace
   from `git apply -3` fixed).
2. **BUG-072 load_pe hardening** — `fopen()` on a DIRECTORY succeeds on
   Linux; `ftell` then fails, `malloc((size_t)-1)` returns NULL and the
   loader SEGVs at `dos->e_magic`. This was the real cause of "Bginfo64
   exit 139 0-stub" and part of the SW-801h "environment sensitivity".
   Now: `stat()`+`S_ISREG` gate, every I/O result checked, honest
   `[ERROR] Not a regular file`.
3. **BUG-073 BSTR issuance registry** — `SysFreeString` only frees BSTRs
   issued by `bstr_alloc_len`; foreign BSTRs (guest-built / from
   unimplemented APIs) are reported and kept. Bginfo64 was crashing
   inside libc `free()` via `mw_SysFreeString`. Bginfo64 139 -> exit0 x3.
4. **SW-652 topology family + full GLPIEx** — GetNumaHighestNodeNumber,
   SetThreadGroupAffinity, GetThreadGroupAffinity, GetNumaNodeProcessorMask
   (Ex + non-Ex), GetActiveProcessorCount/GroupCount,
   GetCurrentProcessorNumberEx; GetLogicalProcessorInformationEx rewritten
   as a full multi-entry walk (Core xN, NumaNode, Package, Group) with
   old-SDK NUMA layout (GroupCount sub+22) and `cls=0xFFFF`=RelationAll;
   unknown class = honest ERROR_INVALID_PARAMETER.
   Coreinfo64 139 -> exit0 x3 with full topology dump (SW-561 version gate
   passes too: Coreinfo v4.02 banner + CPU/CPUID/cache/group maps).
5. **SW-801k winsta.dll pseudo-module** — WinStation family with honest
   contracts; Procexp64 version-probe MessageBoxes 3 -> 0.

## SW-801h root cause correction
SWEEP-015's sweep ran `sweep50/dl_extract/*.exe` — a directory that no
longer exists (untracked scratch lost between sessions). The 139s were
never environment sensitivity: the current corpus is the re-downloaded
2026-era official builds under `dl/<dir>/<exe>`. Re-baselined x3.

## Measured results (artifacts/sweep016/sweep/sweep016_sweep.tsv)
- Bginfo64: 139x3 -> exit0 x3 (SW-651 CLOSED)
- Coreinfo64: 139x3 -> exit0 x3, 16,189 B stdout (SW-652 + SW-561 CLOSED)
- procexp64: winsta dialogs 3 -> 0, new frontier CLR-adjacent init (SW-809)
- regression: G1 12/12 re-pinned sha a5d94299, calc 9/9, snake 3x exit0
  (30 frames byte-identical), breakout 3x exit0 (381 frames, 27 bricks)
- 9 remaining 139s each filed as worklist items SW-802..SW-810 with their
  exact crash signatures.

## Crash-signature method
gdb unavailable in this sandbox; used loader crash dumps + addr2line on
loader frames + pefile IAT decode of `call [rip+disp32]` slots +
objdump -b binary disassembly at guest RIP. Key finding chain for
Coreinfo64: GetProcAddress(GLPIEx) -> probe cls=0xFFFF ->
success-with-0-entries lie -> uninit-stack walk -> `cmpw [rbx+2]`.
