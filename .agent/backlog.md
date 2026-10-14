# Engineering Backlog — stable IDs, explicit dependencies

Statuses: PENDING / READY / IN_PROGRESS / BLOCKED / IMPLEMENTED / TESTED / VERIFIED / PARTIAL / SUPERSEDED
Implementation status and verification status are tracked separately (e.g. IMPLEMENTED ≠ VERIFIED).

## P0 — acceptance path

### TASK-BUG-026 — Fix g_cap_rip capture operand in mw_RaiseException
- Priority: P0. Status: READY. Dependencies: none.
- Scope: `src/loader/loader.c` L1697-1702 ONLY (restore `movq 0x08(%rsp),%rax` before `movq %rax, g_cap_rip(%rip)`, mirroring exp_next2_backup L842-843).
- Hypothesis: restoring the return-address capture re-enables the verified dispatch path (5-frame walk → handlers → RtlUnwindEx), eliminating terminate/abort(134).
- Required evidence: before/after regression results; fresh UPX trace showing Frame[0..4] walk.
- Boundaries: do NOT touch EH_FIX, CXX-EH-FIX, RtlUnwindEx, or unwind logic in this task.
- Test plan: `make clean && make`; regression suite; UPX run with trace capture.
- Acceptance: regression 12/12 (tests 8 & 10 pass again — exit 139 era); UPX trace shows ≥4 frames walked and at least one handler call OR a new, distinctly-identified failure point (progress, not completion).
- Rollback: single-line revert; pre-change checkpoint `checkpoint_pre_bug026.zip`.
- Result: (open)

### TASK-EXP-036 — Live re-run of UPX dispatch path after BUG-026
- Priority: P0. Status: PENDING. Dependencies: TASK-BUG-026.
- Scope: run + capture trace/forensics; compare against forensics §3/§4 predictions.
- Acceptance: written experiment report `docs/master/experiments/EXP-036-*.md` with evidence paths.

## P1 — landing-pad correctness (after EXP-036)

### TASK-EXP-037 — Verify EXP-035 post_prologue arithmetic live (PENDING, dep EXP-036)
### TASK-EXP-038 — OriginalContext adoption / RDX selector propagation (PENDING, dep EXP-036; ADR-008)
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
