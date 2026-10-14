# Engineering State — CANONICAL COMPACT SNAPSHOT

**Updated:** 2026-10-15 (CONTINUE #5: **BUG-031 fixed — ACCEPTANCE G1 MET**; EXP-039; ADR-011). Supersedes the
prior snapshot; prior versions remain in git history.

| Field | Value |
|---|---|
| Current HEAD (local) | `BUG-031/G1` commit (child of `24bb742`) — see `git log --oneline -3` |
| Origin/main | `24bb742a…` (API-005/BUG-030 commit; push of the G1 commit happens this session; credentials in ~/.git-credentials, outside repo) |
| Source SHA-256 (loader.c) | see `evidence/master/bug031/hashes.txt` (BUG-031: ADR-011 + raise-site register pinning + asm constraint fix) |
| Source SHA-256 (include/pe.h) | `928a780c…` (unchanged) |
| Build status | CLEAN — 0 errors; binary `e0bdbdcf…` |
| Test status | Regression **12/12** (test 10 asserts the SUCCESS state: exit 0 + UPX banner + no crash); EXP-NEXT-2 PASS; MINWIN_EH_HACKS=1 legacy compat verified (134 + 'unsigned long long', byte-identical legacy); dispatch tests BROKEN/VACUOUS (TEST-004 pending); dalvik 24/24 |
| Current target | UPX 4.2.4 decompressed — **G1 MET: full --version banner printed, exit 0, 5/5 deterministic, no crash dumps** |
| Exact current blocker | none for G1. Next frontier = generalization hardening (TEST-004/005 synthetic EH suite) + continuation-walk generalization (BUG-033 note in EXP-039 §10) |
| Active task ID | none (G1 milestone complete) |
| Last completed task ID | TASK-BUG-031 (CLOSED TESTED) — this session |
| Next task ID | TASK-TEST-004 (fix dispatch tests) — P1 enabler for the synthetic EH suite (TEST-005) that must now guard G1 |
| Blocking dependencies | TEST-005 depends on TEST-004 |
| Evidence links | docs/master/experiments/EXP-039-…md; docs/bugs/BUG-031.md (closed); evidence/master/bug031/* (trace 943 events, stdout, hashes, regression log); checkpoint_pre_task-bug031_20261015.zip |
| Known repository inconsistencies | (1) ALLOC_LARGE scaling formulas wrong in 4 copies + windows_abi doc (TASK-API-006; ALLOC_SMALL already corrected by BUG-027); (2) dispatcher passes post-unwind ctx (Rsp=est) to handlers — Windows passes pre-unwind throw-site ctx (P-B variable, not needed so far); (3) dc-layout contract: the PE's personality/core read OUR dc layout (ImageBase@0x08/FunctionEntry@0x10/ContextRecord@0x28/HandlerData@0x38) — generalization audit vs OS canonical DISPATCHER_CONTEXT pending (BUG-031 risk note); (4) sample hash mis-stated as 254ac80d in BUG-001/NEW_AGENT_START (true: 14e07e29); (5) dispatch tests broken/vacuous (TEST-004) |

**Key architectural facts established this session (GCC catch protocol, EXP-038 §4):**
- GCC bridge site A: `RtlUnwindEx(est, LP, &ER, object, ctx, …)` with the ER upgraded to **4 params
  `{object, est, landing_pad, selector}`**; the selector is computed by the core (0x4AFDB0), stashed at
  `obj−0x24`, and reaches the landing pad in **RDX** (`mov rbx,rdx; sub rbx,1; jne fail` at 0x40331c).
- The personality/core read our DISPATCHER_CONTEXT layout (LSDA via [dc+0x38], func_start via
  [dc+0x10]+[dc+0x08]) — our dc construction is the live contract for this PE.
- Unwind-phase protocol (for BUG-031): intermediate INSTALL → re-raise `0x21474343` (4 params,
  flags=1); cleanup LPs end with `0x49d5c0` → continue-raise `0x120474343` (low dword = 0x20474343)
  with `{?, est, LP, sel}` — the chain resumes each segment until the target catch LP.

**Rule honored:** the BUG-031 fix is protocol-driven (fingerprint: code+nparams+NONCONTINUABLE+LP-inside-image — no sample-specific constants); regression expectation evolution carries inline evidence comments; the whole E1–E5 bisection is documented (EXP-039 §4) including the compile-dependent-behavior finding.

**Key facts established this session (EXP-039):**
- The GCC bridge's unwind-phase re-raise (`0x21474343`, 4-param ER) is a RESUME COMMAND, not an exception: the dispatcher synthesizes the landing-pad resume (Rip=param[2], Rax=param[0], Rdx=param[3], Rsp=param[1]−alloc(param[2])) — ADR-011 design (a).
- The cleanup LP's continue helper (0x49d5c0) calls RtlUnwindEx DIRECTLY with the OriginalContext; the dispatcher's setjmp frame is gone, so this path walks remaining cleanups from the captured context and resumes the target LP without longjmp.
- CONTEXT volatile GPRs must come from the true raise-site captures (stub `g_cap_*`), and `call_handler_aligned` must pin rax/r10/r11 to them: with CPU-state garbage, phase-1 INSTALL vs ContinueSearch flips per recompile (E4/E5) and the GCC core can crash (0x4af733). Inline-asm constraints: `"=&a"` + `rdi`/`rbp` clobbers; the below-RSP poison block is REMOVED (early-clobber proof: ER←RAX).
- `%rip` in inline-asm templates must be written `%%rip` (single % = operand modifier).
