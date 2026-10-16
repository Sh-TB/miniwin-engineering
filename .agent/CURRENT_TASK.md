# CURRENT TASK

**Task ID:** TASK-TEST-004-C1 (+D1 queued behind it)
**Title:** test_c: real ContinueSearch chain (two-handler walk)
**Status:** READY
**Priority:** P2

## One-line description
Slice 2 closed B-1/B-2/B-3: all 4 synthetic tests now raise through the real IAT call,
reach RtlDispatchException, get a handler invoked, and resume cleanly (suite 4/4 on
dispatch contracts, 3x deterministic; G1 pin OK; loader.c untouched). Next micro-task:
test_c's ContinueSearch handler is dead code — restructure so the walk really continues.

## Where slice 2 landed (facts you may rely on)
- B-1: test_b/c/d call fixups were off-by-one (disp measured from func_rva+13 instead
  of +12; call sits at offset 7). Fixed in make_test_b/c/d; verifier rewritten to be
  non-vacuous (12 calls verified: every e8 target = a function entry, every ff15
  target = IAT 0x2048).
- B-2: UNWIND_CODEs now canonical — byte0=CodeOffset, byte1=(OpInfo<<4)|Op, array in
  REVERSE prolog execution order, offsets = end-of-instruction boundaries (push=1,
  alloc=5). Byte-proof in built PE: `09 05 02 00 | 05 52 | 01 30`.
- B-3: all frames `push rbx; sub rsp,0x30` (ms_abi aligned, ALLOC_SMALL opinfo=5);
  no movdqa SIGSEGV anywhere.
- Exit-code contract: after ContinueExecution resume the RaiseException stub's EAX(1)
  is the EP return -> loader exit 1 (trace-evidenced "Entry point returned 1");
  harness rows assert ec=1 as SUCCESS.
- Hashes: builder `64735e05…`, harness `19cc822a…`, test_a/d `048f6d27…`,
  test_b `8fd829f7…`, test_c `17ca43ea…`, loader binary `40646f4b…` (unchanged).

## C-1 scope
- make_test_c: func_C handler -> ContinueSearch (mov eax,1; ret); func_B handler ->
  ContinueExecution (xor eax,eax; ret). Expected: TWO HANDLER_CALL events
  (disposition=1 at frame 0, then disposition=0 at frame 1) + clean resume, ec=1.
- Evolve the test_c row to assert both dispositions.
- Builder-only. If a loader defect surfaces in the ContinueSearch-continue path,
  register BUG-032 with a trace; do NOT touch loader.c in this task.
- Guardrail: bash scripts/g1_accept.sh must stay OK; test_a/b/d rows stay green.

## Then: TASK-TEST-004-D1 (see backlog)
test_a.exe == test_d.exe (sha256 048f6d27…) — differentiate test_d (prefer a deeper
no-handler chain: handler discovered at frame 3).
