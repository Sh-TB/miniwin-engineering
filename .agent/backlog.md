# Engineering Backlog — stable IDs, explicit dependencies

Statuses: PENDING / READY / IN_PROGRESS / BLOCKED / IMPLEMENTED / TESTED / VERIFIED / PARTIAL / SUPERSEDED
Implementation status and verification status are tracked separately (e.g. IMPLEMENTED ≠ VERIFIED).

## P0 — acceptance path

### TASK-BUG-026 — Fix g_cap_rip capture operand in mw_RaiseException
- Priority: P0. Status: **TESTED (CLOSED)**. Dependencies: none.
- Scope was: `src/loader/loader.c` mw_RaiseException stub ONLY (single operand: `[rsp+0x00]` → `[rsp+0x08]`, stray `0x58` reload deleted; mirroring exp_next2_backup L842-843).
- Result (2026-10-14): regression **12/12** (was 10/12); UPX progressed from walk-never-starts/exit-134 to full 4-frame walk → Frame[3] handler → real `RtlUnwindEx(TargetIp=0x4dcd24)` → landing-pad resume, then a NEW distinct failure (exit 139, RIP=0x2004167 heap) = **BUG-027**. Report: `docs/master/experiments/EXP-036-bug026-gcap-rip-fix-live-rerun.md`; closure: `docs/bugs/BUG-026.md`; evidence: `evidence/master/bug026/` + `evidence/master/bug026_results.json`.

### TASK-BUG-027 — mw_RtlUnwindEx landing-pad resume state wrong (slot-stride decode)
- Priority: P0. Status: **TESTED (CLOSED)**. Dependencies: TASK-BUG-026 (done).
- Scope: `mw_RtlUnwindEx` resume-state assembly ONLY. Preferred fix (ADR-008): resume with the personality-provided ContextRecord state (Rsp/Rdx from r8), override only Rip=TargetIp. Minimal alternative: fix ALLOC_SMALL `(OpInfo+1)*8` decode in mw_RtlUnwindEx AND adopt RDX from OriginalContext.
- Evidence: EXP-036 §4 (alloc_total 0x40 vs true 0x48; correct RSP 0x7ffe60f7deb0 vs used 0x7ffe60f7deb8; landing pad = return address of throw call `call 0x4a95a8` @0x4dcd1f, selector switch `sub rdx,1; jne`).
- Boundaries: do NOT touch EH_FIX, CXX-EH-FIX, the RaiseException stub, or the walk itself.
- Acceptance: regression stays 12/12; fresh UPX trace shows landing-pad entry registers (RSP=0x7ffe60f7deb0, RDX=selector, RAX=0x3bc8320) and a NEW failure point further along (progress) or UPX `--version` output (completion).
- Result (2026-10-14): actual root cause was a 4-byte slot STRIDE in the unwind-code loop (`uint16_t* tgt_codes` + `tgt_slot*2` => byte offset slot*4), not the ALLOC_SMALL formula. Fix = one declaration (`uint8_t* tgt_codes`). Trace-verified: alloc_total=0x48, post_prologue_RSP=establisher−0x48 (= Frame[3]-entry RSP), landing pad 0x4dcd24 resumed, catch TLS machinery ran, then terminate→abort (exit 134) = BUG-029. EXP-036's RDX-selector claim withdrawn (disassembly: raise helper `xor edx,edx`; jne branch is normal) and the "adopt r8 OriginalContext" direction retracted (personality passes r8=&ExceptionRecord). Report: `docs/master/experiments/EXP-037-…md`; closure: `docs/bugs/BUG-027.md`.

### TASK-EXP-036 — Live re-run of UPX dispatch path after BUG-026
- Priority: P0. Status: **TESTED (CLOSED)**. Dependencies: TASK-BUG-026.
- Result (2026-10-14): executed in the same session as BUG-026; full report `docs/master/experiments/EXP-036-bug026-gcap-rip-fix-live-rerun.md` (§3 verification, §4 new-failure root cause, §7 falsifiable predictions P-1/P-2/P-3).

### TASK-BUG-029 — terminate→abort after correct landing-pad resume (catch machinery; 'unsigned long long' in flight)
- Priority: P0. Status: **TESTED (CLOSED — root cause isolated & superseded, 2026-10-15)**. Dependencies: TASK-BUG-027 (done).
- Result (EXP-038): first step TASK-API-005 executed (EH_FIX + CXX-EH-FIX flag-guarded behind `MINWIN_EH_HACKS`, default OFF, ADR-010). With hacks OFF the EH_FIX dispatch-1 hijack is confirmed as the poison: single clean dispatch, no re-raise chain, no fake machinery; the PE now reaches its own `dt_check.cpp` assertion. Remaining failure decomposed into BUG-030 (fixed) + BUG-031 (open).

### TASK-BUG-030 — landing pad resumed without the catch selector in RDX (4-param protocol ER)
- Priority: P0. Status: **TESTED (CLOSED, 2026-10-15)**. Dependencies: TASK-API-005 (done).
- Ground truth (EXP-038 §4): GCC bridge site A passes arg3=&ER with `param={object, est, LP, selector}` (core 0x4AFDB0 stores selector at obj−0x24, phase-2 writes blob[1]=sel → ER.param[3]; bridge `flags&0x20` branch independently writes ctx->Rdx=param[3]); landing pad 0x40331c consumes it via `mov rbx,rdx; sub rbx,1; jne fail`.
- Fix: fingerprint-guarded `CTX.Rdx = ER.param[3]` in mw_RtlUnwindEx (`nparams==4 && param[1]==tgt_frame && param[2]==tgt_ip`) — protocol-driven, no hardcoded addresses.
- Verified: trace `sel=1 -> CTX.Rdx=1`; selector branch taken; regression 12/12 (test-10 expectation evolved with evidence comment); hacks-ON backward-compat exact. Closure: `docs/bugs/BUG-030.md`.

