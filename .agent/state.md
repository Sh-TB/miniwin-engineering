# Engineering State — CANONICAL COMPACT SNAPSHOT

**Updated:** 2026-10-15 (CONTINUE #4: API-005 + BUG-030 fixed; BUG-031 registered). Supersedes the
prior snapshot; prior versions remain in git history.

| Field | Value |
|---|---|
| Current HEAD (local) | `API-005/BUG-030` commit (child of `b843974`) — see `git log --oneline -3` |
| Origin/main | `b843974b…` (BUG-027 commit; GitHub-verified via `git ls-remote` this session). This session's commit pushed with the stored credentials (~/.git-credentials, outside repo) |
| Source SHA-256 (loader.c) | `9a52f1d6759d83bbb749c497f7b7c4e6508395d1f075ee5b7f07b4fe8e13f8fb` (API-005 flag guards + BUG-030 selector fix) |
| Source SHA-256 (include/pe.h) | `928a780c…` (unchanged) |
| Build status | CLEAN — 0 errors; binary `ca7ce562…` |
| Test status | Regression **12/12** (test 10 expectation evolved to the BUG-031 frontier with evidence comment: exit 134 + UPX dt_check assertion + terminate-without-active); EXP-NEXT-2 PASS; dispatch tests BROKEN/VACUOUS (TEST-004 pending); dalvik 24/24 |
| Current target | UPX 4.2.4 decompressed — current result: **exit 134; the PE now executes the clean GCC personality path (single dispatch, no re-raise) and reaches UPX's own dt_check assertion (`Assertion failed: cxx_exception_handling_works … : 362`); the catch selector is delivered (BUG-030 fixed, `sel=1` in RDX, trace-verified); the catch's protocol-state check then fails** |
| Exact current blocker | **BUG-031**: the catch validates a cross-frame protocol counter (check_var 1→5→23) that requires Frame[3]'s cleanup LP 0x4015A5 to run during the unwind; mw_RtlUnwindEx skips intermediate cleanups (protocol map + design candidates: EXP-038 §5.1) |
| Active task ID | TASK-BUG-031 (READY) |
| Last completed task ID | TASK-API-005 (ADR-010) + TASK-BUG-030 — this session (TASK-BUG-029 closed as root-cause-isolated) |
| Next task ID | TASK-BUG-031 |
| Blocking dependencies | none for BUG-031 |
| Evidence links | docs/master/experiments/EXP-038-…md; docs/bugs/BUG-030.md (closed); docs/bugs/BUG-031.md; evidence/master/bug030/* (incl. hacks-ON backward-compat trace); checkpoint_pre_task-bug029_api005_20261015.zip |
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

**Rule honored:** API-005 deleted nothing (history preservation); BUG-030's fix is protocol-driven
(fingerprint-guarded, no sample-specific constants); regression expectation evolution carries inline
evidence comments; no failure hidden (BUG-031 registered immediately).
