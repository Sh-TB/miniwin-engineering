# EVIDENCE INDEX — machine-readable pointers

Format: `ID | path | what it proves | session`

## Repository & integrity
- EV-001 | docs/master/REPOSITORY_BASELINE.md | HEAD/branch/visibility/inventory observed at 39181aa | 2026-10-14
- EV-002 | knowledge-transfer/evidence/hashes/checkpoint_hashes_20260806.txt | independent hash manifest (sample=14e07e29, loader=254ac80d) | historical
- EV-003 | checkpoints/*.zip (6) | recoverable history incl. samples/upx_decompressed.exe | historical

## Baseline (TASK-002)
- EV-010 | evidence/master/baseline_results.json | machine-readable build+test results | 2026-10-14
- EV-011 | evidence/master/build_log_head_39181aa.txt | clean build, 0 errors, warning inventory | 2026-10-14
- EV-012 | evidence/master/dispatch_test_logs/ | test_a SIGSEGV + vacuous b/c/d outputs | 2026-10-14
- EV-013 | evidence/master/exp_next2_results_fresh.txt | EXP-NEXT-2 PASS (source build) | 2026-10-14
- EV-014 | evidence/master/unwind_unit_results_fresh.txt | 35/35 (source rebuild) | 2026-10-14
- EV-015 | evidence/master/dalvik_frame_results_fresh.txt | 24/24 (out-of-scope preserve) | 2026-10-14

## UPX failure (TASK-004)
- EV-020 | evidence/master/api_trace_head_39181aa.json | both dispatches end Frame[0] RIP-outside-PE; 159 imports; CRT flow | 2026-10-14
- EV-021 | evidence/master/upx_run_stdout.txt | "terminate called after throwing an instance of 'unsigned long long'" | 2026-10-14
- EV-022 | evidence/master/upx_run_stderr.txt | loader trace | 2026-10-14
- EV-023 | docs/master/UPX_FAILURE_FORENSICS.md §1 F8 | probe proof g_cap_rip==rsp_entry; real ret 0x49d5b1 | 2026-10-14
- EV-024 | UPX_FAILURE_FORENSICS.md §3 | disassembly 0x401550/0x40331c; RF[76] UNWIND_INFO decode (0x68); crash arithmetic | 2026-10-14
- EV-025 | UPX_FAILURE_FORENSICS.md §4 | LSDA byte dumps 0x201088/0x2011fc/0x20113c; ALLOC_LARGE defects; OriginalContext ABI mapping | 2026-10-14

## Reconciliation (TASK-001)
- EV-030 | docs/master/ENGINEERING_HISTORY_RECONCILIATION.md | 34 claims R-01..R-34 with statuses; contradictions | 2026-10-14

## Provenance notes
- The forensic probe (EV-023) was built from a /tmp copy of loader.c with one added fprintf; repo source untouched (verified by git status at commit time).
- Prebuilt committed ELFs (working-tree/tests/**) were never used as PASS evidence; all results are source builds.

## BUG-026 fix + EXP-036 (CONTINUE #2, 2026-10-14)
- EV-026 | evidence/master/bug026/upx_trace.json | post-fix live trace: 4-frame walk completes; personality calls RtlUnwindEx(0x4dcd24); landing-pad resume then crash | 2026-10-14
- EV-027 | evidence/master/bug026/regression_12of12.txt | regression 12/12 PASS on fresh post-fix build | 2026-10-14
- EV-028 | evidence/master/bug026/upx_stderr.txt | crash dump: SIGSEGV RIP=0x2004167 (heap, outside PE), RSP=0x7ffe60f7dd48, RDX=0xfffffffffffffffe | 2026-10-14
- EV-029 | evidence/master/bug026/exp036_landing_pad_analysis.txt | disasm around 0x4dcd24 (landing pad = return addr of throw call); Frame[3] region | 2026-10-14
- EV-030 | docs/master/experiments/EXP-036-bug026-gcap-rip-fix-live-rerun.md | full experiment: fix + verification + new-failure root cause (§4) + predictions (§7) | 2026-10-14
- EV-031 | evidence/master/bug026_results.json | machine-readable post-fix results (12/12, exit 139, BUG-027 pointer) | 2026-10-14
- EV-032 | docs/bugs/BUG-027.md | BUG-027 record: RSP +8 (ALLOC_SMALL +8 missing in mw_RtlUnwindEx) + RDX selector not propagated | 2026-10-14

## BUG-027 fix + EXP-037 + OWNER-001 (CONTINUE #3, 2026-10-14)
- EV-033 | evidence/master/bug027/upx_trace.json | post-fix trace: alloc_total=0x48, post_prologue_RSP=est−0x48 (= Frame[3]-entry RSP), landing pad 0x4dcd24 resumed, catch TLS calls, abort | 2026-10-14
- EV-034 | evidence/master/bug027_probe_personality_disasm.txt | personality ground truth: site A passes r8=&ExceptionRecord (NOT a CONTEXT); raise helper xor edx,edx; landing-pad jne branch is normal | 2026-10-14
- EV-035 | evidence/master/bug027/regression_12of12_post_bug027.txt | 12/12 with test-10 expectation evolved to verified frontier (134 + terminate-message) | 2026-10-14
- EV-036 | docs/master/experiments/EXP-037-bug027-alloc-decode-fix-live-rerun.md | full report: stride root cause, retraction of EXP-036 §4.4/4.5 claims, BUG-029 candidates | 2026-10-14
- EV-037 | docs/bugs/BUG-029.md | new frontier record: terminate after correct landing-pad resume | 2026-10-14

## API-005 + BUG-030 + BUG-031 (CONTINUE #4, 2026-10-15)
- EV-038 | evidence/master/bug030/upx_trace.json | hacks-OFF clean path: single dispatch, 4-param ER fingerprint matched (sel=1 -> CTX.Rdx=1), selector branch taken, then protocol-state failure | 2026-10-15
- EV-039 | evidence/master/bug030/upx_trace_hacks_on.json + upx_stdout_hacks_on.txt | MINWIN_EH_HACKS=1 backward-compat: legacy BUG-029 behavior byte-identical ('unsigned long long' + EH_FIX hijack lines) | 2026-10-15
- EV-040 | evidence/master/bug030/upx_stdout.txt | UPX's own dt_check assertion reached: "Assertion failed: cxx_exception_handling_works (...: 362)" + "terminate called without an active exception" | 2026-10-15
- EV-041 | evidence/master/bug030/regression_12of12.txt | 12/12 with test-10 expectation evolved to BUG-031 frontier (inline evidence comment) | 2026-10-15
- EV-042 | docs/master/experiments/EXP-038-api005-eh-hacks-bug030-selector.md | full experiment: API-005 gates, GCC catch protocol ground truth (core 0x4AFDB0, selector at obj-0x24, 4-param ER), BUG-030 fix+verify, BUG-031 protocol map + predictions | 2026-10-15
- EV-043 | docs/bugs/BUG-030.md (closed) + docs/bugs/BUG-031.md (open) | defect records: selector delivery (fixed) and intermediate-cleanup chain (next) | 2026-10-15
- EV-044 | checkpoints/checkpoint_pre_task-bug029_api005_20261015.zip | pre-task checkpoint (sha256 bc8617e6...) | 2026-10-15

## BUG-031 fix + G1 ACCEPTANCE (CONTINUE #5, 2026-10-15)
- EV-045 | evidence/master/bug031/upx_trace_g1.json | G1 trace (943 events): first walk (EH_UNWINDING) → resume-cmd 0x21474343 (lp=0x4015a5 alloc=0x48) → continuation unwind → 4-param selector delivery (sel=1→CTX.Rdx) → catch LP 0x40331c (check_var 1→5→23) → PE banner | 2026-10-15
- EV-046 | evidence/master/bug031/upx_stdout_g1.txt | full UPX --version banner (upx 4.2.4 + libraries + copyrights + no-warranty), exit 0, 5/5 deterministic | 2026-10-15
- EV-047 | evidence/master/bug031/regression_12of12.txt | 12/12 with test-10 evolved to the SUCCESS state (exit 0 + banner + no crash) | 2026-10-15
- EV-048 | evidence/master/bug031/hashes.txt | pinned binary sha256 e0bdbdcf… + loader.c hash | 2026-10-15
- EV-049 | docs/master/experiments/EXP-039-bug031-cleanup-chain-g1.md | full report: reproduce-first table, early-clobber proof, E1–E5 bisection (compile-dependent phase-1 flip), rax/r10/r11 boundary pinning, G1 scorecard, P-F/P-G/P-H | 2026-10-15
- EV-050 | docs/bugs/BUG-031.md (CLOSED) + docs/master/ARCHITECTURE_DECISIONS.md ADR-011 | defect closure + architecture decision (resume commands + raise-site register fidelity) | 2026-10-15
- EV-051 | checkpoints/checkpoint_pre_task-bug031_20261015.zip | pre-task checkpoint from the interrupted session (WIP origin) | 2026-10-15

## appcompat sweep (CONTINUE #6, 2026-10-15)
- EV-052 | evidence/master/appcompat/run-20261015_053015/summary.csv | final sweep: t01..t07 PASS, t08 CRASH (BUG-037), t09 CRASH (wave-3 APIs) | 2026-10-15
- EV-053 | evidence/master/appcompat/run-20261015_044710/summary.csv | FIRST sweep: 0/9 PASS — every PE died at `call [iat]` on unresolved CRT startup imports (RIP=raw RVA 0x92f8 signature) | 2026-10-15
- EV-054 | docs/master/experiments/EXP-040-appcompat-sweep.md | full report: 8-stage fix trail, findings APP-F-01..08, BUG-037 ground truth + fix plan, wave-3 API list (44) | 2026-10-15
- EV-055 | docs/bugs/BUG-032.md | iob FILE* stride-0x30 window; setvbuf(stream=base+0x60) live evidence | 2026-10-15
- EV-056 | docs/bugs/BUG-033.md | printf ms->SysV repack; float crash + integer-luck explanation | 2026-10-15
- EV-057 | docs/bugs/BUG-034.md | CS 40-byte contract; dtoa freelist[0]=1 corruption at CS@0x14000b178/freelist@+0x28 | 2026-10-15
- EV-058 | docs/bugs/BUG-035.md | qsort comparator ABI; cmp_int NULL deref (RCX=0) | 2026-10-15
- EV-059 | docs/bugs/BUG-036.md | jmp_buf ownership; t07 longjmp(val=1) infinite loop trace | 2026-10-15
- EV-060 | docs/bugs/BUG-037.md | OPEN: unwind interpreter fault (R9=0x27) on UI RVA 0x50458; handler/disposition trace preceding it | 2026-10-15
- EV-061 | evidence/master/appcompat/run-20261015_053015/t08_big_cpp_stl.stderr + api_trace | C++ EH machinery live: personality called, disposition=1, frames 2->3 walked, then loader fault | 2026-10-15
- EV-062 | tests/appcompat/ (t01..t09 + run_sweep.sh) | reproducible sweep suite; every program self-verifying with deterministic exit codes | 2026-10-15

## CONTINUE #7 (2026-10-15)
- EV-041a: t08 UI@0x50458 raw bytes + reference decode (13 slots, END-offsets,
  handler 0x131b0@+32) — scripts/bug037_ref_decode.py output; root cause 1/2/3.
- EV-041b: standalone alloc harness reproduce 0xff0191 → fixed 0x188 —
  scripts/bug037_alloc_harness.c (CountOfCodes=slots proof).
- EV-041c: full LSDA decode of t08 main frame — 73 entries; throw region
  [0x806,0x829) lp=0x82d → VA 0x1c9d action=5; NO entry with lp=0x813 —
  scripts/bug037_lsda_parse.py (BUG-038 ground truth).
- EV-041d: __p__* function-vs-data crash signatures: __p__fmode=0x2039038,
  __p__commode=0x2039034, __p___argc=0x2039050 (IAT value used as call target).
- EV-041e: map_crt_stream fallback bug repro: /tmp/snapt.exe r=5 buf=[] →
  r=5 buf=[3.142] after pass-through fix.
- EV-041f: G1 pins: evidence/master/g1/20261015_{063340,064317,073727}/.
- EV-041g: sweeps: evidence/master/appcompat/run-20261015_{071313,071634,073729}/
  (final 10/12: t01..t07, t10, t11, t12 PASS).
- EV-041h: toolchain re-download sha/size 87,950,564 B (llvm-mingw 20240917 ucrt).

---
## EV-ISS1 (2026-10-15, updated post-merge) — Issue #1 Phase 0/1 slice
- evidence/master/issue1_final_*/ — final 3× runs on MERGED loader (sha256 0630d3ce…): upx/t09/t10/t11/t12 exit 0 ×3 byte-identical; t08 exit 1 ×3 (159/255, deterministic)
- evidence/master/appcompat/run-20261015_091659/ — merged-tree sweep: 10/12 PASS (t08 FAIL, t09_big_cpp_iostream TIMEOUT — both C++ EH family, BUG-038 lineage)
- artifacts/audit/repository_state.md · artifacts/audit/issue_reconciliation.md · artifacts/results/issue1_phase1_manifest.md — Phase-0/1 audit + reconciliation + manifest
- tests/corpus/ — 5 recreated Phase-1 fixtures + 2 diagnostic probes (SHA256SUMS committed)
- tests/appcompat/*.exe — rebuilt sweep binaries (sources pre-existed; binaries not committed upstream)
- KEY SEMANTIC CONTRACTS discovered this slice (do not regress):
  1) fopen MUST return MSVCRT-strided registry handle (guest CRT does InitializeCriticalSection(fp+0x30)); raw glibc FILE* → heap corruption SIGSEGV
  2) real ungetc via glibc on mapped stream (stub lost pushback → fscanf off-by-one per token)
  3) errno mirror (g_errno_storage) must sync on fopen/remove failure (guest _errno path)
