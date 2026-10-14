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
- Priority: P0. Status: **READY (next task)**. Dependencies: TASK-BUG-030 (done).
- Symptom: with sel=1 delivered, the catch's `check_var==0x17` check fails (chain 1→5→23 requires Frame[3]'s cleanup LP 0x4015A5 to run during the unwind; mw_RtlUnwindEx skips intermediate cleanups).
- Protocol map + design candidates (a)/(b) and the C-stack-walk caution: EXP-038 §5.1; predictions P-C/P-D/P-E: EXP-038 §7. Closure doc: `docs/bugs/BUG-031.md`.

### TASK-BUG-028 (candidate, P3) — dead `movq 0x30/0x38/0x40/0x48(%rsp), rcx/rdx/r8/r9` loads in RaiseException stub
- Miscommented leftover of an older no-push stub variant (absent in exp_next2_backup; offsets hold pushed RBP/R12/R13/R14 today). Benign; fold into TASK-API-016 dead-code cleanup. Discovered during BUG-026 (EXP-036 §2).

## P1 — landing-pad correctness (after EXP-036)

### TASK-EXP-037 (old task ID) — Verify EXP-035 post_prologue arithmetic live (SUPERSEDED — the live numbers landed inside the BUG-027 experiment, which is logged as experiment report EXP-037 to keep the experiment sequence chronological; this task ID is retired to avoid duplicate numbering)
### TASK-EXP-038 (old task ID, RETIRED) — OriginalContext adoption / RDX selector propagation (MERGED into TASK-BUG-027 in CONTINUE #3; the experiment-report ID "EXP-038" was re-used in CONTINUE #4 for the API-005/BUG-030 experiment — the old task ID remains retired, the report is canonical)
### TASK-API-006 — ALLOC decode fixes: scale rule in 4 copies + windows_abi doc (BUG-027 confirmed the +8 defect ALSO hits ALLOC_SMALL in mw_RtlUnwindEx — extend scope to ALLOC_SMALL there)
### TASK-API-005 — Isolate EH_FIX + CXX-EH-FIX behind MINWIN_EH_HACKS flag, default OFF (**TESTED (CLOSED) 2026-10-15** — ADR-010; both hacks flag-guarded, nothing deleted; `MINWIN_EH_HACKS=1` restores legacy behavior exactly; details in the TASK-BUG-029/030 entries above)
### TASK-API-002 — Trace flush in mw_ExitProcess (READY — independent, small)
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
