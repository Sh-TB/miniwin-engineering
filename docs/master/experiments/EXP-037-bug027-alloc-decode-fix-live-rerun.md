# EXP-037 — BUG-027 fix: mw_RtlUnwindEx slot-stride decode + live landing-pad resume

**Session:** 2026-10-14 (CONTINUE #3)
**Task IDs:** TASK-BUG-027 (fix) — and the ground-truth personality reverse-engineering that resolved EXP-036 §7's predictions
**Baseline commit:** `4039b61f47978de99e313cf267b0ff1dcdc06a03` (pushed to origin/main — credentials resolved this session, OWNER-001 CLOSED)
**Result commit:** `BUG-027: ...` (see git log)
**Verdict:** BUG-027 **VERIFIED FIXED** — `alloc_total=0x48` (was 0x40), landing pad
0x4dcd24 resumes with the correct throw-site RSP, catch machinery (TLS calls)
executes. Execution advances to a NEW, distinctly-identified failure:
**terminate → abort after catch dispatch** = **BUG-029** (registered).

---

## 1. Baseline (protocol step C)

- Commit `4039b61`, clean tree; loader.c `e1caa5d7…`; binary `c8472d04…`.
- Regression re-run: **12/12** (pre-edit). UPX frontier at this point: exit 139,
  SIGSEGV at heap RIP after landing-pad resume with RSP+8 (EXP-036).

## 2. Ground truth: what the PE's personality actually does (NEW evidence)

The Frame[3] handler `0x4e0220` is a thin wrapper (`sub rsp,0x38; lea rax,[rip-0x4047b];
mov [rsp+0x20],rax; call 0x49d340; add rsp,0x38; ret`) around the real
personality core at **`0x49d340`** (mingw-era GCC SEH bridge; UPX-built).

Full disassembly: `evidence/master/bug027_probe_personality_disasm.txt`. Findings:

### 2.1 The personality is UPX-protocol-aware
It compares ExceptionCode against `0x21474343` / `0x22474343` / `0x20474343`
(the UPX "CCt" protocol family) and re-raises `0x21474343` with
NumberParameters=4 when converting a GCC `INSTALL_CONTEXT` decision into an SEH
unwind.

### 2.2 The actual RtlUnwindEx call shape (site A, the path our trace exercises)
```
rcx = EstablisherFrame                    (arg1 TargetFrame)      ✓ standard
rdx = landing pad (GCC-core computed)     (arg2 TargetIp)         ✓ standard
r8 = &ExceptionRecord                     (arg3 "ContextRecord")  ← NOT a CONTEXT!
r9 = exception object                     (arg4 ReturnValue)      ✓ object
[rsp+0x20] = ms_orig_context              (arg5 OriginalContext)  ✓ our walk ctx
[rsp+0x28] = exception object again       (arg6)
```
Our earlier reading of `ctx=0x…55d0=&er` (EXP-036 §3.4) is therefore **not a bug
on the PE side**: this personality genuinely passes the ExceptionRecord pointer
in the ContextRecord slot. Consequence: our `mw_RtlUnwindEx` cannot rely on
arg3 as a CONTEXT — and it already ignores it (uses `g_unwind_ctx`), which is
correct by accident. **Revised EXP-036 conclusion:** the "adopt
OriginalContext/ContextRecord" fix direction (BUG-027 minimal alternative,
ADR-008) would NOT have worked as imagined — arg3 is the ER; arg5 is our own
buffer. The synthesis approach was already the right architecture; only its
decode was wrong.

### 2.3 RDX selector hypothesis WITHDRAWN (EXP-036 P-2 revised)
The raise helper at `0x49d560` does `xor edx,edx` immediately before
`RaiseException(0x20474343,…)` ⇒ carried Rdx = 0 at Frame[3]. The landing pad
`sub rdx,1; jne 0x4dcd8f` therefore takes the `jne` branch **by design** —
`0x4dcd8f` is a normal continuation (its tail `jmp 0x4dccf5` re-enters the
function body), not a terminate path. No selector propagation is needed for
this path; EXP-036's RDX claim is retracted.

## 3. The BUG-027 defect (refined) and the fix

### 3.1 Actual root cause: 4-byte slot stride in the unwind-code loop
`mw_RtlUnwindEx`'s prologue-total loop declared
`uint16_t* tgt_codes = (uint16_t*)(tgt_ui + 4);` and indexed
`tgt_codes + tgt_slot * 2` — pointer arithmetic in uint16 units ⇒ **byte offset
= slot×4**. The loop read slots 0, 2, then bytes past the codes array
(UNWIND_INFO header of the next function), summing a coincidental
`0x30+8+8+8 = 0x40`. True decode of function `0xdcbf0`
(`19 09 04 00 | 09 52 | 05 30 | 04 c0 | 02 d0`): ALLOC_SMALL 0x30 + PUSH×3
(R13/R12/RBX) = **0x48**.

