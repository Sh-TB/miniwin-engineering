# CURRENT TASK

**Task ID:** TASK-TEST-004
**Title:** Fix dispatch tests (import patching + fail-if-not-dispatched)
**Status:** READY
**Priority:** P1 (promoted — now blocks TEST-005 which must guard the G1 success)

## One-line description
The dispatch test suite is broken/vacuous (it passes even when dispatch never happens,
and patches imports in a way that no longer matches the loader's import resolution).
Fix it so it genuinely exercises RaiseException->walk->personality on a synthetic PE,
failing loudly when dispatch does not occur.

## Why this is the task (evidence)
- state.md "Known repository inconsistencies" item (5): dispatch tests broken/vacuous (TEST-004).
- G2 requires Tier-B green; TEST-005 (multi-frame EH case set) depends on this suite's
  harness. With G1 met (EXP-039), the success state must be GUARDED by synthetic cases
  (P-F/P-G in EXP-039 §9) — the flip class (compile-dependent phase-1) makes any loader.c
  change suspect until re-pinned (also see TASK-G1-001).

## Execution notes for the next agent (ordered steps)
1. Baseline per protocol: clean build + 12/12 regression (test-10 now asserts G1 success),
   binary hash from evidence/master/bug031/hashes.txt.
2. Read tests/dispatch_tests/ + the loader's import resolution path; identify why the
   tests are vacuous (likely: they patch a copy of the IAT the loader never uses, or the
   assertion compares trace lines the loader no longer emits).
3. Rebuild the harness on the synthetic PE builder (tests/synthetic_pe/) so dispatch
   failure is a hard FAIL, not a vacuous PASS.
4. Do NOT touch loader.c dispatch logic in this task; if a loader defect surfaces,
   register a BUG with a trace and keep the task scoped to tests.

## Verified facts you may rely on (this session)
- G1 acceptance met: UPX --version exit 0 (5/5); regression 12/12; EXP-NEXT-2 PASS.
- The GCC SEH chain (resume-command intercept + continuation unwind + raise-site
  register pinning) is the code under test — ADR-011, EXP-039 §6.
- MINWIN_EH_HACKS=1 restores legacy behavior exactly; tests must run with hacks OFF.

## Boundaries
- No loader.c changes in this task.
- No sample-specific constants in tests: synthetic PEs only.
- If G1 regresses at any point: STOP, register BUG-032 with full trace, revert to the
  pinned binary evidence (hashes.txt).
