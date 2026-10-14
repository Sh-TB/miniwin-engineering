# UPX FAILURE FORENSICS — Master Record

**Produced:** TASK-004, 2026-10-14, at HEAD `39181aa`
**Target:** `samples/upx_decompressed.exe` (UPX 4.2.4 win64, MinGW/GCC, sha256 `14e07e29...`, PE32+ ImageBase 0x400000, EP RVA 0x14f0, 10 sections, .pdata 3030 entries)
**Command:** `./minwin_loader samples/upx_decompressed.exe --version`
**Evidence files:** `evidence/master/api_trace_head_39181aa.json`, `evidence/master/upx_run_stdout.txt`, `evidence/master/upx_run_stderr.txt`, probe build artifacts described below (probe source was a /tmp copy; repo source untouched)

---

## 1. Observed Facts (fresh reproduction, this session)

| # | Fact | Evidence |
|---|------|----------|
| F1 | Exit code **134 (SIGABRT-equivalent)**; stdout ends with `terminate called after throwing an instance of 'unsigned long long'`; no version output | `upx_run_stdout.txt`; baseline run |
| F2 | **Two** RaiseException(0x20474343, flags=0, nargs=1, param0=exception obj) dispatches occur | trace `[DISPATCH] === RtlDispatchException ===` ×2 |
| F3 | Both dispatches end at Frame[0] with `RIP outside PE — walk ends`; reported RIP = `0x7ffdb4eb05e8` / `0x7ffdb4eb04c8` — stack addresses (RIP = RSP − 8 invariant in both) | trace |
| F4 | No frame walk, no RUNTIME_FUNCTION lookup, no handler call, no RtlUnwindEx occurs in the fresh build | trace (absence of `[DISPATCH] Frame[1..]`, `[HANDLER_CALL]`) |
| F5 | Exception object class = `0x474e5543432b2b00` (`\0++CUNG` = GCC "GNUCC++") at `0x3ed6320` | trace `[EXC_STATE]` |
| F6 | VEH chain = 0 handlers at dispatch time (UPX registered one earlier via AddVectoredExceptionHandler — registration not persisting/not reached) | trace `[DISPATCH] VEH chain: 0 handlers` |
| F7 | Failure terminal = PE-side libstdc++ terminate → `abort` import → `mw_abort` → `_exit(134)` (L2722-2727). `crash_handler` never fires (no real signal raised) | trace `[API] abort()`; source |
| F8 | **`g_cap_rip` == `g_cap_rsp_entry` (stack address)** while the true return address `[rsp_entry]` = **0x49d5b1** — probe run printed both; `match_rip_to_ret=0`, `match_rip_to_rspentry=1` for both dispatches | probe: /tmp/minwin_loader_probe (repo copy + one fprintf at `seh_dispatch_exception` entry, rebuilt with identical flags) |
| F9 | The real return address 0x49d5b1 matches the historically documented "exception site: RIP=0x49d5b1" — capture machinery otherwise intact | `.agent/state.md` cross-check |

## 2. Verified Root Cause (current failure — BUG-026)

**`mw_RaiseException` naked stub (L1697-1702) loads the wrong slot into `g_cap_rip`.**

Current source:
```asm
movq 0x58(%rsp), %rax        # rax = rsp_entry (leftover reload)
movq 0x00(%rsp), %rax        # ← loads rsp_entry (the saved stack pointer)
movq %rax, g_cap_rip(%rip)   # g_cap_rip = STACK ADDRESS
movq %rax, g_cap_rsp_entry(%rip)
```
Known-good reference (`loader.c.exp_next2_backup` L842-843, the build that produced the verified 5-frame walk evidence):
```asm
movq 0x08(%rsp), %rax        # ← loads the SAVED RETURN ADDRESS ([rsp_entry])
movq %rax, g_cap_rip(%rip)
```
Chain of causation: `CONTEXT.Rip` = stack address → `Frame[0]: RIP outside PE` → walk ends → dispatcher returns NOT_HANDLED → naked stub returns normally to PE → PE's `__gxx_personality` never consulted → libstdc++ `std::terminate` → `abort` → exit 134. This single operand explains F1–F4, F7, and both regression failures ("RUNTIME_FUNCTION lookup NOT found"; "exit 134 ≠ 139").

**Classification: regression introduced by the working-tree "fix broken inline assembly strings" edits (commit fdb939c era) and synced in d36fc5e.** The behavior documented in `.agent/state.md` (landing pad reached) predates this breakage and cannot be reproduced from current source — as suspected in `ENGINEERING_HISTORY_RECONCILIATION.md` (R-13).

