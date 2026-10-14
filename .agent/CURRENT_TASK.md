# CURRENT TASK

**Task ID:** TASK-TEST-004 (slice 2)
**Title:** Fix the synthetic builder codegen (B-1/B-2/B-3) so all 4 dispatch tests reach dispatch
**Status:** READY
**Priority:** P1

## One-line description
The non-vacuous real-loader harness (tests/dispatch_tests/test_dispatch_v2.sh, slice 1)
is green on load/run contracts; the next slice fixes the three builder defects it found
so test_b/c/d actually raise and dispatch, and test_a's post-dispatch crash goes away.

## The three defects (all found by slice 1 — evidence in the harness header)
- **B-1 call-rel32 fixups wrong** (test_b/c/d never raise): the call rel32 patch math
  uses wrong instruction offsets — calls land on the next function's `ret`
  (disassembly: `call 0x140001023` = the `ret` itself). Fix: compute each call's
  displacement from its REAL instruction address (emit position + 5).
- **B-2 UNWIND_CODE nibble order**: the unwind byte is (CodeOffset << 4) | UnwindOp —
  OpCode is the LOW nibble. The builder packs `(opinfo << 12) | (op << 8) | offset`
  into a 16-bit LE word = swapped. Rewrite `_emit_xdata`'s code emission to the
  canonical byte layout (op byte = (offset<<4)|op; opinfo lives in the second slot
  byte for wide ops, not the word's high byte).
- **B-3 ms_abi frame alignment**: push rbx + sub 0x28 leaves RSP%16 != 0 before the
  RaiseException call → the dispatcher's aligned SSE spill faults (movdqa SIGSEGV at
  0x200fdff, CONTINUE #5 live). Fix frames to sub 0x30 AND regenerate the matching
  unwind codes (ALLOC_SMALL info=5) — must be done together with B-2.

## Execution notes
1. Baseline: bash tests/dispatch_tests/test_dispatch_v2.sh (4/4 on current contracts) +
   bash scripts/g1_accept.sh (G1 pin, 12/12). Hash: evidence/master/g1/<latest>.
2. Fix B-1 first (pure fixup math) — verify test_b/c/d traces gain RaiseException +
   [DISPATCH] lines. Then B-2+B-3 together — verify test_a runs to its handler without
   the movdqa crash and test_b/c/d keep dispatching (unwind math must stay consistent).
3. Evolve the harness rows from load/run contracts to dispatch contracts as each defect
   closes (each upgrade = a real PASS).
4. Do NOT touch loader.c dispatch logic; if a loader defect surfaces, register BUG-032
   with a trace.
5. Guardrail: bash scripts/g1_accept.sh must stay OK after every loader-adjacent change.

## Verified facts you may rely on
- G1 acceptance MET (EXP-039); regression 12/12; IAT writability fix in the loader is
  Windows-faithful (mprotect around the import pass, restore after).
- The old harness (test_dispatch_harness.c) is superseded by test_dispatch_v2.sh.
