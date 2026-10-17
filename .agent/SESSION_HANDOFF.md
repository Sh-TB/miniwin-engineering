# SESSION HANDOFF — 2026-10-15 (CONTINUE #6: appcompat sweep — 7/9 PASS, 5 bugs fixed, TLS implemented)

## What this session did
- **User directive:** continue + test real programs (small + two big) + grow the task count.
- Built the appcompat infrastructure: llvm-mingw 20261006-msvcrt (clang 23.1.3) toolchain
  pinned at /home/z/my-project/tools-mingw (NOT in repo); 9 deterministic self-verifying
  programs in tests/appcompat/ (t01..t07 C, t08 C++ STL/EH, t09 iostream probe);
  run_sweep.sh harness with PASS/VERIFY_FAIL/CRASH/TIMEOUT classification; pefile import audits.
- Sweep result by stage (each stage = one root-caused fix + full gates):
  0/9 -> wave-1 APIs (atexit/_exit/setvbuf/GetCPInfo/_XcptFilter) -> BUG-032 (iob FILE*
  window, map_crt_stream) -> BUG-033 (printf ms->SysV fmt-aware repack) -> BUG-034
  (CRITICAL_SECTION 40-byte contract, dtoa freelist corruption) -> BUG-035 (qsort_r ms_abi
  bridge) -> BUG-036 (loader-owned jmp_buf: full ms reg file + rsp alignment) -> **7/9 PASS**
  with wave-2 APIs (26 new: SRW/CV/Fls/InitOnce/RtlRestoreContext/wide CRT/aligned) and
  **TASK-TLS-001** (native TLS: gs:0x58 ThreadLocalStoragePointer, static slot 0, callbacks).
- t08 frontier = BUG-037 (OPEN): C++ EH machinery runs (handler called, dispositions OK,
  0 unresolved imports) and dies decoding CRT-startup UNWIND_INFO (R9=0x27 raw offset as
  address). t09 frontier = 44 missing APIs (TASK-API-009 wave 3 list recorded).
- Gates held after EVERY change: regression 12/12, UPX --version exit 0 (G1).
- Docs: EXP-040 report; BUG-032..037 docs; backlog +31 tasks; API table 162 -> 188;
  state.md, CURRENT_TASK.md (TASK-BUG-037), this handoff, evidence
  evidence/master/appcompat/run-20261015_053015/ + run-*.csv summaries.

## Next session exact steps
1. `CONTINUE` -> TASK-BUG-037 per CURRENT_TASK.md (reference decoder + golden vector first).
2. After BUG-037: t08 3x determinism, then TASK-BUG-038 (G2 C++ EH acceptance sample).
3. In parallel if time: TASK-API-009 (wave 3, t09) and TASK-TEST-013 (ABI boundary unit tests).
4. Push state (credentials in ~/.git-credentials outside repo; OWNER-001 closed).

---
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


---

## CONTINUE #5 addendum (TEST-004 slice 1, same session)
- TASK-G1-001 CLOSED: `scripts/g1_accept.sh` — one-command G1 pin (build + banner +
  no-crash + 12/12 + evidence dir). Ran OK 3x (evidence/master/g1/20261014_*).
- TASK-API-002 CLOSED: `mw_trace_shutdown_flush()` — trace JSON self-closes on every
  exit path (ExitProcess/abort direct; exit() via atexit); stdout/stderr flushed.
- TASK-API-006 CLOSED: ALLOC_LARGE spec rule in all 4 loader copies (info==0 ⇒ ×8;
  info==1 ⇒ uint32 no-shift) + windows_abi doc correction; seh_total_prologue_alloc
  documented as the reference decoder. G1 re-pinned after each change.
- TASK-TEST-004 slice 1 CLOSED: `test_dispatch_v2.sh` (real-loader, non-vacuous);
  builder import layout fixed (canonical PE32+ thunks, ILT/IAT separated, hint/name
  RVA, OUTPUT_DIR, syntax); **loader: IAT pages made writable during the import pass
  with per-section restore** (OS-loader semantics; unblocks synthetic PEs whose IAT
  lives in read-only .rdata; UPX .idata unaffected).
- Honest frontier: test_a reaches dispatch then faults on the builder's ms_abi frame
  misalignment (movdqa, 0x200fdff); test_b/c/d never raise (call-rel32 fixups) —
  builder defects B-1/B-2/B-3 documented = slice 2. Suite green on current contracts.
