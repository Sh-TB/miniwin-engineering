# EXP-039 — BUG-031 intermediate-cleanup chain: bisection, fix, and G1 acceptance

**Session:** CONTINUE #5 (2026-10-15)
**Baseline:** `24bb742` (origin/main == HEAD at session start), binary `ca7ce562…` byte-rebuilt
**Result:** **ACCEPTANCE G1 MET** — `./minwin_loader samples/upx_decompressed.exe --version`
prints the full UPX 4.2.4 banner and exits **0**, 5/5 deterministic runs, regression **12/12**
(test-10 expectation evolved to the success state).

---

## 1. Starting state

TASK-BUG-031 was READY with an **uncommitted 365-line WIP** in `src/loader/loader.c`
(ADR-011: throw-site CONTEXT snapshot `g_throw_ctx_snapshot`, GCC-bridge resume-command
intercept for `0x21474343`, `seh_total_prologue_alloc` + `seh_walk_intermediate_cleanups`
helpers, a continuation path in `mw_RtlUnwindEx`, raise-site volatile captures
`g_cap_rax/r10/r11`, and a TEMP `DCDUMP/ERDUMP` trace block). The interrupted session had
also poisoned 8 KB below RSP inside `call_handler_aligned` (the `rep stosq` block).

## 2. Reproduce-first findings (protocol §3)

| Build state | Exit | Frontier |
|---|---|---|
| WIP as found | 135 (SIGBUS) | crash_handler self-fault on poisoned RBP chain |
| WIP as found | 139 (10/10) | SIGSEGV at PE `0x49d34c` (`mov 0x4(%rcx),%eax`, RCX=`0xAAAA…`) |
| baseline `24bb742` | 134 (3/3) | dt_check assertion + terminate-without-active (BUG-031 frontier) |

## 3. First root cause — inline-asm early-clobber (proven from compiled code)

The poison block clobbers RDI/RCX/RAX **before** the ms_abi argument reloads, but the
constraints were `"=a"(disposition)` + clobbers `rcx,rdx,r8,r9,r10,r11` — so GCC legally
assigned INPUT operands to RDI and RAX:

- walk site (`seh_walk_intermediate_cleanups`): `mov %rax,%rcx` ⇒ **arg1 (ER) = poison**
  ⇒ SIGSEGV at core entry `0x49d34c`. Exact match with the live fault.
- dispatcher site: `est←RDI`, `ctx←RAX` both destroyed by the poison.

Fix: `"=&a"` (early-clobber output) + `"rdi"` clobber. After the fix the crash moved
(139 at PE `0x4af733`), so the poison was a second hazard — **E2 removed it entirely**.

## 4. The compile-dependent-behavior swamp (E1–E5 bisection)

With the poison gone the binary STILL failed, but *differently per recompile*:

| Exp | Δ vs previous | Exit | Frontier (normalized trace diff vs baseline) |
|---|---|---|---|
| E1 | volatiles reverted to CPU-state reads | 139 | crash `0x4af733` |
| E2 | poison disabled (constraints fix kept) | 139 | crash `0x4af733` |
| E3 | TEMP DCDUMP/ERDUMP removed | 139 | crash `0x4af733` |
| E4 | constraints reverted to baseline `"=a"` | 134 | **727 events**, Frame[4] phase-1 returns **ContinueSearch** (should be INSTALL) ⇒ old BUG-029 frontier (`terminate … 'unsigned long long'`) |
| E5 | stub g_cap stores + snapshot memcpy + intercept all neutralized (dead helpers only) | 134 | same 727-event wrong frontier |

Decisive observations:

1. **`0x4af733` fault anatomy** (`movzbl (%rsi),%ecx` in the GCC-core helper `0x4af710`,
   called from `0x4afebe`): RSI=`0xc400000090` — a garbage byte-stream cursor — while the
   same function's `mov %rdx,%rsi` guarantees RSI==RDX on the straight-line path, so the
   faulting path enters the decode loop from `0x4af7d0` with a cursor already garbage.
