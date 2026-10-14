# CURRENT TASK

**Task ID:** TASK-BUG-031
**Title:** Execute intermediate-frame cleanups during unwind (catch protocol-state chain)
**Status:** READY
**Priority:** P0

## One-line description
With BUG-030 fixed (selector=1 in RDX, trace-verified), the landing pad 0x40331c takes the
selector branch but fails its protocol-state validation: `check_var` must be 23 (0x17) but is 7,
because the chain 1 →(A's cleanup) 5 →(B's catch LP) 23 requires Frame[3]'s cleanup LP 0x4015A5
to run during the unwind — and `mw_RtlUnwindEx` skips intermediate-frame cleanups entirely.

## Why this is the task (evidence)
- EXP-038 §4/§5: full ground truth of the catch (B 0x4032c0, LP 0x40331c disassembly), the GCC
  core 0x4AFDB0, the 4-param ER protocol, and the post-fix trace (`evidence/master/bug030/upx_trace.json`).
- A's cleanup LP 0x4015A5 (Frame[3] LSDA cs_lp=0x35 → 0x401570+0x35) runs `0x401550(&A_struct)` ⇒
  `check_var = 1 + 4*1 = 5`, then `0x49d5c0` (continue-unwind). B's LP then computes 3+4*5=23 ✓.
- Post-fix trace: selector branch taken, `0x4dfce0` runs, then `0x4dfef0` (exception discard,
  `free(obj−0x60)`) → UPX assertion → terminate → abort (134).

## Execution notes for the next agent (ordered steps)
1. Baseline per protocol (clean build, 12/12, hashes from `evidence/master/bug030/hashes.txt`).
2. Decide the design (EXP-038 §5.1): (a) dispatcher honors `{0x21474343, 0x120474343}` 4-param
   records as resume commands (synthesized resume: Rip=param[2], Rax=obj, Rdx=param[3],
   Rsp=param[1]−alloc(param[2])); or (b) mw_RtlUnwindEx walks intermediate cleanups directly
   (personality calls with EH_UNWINDING flags) before resuming the target LP. Caution: re-raises
   through the naked stub re-enter the walk from the C dispatcher stack (no PE RUNTIME_FUNCTION
   for our C frames) — EXP-038 §5.1; P-E tests design (a).
3. Implement minimally; do NOT touch the naked stub or BUG-026/027/030 resume logic unless a
   trace implicates them.
4. Re-run UPX (hacks OFF) with trace; verify EXP-038 §7 P-C (check_var converges to 23; the
   `[rax]==1` state passes) — or register P-D (TLS state machine) as the next isolation.
5. If a new failure point appears → BUG-032 with full trace; if UPX prints its version → G1,
   run the full release-criteria check.

## Verified facts you may rely on (this session)
- The 4-param ER protocol `{obj, est, LP, selector}` is live-confirmed (fingerprint matched in
  the trace; `sel=1 → CTX.Rdx=1`).
- The personality/core read our dc layout: LSDA=[dc+0x38], func_start=[dc+0x10]+[dc+0x08],
  blob[0]=ctx->Rsp via [dc+0x28]+0x98 (EXP-038 §4.2).
- `MINWIN_EH_HACKS=1` restores the legacy hacks exactly (backward-compat proven); default OFF.
- Regression test 10 now asserts the BUG-031 frontier (134 + dt_check assertion +
  terminate-without-active) with an inline evidence comment.

## Boundaries
- EH_FIX/CXX-EH-FIX stay flag-guarded OFF (ADR-010); do not re-enable them to "fix" this.
- No sample-specific constants: generalize via the PE's own protocol data (fingerprint guards).
- ALLOC_LARGE scaling (TASK-API-006) remains out of scope unless a trace implicates it.
