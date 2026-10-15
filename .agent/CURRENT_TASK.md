# CURRENT TASK

**Task ID:** TASK-BUG-037
**Title:** Unwind-code interpreter faults on CRT-startup UNWIND_INFO (blocks C++ EH)
**Status:** READY (NEXT)
**Priority:** P0

## One-line description
During t08's first C++ throw the dispatcher walks correctly through frames 2->3 and
then the loader's opcode interpreter faults (`mov (%r9),%rbx`, R9=0x27 = raw slot
offset used as an address) on the mingw CRT-startup frame's UNWIND_INFO
(RF begin=0x1470 end=0x23d9, UI @ RVA 0x50458, v1, flags=0x03, 13 slots, frame 0x85).

## Fix plan (evidence-first, per protocol)
1. Standalone reference decoder (Python) for UNWIND_INFO; pin RVA 0x50458's exact
   slot sequence as a golden vector in tests/ (decode by hand first, then match).
2. Align the loader interpreter: SET_FPREG establishes the FP baseline (FP-relative
   saves consume it); SAVE_XMM128 uses 16-byte slots; UWOP_EPILOGUE (0x06) is
   version>=2-only and must not decode as v1; re-check cursor bookkeeping when
   prologue_size (34) exceeds count*2.
3. Verify: t08 reaches its own catch and prints `t08: PASS` (exit 0, 3/3
   deterministic runs); regression 12/12; UPX G1 gate; then evaluate G2 (C++ EH).

## Ground rules
- Do not touch EH_FIX / CXX-EH-FIX (flag-guarded, default OFF), the RaiseException
  stub, or call_handler_aligned's pinned-register block (EXP-039 lessons).
- Every change keeps regression 12/12 + G1 gate green.

## Context from CONTINUE #6 (this session)
- Appcompat sweep landed: 7/9 PASS (all C programs incl. big t07; both small+big
  C tiers). BUG-032..036 CLOSED TESTED; TASK-TLS-001 + TASK-API-007/008 CLOSED
  TESTED (import table 162 -> 188). t09 blocked on wave-3 API set (TASK-API-009).
- Backlog expanded with 31 new tasks (sweep-driven).
