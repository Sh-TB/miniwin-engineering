# API IMPLEMENTATION BACKLOG — Priority by Target Evidence

**Produced:** TASK-005, 2026-10-14, at HEAD `39181aa`
**Principle:** implement only behavior required by *measured* targets; keep implementations reusable; every entry carries an acceptance criterion and the evidence that justifies its priority. Complements `API_COMPATIBILITY_DATABASE.md` (what exists) and `TESTING_STRATEGY.md` (how proven).

## P0 — Blocking UPX acceptance path (evidence: UPX_FAILURE_FORENSICS.md)

| ID | Item | Justification (measured evidence) | Acceptance criterion |
|---|---|---|---|
| API-001 | **Fix `g_cap_rip` capture** (BUG-026): restore `movq 0x08(%rsp),%rax` before `movq %rax,g_cap_rip` | Probe proof F8: `g_cap_rip==rsp_entry`, real ret=0x49d5b1 | Regression 12/12 restored (tests 8 & 10); UPX re-enters dispatch walk |
| API-002 | Trace flush in `mw_ExitProcess` (flush `g_trace_log` + stdio before `_exit`) | ExitProcess path discards trace tail — evidence loss during Phase-B experiments | Trace complete when UPX exits via ExitProcess |
| API-003 | Wire `SetUnhandledExceptionFilter` invocation into walk-exhaustion path (S3) | UPX registers filter at 0x49c9c0 (trace); never called | Synthetic test: walk-exhaustion calls filter once with correct EXCEPTION_POINTERS |

## P1 — Landing-pad correctness (evidence: UPX_FAILURE_FORENSICS §3–4)

| ID | Item | Justification | Acceptance criterion |
|---|---|---|---|
| API-004 | Adopt `OriginalContext` in `mw_RtlUnwindEx` (5th arg = [RSP+0x28]), or explicitly set RDX selector + all NV regs | N-4: landing pad `mov %rdx,%rbx; sub $1,%rbx; jne` reads garbage selector | Synthetic Tier-B test with selector-dependent landing pad passes |
| API-005 | ~~Disable/flag EH_FIX (dispatcher LSDA override) and CXX-EH-FIX (load_pe byte patch) behind `MINWIN_EH_HACKS=0/1` (default off)~~ **DONE 2026-10-15** (ADR-010, EXP-038): both flag-guarded, default OFF, nothing deleted; `MINWIN_EH_HACKS=1` restores legacy behavior byte-identically; default-off run reaches the PE's own dt_check assertion via the clean personality path | N-2/N-3: app-specific, undocumented, target-modifying; violate engineering rules | ~~Default-off run reaches landing pad via personality path; hacks only restore old behavior with flag~~ MET (EXP-038 §3/§6) |
| API-006 | Fix ALLOC_LARGE scaling ×4 copies (+ fix `docs/windows_abi/windows_x64_complete.md` table) | N-1: latent; blocks any target with >128-byte prolog allocs; doc propagated the bug | Tier-A unit test with ALLOC_LARGE(info=0/1) prologs computes exact RSP |
| API-007 | Unresolved-import trap stub (replace "leave name RVA in IAT" policy) | BUG-025 crash mechanism: IAT slot with name RVA jumped as code | Tier-D test: PE importing unknown API gets loud diagnostic + controlled abort |

## P2 — Required by second targets (evidence: UPX uses them; generalization plan)

| ID | Item | Justification | Acceptance criterion |
|---|---|---|---|
| API-008 | VirtualAlloc/VirtualFree (real mmap-backed) | Absent from table; any typical MinGW target using KERNEL32 heap/virtual APIs fails at import resolution (loud trap after API-007) | Tier-B synthetic calling VirtualAlloc/Free round-trip |
| API-009 | HeapAlloc/HeapFree/HeapReAlloc/GetProcessHeap (arena-backed, real free) | Same absence; bump-allocator history shows need | Tier-B synthetic alloc/free/realloc stress |
| API-010 | CreateFileA/ReadFile/WriteFile/CloseHandle real file semantics | CloseHandle is stub; KERNEL32 file APIs absent | Tier-B synthetic writes+reads back a temp file |
| API-011 | Thread APIs decision: implement `_beginthreadex` via pthread + per-thread TEB, or explicitly reject multi-thread targets | Single static TEB today; silent misbehavior risk | Documented scope + test for whichever path chosen |
| API-012 | `__C_specific_handler` real implementation (SEH scope tables) | Currently unconditional ContinueSearch; blocks MSVC-style targets | Tier-B synthetic __try/__except passes |
| API-013 | `_Unwind_DeleteException` | Exception objects leak on each catch | Synthetic: delete called on catch path |

## P3 — Hygiene (evidence: build warnings TASK-002)

| ID | Item | Acceptance criterion |
|---|---|---|
| API-014 | Fix MW_TRACE format warnings (L702, L1448, L1593, L1630, L1799, L1805, L3334-3340) | `make` warning count drops to only accepted classes |
| API-015 | Fix realloc use-after-free smell in `mw_realloc` trace (L2468-2469) | Copy `ptr` value before realloc; warning gone |
| API-016 | Remove dead code: `mw_RaiseException_baseline`, unused vars (`pre_unwind_rsp`, `prolog_size`, `page_off`, `g_errno_ptr`, `hint`) | Build clean; no behavior change (regression green) |

**Rollback plan for every entry:** each is a separate bounded task with its own commit; `checkpoints/` pre-change archive for risky items (API-004/005/012); regression suite + Tier-B suite green required before/after.