**Rejected hypotheses for the CURRENT failure:**
- ❌ ASLR / image-base problem — PE maps at 0x400000 as intended (trace)
- ❌ Missing import / stub failure — 159/159 resolved (trace)
- ❌ .pdata corruption — 3030 entries parsed; lookups succeed when given valid RVAs (regression test history + unit tests)
- ❌ Exception-code mismatch — exception record fields all correct (trace)
- ❌ RtlUnwindEx / landing-pad defects — real but **unreachable** in the current build; not today's blocker

## 3. Historical Landing-Pad Crash (state.md era) — Reconstructed with Static Evidence

Although unreachable today, the post-fix path will re-enter this area. Static analysis of the recovered sample now explains the historical crash **better than the recorded hypotheses did**:

### 3.1 The landing pad (VA 0x40331c, function RVA 0x32c0–0x3368, RF[76])

```asm
40331c: mov %rdx,%rbx          ← selector (NOT "mov rbx,rdi" as state.md recorded)
40331f: lea 0x30(%rsp),%rcx    ← &cleanup-struct in THIS function's frame
403324: mov %rax,%r12          ← exception object (from RAX)
403327: call 0x401550          ← same helper the normal flow calls (0x4032f7)
40332c: sub $0x1,%rbx
403330: mov %r12,%rcx
403333: jne 0x403355           ← loop over remaining cleanups
```
RF[76] UNWIND_INFO (RVA 0x2011f0): ver=1, **flags=0x3 (E+UHANDLER)**, prolog=0x7, codes: ALLOC_SMALL 88 + PUSH RBX + PUSH R12 → **total prolog stack = 0x68**. Body RSP = entry-RSP − 0x68.

### 3.2 The crash site (function RVA 0x1550–0x156b, RF[9], no prolog)

```asm
401550: lea 0xe1221(%rip),%rax   # global 0x4e2778
401557: mov 0x10(%rcx),%edx
40155a: mov %rax,(%rcx)
40155d: mov 0x8(%rcx),%rax       # P = [rcx+8]
401561: mov (%rax),%r8d          # ← historical fault: deref P (RAX=0 → P was NULL)
401564: lea (%rdx,%r8,4),%edx
401568: mov %edx,(%rax)
40156a: ret
```

### 3.3 Arithmetic reconstruction (numbers from state.md)

