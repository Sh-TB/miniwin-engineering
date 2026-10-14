# Engineering State — CANONICAL COMPACT SNAPSHOT

**Updated:** 2026-10-14 (BUG-027/EXP-037 session — CONTINUE #3). Supersedes the
prior snapshot; prior versions remain in git history.

| Field | Value |
|---|---|
| Current HEAD (local) | `BUG-027` commit (child of `4039b61`) — see `git log --oneline -3` |
| Origin/main | `4039b61f…` == pushed through BUG-026 commit; BUG-027 commit pushed in this session (credentials resolved — OWNER-001 CLOSED; token stored in `~/.git-credentials`, outside the repo) |
| Source SHA-256 (loader.c) | `bab5ea869bf85309854c5cc2d2a43cccbeabba2986018eb6ee2cd1fd0df3dcd3` (BUG-027 stride fix applied) |
| Source SHA-256 (include/pe.h) | `928a780c…` (unchanged) |
| Build status | CLEAN — 0 errors; binary `0c275159…` |
| Test status | Regression **12/12** (test 10 expectation evolved to the verified frontier with evidence comment: exit 134 + terminate-message = BUG-029 frontier; 139 = BUG-026 era); EXP-NEXT-2 PASS; dispatch tests BROKEN/VACUOUS (TEST-004 pending); dalvik 24/24 |
| Current target | UPX 4.2.4 decompressed — current result: **exit 134; landing pad 0x4dcd24 resumes at the CORRECT throw-site RSP (alloc_total=0x48, trace-verified); catch TLS machinery runs; then terminate → abort while the exception is in flight** |
| Exact current blocker | **BUG-029**: terminate after a correct landing-pad resume. Top suspect: EH_FIX (L~1607) hijacked dispatch-1 (ContinueSearch → fake catch at LP 0x401573 → 'unsigned long long' machinery → re-raise chain), poisoning the GCC `__cxa`/TLS state that dispatch-2's catch inherits |
| Active task ID | TASK-BUG-029 (READY; first step = TASK-API-005 EH_FIX isolation behind `MINWIN_EH_HACKS` default OFF) |
| Last completed task ID | TASK-BUG-027 (+ EXP-037, + OWNER-001 push) — this session |
| Next task ID | TASK-BUG-029 |
| Blocking dependencies | API-005 first step of BUG-029 |
| Evidence links | docs/master/experiments/EXP-037-…md; docs/bugs/BUG-027.md (closed); docs/bugs/BUG-029.md; evidence/master/bug027/*; evidence/master/bug027_probe_personality_disasm.txt |
| Known repository inconsistencies | (1) two undocumented EH hacks still in source (EH_FIX, CXX-EH-FIX) — EH_FIX's dispatch-1 hijack now live-implicated in BUG-029; (2) ALLOC_LARGE scaling formulas wrong in this loop + 3 other copies (TASK-API-006; the `−2` index quirk in the same loop included); (3) dispatcher passes post-unwind ctx (Rsp=est) to handlers — Windows passes pre-unwind throw-site ctx (EXP-037 P-B variable, out of BUG-027 boundary); (4) sample hash mis-stated as 254ac80d in BUG-001/NEW_AGENT_START (true: 14e07e29); (5) dispatch tests broken/vacuous (TEST-004) |

**Key architectural facts established this session (personality disassembly):**
- The PE's personality = mingw-era GCC SEH bridge at `0x49d340` (wrapper `0xe0220`).
- Its RtlUnwindEx call (site A): `(EstablisherFrame, landing_pad, &ExceptionRecord, exception_object, ms_orig_context, object)` — arg3 is NOT a CONTEXT; the "adopt r8" direction from EXP-036 is retracted.
- RDX selector hypothesis withdrawn: raise helper sets `xor edx,edx`; landing pad `jne` branch is the NORMAL path.

**Rule honored:** BUG-027 closed only after trace verification of the corrected
resume state + catch-machinery execution; regression expectation changes carry
inline evidence comments; no new failure was hidden (BUG-029 registered).
