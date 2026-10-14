# CURRENT TASK

**Task ID:** TASK-BUG-029
**Title:** terminate→abort after correct landing-pad resume (catch machinery)
**Status:** READY (evidence complete; first step defined)
**Priority:** P0

## One-line description
After BUG-027, the landing pad 0x4dcd24 resumes at the correct throw-site RSP
and the catch machinery runs (TLS calls visible), then terminate → abort with
`terminate called after throwing an instance of 'unsigned long long'` and NO
new RtlDispatchException in the trace — terminate fires while the original
exception is still in flight.

## Why this is the task (evidence)
- EXP-037 (trace-verified): `alloc_total=0x48`, `post_prologue_RSP = establisher−0x48`
  = Frame[3]-entry RSP; resume `RIP=0x4dcd24 RSP=0x…5c70 RAX=0x303a320`; then
  `TlsGetValue/SetLastError/GetLastError` sequence → `abort()`
  (`evidence/master/bug027/upx_trace.json`).
- The 'unsigned long long' machinery was built during **dispatch-1**, whose
  personality legitimately returned ContinueSearch but was then **hijacked by
  EH_FIX** (loader.c L~1607-1644: "Override: personality failed, catching at
  LP=0x401573") — the fake catch built the message machinery and re-raised via
  the helper chain (dispatch-2 from RIP=0x49d5b1), leaving GCC `__cxa`/TLS
  state poisoned before dispatch-2's catch ran.

## Execution notes for the next agent (ordered steps)
1. **First step — TASK-API-005:** wrap EH_FIX (L~1607-1644) and CXX-EH-FIX
   (L~3263-3287) in `MINWIN_EH_HACKS` env flag, default OFF. Do NOT delete them
   (history preservation). Document in ADR + API-005 backlog entry.
2. Rebuild, re-run regression (12/12 with the current test-10 expectation —
   if the frontier moves again, evolve the expectation WITH an evidence comment
   like test 10 has now).
3. Re-run UPX with trace; compare against EXP-037 §7 predictions:
   - P-A (expected): dispatch-1 → personality → GCC core → site A → RtlUnwindEx
     → landing pad 0x401573 (dispatch-1's own catch) — no 'unsigned long long'
     machinery, no re-raise chain, no terminate.
   - P-B (if terminate persists): pass the PRE-unwind context (throw-site RSP,
     Rip−1 rule preserved) to the handler instead of the post-unwind one —
     a dispatcher change, now in-scope for this task.
4. If a new failure point appears, register it as a new BUG with a full trace;
   if UPX prints its version — G1 achieved, run the full release-criteria check.

## Verified facts you may rely on (this session)
- Push works: OWNER-001 closed; `~/.git-credentials` holds the token (outside the repo).
- The personality disassembly ground truth (EXP-037 §2): site A call shape
  `(EstablisherFrame, landing_pad, &ExceptionRecord, object, ms_orig_context, object)`;
  Rdx=0 at raise (helper `xor edx,edx`); landing-pad `jne` branch is NORMAL.
- Regression suite expects exit 134 + "terminate called after throwing" NOW;
  if the frontier moves, update the expectation with an evidence comment.

## Boundaries
- EH_FIX/CXX-EH-FIX: flag-guard ONLY (no deletion, no semantic edits).
- Do not touch the RaiseException stub (BUG-026 scope) or the RtlUnwindEx
  resume synthesis (BUG-027 scope) unless a trace implicates them.
