# EXP-036 — Live re-run after BUG-026 (g_cap_rip operand fix)

**Session:** 2026-10-14 (post-foundation CONTINUE #2)
**Task IDs:** TASK-BUG-026 (fix) + TASK-EXP-036 (live re-run) — closed together
**Baseline commit:** `08d94ac62fe9cfdb79d061b57d82ba3bc5c9d12d` (clean tree)
**Result commit:** see git log — `BUG-026: restore g_cap_rip capture operand`
**Verdict:** BUG-026 fix **VERIFIED** (12/12 regression; UPX progresses from
walk-never-starts to full 4-frame walk → handler → real RtlUnwindEx → landing-pad
resume). A **NEW, distinct failure** now exists at the post-unwind resume
(RSP off by +8 due to ALLOC_SMALL decode bug in `mw_RtlUnwindEx`; RDX selector not
propagated) → registered as **BUG-027**.

---

## 1. Baseline before edit (protocol step C)

| Item | Value |
|---|---|
| Commit | `08d94ac`, tree clean |
| loader.c sha256 | `502ae5880350aaa325c28c49291d96fc8bc0521d2cdd51da9edff15a410d1953` |
| include/pe.h sha256 | `928a780c688f5e4136a53caf2c22e3bb469f264570838fd2439c5f028291a9f2` |
| Clean build | OK, 0 errors; binary `afa7744c…291872B` — **byte-identical to baseline_results.json record** (reality check passed) |
| Regression suite | **10/12** — FAIL: "RUNTIME_FUNCTION lookup NOT found", "Exit code 134 (expected 139)" |
| Rollback path | git (`08d94ac`); change is 1-line operand, no zip checkpoint created (git IS the pre-checkpoint) |

## 2. Change (protocol step D — minimal)

`src/loader/loader.c` mw_RaiseException naked stub, "Copy to globals" block:

```asm
- movq 0x58(%rsp), %rax      ; stray reload of rsp_entry
- movq 0x00(%rsp), %rax      ; rsp_entry (stack address)
+ movq 0x08(%rsp), %rax      ; saved rip slot (return address)   [BUG-026 fix]
  movq %rax, g_cap_rip(%rip)
  movq 0x00(%rsp), %rax
  movq %rax, g_cap_rsp_entry(%rip)
```

Nothing else touched. EH_FIX, CXX-EH-FIX, RtlUnwindEx, unwind logic: untouched
(per task boundary). Post-fix loader.c sha256:
`e1caa5d7b0915750e49322c303cf144960245c7d95a840bb0df48c6c9f689864`.

**Side observation (recorded, NOT fixed — out of scope):** the four loads
`movq 0x30/0x38/0x40/0x48(%rsp), rcx/rdx/r8/r9` immediately above (comments
"save PE RCX/RDX/R8/R9") do not exist in `loader.c.exp_next2_backup` and do not
match the current frame layout (those offsets hold pushed RBP/R12/R13/R14, not
caller shadow space). Functionally benign today (dispatcher reads g_cap_* globals
from stack slots, and the SysV callee ignores those registers), but they are a
miscommented leftover of an older no-push stub variant. Cleanup candidate:
BUG-028 / API-016 (dead code) family.

## 3. Verification (protocol step E — exact outcomes)

### 3.1 Build
`make clean && make` → 0 errors; binary sha256 `c8472d04637550423e64d5cbb654b0b061de27f8c58f9a39f738cdd06ee09c75`.

### 3.2 Regression suite — **12/12 PASS** (was 10/12)

```
PASS: Entry point reached
PASS: Imports resolved (159 entries)
PASS: CRT init (_initterm) executed
PASS: __getmainargs executed
PASS: SetUnhandledExceptionFilter called
PASS: RaiseException(0x20474343) reached
PASS: .pdata parsed (3030 entries)
PASS: RUNTIME_FUNCTION lookup succeeded          <-- was FAIL
PASS: UNWIND_INFO parsed
PASS: Exit code 139 (SIGSEGV after unhandled exception)  <-- was FAIL (134)
PASS: malloc calls succeed
PASS: InitializeCriticalSection works
```

This matches the CURRENT_TASK.md prediction exactly and REVERSES the
reconciliation finding R-xx "regression 12/12 claim is stale for current source"
— 12/12 is now true again for the source tree (fresh build, not stale binary).

### 3.3 UPX acceptance run — exit 139 (was 134), NEW failure point

Command: `./minwin_loader samples/upx_decompressed.exe --version`
- stdout: loader banner only (no UPX `--version` output — acceptance G1 still NOT MET)
- stderr: `[CRASH] Signal 11 at RIP=0x2004167` — **outside the PE**
  (PE range 0x400000–0x613000); RSP=0x7ffe60f7dd48, RAX=0x3bc8320,
  RDX=0xfffffffffffffffe, RCX=0x0
- Exit: 139 (SIGSEGV), not 134 (SIGABRT/terminate) — the terminate path is gone

### 3.4 Live dispatch trace (what CHANGED vs pre-fix)

Pre-fix (EV-020): both RaiseException dispatches died at `Frame[0]: RIP outside PE`
because CONTEXT.Rip was a stack address.

Post-fix trace (`evidence/master/bug026/upx_trace.json`, EV-026):

1. **Dispatch 1** (throw from RIP=0x49d5b1 chain → Frame[3]=0x401593, handler
   0xe0220, lsda 0x601088): handler called, returned ContinueSearch (disp=1),
   then **EH_FIX overrode** and resumed at LP=0x401573 ("catching at LP").
2. PE catch machinery ran (TlsGetValue/realloc/fwrite/fputs "unsigned long
   long") and re-threw → **Dispatch 2** from RIP=0x49d5b1 (RSP=0x7ffe60f7de20).
3. **Dispatch 2 walk — FULLY CORRECT for the first time live:**
   - Frame[0] RIP=0x49d5b1 (RF 0x9d560–0x9d5bb) no handler → parent 0x49d680
   - Frame[1] RIP=0x49d680 (RF 0x9d670–0x9d68c) no handler → parent 0x4e017e
   - Frame[2] RIP=0x4e017e (RF 0xe0140–0xe0190) no handler → parent 0x4dccc1
   - Frame[3] RIP=0x4dccc1 (RF 0xdcbf0–0xdcdaf, ui 0x209420) **EHANDLER 0xe0220**
     lsda=0x609430 → personality called
4. Personality found the catch and **called RtlUnwindEx**:
   `RtlUnwindEx(target_frame=0x7ffe60f7def8, target_ip=0x4dcd24, ctx=0x7ffe60f7d810, exc=0x3bc8320)`
5. Our `mw_RtlUnwindEx` synthesized the landing-pad state:
   `target_func RVA=0xdcbf0 alloc_total=0x40 post_prologue_RSP=0x7ffe60f7deb8`,
   set `CTX.Rax=0x3bc8320`, longjmped back, stub restored g_unwind_regs, PE
   resumed at **RIP=0x4dcd24, RSP=0x7ffe60f7deb8**.
6. PE ran ~0x170 bytes deeper, then SIGSEGV at heap address 0x2004167.

## 4. Root cause of the NEW failure (static + live evidence)

### 4.1 Target 0x4dcd24 is the return address of the throw call

Disassembly around the target (`evidence/master/bug026/exp036_landing_pad_analysis.txt`):

```
0x4dcd1a: call 0x4a9558
0x4dcd1f: call 0x4a95a8        ; C++ throw helper
0x4dcd24: sub rdx, 1          ; <-- TargetIp lands HERE (return address of the call)
0x4dcd28: mov rcx, rax
0x4dcd2b: jne 0x4dcd8f        ; selector switch: RDX==1 -> inline catch body
0x4dcd2d: call 0x4dfce0
0x4dcd35: mov rax, [rax]
0x4dcd38: call [rax+0x10]     ; virtual call on exception object
```

So the catch resumes **inside function 0xdcbf0 with its frame intact** — the
correct RSP at resume is the RSP at the throw call site = the carried walk
context's RSP = `0x7ffe60f7deb0` (= Frame[3] entry CTX.Rsp).

### 4.2 Prologue of function 0xdcbf0 — correct decode = 0x48

Raw UNWIND_INFO @ RVA 0x209420: `19 09 04 00 | 09 52 | 05 30 | 04 c0 | 02 d0`.

Per winnt.h `UNWIND_CODE` (byte0=CodeOffset; byte1 low nibble=OpCode, high
nibble=OpInfo), slots decode as (array is reverse-execution order):

| Slot | Decode | Effect |
|---|---|---|
| `09 52` | ALLOC_SMALL OpInfo=5 | `sub rsp, (5+1)*8 = 0x30` @0x09 |
| `05 30` | PUSH_NONVOL OpInfo=3 | push RBX @0x05 |
| `04 c0` | PUSH_NONVOL OpInfo=0xC | push R12 @0x04 |
| `02 d0` | PUSH_NONVOL OpInfo=0xD | push R13 @0x02 |

Total = 0x18 (pushes) + 0x30 (alloc) = **0x48** — independently confirmed by
the walk itself: establisher `0x7ffe60f7def8` − entry `CTX.Rsp 0x7ffe60f7deb0`
= 0x48. Cross-checks on Frames 0/1/2 of the same dispatch all agree
(0x28/0x28/0x28 = est − ctx.rsp).

### 4.3 Our `mw_RtlUnwindEx` computed 0x40 — the +8 is missing

Trace: `alloc_total=0x40 post_prologue_RSP=0x7ffe60f7deb8`.
0x7ffe60f7def8 − 0x40 = 0x7ffe60f7deb8 ≠ correct 0x7ffe60f7deb0.
`0x40 = 0x18 + 0x28` ⇒ ALLOC_SMALL decoded as `OpInfo*8` (4*8=0x28) instead of
`(OpInfo+1)*8` (0x30). This is the **same missing-+8 defect family** recorded in
forensics §4 N-1 ("ALLOC_LARGE info=0 missing ×8") — it also affects
ALLOC_SMALL in the `mw_RtlUnwindEx` copy. Consequence: **resume RSP is 8 bytes
too high**; every local/frame-relative access in the landing pad is shifted;
the catch path reads garbage → indirect/virtual call → RIP=0x2004167 (heap).

### 4.4 RDX selector is not propagated (forensics N-4 confirmed live)

Landing pad does `sub rdx,1; jne` — RDX must carry the C++ selector (1 → inline
catch). At crash RDX=0xfffffffffffffffe, consistent with `sub rdx,1` executed
on a restored RDX of −1 (stale value), not selector=1. Our resume state takes
RDX from the stale Frame[3]-entry context / g_unwind_regs, not from the
personality-prepared ContextRecord (r8). Forensics §4 N-4/N-5 and ADR-008
predicted exactly this.

### 4.5 Architectural root: synthetic landing-pad state

`mw_RtlUnwindEx` ignores both the 5th (OriginalContext, [rsp+0x28]) and the
writable ContextRecord (r8) the personality provides; it rebuilds
RSP=establisher−alloc_total and RAX=ReturnValue instead. Correct Windows
semantics (ReactOS `RtlpUnwindEx` + MSVC personality): resume with the
**carried/passed ContextRecord** state (RSP from the walk = 0x7ffe60f7deb0,
RDX=selector, RAX=exception object), overriding only RIP=TargetIp. The
alloc_total synthesis is then unnecessary — and its decode bug (4.3) becomes
harmless for this path.

## 5. Task boundary decision

BUG-026's acceptance criteria are fully met (12/12 + "≥4 frames walked and at
least one handler call OR a new, distinctly-identified failure point"). The new
failure is a **different defect at a different site** (mw_RtlUnwindEx resume
state), registered as TASK-BUG-027 with a complete evidence chain. Per protocol
step G, no further code changes were made in this task.

## 6. Evidence index for this experiment

| Artifact | Path |
|---|---|
| Pre-fix regression (10/12) | recorded in §1; suite output also in EV-010-era baseline |
| Post-fix regression (12/12) | `evidence/master/bug026/regression_12of12.txt` |
| Post-fix UPX stdout/stderr | `evidence/master/bug026/upx_stdout.txt`, `upx_stderr.txt` |
| Post-fix UPX trace (JSON) | `evidence/master/bug026/upx_trace.json` |
| Landing-pad static analysis | `evidence/master/bug026/exp036_landing_pad_analysis.txt` |
| UNWIND_INFO raw decode | §4.2 above (also in analysis artifact) |
| Analysis script | `scripts/analysis`-style: `/home/z/my-project/scripts/exp036_landing_pad_analysis.py` (external workspace) |

## 7. Predictions for BUG-027 (falsifiable)

P-1: Resuming at RSP=0x7ffe60f7deb0 (not 0x7ffe60f7deb8) + RDX=1 + RAX=0x3bc8320
moves execution through `0x4dcd24→0x4dcd2d→…` catch body without the current
SIGSEGV; UPX proceeds further (new failure point later, or version output).
P-2: If instead only the +8 decode is fixed (alloc_total 0x48) but RDX stays
stale, execution takes the `jne 0x4dcd8f` branch (selector mismatch) — a
distinguishable crash/output. This separates the two sub-defects.
P-3: The EH_FIX dispatch-1 override remains active and will need isolation
(API-005) before any G-gate can pass honestly.