### TASK-BUG-031 — catch protocol-state validation fails: intermediate-frame cleanup never runs
- Priority: P0. Status: **TESTED (CLOSED — G1 ACCEPTANCE MET, 2026-10-15)**. Dependencies: TASK-BUG-030 (done).
- Result (EXP-039): ADR-011 — dispatcher honors the GCC bridge resume command (0x21474343, 4-param ER) as a synthesized landing-pad resume; continuation unwind walks remaining cleanups and resumes the target LP directly; raise-site register fidelity (stub g_cap_* volatiles + rax/r10/r11 pinning in call_handler_aligned) eliminates the compile-dependent phase-1 flip. Two latent asm bugs fixed (early-clobber ER←RAX; %rip escaping). UPX prints the full --version banner, exit 0, 5/5 deterministic; regression 12/12 (test-10 = success state); MINWIN_EH_HACKS=1 legacy compat exact. Evidence: evidence/master/bug031/.
- Symptom: with sel=1 delivered, the catch's `check_var==0x17` check fails (chain 1→5→23 requires Frame[3]'s cleanup LP 0x4015A5 to run during the unwind; mw_RtlUnwindEx skips intermediate cleanups).
- Protocol map + design candidates (a)/(b) and the C-stack-walk caution: EXP-038 §5.1; predictions P-C/P-D/P-E: EXP-038 §7. Closure doc: `docs/bugs/BUG-031.md`.

### TASK-BUG-028 (candidate, P3) — dead `movq 0x30/0x38/0x40/0x48(%rsp), rcx/rdx/r8/r9` loads in RaiseException stub
- Miscommented leftover of an older no-push stub variant (absent in exp_next2_backup; offsets hold pushed RBP/R12/R13/R14 today). Benign; fold into TASK-API-016 dead-code cleanup. Discovered during BUG-026 (EXP-036 §2).

## P1 — landing-pad correctness (after EXP-036)

### TASK-EXP-037 (old task ID) — Verify EXP-035 post_prologue arithmetic live (SUPERSEDED — the live numbers landed inside the BUG-027 experiment, which is logged as experiment report EXP-037 to keep the experiment sequence chronological; this task ID is retired to avoid duplicate numbering)
### TASK-EXP-038 (old task ID, RETIRED) — OriginalContext adoption / RDX selector propagation (MERGED into TASK-BUG-027 in CONTINUE #3; the experiment-report ID "EXP-038" was re-used in CONTINUE #4 for the API-005/BUG-030 experiment — the old task ID remains retired, the report is canonical)
### TASK-API-006 — ALLOC decode fixes: scale rule in 4 copies + windows_abi doc (BUG-027 confirmed the +8 defect ALSO hits ALLOC_SMALL in mw_RtlUnwindEx — extend scope to ALLOC_SMALL there)
### TASK-API-005 — Isolate EH_FIX + CXX-EH-FIX behind MINWIN_EH_HACKS flag, default OFF (**TESTED (CLOSED) 2026-10-15** — ADR-010; both hacks flag-guarded, nothing deleted; `MINWIN_EH_HACKS=1` restores legacy behavior exactly; details in the TASK-BUG-029/030 entries above)
### TASK-API-002 — Trace flush in mw_ExitProcess
- Priority: P1. Status: **TESTED (CLOSED, 2026-10-15)**. Dependencies: none.
- Result: `mw_trace_shutdown_flush()` — trace JSON terminator + stdio flush on every _exit path (ExitProcess/abort direct; exit() via atexit at trace-open). api_trace.json now self-closing on all exit paths; G1 re-pinned OK.
### TASK-API-003 — Wire SetUnhandledExceptionFilter into walk-exhaustion (PENDING, dep BUG-026)
### TASK-TEST-001 — Port unwind unit tests to tests/unit/ with portable includes (READY — independent)
### TASK-TEST-002 — ALLOC_LARGE unit tests, spec vectors (PENDING, pairs TASK-API-006)
### TASK-API-006 — Fix ALLOC_LARGE scaling in 4 copies + windows_abi doc (READY — independent; P1 priority despite independent nature because latent)
### TASK-TEST-004 — Fix dispatch tests (import patching + fail-if-not-dispatched) (READY — independent)
### TASK-TEST-005 — Synthetic PE builder v2 + Tier-B case set (PENDING, dep TEST-004 or parallel)

## P2 — generalization enablers
TASK-API-007 (unresolved-import trap), TASK-API-008..013 (VirtualAlloc/Heap/file/thread/__C_specific_handler/_Unwind_DeleteException), TASK-MAPPING-01..07 (per IMAGE_MAPPING_AND_RELOCATION_AUDIT.md), TASK-TEST-006 (Tier-D suite), TASK-TEST-007 (golden-trace), TASK-TEST-008 (UPX acceptance harness).

## P3 — hygiene
TASK-API-014 (format warnings), TASK-API-015 (realloc trace smell), TASK-API-016 (dead code), DOC-001 (README capabilities refresh), DOC-002 (BUG-001/NEW_AGENT_START hash correction note), OWNER-001 (push credentials), OWNER-002 (knowledge-transfer/ dedup decision).

