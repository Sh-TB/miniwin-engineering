# SESSION HANDOFF — 2026-10-15 (CONTINUE #5: BUG-031 fixed — **ACCEPTANCE G1 MET**)

## What this session did
- **Step A sync:** origin/main == HEAD == 24bb742 verified; WIP BUG-031 (365-line uncommitted
  ADR-011 implementation) found from the interrupted session — NO docs/evidence existed for it.
- **Reproduce-first:** WIP as found = exit 135/139 (crash_handler self-fault on poisoned RBP;
  then SIGSEGV at PE 0x49d34c with ER=0xAAAA...); baseline 24bb742 re-verified = 134 (3/3).
- **Root cause 1 (compiled-code proof):** call_handler_aligned's new below-RSP poison block
  clobbered RDI/RCX/RAX while GCC had legally assigned INPUT operands there (ER←RAX at the
  walk site; est←RDI + ctx←RAX at the dispatcher site) ⇒ personality called with a poisoned
  ExceptionRecord. Fix: poison REMOVED + constraints "=&a" + rdi/rbp clobbers.
- **Root cause 2 (E1–E5 bisection, EXP-039 §4):** with executing deltas neutralized the flow
  STILL diverged per recompile — seh_build_context's CPU-state volatile reads are
  compile-dependent garbage (the interrupted session's own diagnosis) that flips phase-1
  INSTALL vs ContinueSearch; E4/E5 demonstrated the 727-event 'unsigned long long' frontier
  (old BUG-029-era) purely from code-shape changes.
- **Root cause 3:** the PE's GCC core depends on rax/r10/r11 at the personality-call
  boundary (ms_abi-undefined) — call_handler_aligned now pins them to the stub's true
  raise-site captures (g_cap_rax/r10/r11) before the call. Two compile traps documented
  (%rip must be %%rip; register budget with "=&a"+clobbers).
- **RESULT — G1 MET:** UPX prints the full --version banner (upx 4.2.4 + 5 libraries +
  5 copyrights + no-warranty line), **exit 0, 5/5 deterministic, no crash dumps**. Trace
  shows the full chain: first walk (EH_UNWINDING) → resume-cmd 0x21474343 (lp=0x4015a5,
  alloc=0x48) → A's cleanup → continuation unwind → 4-param selector delivery (sel=1 →
  CTX.Rdx) → catch LP 0x40331c resumed (check_var 1→5→23). EXP-038 P-C/P-D/P-E confirmed.
- Regression **12/12** (test-10 expectation evolved to the SUCCESS state with full history
  comment); EXP-NEXT-2 PASS; MINWIN_EH_HACKS=1 legacy compat exact (134 + same message).
- **Docs:** EXP-039 (full report incl. G1 scorecard §8, predictions P-F/P-G/P-H §9, honesty
  notes §10), BUG-031 CLOSED, ADR-011, RELEASE_CRITERIA G1/G2 checkboxes updated.
- **Backlog expanded (+25 tasks, 55 total):** post-G1 hardening (BUG-033 continuation-walk
  generalization, G1-001 pin script, TEST-004/005 promotion, TEST-002/009 decoder fuzzing),
  generalization enablers (API-017..024, RESEARCH-01/02), hygiene/docs/infra (DOC-003/004,
  INFRA-001/002, OWNER-003).

## Commits
- This session: "BUG-031: …" (loader.c ADR-011 completion + asm fixes, test-10 evolution,
  EXP-039, BUG-031 closure, ADR-011, RELEASE_CRITERIA, evidence/master/bug031/, .agent updates).
- Chain: 39181aa → 08d94ac → 4039b61 (BUG-026) → b843974 (BUG-027) → 24bb742 (API-005+BUG-030) → this commit (HEAD).
- **Push: YES** (credentials in ~/.git-credentials, outside repo; GitHub-verified).

## Current state
**G1 acceptance MET** (RELEASE_CRITERIA G1 checked 2026-10-15). Next frontier is guarding
the success: TEST-004 (dispatch tests) → TEST-005 (synthetic multi-frame EH set) — see
CURRENT_TASK.md.

## Next
Next task ID: **TASK-TEST-004**. Exact next command from the owner: **CONTINUE**.

## Unverified / unknown (honest list)
- Continuation walk started with RIP=0 in the G1 run (target resume still correct) —
  generalization = TASK-BUG-033, untested beyond this target.
- rax/r10/r11 pinning values are observed, not Windows-spec'd (EXP-039 §10) — synthetic
  suite must confirm no target dependence.
- ALLOC decode copies in mw_RtlUnwindEx/mw_RtlVirtualUnwind still carry the API-006
  deviation (latent; seh_total_prologue_alloc is the corrected reference).
- Dispatch tests still broken/vacuous (TEST-004 = current task).
