# EXP-038 — API-005 EH-hack isolation + live re-run: UPX reaches its own dt_check; BUG-030 (selector) found & fixed; BUG-031 (cleanup chain) registered

**Session:** 2026-10-15 (CONTINUE #4)
**Task IDs:** TASK-BUG-029 step 1 = TASK-API-005 (flag-guard EH hacks) → live re-run → BUG-030 (found+fixed) → BUG-031 (registered)
**Baseline commit:** `b843974b9d27cc1a2dcb7a7db7da45eb2ad4d0f6` (== origin/main, GitHub-verified)
**Result hashes:** loader.c `9a52f1d6…`, binary `ca7ce562…`
**Verdict:** API-005 **VERIFIED** (both hacks flag-guarded, default OFF; `MINWIN_EH_HACKS=1` reproduces the
exact BUG-029-era behavior). With hacks OFF the PE executes the **clean GCC personality path** (single
dispatch, no re-raise, no fake catch) and reaches **UPX's own C++ EH self-test assertion**
(`Assertion failed: cxx_exception_handling_works … dt_check.cpp: 362`). A new defect was discovered and
fixed in the same session: **BUG-030** — the landing pad's catch selector was not delivered in RDX
(fix: fingerprint-guarded `CTX.Rdx = ER.param[3]` from the 4-param protocol ER; trace-verified `sel=1`).
Execution advanced to **BUG-031**: the catch's protocol-state validation fails because the
**intermediate frame's cleanup (A) never runs** during the unwind (check_var chain 1→5→23; we deliver 1→7).

---

## 1. Baseline (protocol steps A–C)

- `git fetch`: origin/main == HEAD == `b843974` (GitHub-confirmed via `git ls-remote`; OWNER-001 push
  infrastructure verified working from CONTINUE #3).
- Reality check: clean build 0 errors; binary `0c275159…` byte-identical to state.md; regression **12/12**
  (test 10 = BUG-029 frontier expectation); loader.c `bab5ea86…`, pe.h `928a780c…` — all claims verified.
- Pre-task checkpoint: `checkpoints/checkpoint_pre_task-bug029_api005_20261015.zip`
  (sha256 `bc8617e6…`).

## 2. TASK-API-005 — EH hack isolation (ADR-010)

Both undocumented app-specific hacks are now flag-guarded; **nothing deleted** (history preservation rule):

| Hack | Location | Gate |
|---|---|---|
| EH_FIX (dispatcher LSDA override → fake catch at LP 0x401573) | `mw_RtlDispatchException` walk, ContinueSearch branch | `mw_eh_hacks_enabled() &&` prepended to the LSDA-parse condition |
| CXX-EH-FIX (load_pe byte patch RVA 0x2010C8 → 0x7f catch-all) | `load_pe` tail | whole block moved under `g_image_base && mw_eh_hacks_enabled()`; explicit skip-trace when disabled |

- Flag semantics: `MINWIN_EH_HACKS` set to a non-empty value other than `"0"` ⇒ ON; default **OFF**.
  Resolved once, cached, first-resolution traced (`[API-005] EH hacks …`).
- **Backward-compat proof:** `MINWIN_EH_HACKS=1` run reproduces the exact BUG-029 frontier —
  exit 134, stdout `terminate called after throwing an instance of 'unsigned long long'`,
  trace contains the EH_FIX dispatch-1 hijack lines
  (`evidence/master/bug030/upx_trace_hacks_on.json`, `upx_stdout_hacks_on.txt`).

## 3. Live re-run with hacks OFF — the frontier moved massively

Single dispatch (ExceptionCode=0x20474343 from the GCC raise helper 0x49d5b1), **no EH_FIX hijack,
no re-raise chain, no 'unsigned long long' machinery** — EXP-037 §7 P-A's core prediction confirmed
(the predicted LP address differed: the real flow targets the true catch LP, not the fake 0x401573):

```
[DISPATCH] === RtlDispatchException === (one dispatch total)
Frame[0] 0x49d5b1 (raise helper)        no handler
Frame[1] 0x4e0203 (wrapper)             no handler
Frame[2] 0x4e02d9 (thunk)               no handler
Frame[3] 0x401593 fn 0x401570 (thrower) personality 0xe0220 → ContinueSearch (real decision)
Frame[4] 0x4032f2 fn 0x4032c0 (catch owner) personality → GCC core → INSTALL
RtlUnwindEx(target_frame=est(F4), target_ip=0x40331c, ctx=&ER, exc=obj)
  target_func RVA=0x32c0 alloc_total=0x68 post_prologue_RSP=est−0x68   (BUG-027 decode correct)
→ landing pad 0x40331c resumed → UPX protocol machinery → UPX's own assertion → terminate → abort (134)
```

stdout: `Assertion failed: cxx_exception_handling_works (/home/mfx/code/github/upx/upx4/src/check/
dt_check.cpp: check_basic_cxx_exception_handling: 362)` + `terminate called without an active exception`.
**The PE is now executing its own startup self-test logic** — the assertion text is UPX's, not ours.

## 4. Ground truth: the catch protocol (reverse-engineered, all live-confirmed)

### 4.1 The catch owner function B (0x4032c0) and its landing pad 0x40331c

```
B:  push r12; push rbx; sub rsp,0x58            ; prologue total 0x68 (= alloc_total ✓)
    [rsp+0x2c] = 1                              ; check_var = 1
    [rsp+0x30] = 0x4e2778 (protocol descriptor)
    [rsp+0x38] = &check_var
    [rsp+0x40] = 3                              ; protocol addend
    call 0x401570                               ; A(thrower) — raise site 0x401592
…normal path: assert-print setup (0x4032fc: args incl. r8d=0x16a=362=line no.) …
LP 0x40331c: mov rbx,rdx                        ; ← RDX = catch SELECTOR
    lea rcx,[rsp+0x30]; mov r12,rax             ; ← RAX = exception object
    call 0x401550                               ; protocol step: check_var = 3 + 4*check_var
    sub rbx,1; jne 0x403355                     ; selector MUST be 1
    call 0x4dfce0                               ; TLS exception-chain enter
    cmp qword [rax],1; jne 0x403361             ; protocol state must be 1
    cmp dword [rsp+0x2c],0x17; jne 0x403361     ; check_var MUST be 23 (0x17)
    call 0x4dfef0; ret                          ; SUCCESS → self-test passes
```

### 4.2 The GCC core (0x4AFDB0) and the blob helpers (0x49d270/0x49d2b0/0x49d2c0/0x49d2d0)

The personality core 0x49d340 calls the GCC phase engine at **0x4AFDB0** through the wrapper's 5th
argument (`call [rsp+0xc0]`; wrapper 0xe0220 passes `lea rax,[rip−0x3047b]` = 0x4AFDB0). Established:

- Phase-1 call `(ecx=1, edx=1, r8=[obj], r9=obj, stack5=&blob)` scans the GCC call-site table
  (decoder 0x4af710 + uleb readers 0x4af8a0/0x4af960); on hit it stashes unwinder state in the
  exception header (`obj−0x24` dword = **selector**, `obj−0x18/−0x10/−0x08` = private_ fields) and
  returns 6 ("found, run phase 2"); not-mine returns 8.
- Phase-2 call `(ecx=1, edx=6, …)`: reads the selector back (`mov edi,[r9−0x24]`), then
  `0x49d270(blob,0,obj)` → `blob+0x10 = object`, `0x49d270(blob,1,sel)` → `blob+0x18 = selector`,
  `0x49d2c0(blob,lp)` → `blob+0x8 = landing pad`; returns 7 (INSTALL).
- Personality site A (0x49d4e6–0x49d528) copies the blob into the RtlUnwindEx call:
  `rcx=est(blob+0x8→rdx=LP), r8=&ER, r9=blob+0x10(object), [rsp+0x20]=ctx, [rsp+0x28]=…`, and
  **ER is upgraded to 4 parameters**: `NumberParameters=4`,
  `param = {object, est, landing_pad, selector}` (`[rbx+0x20/0x28/0x30/0x38]`).
- The bridge's `flags & 0x20` entry branch (0x49d440) does `ctx->Rdx(+0x88) = ER.param[3]` —
  independent confirmation that **param[3] is the selector destined for RDX**.
- dc-layout contract (empirical, disasm-verified): the core reads
  `LSDA = [dc+0x38]` (0x49d2d0), `func_start = dword[dc+0x10] + [dc+0x08]` (0x49d2e0) — i.e. the
  layout our dispatcher builds (`ImageBase@0x08, FunctionEntry@0x10, ContextRecord@0x28,
  HandlerData@0x38`) is exactly what this personality expects; blob[0] = `ctx->Rsp` via
  `[dc+0x28]+0x98`. (Whether this equals the OS' canonical DISPATCHER_CONTEXT is a
  generalization-audit question — recorded as a risk note in BUG-031.)

### 4.3 BUG-030 — resume did not deliver the selector in RDX

Our `mw_RtlUnwindEx` set `CTX.Rax = arg4` (object ✓) but left `CTX.Rdx = 0` (raise-time value from
`xor edx,edx`). The LP's `mov rbx,rdx; sub rbx,1; jne <fail>` therefore took the failure branch →
assert → terminate → abort. **Fix (fingerprint-guarded, protocol-driven — no app-specific constants):**

```c
if (er_nparams == 4 && er_params[1] == tgt_frame && er_params[2] == tgt_ip)
    *(uint64_t*)(ctx + CTX_Rdx) = er_params[3];   /* catch selector */
```

### 4.4 Post-fix verification (trace)

```
RtlUnwindEx: 4-param protocol ER (obj=0x2de2320 est=0x7ffcdf491d28 lp=0x40331c sel=1) -> CTX.Rdx=1
```

- Fingerprint matched live (`param[1]==tgt_frame && param[2]==tgt_ip`) — the protocol shape is
  confirmed on the real run, not just statically.
- Post-resume sequence changed: LP protocol check (TlsGetValue) → **selector branch taken**
  (`call 0x4dfce0` now executes — present in the trace) → then the protocol-state check fails →
  `0x4dfef0` (exception discard: `free(obj−0x60)`) → assert print → terminate → abort (134).

## 5. New frontier → BUG-031: the intermediate cleanup never runs

The catch validates a **protocol counter chain across two frames**:

1. B sets `check_var = 1`; A's frame holds `{descriptor, &check_var, 1}` (A received `&check_var` in rdx).
2. **A's cleanup landing pad 0x4015A5** (Frame[3] LSDA cs_lp=0x35 → 0x401570+0x35 ✓) runs
   `0x401550(&A_struct)` ⇒ `check_var = 1 + 4*1 = 5`, then `call 0x49d5c0` (continue-unwind protocol).
3. B's catch LP runs `0x401550(&B_struct)` ⇒ `check_var = 3 + 4*5 = 23 = 0x17` ✓ — the check exists
   precisely to prove that A's cleanup executed in between.

Our `mw_RtlUnwindEx` jumps straight to the target LP and **skips intermediate-frame cleanups**
(documented simplification in its comment block) ⇒ `check_var = 3 + 4*1 = 7 ≠ 0x17` → failure.
Additionally the TLS chain state (`cmp qword [rax],1` after `0x4dfce0`) is populated by the same
machinery — same root cause.

### 5.1 Windows/PE protocol map for the fix (from disassembly — input to BUG-031)

- Unwind-phase INSTALL at an intermediate frame ⇒ bridge re-raises
  `RaiseException(0x21474343, flags=1, 4 params {obj, est, LP, selector})` (0x49d3e9–0x49d413).
- Personality entry with `code == 0x21474343 && est == param[1]` ⇒ site-A' RtlUnwindEx
  `(est, param[2], &ER, obj, ctx, …)` (0x49d420 → 0x49d533) ⇒ resume the **cleanup** LP with
  `Rax=obj, Rdx=param[3]`.
- Cleanup LPs end with `call 0x49d5c0(obj)` ⇒ continue-unwind raise
  `0x120474343` (**low dword = 0x20474343** — matches the core's 0x20474343 compare!), 4 params
  `{?, est_target, LP_target, selector}` from `obj+0x18/0x20/0x28` (0x49d610–0x49d645) ⇒ the chain
  re-enters the same resume machinery for the next segment until the target frame's catch LP runs.
- **Design caution:** routing the re-raises through our naked RaiseException stub re-enters
  `mw_RtlDispatchException` whose walk starts from the C dispatcher stack (bridge → our
  `mw_RtlUnwindEx` C frame — no PE RUNTIME_FUNCTION) — the walk cannot traverse our C frames.
  Candidate designs (to be decided with an experiment): (a) the dispatcher itself honors
  `{0x21474343, 0x120474343}` 4-param records as resume commands (synthesized resume like
  mw_RtlUnwindEx: Rip=param[2], Rax=obj, Rdx=param[3], Rsp=param[1]−alloc(param[2])); or (b)
  mw_RtlUnwindEx performs the intermediate-cleanup walk directly (personality calls with
  EH_UNWINDING flags) and only then resumes the target LP.

## 6. Verification summary (protocol step E)

| Check | Result |
|---|---|
| Clean build | 0 errors; binary `ca7ce562…` (loader.c `9a52f1d6…`) |
| Regression | **12/12** (test 10 expectation evolved to the BUG-031 frontier with inline evidence comment: exit 134 + UPX dt_check assertion + terminate-without-active) |
| Hacks OFF re-run | single clean dispatch; UPX's own assertion reached; selector delivered (`sel=1`) |
| Hacks ON re-run | byte-identical BUG-029-era behavior restored (flag backward-compat) |
| EXP-037 P-A | core prediction confirmed (clean personality path, no re-raise/machinery); LP address prediction superseded by ground truth |

## 7. Falsifiable predictions for BUG-031

- P-C: executing A's cleanup LP (0x4015A5) with `Rax=obj, Rdx=selector, Rsp=est_A−0x48` and then
  resuming B's catch LP yields `check_var = 23` and the `cmp [rax],1` state passes (the chain was
  built to converge exactly there) → the catch returns normally → dt_check proceeds → next failure
  point is strictly later (or UPX prints its version — G1).
- P-D: if the TLS chain check (`[rax]==1`) still fails with the cleanup executed, the throw-side
  TLS state machine (0x4e0010/0x4e0080 slots) has a second dependency to isolate — same task, next
  trace.
- P-E: the `{0x21474343, 0x120474343}` resume commands can be honored inside the dispatcher without
  traversing our C frames (design (a)) — if the walk-from-C-stack problem re-appears in the trace,
  design (b) becomes mandatory.

## 8. Evidence index (this experiment)

| Artifact | Path |
|---|---|
| Hacks-OFF trace (sel delivered, post-resume path) | `evidence/master/bug030/upx_trace.json` |
| Hacks-OFF stdout/stderr | `evidence/master/bug030/upx_stdout.txt`, `upx_stderr.txt` |
| Hacks-ON backward-compat trace/stdout | `evidence/master/bug030/upx_trace_hacks_on.json`, `upx_stdout_hacks_on.txt` |
| Regression 12/12 | `evidence/master/bug030/regression_12of12.txt` |
| Hashes | `evidence/master/bug030/hashes.txt` |
| Pre-task checkpoint | `checkpoints/checkpoint_pre_task-bug029_api005_20261015.zip` (sha256 `bc8617e6…`) |
| Analysis scripts (external workspace) | `/home/z/my-project/scripts/exp038_frame4_landing_pad.py`, `exp038_helper_disasm.py`, `exp038_gcc_core_disasm.py` |