## Superseded entries (historical M8/M9 etc.)
- Old "M8: RtlUnwindEx context restoration" — SUPERSEDED by TASK-BUG-026 + EXP-037/038 chain (the M8 framing assumed the current source still reached the landing pad; it does not).
- Old "M9: verify UPX --version output" — folded into TEST-008 / RELEASE_CRITERIA G1.

## P0.5 — post-G1 hardening (G2 completion; the success must be guarded)

### TASK-BUG-032 — RESERVED (do not assign preemptively)
- Reserved ID for the next live-trace failure discovered on any target. Rule: a new failure point gets the next BUG id with a full trace before any code change (CONTINUE protocol §4).

### TASK-BUG-033 — Generalize the continuation unwind walk (post-G1 robustness)
- Priority: P1. Status: PENDING. Dependencies: TASK-BUG-031 (done).
- Observed (EXP-039 §7/§10): the continuation walk logged `cont walk: RIP 0x0 outside PE — walk ends` — the start context for the walk came from the continue-helper's captured state with a zero RIP in this run; the target LP resume was still correct for this target. Generalize: validate/derive the walk start (OriginalContext at [arg5] vs g_unwind_ctx fallback), require non-zero in-image RIP, and add a trace line distinguishing "walk did nothing" from "walk ran N frames". Must NOT change the G1 behavior (guarded by test-10).

### TASK-G1-001 — G1 acceptance pin script (one command, evidence-producing)
- Priority: P1. Status: **TESTED (CLOSED, 2026-10-15)**. Dependencies: none.
- Result: `scripts/g1_accept.sh` (clean build 0-errors enforced, banner byte-check, no-[CRASH] check, 12/12, evidence dir per run). Ran OK twice; second run re-pinned after API-006 (evidence/master/g1/20261014_215737 + 20261014_220048).
- Scope: `scripts/g1_accept.sh` — builds clean, runs UPX --version, asserts exit 0 + exact banner lines (byte-for-byte on `upx 4.2.4`), asserts no `[CRASH]` in stderr, asserts regression 12/12, records binary hash + trace into `evidence/master/g1/` with timestamp. Rationale: P-G (EXP-039 §9) — the phase-1 flip class makes any loader.c change suspect until re-pinned.

### TASK-TEST-004 — Fix dispatch tests (import patching + fail-if-not-dispatched) — now blocks TEST-005
- Priority: P1 (promoted: TEST-005 must guard G1). Status: **IN_PROGRESS — slice 1 CLOSED (2026-10-15)**: non-vacuous real-loader harness `tests/dispatch_tests/test_dispatch_v2.sh` (4/4 on load/run contracts), builder import layout fixed (canonical PE32+ thunks), loader IAT-writability fix (Windows-faithful, G1 re-pinned). **Slice 2 = builder codegen defects B-1 (call-rel32 fixups), B-2 (UNWIND_CODE nibble order), B-3 (ms_abi frame alignment)** — see CURRENT_TASK.md.

### TASK-TEST-005 — Synthetic PE builder v2 + Tier-B multi-frame EH case set
- Priority: P1 (promoted). Status: PENDING, dep TEST-004.
- Must cover (P-F, EXP-039 §9): catch across ≥2 frames WITH intermediate cleanups (G1-shaped), catch without cleanups, cleanup-only unwind, rethrow from catch, selector != 1, nested exception during unwind, exception object with destructor (dtor ordering), TLS-state interplay. Case = PASS iff the PE's own printed contract matches (no loader-side shortcuts).

### TASK-TEST-002 — ALLOC_LARGE/ALLOC_SMALL unit tests with spec vectors
- Priority: P1. Status: PENDING. Dependencies: pairs TASK-API-006.
- Vectors: x64 unwinding spec prologues (ALLOC_LARGE info=0/1, ALLOC_SMALL 1..160, PUSH_MACHFRAME 0/1, chained info) decoded by BOTH `seh_total_prologue_alloc` (correct reference) and the mw_RtlUnwindEx/mw_RtlVirtualUnwind copies — the test must FAIL until API-006 lands.

### TASK-API-006 — ALLOC decode scale rule in 4 copies + windows_abi doc
- Priority: P1. Status: **TESTED (CLOSED, 2026-10-15)**. Dependencies: —
- Result: all 4 loader.c copies now spec-correct (ALLOC_LARGE info==0 ⇒ uint16×8; info==1 ⇒ uint32 no-shift; ALLOC_SMALL (info+1)*8 already BUG-027-correct); windows_abi doc corrected with a dated correction note; seh_total_prologue_alloc documented as the reference decoder. G1 re-pinned OK after the change. Remaining follow-up: the exp_next2/dalvik prototype decoders (research artifacts) + unit vectors = TASK-TEST-002/009.
- Copies: mw_RtlUnwindEx decode loop, mw_RtlVirtualUnwind decode loop, exp_next2 harness decoder, dalvik prototype decoder + the windows_abi doc section. ALLOC_SMALL is already correct post-BUG-027 in mw_RtlUnwindEx; `seh_total_prologue_alloc` (BUG-031) is the spec-correct reference implementation — extract shared helper instead of a 5th copy.

## P2 — generalization enablers (second-target preparation)

### TASK-API-007 (exists) — unresolved-import trap (fail-visible)
### TASK-API-008..013 (exist) — VirtualAlloc/Heap/file/thread/__C_specific_handler/_Unwind_DeleteException

