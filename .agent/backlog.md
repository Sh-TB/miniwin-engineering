# Engineering Backlog — stable IDs, explicit dependencies

Statuses: PENDING / READY / IN_PROGRESS / BLOCKED / IMPLEMENTED / TESTED / VERIFIED / PARTIAL / SUPERSEDED
Implementation status and verification status are tracked separately (e.g. IMPLEMENTED ≠ VERIFIED).

## P0 — acceptance path

### TASK-BUG-026 — Fix g_cap_rip capture operand in mw_RaiseException
- Priority: P0. Status: **TESTED (CLOSED)**. Dependencies: none.
- Scope was: `src/loader/loader.c` mw_RaiseException stub ONLY (single operand: `[rsp+0x00]` → `[rsp+0x08]`, stray `0x58` reload deleted; mirroring exp_next2_backup L842-843).
- Result (2026-10-14): regression **12/12** (was 10/12); UPX progressed from walk-never-starts/exit-134 to full 4-frame walk → Frame[3] handler → real `RtlUnwindEx(TargetIp=0x4dcd24)` → landing-pad resume, then a NEW distinct failure (exit 139, RIP=0x2004167 heap) = **BUG-027**. Report: `docs/master/experiments/EXP-036-bug026-gcap-rip-fix-live-rerun.md`; closure: `docs/bugs/BUG-026.md`; evidence: `evidence/master/bug026/` + `evidence/master/bug026_results.json`.

### TASK-BUG-027 — mw_RtlUnwindEx landing-pad resume state wrong (RSP +8 via ALLOC_SMALL decode; RDX selector not propagated)
- Priority: P0. Status: **READY (next task)**. Dependencies: TASK-BUG-026 (done).
- Scope: `mw_RtlUnwindEx` resume-state assembly ONLY. Preferred fix (ADR-008): resume with the personality-provided ContextRecord state (Rsp/Rdx from r8), override only Rip=TargetIp. Minimal alternative: fix ALLOC_SMALL `(OpInfo+1)*8` decode in mw_RtlUnwindEx AND adopt RDX from OriginalContext.
- Evidence: EXP-036 §4 (alloc_total 0x40 vs true 0x48; correct RSP 0x7ffe60f7deb0 vs used 0x7ffe60f7deb8; landing pad = return address of throw call `call 0x4a95a8` @0x4dcd1f, selector switch `sub rdx,1; jne`).
- Boundaries: do NOT touch EH_FIX, CXX-EH-FIX, the RaiseException stub, or the walk itself.
- Acceptance: regression stays 12/12; fresh UPX trace shows landing-pad entry registers (RSP=0x7ffe60f7deb0, RDX=selector, RAX=0x3bc8320) and a NEW failure point further along (progress) or UPX `--version` output (completion).
- Closure doc: `docs/bugs/BUG-027.md`.

### TASK-EXP-036 — Live re-run of UPX dispatch path after BUG-026
- Priority: P0. Status: **TESTED (CLOSED)**. Dependencies: TASK-BUG-026.
- Result (2026-10-14): executed in the same session as BUG-026; full report `docs/master/experiments/EXP-036-bug026-gcap-rip-fix-live-rerun.md` (§3 verification, §4 new-failure root cause, §7 falsifiable predictions P-1/P-2/P-3).

### TASK-BUG-028 (candidate, P3) — dead `movq 0x30/0x38/0x40/0x48(%rsp), rcx/rdx/r8/r9` loads in RaiseException stub
- Miscommented leftover of an older no-push stub variant (absent in exp_next2_backup; offsets hold pushed RBP/R12/R13/R14 today). Benign; fold into TASK-API-016 dead-code cleanup. Discovered during BUG-026 (EXP-036 §2).

## P1 — landing-pad correctness (after EXP-036)

### TASK-EXP-037 — Verify EXP-035 post_prologue arithmetic live (SUPERSEDED by BUG-027 evidence: EXP-036 §4.2/4.3 measured alloc_total=0x40 vs true 0x48 live — arithmetic is WRONG, subsumed into TASK-BUG-027)
### TASK-EXP-038 — OriginalContext adoption / RDX selector propagation (MERGED into TASK-BUG-027 — live numbers now exist, ADR-008 governs; the separate experiment is no longer needed before the fix)
### TASK-API-006 — ALLOC decode fixes: scale rule in 4 copies + windows_abi doc (BUG-027 confirmed the +8 defect ALSO hits ALLOC_SMALL in mw_RtlUnwindEx — extend scope to ALLOC_SMALL there)
### TASK-API-005 — Isolate EH_FIX + CXX-EH-FIX behind MINWIN_EH_HACKS flag, default OFF (PENDING, dep EXP-036)
### TASK-API-002 — Trace flush in mw_ExitProcess (READY — independent, small)
### TASK-API-003 — Wire SetUnhandledExceptionFilter into walk-exhaustion (PENDING, dep BUG-026)
### TASK-TEST-001 — Port unwind unit tests to tests/unit/ with portable includes (READY — independent)
### TASK-TEST-002 — ALLOC_LARGE unit tests, spec vectors (PENDING, pairs TASK-API-006)
### TASK-API-006 — Fix ALLOC_LARGE scaling in 4 copies + windows_abi doc (READY — independent; P1 priority despite independent nature because latent)
### TASK-TEST-004 — Fix dispatch tests (import patching + fail-if-not-dispatched) (READY — independent)
### TASK-TEST-005 — Synthetic PE builder v2 + Tier-B case set (PENDING, dep TEST-004 or parallel)

## P2 — generalization enablers
TASK-API-007 (unresolved-import trap), TASK-API-008..013 (VirtualAlloc/Heap/file/thread/__C_specific_handler/_Unwind_DeleteException), TASK-MAPPING-01..07 (per IMAGE_MAPPING_AND_RELOCATION_AUDIT.md), TASK-TEST-006 (Tier-D suite), TASK-TEST-007 (golden-trace), TASK-TEST-008 (UPX acceptance harness).

## P3 — hygiene
TASK-API-014 (format warnings), TASK-API-015 (realloc trace smell), TASK-API-016 (dead code), DOC-001 (README capabilities refresh), DOC-002 (BUG-001/NEW_AGENT_START hash correction note), OWNER-001 (push credentials), OWNER-002 (knowledge-transfer/ dedup decision).

## Superseded entries (historical M8/M9 etc.)
- Old "M8: RtlUnwindEx context restoration" — SUPERSEDED by TASK-BUG-026 + EXP-037/038 chain (the M8 framing assumed the current source still reached the landing pad; it does not).
- Old "M9: verify UPX --version output" — folded into TEST-008 / RELEASE_CRITERIA G1.
