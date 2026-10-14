# SESSION HANDOFF — 2026-10-15 (CONTINUE #4: API-005 + BUG-030 fixed; BUG-031 registered)

## What this session did
- **Step A sync:** origin/main == HEAD == `b843974` verified on GitHub (`git ls-remote`); baseline
  reality check passed (binary `0c275159…` byte-identical, regression 12/12, hashes match state.md).
- **TASK-API-005 (ADR-010):** EH_FIX (dispatcher LSDA override) and CXX-EH-FIX (load_pe byte patch)
  flag-guarded behind `MINWIN_EH_HACKS`, default OFF; nothing deleted. `MINWIN_EH_HACKS=1` restores
  the legacy BUG-029-era behavior byte-identically (verified: 'unsigned long long' message + 4
  EH_FIX trace lines). Pre-task checkpoint: `checkpoint_pre_task-bug029_api005_20261015.zip`.
- **Live re-run (hacks OFF):** EXP-037 P-A core prediction confirmed — single clean dispatch, no
  re-raise chain, no fake machinery. **The PE now reaches its own dt_check.cpp assertion**
  (`Assertion failed: cxx_exception_handling_works … : 362`): UPX's own C++ EH self-test executes.
- **Ground truth (decisive):** reverse-engineered the full GCC catch protocol — catch owner B
  (0x4032c0) + LP 0x40331c consumes the **catch selector in RDX**; GCC core 0x4AFDB0 computes it
  (stashed at obj−0x24), phase-2 blob[1]=sel → **ER 4-param protocol `{obj, est, LP, selector}`**
  at site A; bridge `flags&0x20` branch independently confirms `ctx->Rdx = param[3]`; the
  personality/core read OUR dc layout (LSDA=[dc+0x38], func_start=[dc+0x10]+[dc+0x08]).
- **BUG-030 found & fixed:** mw_RtlUnwindEx did not deliver the selector. Fix = fingerprint-guarded
  `CTX.Rdx = ER.param[3]` (`nparams==4 && param[1]==tgt_frame && param[2]==tgt_ip`) — protocol-driven,
  no hardcoded addresses. Trace-verified `sel=1 → CTX.Rdx=1`; selector branch then taken.
- **Frontier moved again → BUG-031 registered:** the catch's protocol-state check (`check_var==0x17`)
  fails because the chain 1→5→23 requires Frame[3]'s cleanup LP 0x4015A5 to run during the unwind;
  mw_RtlUnwindEx skips intermediate cleanups (documented simplification). Full protocol map for the
  unwind phase (0x21474343 / 0x120474343 re-raise codes + the C-stack-walk caution) recorded in
  EXP-038 §5.1; predictions P-C/P-D/P-E in §7.
- **Regression:** test-10 expectation evolved to the BUG-031 frontier (exit 134 + UPX dt_check
  assertion + terminate-without-active) with an inline evidence comment; suite **12/12**.

## Commits
- This session: `API-005+BUG-030: …` (loader.c flag guards + selector fix, EXP-038, BUG-029/030/031
  docs, ADR-010, regression expectation evolution, .agent updates, evidence).
- Chain: `39181aa` → `08d94ac` → `4039b61` (BUG-026) → `b843974` (BUG-027) → this commit (HEAD).
- **Pushed: YES** (credentials in ~/.git-credentials, outside repo; GitHub-verified).

## Current blocker
BUG-031 (see `.agent/CURRENT_TASK.md`): intermediate-frame cleanup execution during unwind —
design decision (a) dispatcher-honored resume commands vs (b) mw_RtlUnwindEx direct cleanup walk.

## Next
Next task ID: **TASK-BUG-031**. Exact next command from the owner: **CONTINUE**.

## Unverified / unknown (honest list)
- Whether design (a) or (b) is correct for BUG-031 — untested; the C-stack-walk problem is
  predicted, not yet observed.
- Whether the TLS chain state (`cmp [rax],1` after 0x4dfce0) passes once the cleanup runs (P-D
  variable) — untested.
- Whether the dc layout we build matches the OS canonical DISPATCHER_CONTEXT (generalization
  audit pending — the PE's personality reads OUR layout, which is what matters for this target).
- ALLOC_LARGE scaling formulas (TASK-API-006) — latent, unexercised by this path.
- Dispatch tests still broken/vacuous (TEST-004).