### TASK-API-017 — RaiseException continuable path + SetUnhandledExceptionFilter wiring at walk exhaustion
- Priority: P2. Status: PENDING. Deps: none.
- Scope: continuable exceptions (flags without NONCONTINUABLE) must support ExceptionContinueExecution; walk exhaustion must call the PE's registered unhandled filter (TASK-API-003 merge target). Evidence hook: SetUnhandledExceptionFilter(handler=0x49c9c0) is registered by UPX and never invoked.

### TASK-API-018 — TLS callbacks (TLS_DIRECTORY) executed before EP
- Priority: P2. Status: PENDING. Deps: none.
- Scope: parse the TLS data directory, invoke callbacks in order at load (before EP), keep UPX (no TLS callbacks) byte-identical. Guarded by a synthetic PE from TEST-005 builder.

### TASK-API-019 — __C_specific_handler (MSVC C SEH) scope-table interpreter
- Priority: P2. Status: PENDING. Deps: TEST-005 builder.
- Scope: non-C++ __try/__except x64 PEs (MSVC) — filter evaluation, local unwind, __finally. MiniWin already parses the RUNTIME_FUNCTION layer; this adds the scope-table layer. Out of first release by non-goals, but blocks "second target".

### TASK-API-020 — RtlAddFunctionTable/RtlInstallFunctionTableCallback completeness
- Priority: P2. Status: PENDING. Deps: none.
- Scope: dynamic function tables for JIT/DLL-style code; currently RtlLookupFunctionEntry only consults the static .pdata (trace shows the export exists — verify semantics against MSDN and add unit vectors).

### TASK-API-021 — GetProcAddress/load-time import fidelity audit (ordinal, forwarded, delay-load)
- Priority: P2. Status: PENDING. Deps: API-007.
- Scope: ordinal imports (IMPORT_BY_ORDINAL), forwarded exports (kernel32→ntdll strings), delay-load import directory (first call resolution). Each needs a synthetic PE case.

### TASK-API-022 — EntryPoint argv/env fidelity
- Priority: P2. Status: PENDING. Deps: none.
- Evidence: `__getmainargs(argc=0x60b028…)` — verify argc/argv/envp actually carry the loader's argv (`--version` must be visible to the PE; UPX's banner already proves the flag path works, but a synthetic case must assert argv[1] content and env passthrough).

### TASK-MAPPING-01..07 (exist) — per IMAGE_MAPPING_AND_RELOCATION_AUDIT.md
### TASK-TEST-006 (exists) — Tier-D mapping/collision suite
### TASK-TEST-007 (exists) — golden-trace harness (normalize addresses, diff full API sequences)
- Priority: P2. Note (this session): the normalized-trace diff tooling built ad-hoc in CONTINUE #5 (bash + python, /tmp) is the seed — productize as `tests/tools/trace_diff.py` with masks for loader/libc/stack/heap.

### TASK-TEST-008 (exists) — UPX acceptance harness — the G1 half is DONE (test-10 success state); extend to full command matrix (`-L`, `-h`, bad-args exit codes)

### TASK-TEST-009 — Unwind-code decoder differential fuzzer
- Priority: P2. Status: PENDING. Deps: TEST-002.
- Scope: generate random valid prologue byte streams, decode with seh_total_prologue_alloc (reference) vs the other copies; any divergence = defect (catches the API-006 class permanently).

### TASK-TEST-010 — PE shape edge-case suite
- Priority: P2. Status: PENDING. Deps: TEST-005 builder.
- Cases: PE32+ with .pdata at section edge, unwind info chaining, exception directory absent, corrupted .pdata (must fail-visible), SizeOfImage padding, section VIRTUAL_ADDRESS gaps (MAPPING-02/07 interplay).

### TASK-RESEARCH-01 — MSVC x64 C++ EH (0xE06D7363) protocol map (research-only, no code)
- Priority: P3 research. Status: PENDING.
- Scope: duplicate the EXP-038/039 method (disassembly ground truth + live trace) for the MSVC chain: __CxxFrameHandler3, FuncInfo/HandlerType/TryBlockMap, EHActionState — producing the design doc that a future TASK-API-023 would implement. Explicitly NOT first-release scope (non-goals).

### TASK-RESEARCH-02 — Collided-unwind semantics on Windows (research-only)
- Priority: P3 research. Status: PENDING.
- Scope: DISPATCH_COLLIDED dispositions (dispatch table col 3) — what real Windows does when unwinding crosses a frame that is itself inside a dispatcher callback; produce a doc + whether MiniWin's single-threaded model can ever hit it.

## P3 — hygiene and docs

### TASK-API-014 (exists) — format warnings
### TASK-API-015 (exists) — realloc trace smell
### TASK-API-016 (exists) — dead code (absorb BUG-028 dead stub loads)

### TASK-BUG-028 (candidate, P3 — unchanged) — dead `movq 0x30/0x38/0x40/0x48(%rsp)` loads in RaiseException stub
- Fold into TASK-API-016. (Discovered EXP-036 §2; still benign.)

### TASK-API-023 — MW_TRACE plumbing cleanup
- Priority: P3. Status: PENDING. Deps: none.
- Scope: split the MW_TRACE macro into API-trace vs debug-trace channels with an env switch (MINWIN_DEBUG_TRACE), remove the TEMP/BISECTION comment residue left by EXP-039 in call_handler_aligned comments, keep the trace-file format stable (TEST-007 depends on it).

### TASK-API-024 — Stack-usage budget for dispatcher paths
- Priority: P3. Status: PENDING. Deps: none.
- Scope: `seh_walk_intermediate_cleanups` allocates a full CONTEXT on the C stack per invocation + call frames; measure worst-case stack depth (UPX run + synthetic deep recursion case) and document the headroom; consider static scratch if needed. Guard: no behavior change (test-10).

