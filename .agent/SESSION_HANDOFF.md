# SESSION HANDOFF — 2026-10-14 (CONTINUE #3: BUG-027 fixed + pushed; BUG-029 registered)

## What this session did
- **OWNER-001 CLOSED:** GitHub token verified (user `Sh-TB`, push access), credential store
  configured OUTSIDE the repo (`~/.git-credentials`). Pushed both pending commits:
  `39181aa..4039b61 main→main`, GitHub-confirmed (`origin/main == 4039b61f…`).
- **Protocol steps A–C:** synced at `4039b61` (clean); pre-edit baseline re-verified
  (regression 12/12; loader.c `e1caa5d7…`, binary `c8472d04…`).
- **Ground-truth reverse engineering (decisive):** disassembled the real personality
  (wrapper `0xe0220` → core `0x49d340`, mingw GCC SEH bridge, UPX "CCt"-protocol aware).
  Established: (a) RtlUnwindEx call site A passes `r8=&ExceptionRecord` (NOT a CONTEXT) —
  EXP-036's "adopt r8/OriginalContext" direction RETRACTED; (b) raise helper does
  `xor edx,edx` — carried Rdx=0 is normal, landing-pad `jne` branch is the normal path —
  RDX-selector hypothesis WITHDRAWN.
- **Step D (fix):** BUG-027's true defect = 4-byte slot STRIDE in the unwind-code loop
  (`uint16_t* tgt_codes` + `tgt_slot*2` ⇒ byte offset slot×4 → read every other slot +
  garbage → coincidental 0x40). Fix = one declaration (`uint8_t* tgt_codes = tgt_ui + 4;`).
  loader.c → `bab5ea86…`, binary → `0c275159…`.
- **Step E (verify):** trace-verified `alloc_total=0x48`, `post_prologue_RSP=est−0x48`
  (= Frame[3]-entry RSP — exactly the throw-site RSP); landing pad 0x4dcd24 resumed;
  catch TLS machinery ran; then terminate → abort (exit 134). Regression: test 10's
  139-expectation was the BUG-026-era frontier — evolved to the verified frontier
  (134 + terminate-message assertion) with an inline evidence comment; suite **12/12**.
- **Registered BUG-029** (terminate after correct landing-pad resume; candidates ordered:
  EH_FIX dispatch-1 hijack → GCC-core ctx-RSP dependency → site B/C audit).

## Commits
- This session: `BUG-027: …` (loader.c fix + EXP-037 + BUG-027 closure + BUG-029 +
  regression expectation evolution + .agent updates + evidence).
- Chain: `39181aa` → `08d94ac` → `4039b61` (BUG-026) → BUG-027 commit (HEAD).
- **Pushed: YES** (credentials now configured; origin/main will equal HEAD after this push).

## Current blocker
BUG-029 (see `.agent/CURRENT_TASK.md`): terminate during catch processing after a
correct landing-pad resume. First step = TASK-API-005 (flag-guard EH hacks, default OFF).

## Next
Next task ID: **TASK-BUG-029** (starting with TASK-API-005). Exact next command from the
owner: **CONTINUE**.

## Unverified / unknown (honest list)
- Whether EH_FIX removal from the dispatch-1 path lets the GCC core reach site A on
  dispatch-1 (P-A) — the core returned non-INSTALL with our inputs; untested without the hack.
- Dispatcher handler-call context semantics (post-unwind Rsp=est vs Windows' pre-unwind
  throw-site RSP) — P-B variable, untested.
- Personality site B/C (0x21474343 protocol) behavior under our loader — unexercised.
- ALLOC_LARGE scaling formulas in this loop + 3 other copies (TASK-API-006) — latent.
- Dispatch tests still broken/vacuous (TEST-004).