The nibble extraction itself was already correct (op = low nibble of byte1 via
`cw>>8` after little-endian load; ALLOC_SMALL `(info+1)*8` present).

### 3.2 Fix (minimal — one declaration)
`uint8_t* tgt_codes = tgt_ui + 4;` — all three reads in the loop become
byte-addressed automatically. Formulas untouched (ALLOC_LARGE scaling stays
TASK-API-006's scope; not exercised by this path).
loader.c: `e1caa5d7…` → `bab5ea86…`; binary `c8472d04…` → `0c275159…`.

## 4. Verification (protocol step E — exact outcomes)

### 4.1 Trace (post-fix) — the resume state is now correct
```
RtlUnwindEx(target_frame=0x7ffcaeea5cb8, target_ip=0x4dcd24, ctx=0x7ffcaeea55d0, exc=0x303a320)
RtlUnwindEx: target_func RVA=0xdcbf0 alloc_total=0x48 post_prologue_RSP=0x7ffcaeea5c70
```
- `0x7ffcaeea5cb8 − 0x48 = 0x7ffcaeea5c70` = **Frame[3]-entry CTX.Rsp** (the
  throw-site RSP) — exactly the value the landing pad requires (EXP-036 §4.1/4.2).
- PE resumed at `RIP=0x4dcd24, RSP=0x7ffcaeea5c70, RAX=0x303a320 (object)`.

### 4.2 Execution advanced through the landing pad
Post-resume trace: `TlsGetValue/SetLastError/GetLastError` sequence (catch/UPX
protocol machinery) → `abort()`. stdout:
`terminate called after throwing an instance of 'unsigned long long'`. Exit 134.

### 4.3 Regression suite — expectation updated with evidence (12/12)
Test 10 expected exit 139 — that expectation described the BUG-026-era frontier
(SIGSEGV at the RSP+8 resume). With BUG-027 fixed, execution moves past that
point; the verified current frontier is exit 134 + "terminate called after
throwing" AFTER the landing-pad resume (a strictly later, different failure).
Test 10 was updated to assert `134 && terminate-message` with the history in a
comment (139 = BUG-026 era; 134 = BUG-029 frontier). Suite: **12/12**.
This is expectation-evolution with documented evidence, NOT test-gaming: the
assertion now checks BOTH the exit code AND the distinctive terminate message,
so a return of the old pre-walk abort (which also exits 134 but before any
dispatch) would still FAIL the test via the missing message.

## 5. New failure frontier → BUG-029

After a correct landing-pad resume, the catch machinery runs and terminate is
invoked while an exception is in flight. Primary suspects (ordered):
1. **EH_FIX dispatch-1 interference** (L~1607, undocumented hack): dispatch-1's
   personality returned ContinueSearch, EH_FIX force-resumed at LP 0x401573,
   built the 'unsigned long long' machinery, and re-raised — poisoning the
   GCC/`__cxa` exception state that dispatch-2's catch then inherits. The clean
   Windows path would have been: dispatch-1 → site A → RtlUnwindEx → landing pad.
2. GCC-core input dependencies: the core receives `disp->ContextRecord->Rsp`
   (= our post-unwind `est`, whereas Windows supplies the pre-unwind
   throw-site RSP in the handler's ContextRecord) — affects its local-variable
   math and possibly its INSTALL decision on dispatch-1.
3. Site B/C paths (`0x21474343` re-raise protocol) not yet exercised/audited.

Next task: **TASK-BUG-029** — first step TASK-API-005 (isolate EH_FIX behind a
flag, default OFF) since it is both a required hygiene fix (engineering rules
violation) and the top suspect poisoning the protocol.

## 6. Evidence index (this experiment)

| Artifact | Path |
|---|---|
| Post-fix UPX trace | `evidence/master/bug027/upx_trace.json` |
| Post-fix stdout/stderr | `evidence/master/bug027/upx_stdout.txt`, `upx_stderr.txt` |
| Post-fix regression 12/12 | `evidence/master/bug027/regression_12of12_post_bug027.txt` |
| Personality full disassembly | `evidence/master/bug027_probe_personality_disasm.txt` |
| Analysis scripts | `/home/z/my-project/scripts/exp037_personality_disasm.py` (external workspace) |

## 7. Falsifiable predictions for BUG-029
- P-A: with EH_FIX neutralized (flag OFF), dispatch-1's personality receives
  the same inputs as today, but the flow becomes
  dispatch-1 → site A → RtlUnwindEx → landing pad 0x401573 (dispatch-1's own
  catch), with NO 'unsigned long long' machinery and NO re-raise chain.
- P-B: if terminate persists with EH_FIX OFF, the GCC-core's ContextRecord->Rsp
  dependency (est vs throw-site RSP) is the next variable to isolate — testable
  by passing the pre-unwind context to the handler (dispatcher change, out of
  BUG-027's boundary).