### TASK-DOC-001 (exists) — README capabilities refresh — NOW INCLUDES: update to state G1 acceptance met (upx --version exit 0), remove stale claims
### TASK-DOC-002 (exists) — BUG-001/NEW_AGENT_START hash correction note
### TASK-DOC-003 — API_COMPATIBILITY_DATABASE refresh post-G1
- Priority: P3. Status: PENDING. Deps: none.
- Scope: reclassify every entry the G1 run exercised (RaiseException/RtlUnwindEx/personality-boundary semantics) from claimed→verified with evidence pointers to EXP-039 + bug031 evidence dir.

### TASK-DOC-004 — Engineering knowledge transfer: "compile-dependent behavior" class
- Priority: P3. Status: PENDING. Deps: none.
- Scope: distill EXP-039 §4 into a section of ENGINEERING_RULES.md or KNOWLEDGE_BASE.md: any code that reads CPU state to build ABI-visible data must pin it to captured values; inline-asm checklist ("=&a", clobbers for every written reg, %%rip escaping, no comments inside templates).

### TASK-INFRA-001 — Makefile release target
- Priority: P3. Status: PENDING. Deps: none.
- Scope: `make release` = clean build + strip-copy + sha256 manifest + version stamp file; feeds G3 clean-clone reproducibility.

### TASK-INFRA-002 — Checkpoint automation
- Priority: P3. Status: PENDING. Deps: none.
- Scope: `scripts/checkpoint.sh <task-id>` producing the pre-task checkpoint zip (repo state + hashes + trace snapshot) — replaces the hand-rolled zip; protocol §2 compliance helper.

### TASK-OWNER-002 (exists) — knowledge-transfer/ dedup decision
### TASK-OWNER-003 — Post-G1 roadmap decision
- Priority: P3 (owner decision). Status: PENDING.
- Scope: after G1+G2, choose the next acceptance target: (a) second mingw sample exercising different EH shapes (via TEST-005 builder), (b) a UPX-packed (not decompressed) sample requiring the self-extractor path, (c) MSVC C EH (__C_specific_handler). Record the decision in ROADMAP.md.

## Superseded entries (historical M8/M9 etc.)
- Old "M8: RtlUnwindEx context restoration" — SUPERSEDED by TASK-BUG-026 + EXP-037/038 chain (the M8 framing assumed the current source still reached the landing pad; it does not).
- Old "M9: verify UPX --version output" — **ACHIEVED as G1 (2026-10-15, EXP-039)**; residual harness work lives in TEST-008.

---

## 2026-10-15 — appcompat sweep expansion (CONTINUE #6)