- Commits: 954d1f0 (BUG-031/G1) → 4a397e4 (G1-001+API-002+API-006) → 52c0f8d
  (TEST-004 slice 1) → this commit. All pushed; origin verified.
- Next: TASK-TEST-004 slice 2 (B-1/B-2/B-3) — see CURRENT_TASK.md.

---

## CONTINUE #7 (2026-10-15) — BUG-037 CLOSED, UCRT wave, sweep 10/12

**User directive honored:** tested programs for real execution progress
(several small + big), expanded the backlog (now 117 tasks), pushed.

### What happened
1. **Push verified:** origin/main == local 709b9ee at session start (the
   "ahead 1" was a stale tracking ref). Token re-validated (Sh-TB).
2. **BUG-037 root-caused and fixed (CLOSED TESTED):** three stacked spec
   defects, all ours: (a) deprecated UWOP numbering (6/7/8) vs the current
   spec 6=EPILOG/7=SPARE/8=XMM128/9=XMMFAR/10=MACHFRAME (MS doc + LLVM 19
   Win64EH.h + .NET + ReactOS cross-confirmed); (b) CountOfCodes counts
   SLOTS — five decode loops bounded on the code index and consumed
   padding/handler bytes as phantom codes (standalone harness reproduced
   alloc_total=0xff0191 vs correct 0x188); (c) frame byte nibbles swapped
   (RBP/off8 misread as R8/off5). Plus: SET_FPREG re-anchors RSP from the
   LIVE FP (ReactOS), skip-path advances full slot size (new shared
   seh_unwind_op_slots helper), mw_RtlUnwindEx FP-frame body-RSP formula,
   missing SAVE_NONVOL advance + ALLOC_LARGE u32 rule in the last deviating
   copy. New scripts: bug037_ref_decode.py, bug037_scan_uis.py,
   bug037_lsda_parse.py, bug037_alloc_harness.c.
3. **t08 progress measured:** interpreter no longer faults; walk completes;
   alloc_total=0x188; FP body RSP correct; resume attempted. NEW frontier =
   **BUG-038** (registered, next task): llvm-mingw's libunwind passes
   ControlPc as the RtlUnwindEx TargetIp placeholder; the real LP 0x1c9d
   (catch block, proven by full LSDA decode — no entry has 0x813) installs
   only at target-frame delivery via the barrier cache.
4. **Toolchain re-downloaded** (environment reset had wiped tools/):
   llvm-mingw 20240917 ucrt, clang 19.1.0.
5. **Three NEW test programs authored, built, and driven to PASS:**
   t10_float_math (floats/snprintf/sscanf), t11_abi_deep (structs by value,
   recursion, fn pointers, qsort bridge), t12_libc_io (heap churn, FILE*-level
   IO, time, atexit LIFO — exit-handler prints PASS).
6. **UCRT API wave (TASK-API-011/012, CLOSED TESTED):** __acrt_iob_func,
   _configure_narrow_argv, _initialize_narrow_environment, _set_new_mode,
   _set_invalid_parameter_handler, signal stub, _tzset, _set_app_type apiset
   alias, __stdio_common_vfprintf/vsprintf (real bridges via the proven
   MS→SysV va_list repack), vsscanf (GP repack), vfwprintf (loud stub),
   fopen/fclose/fread/fgets/fgetc/rewind/remove/strftime bridges, and ALL
   UCRT __p__* as FUNCTIONS (three crash signatures 0x2039038/0x2039034/
   0x2039050 proved fmode/commode/argc). map_crt_stream now passes unknown
   pointers through (old unknown->stdout fallback ate fmemopen output).
7. **Verification:** regression 12/12 at every build; G1 gate pinned 3x
   (evidence/master/g1/20261015_{063340,064317,073727}); final sweep
   evidence/master/appcompat/run-20261015_073729 = **10/12 PASS**.

### Commits
- CONTINUE #7 commit (this session) on top of 709b9ee. Push: YES (verified).

### Next
Next task ID: **TASK-BUG-038** (docs/bugs/BUG-038.md has the full design).
Exact next command from the owner: **CONTINUE**.

### Unverified / honest notes
- __stdio_common_vfwprintf = returning stub (untested by design).
- vsscanf repack assumes the uniform 8-byte MS slot model (same as printf).
- t12 tz data = UTC stub; clock() resolved via an existing entry (verify which).
- BUG-038's ControlPc double-decrement audit is folded into its fix plan.
- The UI scanner's 40/98 "violations" = expected llvm-mingw FP-relative XMM
  encodings; TASK-TEST-007 turns the count into a CI gate.