2. **Behavior flips with ANY loader source change** — even with every executing delta
   neutralized (E5) — because `seh_build_context`'s CPU-state reads
   (`__asm__ volatile("" : "=a"(rax))` etc.) return **the dispatcher's own register state**,
   which every recompilation reshuffles. The interrupted session's WIP comment had already
   diagnosed exactly this ("compiler-dependent garbage … flipped the GCC personality's
   phase-1 decision") — my E1 revert accidentally re-introduced that poison and masked the
   real picture until E4/E5 isolated it.
3. Therefore the *only* Windows-faithful fix is to build the CONTEXT from the **true
   raise-site values** (what RtlRaiseException snapshots): the stub already captures
   rcx/rdx/r8/r9 (+ now rax/r10/r11) before any scratch use.

## 5. Second root cause — caller-clobbered registers at the personality boundary

Even with raise-site CONTEXT restored (F1), the `"=&a"` builds crashed at `0x4af733`
while the `"=a"` builds answered wrong. The 4 ms_abi args are identical in both forms ⇒
the PE's GCC core depends on **rax/r10/r11 at the personality-call boundary**
(ms_abi-undefined, but what the Windows dispatcher leaves there is *live CPU state*).
GCC-garbage there differs per build ⇒ crash vs wrong-answer.

Fix (F2): `call_handler_aligned` now pins `rax/r10/r11` to the raise-site captures
(`g_cap_rax/g_cap_r10/g_cap_r11`) immediately before `call *%1` — the closest thing to
what the Windows dispatcher would leave in the live registers. Implementation note: the
first two attempts failed to compile because `%rip` inside an inline-asm template must be
written `%%rip` (single `%` = operand modifier → "operand number missing after
%-letter"); the values are therefore loaded with direct `g_cap_*(%%rip)` memory operands
and `rbp` is declared clobbered (the prologue writes it).

## 6. Final code state (BUG-031 fix, ADR-011)

- `call_handler_aligned`: no poison; `"=&a"` + clobbers `rcx,rdx,r8,r9,r10,r11,rdi,rbp`;
  rax/r10/r11 pinned to raise-site captures; 4 ms_abi args reloaded as before.
- `seh_build_context`: volatile GPRs from `g_cap_*` raise-site captures; non-volatiles
  from CPU state (unchanged).
- Dispatcher: GCC-bridge resume-command intercept (`code==0x21474343 && nparams==4 &&
  flags&NONCONTINUABLE && LP inside image && throw-site snapshot valid`) computes
  `resume_rsp = est − seh_total_prologue_alloc(LP)`, sets `g_unwind_regs` (RAX=obj,
  RDX=selector, rest from the throw-site snapshot) and returns CONTINUE_EXEC.
- `mw_RtlUnwindEx` continuation path: a cleanup LP's continue helper (`0x49d5c0`) calls
  RtlUnwindEx directly with the OriginalContext; we walk remaining intermediate cleanups
  from that context, then resume the target LP directly (no longjmp), delivering
  `CTX.Rdx = ER.param[3]` (BUG-030 selector rule) on that path too.
- TEMP DCDUMP/ERDUMP removed.

## 7. Live verification (evidence/master/bug031/)

Trace (`upx_trace_g1.json`, 943 events) shows the full chain:

```
[BUG-031] first walk: personality fn=0x4e0220 est=0x7ffe…ff08 rip=0x401593 lsda=0x601088 flags=0x2 (EH_UNWINDING)
[BUG-031] resume-cmd: 0x21474343 obj=0x2907320 est=0x7ffe…ff08 lp=0x4015a5 sel=0 alloc=0x48 -> RIP=0x4015a5 RSP=0x7ffe…fec0
[BUG-031] continuation unwind: orig_ctx=0x7ffe…f9c0
[BUG-031] cont walk: RIP 0x0 outside PE — walk ends
[BUG-031] continuation: 4-param ER obj=0x2907320 est=0x7ffe…ff78 lp=0x40331c sel=1 -> CTX.Rdx
[BUG-031] continuation resume: RIP=0x40331c RSP=0x7ffe…ff10 alloc=0x68
```

EXP-038 §7 predictions resolved:

- **P-C (cleanup chain converges)**: CONFIRMED — cleanup LP `0x4015a5` runs (check_var
  1→5), then B's catch LP `0x40331c` (check_var → 23), selector branch passes.
- **P-D (TLS state passes once cleanup ran)**: CONFIRMED — dt_check's
  `cxx_exception_handling_works` assertion no longer fires; the catch completes.
- **P-E (design (a) dispatcher-honored resume commands)**: CONFIRMED — the intercept path
  is what the live trace exercised; design (b) (walking with EH_UNWINDING from
  mw_RtlUnwindEx) remains implemented as the continuation walk but was not decisive.

Stdout (`upx_stdout_g1.txt`) — the full, PE-generated banner:

```
upx 4.2.4
NRV data compression library 0.84
UCL data compression library 1.03
zlib data compression library 1.3.1.1-motley
LZMA SDK version 4.43
Copyright (C) 1996-2024 Markus Franz Xaver Johannes Oberhumer
Copyright (C) 1996-2024 Laszlo Molnar
Copyright (C) 2000-2024 John F. Reiser
Copyright (C) 1995-2024 Jean-loup Gailly and Mark Adler
Copyright (C) 1999-2006 Igor Pavlov
UPX comes with ABSOLUTELY NO WARRANTY; for details type 'upx_decompressed -L'.
```

- Exit code **0**, 5/5 runs. stderr contains only the command-line trace line; no
  `[CRASH]` dump (crash_handler unused).
- Regression **12/12** (`regression_12of12.txt`); test-10 expectation evolved to the
  success state (exit 0 + `upx 4.2.4` + `UPX comes with ABSOLUTELY NO WARRANTY` + no
  crash dump) with the full frontier history preserved in comments.
- EXP-NEXT-2 harness: PASS (exit 0).
- `MINWIN_EH_HACKS=1` backward compat: exit 134 + `terminate called after throwing an
  instance of 'unsigned long long'` — byte-identical legacy BUG-029-era behavior.
- Binary sha256 `e0bdbdcfe574823f68589d925aff44a53a5ee4b7d63668501695c9bd689f94e8`;
  loader.c recorded in `hashes.txt`.

## 8. G1 scorecard (RELEASE_CRITERIA.md)

- [x] `upx 4.2.4` printed (byte-exact version line)
- [x] Exit status 0
- [x] No hidden crash (no [CRASH] in stderr)
- [x] No fake success: output produced by PE code through the runtime — the run
      exercised the real GCC SEH chain (raise → walk → phase-1 → bridge resume-command →
      cleanup LP → continuation unwind → catch LP → C++ runtime), not any loader shortcut
- [x] Evidence in `evidence/master/bug031/`

## 9. New falsifiable predictions

- **P-F (generalization)**: any mingw/GCC-SEH PE that throws across ≥2 frames with
  intermediate cleanups completes its unwind on MiniWin without sample-specific
  constants — testable with TASK-TEST-005's synthetic Tier-B set.
- **P-G (fragility class)**: with the raise-site pinning removed, phase-1 decisions flip
  per recompile (E4/E5 demonstrated). The regression suite + `hashes.txt` pin the
  verified binary; any loader.c change must re-run the G1 acceptance before claiming it.
- **P-H (hacks flag)**: `MINWIN_EH_HACKS=1` remains a faithful legacy snapshot and must
  stay OFF for G1; the flag-gate is now load-bearing documentation (ADR-010).

## 10. Honesty notes / limitations

- The `cont walk` ended immediately (`RIP 0x0 outside PE`) — the continuation walk's
  start context had a zero RIP in this run; the target LP was resumed directly and the
  outcome is correct for this target. Generalizing the continuation walk is part of
  TASK-BUG-033 (registered).
- rax/r10/r11 pinning encodes "what the PE's core reads" — observed, not Windows-spec'd;
  the values chosen are the raise-site captures (the most Windows-faithful available).
  A wider synthetic suite (TASK-TEST-004/005) must confirm no target dependence.
- ALLOC decode copies in `mw_RtlUnwindEx`/`mw_RtlVirtualUnwind` still carry the API-006
  deviation; `seh_total_prologue_alloc` is the corrected decoder. TASK-API-006 remains
  open and is now P1.