Source: EXP-040 (tests/appcompat/, 9-program sweep: 7 PASS incl. both big apps' C tier).
New findings/closed: BUG-032..036 (CLOSED TESTED), BUG-037 (OPEN, next), TASK-TLS-001 (CLOSED TESTED),
TASK-API-007/008 (CLOSED TESTED, table 162 -> 188).

### TASK-BUG-037 — unwind-code interpreter faults on CRT-startup UNWIND_INFO
- Priority: P0. Status: **READY (NEXT)**. Deps: none (all gates green).
- Scope: seh_internal_virtual_unwind opcode interpreter + a reference decoder test vector. Fault: `mov (%r9),%rbx` with R9=0x27 (slot offset used as address) while walking RF begin=0x1470 end=0x23d9, UNWIND_INFO @ RVA 0x50458 (v1, flags=0x03, 13 slots, frame byte 0x85, personality 0x131b0).
- Acceptance: golden-vector decoder test in tests/; t08 reaches its own catch and prints `t08: PASS` (exit 0); regression 12/12; UPX G1 gate.
- Evidence: docs/bugs/BUG-037.md; evidence/master/appcompat/run-20261015_053015/t08_big_cpp_stl.*

### TASK-API-009 — wave 3: FILE* stdio + locale _l family (44 APIs, unblocks t09 iostream)
- Priority: P1. Status: PENDING. Deps: none.
- Scope (measured list from t09): fopen/fclose/fread/fseek/fsetpos/fgetpos/ftell? (not imported — verify), setbuf, setlocale, strftime, isdigit/toupper (bool maps), _vsnprintf/_vscprintf, _sys_nerr (data), __pctype_func, _filelengthi64, _wfopen, fgetwc/ungetwc, wcrtomb_s, _iswalpha_l.._iswxdigit_l, _tolower_l/_toupper_l/_towlower_l/_towupper_l, _strcoll_l/_wcscoll_l/_strxfrm_l/_wcsxfrm_l/_strtod_l/_mbtowc_l, AreFileApisANSI, FormatMessageA, GetACP, GetLocaleInfoEx, LocalFree.
- Design note: `_l` functions take _locale_t — bridge to glibc per-thread locale or honest fixed-C-locale stubs (document each).
- Acceptance: t09 reaches its own PASS line (exit 0); import resolution 0 unresolved for t09; regression 12/12; G1 gate.

### TASK-API-010 — callback ABI audit: signal/_beginthreadex/SetUnhandledExceptionFilter/AVEH call sites
- Priority: P1. Status: PENDING. Deps: none.
- Scope: same class as BUG-035 — every PE callback invoked by glibc-side code must go through an ms_abi bridge. Audit mw_signal, mw__beginthreadex, mw_SetUnhandledExceptionFilter, mw_AddVectoredExceptionHandler/Remove; add bridges + doc.
- Acceptance: table of call sites with bridge status; a thread+signal test (t10_signal.c) PASSes; regression 12/12.

### TASK-API-011 — _beginthreadex real threads + WaitForSingleObject on thread handles
- Priority: P1. Status: PENDING. Deps: TASK-API-010.
- Scope: current _beginthreadex is a stub; implement pthread_create bridge returning a handle usable by WaitForSingleObject/WaitForMultipleObjects; _endthreadex pthread_exit bridge; thread-safe TlsAlloc/Fls per-thread arrays.
- Acceptance: t11_threads.c: 4 worker threads sum 100k ints each via Tls + join by handle; PASS; regression 12/12.

### TASK-API-012 — _open flag map fix: _O_NOINHERIT(0x0080) wrongly mapped to O_APPEND
- Priority: P1. Status: PENDING. Deps: none.
- Scope: mw__open maps 0x0080 → O_APPEND; msvcrt _O_APPEND = 0x0008, _O_NOINHERIT = 0x0080. Fix mapping + add _O_APPEND (0x0008) handling; check _O_TEXT/_O_BINARY ignore comments; update windows_abi doc if affected.
- Acceptance: t12_openflags.c: O_APPEND append-behavior test PASS; regression 12/12.

### TASK-TEST-011 — grow the appcompat suite to 15 programs (sweep hardening)
- Priority: P1. Status: PENDING. Deps: TASK-API-009 for t09/t13.
- Scope: add t10_signal (signal+raise), t11_threads, t12_openflags, t13_stdio_fopen (fopen/fprintf/fscanf/fclose), t14_env_locale (getenv/setlocale/strftime), t15_longjmp_thread — each self-verifying like t01..t07; extend run_sweep.sh classification (VERIFY_FAIL vs CRASH vs TIMEOUT already covers).
- Acceptance: run_sweep.sh summary shows all 15 with class; evidence run committed.

### TASK-TEST-012 — deterministic golden-output harness for appcompat
- Priority: P2. Status: PENDING. Deps: TASK-TEST-011.
- Scope: capture stdout of each test into evidence/golden/*.out; sweep diffs against golden (catches silent wrong-output regressions like print drift); wire into g1_accept.sh as optional stage.

### TASK-BUG-038 (reserved) — C++ EH acceptance G2: dt_check-style binary from source
- Priority: P1. Status: PENDING. Deps: TASK-BUG-037.
- Scope: after t08 passes, build a dedicated C++ EH torture program (multiple catch types, nested try, dtors during unwind, rethrow across 3 frames, SEH+C++ mix) and pin as the G2 acceptance sample; update RELEASE_CRITERIA.

### TASK-TLS-002 — TLS callbacks at DLL_THREAD_ATTACH/DETACH + thread-exit Fls callbacks
- Priority: P2. Status: PENDING. Deps: TASK-API-011.
- Scope: extend TASK-TLS-001: run AddressOfCallBacks array with reason 2/3 on thread create/exit (via pthread bridge), invoke Fls callbacks on thread exit (needs per-key registry keyed with ms_abi dtor bridge).

### TASK-TLS-003 — dynamic TLS beyond 64 slots + TlsFree
- Priority: P3. Status: PENDING. Deps: none.
- Scope: Windows expands ThreadLocalStoragePointer for TlsAlloc beyond 64 (ExpansionSlots gs:0x28? verify); implement expansion + TlsFree (currently TlsFree missing from table? verify and add).
- Acceptance: TlsAlloc*70 works; regression 12/12.

### TASK-API-013 — fopen family implementation strategy (FILE* bridging)
- Priority: P1. Status: PENDING. Deps: none.
- Scope: msvcrt fopen returns FILE* — the PE expects MSVCRT_FILE semantics; bridge: fopen → real glibc fopen, and route ALL stream-taking mw_* through map_crt_stream passthrough (already does). Must handle mingw's __acrt_iob_func absence and _fdopen. Decide: glibc FILE* direct (current direction) vs opaque handle table.
- Acceptance: design note in docs/master/ + t13 PASS.

### TASK-API-014 — locale honest-stub policy (_l family, GetACP, GetLocaleInfoEx)
- Priority: P2. Status: PENDING. Deps: TASK-API-009.
- Scope: decide+document: which locale APIs return fixed C-locale values vs bridge to glibc; GetACP=1252 constant; GetLocaleInfoEx minimal table; write docs/master/LOCALE_POLICY.md.

### TASK-API-015 — formatMessageA + LocalFree honest implementation
- Priority: P3. Status: PENDING. Deps: TASK-API-009.
- Scope: FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM) → strerror mapping table for common Win32 codes; LocalFree → free registry (FormatMessage-allocated buffers must be freeable by LocalFree — pair them).

### TASK-DOC-005 — EXP-040 integration into README/ARCHITECTURE docs
- Priority: P2. Status: PENDING. Deps: none.
- Scope: ARCHITECTURE.md gains an "Application compatibility" section (sweep harness, toolchain pin, current matrix); README gains a "Building test apps" note (llvm-mingw path, NOT in repo).

### TASK-DOC-006 — BUG-032..036 class documentation: ABI boundary rules
- Priority: P2. Status: PENDING. Deps: none.
- Scope: one doc (docs/master/ABI_BOUNDARY_RULES.md) codifying the sweep's lessons: (1) never hand raw ms va_list to SysV varargs; (2) never write past Windows struct sizes (CS 40B); (3) glibc-invoked callbacks need ms_abi bridges; (4) jmp_buf ownership; (5) stream pointer provenance. Feeds code review of future API additions.

### TASK-TEST-013 — API boundary unit tests (regression armor for BUG-032..036 class)
- Priority: P1. Status: PENDING. Deps: none.
- Scope: host-side unit tests (C, not PE) exercising map_crt_stream, printf repack (mixed int/float/overflow >5 gp args >8 fp args), CS sentinel within 40 bytes, setjmp/longjmp reg restore (assembly-level), qsort bridge. Run in make test target.
- Acceptance: `make test` runs them; CI-able; regression 12/12 unaffected.

### TASK-TEST-014 — printf repack fuzz (cross-ABI correctness)
- Priority: P2. Status: PENDING. Deps: TASK-TEST-013.
- Scope: generate 500 random format strings (bounded subset: d,i,u,x,X,o,c,s,p,f,e,g,%%, widths, precisions, l/ll/h) with known args; PE prints, host compares vs host-side printf ground truth. Kills misclassification regressions.

### TASK-PERF-001 — sweep-run overhead profiling
- Priority: P3. Status: PENDING. Deps: none.
- Scope: measure loader startup vs program runtime for t01..t08 (time -v); identify hot spots (trace JSON writes dominate?) — informs MINWIN_TRACE default-off paths.

### TASK-INFRA-003 — sweep runner in repo without /tmp dependency
- Priority: P2. Status: PENDING. Deps: none.
- Scope: run_sweep.sh currently uses mktemp /tmp + miniwin-results cwd; make it repo-local (evidence/appcompat run dir), with --keep/--json flags; summary.json machine-readable.

### TASK-INFRA-004 — toolchain pinning script
- Priority: P2. Status: PENDING. Deps: none.
- Scope: scripts/fetch_appcompat_toolchain.sh downloading llvm-mingw 20261006-msvcrt to /home/z/my-project/tools-mingw (documented, NOT vendored) with sha256 verification; used by sweep docs.

### TASK-OWNER-004 — second acceptance sample decision (post-t08)
- Priority: P3 (owner). Status: PENDING. Deps: TASK-BUG-037.
- Scope: choose G2's pinned sample: (a) t08 as-is, (b) a real OSS console tool compiled with llvm-mingw (e.g., a small JSON parser), (c) MSVC-built C sample. Record in ROADMAP.md.

### TASK-BUG-039 (reserved) — dispatch_tests/test_a movdqa ms_abi misalignment (slice-2 carryover)
- Priority: P2. Status: PENDING. Deps: TASK-TEST-004 slice 2 (existing).
- Note: existing TEST-004 slice-2 work; keep ID reserved, do not duplicate.

### TASK-DOC-007 — knowledge-transfer/ working-tree/ dedup execution (OWNER-002 follow-through)
- Priority: P3. Status: PENDING. Deps: OWNER-002 decision.
- Scope: execute the dedup decision: either git rm the duplicates with a REPOSITORY_BASELINE.md note or document keep-reason per directory.

### TASK-RESEARCH-003 — mingw TLS model coverage (emutls vs native) audit
- Priority: P2. Status: PENDING. Deps: TASK-TLS-001.
- Scope: -femutls vs native gs:0x58 TLS in llvm-mingw builds: document which mingw versions/models the loader supports; extend t08 with a __thread heavy program (t16_tls.c: 64 __thread vars, per-thread mutation, self-check).

### TASK-RESEARCH-004 — GS-base vs FS-base coexistence audit
- Priority: P3. Status: PENDING. Deps: none.
- Scope: ARCH_SET_GS clobbers any host GS use (vDSO? stack protector?); document and test that the loader's own glibc code is unaffected across glibc versions; add a guard/test.

### TASK-TEST-015 — PE TLS directory torture test
- Priority: P2. Status: PENDING. Deps: TASK-TLS-001.
- Scope: synthetic PE with TLS template + callbacks + __tls_index consumers (extend build_synthetic_pe.py); verify template copy, zero-fill, callback order (PROCESS_ATTACH), index write.

### TASK-API-016 — Console API batch: GetConsoleScreenBufferInfo/SetConsoleCursorPosition fidelity
- Priority: P3. Status: PENDING. Deps: none.
- Scope: current console APIs are stubs; implement ANSIcon bridge (map to ANSI escapes) or honest defaults; needed by any TUI-style program; measure with a small t17_tui.c probe.

### TASK-SEC-001 — loader crash handler redaction
- Priority: P3. Status: PENDING. Deps: none.
- Scope: crash output prints absolute host addresses; add MINWIN_REDACT=1 mode hashing pointers for shareable logs; keep full mode default for engineering.

### TASK-BENCH-001 — compatibility database automation
- Priority: P2. Status: PENDING. Deps: TASK-INFRA-003.
- Scope: script regenerating API_COMPATIBILITY_DATABASE.md's implemented-table from loader.c ENTRY rows (single source of truth) + per-test import-coverage matrix from sweep evidence.

### TASK-OWNER-005 — milestone: "runs real mingw C apps" announcement pack
- Priority: P3 (owner). Status: PENDING. Deps: TASK-BUG-037 + TASK-TEST-011.
- Scope: CHANGELOG entry + short demo script (t07 run) + screenshots for repo front page.

### TASK-BUG-040 (reserved) — chaininfo/unwind edge cases surfaced by C++ tests
- Priority: P2. Status: PENDING. Deps: TASK-BUG-037.
- Scope: after t08 passes, run t08 5x for determinism; if flaky, isolate (the CXX-EH machinery has compile-dependent history — EXP-039 §4 lesson).

### TASK-TEST-016 — appcompat suite in CI-style make target
- Priority: P2. Status: PENDING. Deps: TASK-INFRA-003, TASK-TEST-013.
- Scope: `make appcompat` = build tests (needs toolchain path env) + run sweep + compare golden outputs + exit non-zero on any regression; document toolchain prerequisite.

### TASK-API-017 — GetFileAttributesA/GetFileAttributesExA (common in CRT init paths)
- Priority: P3. Status: PENDING. Deps: none.
- Scope: implement via stat(); return INVALID_FILE_ATTRIBUTES on error; feeds future programs' startup checks.

### TASK-API-018 — CreateFileA/ReadFile/WriteFile/CloseHandle/GetFileSize minimal handle table
- Priority: P2. Status: PENDING. Deps: none.
- Scope: kernel32 file APIs absent today (only console handles); implement a small fd-backed handle table so win32-style I/O programs run; keep handles distinct from CRT fds (Windows-style) with an internal map.

### TASK-API-019 — VirtualAlloc (absent) + VirtualProtect alignment audit
- Priority: P2. Status: PENDING. Deps: none.
- Scope: VirtualAlloc absent (state.md known gap); implement mmap-backed for allocations; VirtualProtect exists — audit page-granularity behavior vs Windows; test via t18_mem_win.c (VirtualAlloc + guard pages).

### TASK-DOC-008 — TESTING_STRATEGY: promote appcompat tier to Tier 1.5
- Priority: P2. Status: PENDING. Deps: none.
- Scope: TESTING_STRATEGY.md: formalize the sweep as Tier 1.5 (between regression and synthetic dispatch), with pass criteria (C tier must stay 100%, C++ tier tracks BUG-037 frontier), and runbook.

### TASK-RESEARCH-005 — UPX-packed sample (not decompressed) feasibility with current EH chain
- Priority: P3. Status: PENDING. Deps: none.
- Scope: OWNER-003 option (b): try upx-packed.exe (sample exists in checkpoints); document where the self-extractor path diverges; no fix expected this task — a scoping study.

### TASK-API-020 — _ltoa/_ultoa family + _i64toa (missing conversions)
- Priority: P3. Status: PENDING. Deps: none.
- Scope: _i64toa/_ui64toa/_ltoa missing from table (only _ultoa present); implement via snprintf into caller buffer; add to sweep t02.

### TASK-TEST-017 — stress: 200 randomized mixed-API programs via builder
- Priority: P3. Status: PENDING. Deps: TASK-TEST-004 slice 2, TASK-BENCH-001.
- Scope: extend the synthetic builder to compose random call graphs from the supported API table; run 200; classify; feeds both fuzzing and the API database matrix.

### TASK-DOC-009 — release notes draft for the sweep wave
- Priority: P3. Status: PENDING. Deps: none.
- Scope: CHANGELOG.md entry: 5 bugs fixed, TLS support, 26+5 new APIs, appcompat suite (9 programs, 7 PASS incl. 2 big apps), toolchain pinning.

### TASK-API-021 — Environment/Process batch: GetEnvironmentStringsA/GetCommandLineA/W
- Priority: P2. Status: PENDING. Deps: none.
- Scope: GetEnvironmentStringsA/W + FreeEnvironmentStringsA/W bridged to environ; GetCommandLineA/W from g_cmdline (data imports may exist — audit). Needed by any argv/env-consuming win32 program.

### TASK-API-022 — Heap family: HeapCreate/HeapAlloc/HeapFree/GetProcessHeap
- Priority: P2. Status: PENDING. Deps: TASK-API-019.
- Scope: implement thin Heap* over malloc with a heap-id registry; common in UCRT-era binaries and rust-built PEs.

### TASK-API-023 — Synchronization semantics: real Wait* on our semaphore/event objects
- Priority: P2. Status: PENDING. Deps: TASK-API-011.
- Scope: CreateSemaphoreA/WaitForSingleObject currently return instantly (stub-ish); wire to sem_t registry with real blocking semantics + timeouts; needed by threaded programs.

### TASK-TEST-018 — exit-path determinism test (atexit/_onexit/_cexit ordering)
- Priority: P2. Status: PENDING. Deps: none.
- Scope: t19_exit.c registering atexit/_onexit chains and asserting LIFO order + _exit skipping them; pins mw_atexit/_onexit semantics (BUG-033-era additions).

### TASK-RESEARCH-006 — MSVC-built sample feasibility (VCRUNTIME dependency surface)
- Priority: P3. Status: PENDING. Deps: none.
- Scope: compile a small C program with cl.exe equivalents (via wine-msvc? not available — instead document what a VCRUNTIME14-linked PE would import: __C_specific_handler exists, but vcruntime TLS/__security_check_cookie etc. missing) — scoping only.

### TASK-SEC-002 — IAT patch bounds audit (writable-page restore completeness)
- Priority: P2. Status: PENDING. Deps: none.
- Scope: iat_page_ensure_writable/restore — fuzz 50 PEs with IATs straddling section boundaries; assert per-section protection restored byte-identically (extends TEST-004 slice-1 work).

### TASK-BENCH-002 — sweep result trend tracking
- Priority: P3. Status: PENDING. Deps: TASK-BENCH-001.
- Scope: append every sweep's summary.csv into evidence/master/appcompat/trend.csv (date, PASS count, unresolved counts) for long-term progress visibility.

### TASK-DOC-010 — toolchain usage guide (llvm-mingw) in tests/appcompat/README.md
- Priority: P2. Status: PENDING. Deps: TASK-INFRA-004.
- Scope: how to build each tXX, which flags, how imports were audited with pefile, how to add a new program (checklist mirroring t01's structure).