- state.md: RtlUnwindEx set `CTX.Rsp = 0x7ffd2510e6d8` (= target frame = establisher frame), landing pad executed, crash at RSP `0x7ffd2510e6d0` = establisher − 8 (the `call 0x401550` push). **Numbers are self-consistent with pre-EXP-035 code.**
- Landing pad ran with RSP = **establisher** instead of **body RSP (establisher − 0x68)** → `lea 0x30(%rsp)` pointed **0x68 bytes too high** (caller's frame) → `[rcx+8]` = P read caller-frame garbage/zero → `mov (%rax),%r8d` faulted with RAX=0. ✓ matches state.md exactly.
- **H-002 (stack contents) was the closest hypothesis; H-001 ("NV registers not restored") is wrong** — RDI is not even consumed by this landing pad, and all 16 registers *are* mechanically restored; the defect was the RSP window.
- state.md's landing-pad listing (`mov rbx,rdi`) was a mis-transcription; the real first instruction is `mov %rdx,%rbx` (selector in RDX).

### 3.4 Status of EXP-035 (post_prologue_rsp = target_frame − alloc_total)

- Present in source (L1841-1928) but **never verified** (no experiment report; unreachable since the stub breakage).
- For THIS target function it would compute 0x68 correctly (no ALLOC_LARGE involved).
- **However**, the `alloc_total` loop (L1883-1918) contains the same **UWOP_ALLOC_LARGE ABI violations** found in the main unwinder (see §4) — wrong for any function with ALLOC_LARGE. And the deeper design question stands: `mw_RtlUnwindEx` ignores its 5th argument.

### 3.5 ABI finding: the ignored `OriginalContext` (design-level, verified by signature analysis)

Real `RtlUnwindEx` (6-arg, ms_abi): `RCX=TargetFrame, RDX=TargetIp, R8=ExceptionRecord, R9=ReturnValue, [RSP+0x28]=OriginalContext, [RSP+0x30]=HistoryTable`.
`mw_RtlUnwindEx(void* target_frame, void* target_ip, void* ctx_ptr, void* exc_record, void* history)` maps R8→"ctx_ptr" (actually ExceptionRecord), R9→"exc_record" (actually ReturnValue = exception object — consistent with how it's used), and **`history` = [RSP+0x28] = OriginalContext — the CONTEXT the MinGW personality prepared for the landing pad (contains selector in RDX and correct NV/stack state) — currently IGNORED** (the code uses global `g_unwind_ctx` instead). Adopting the passed OriginalContext (with sanity checks) is the architecturally correct path to landing-pad restoration and would also deliver the missing RDX selector (see §5).

## 4. NEW Latent Defects Discovered During Forensics (static, source-grounded)

| ID | Location | Defect | Impact |
|---|---|---|---|
| N-1 | ALLOC_LARGE handling ×4 copies: `seh_internal_virtual_unwind` L1067-1077, CHAININFO copy L968-975, RtlUnwindEx alloc_total L1889-1896, ms_abi export L2042+ | **info=0 missing ×8 scaling** (spec: 16-bit value × 8); **info=1 spurious `<<16`** (spec: 32-bit value in bytes, unscaled). NOTE: repo's own `docs/windows_abi/windows_x64_complete.md` L201-202 encodes the same wrong rule — doc bug propagated to code | Any function whose prolog uses ALLOC_LARGE unwinds with wrong RSP/register-restore windows. Not triggered by UPX's exercised frames (all ALLOC_SMALL) — latent. Correct value for RF[76]: both algorithms agree (0x68) only because no ALLOC_LARGE |
| N-2 | Dispatcher EH_FIX L1607-1644 | Undocumented LSDA override; does not distinguish CATCH vs CLEANUP actions (Frame[3] action 0x35 → per LSDA `ff ff 01 08 20 03 35 00`, TType_enc=0xff ⇒ **no type table ⇒ action can only be a cleanup chain**); computed landing pad LP=LPStart+3=0x401573 lands **inside the `sub $0x40,%rsp` instruction** (0x401572-0x401575) — mid-instruction jump | If ever triggered: corrupts cleanup semantics / jumps mid-instruction. Must be disabled or made spec-correct |
| N-3 | `load_pe` CXX EH FIX L3263-3287 | Patches target-binary byte at hardcoded RVA 0x20113C (inside Frame[3] function's LSDA action/TType area, currently `01`→ rewritten `0x7f` on every run) | App-specific binary patch of the target; undocumented; fires on EVERY run; corrupts any other PE with data at that RVA |
| N-4 | Landing-pad selector | `mw_RtlUnwindEx` never propagates the GCC selector into RDX (personality's `_Unwind_SetGR` #1). Landing pad `mov %rdx,%rbx; sub $1,%rbx; jne` will loop-branch on garbage | Next failure to appear after BUG-026 fix + reach landing pad; resolved by adopting OriginalContext (§3.5) or explicitly setting CTX.Rdx |
| N-5 | `mw_RtlUnwindEx` param mapping | 5-arg signature vs real 6-arg ABI; ctx_ptr actually receives ExceptionRecord (unused — OK by luck); ReturnValue actually lands in `exc_record` (works for C++ object passing — by design or luck) | Document; make explicit |

## 5. Next Best Experiment (distinguishes remaining hypotheses)

**EXP-036 (BUG-026 fix, one bounded task):** restore the stub operand (`movq 0x08(%rsp),%rax` before storing `g_cap_rip`), rebuild, re-run regression + UPX.
- Predicted outcomes: regression 12/12 restored (tests 8/10 pass again with exit 139); UPX proceeds past `terminate` into the documented dispatch path (5-frame walk, handler calls, RtlUnwindEx).
- This experiment converts all §3/§4 static predictions into live observations and isolates the landing-pad state question from the capture bug. It is minimal, reversible, and evidence-mandated (the correct operand is proven by F8 and by the exp_next2_backup reference).
- Follow-up experiments (EXP-037+, do NOT bundle): (a) verify EXP-035 arithmetic live; (b) disable EH_FIX + CXX-EH-FIX hacks behind env flags and compare; (c) wire OriginalContext/RDX selector propagation.

## 6. Evidence Index

| Artifact | Location |
|---|---|
| Fresh API trace (both aborts) | `evidence/master/api_trace_head_39181aa.json` |
| stdout/stderr of acceptance run | `evidence/master/upx_run_stdout.txt`, `upx_run_stderr.txt` |
| Baseline JSON (machine-readable) | `evidence/master/baseline_results.json` |
| Probe method | /tmp copy of loader.c + 1 fprintf at `seh_dispatch_exception` entry; compiled `gcc -O2 -g -Wall -Wno-unused-function -no-pie -Iinclude … -Wl,-Ttext-segment=0x2000000`; run → probe lines quoted in §1 F8 (no repo file modified) |
| Disassembly | objdump -b pei-x86-64 of recovered sample (§3.1, §3.2) |
| UNWIND_INFO decodes | Python parser, 12-byte RUNTIME_FUNCTION stride (§3.1, RF[9]/RF[10]/RF[76]) |
| LSDA byte dumps | §4 N-2/N-3 (0x201088, 0x2011fc, 0x20113c) |
