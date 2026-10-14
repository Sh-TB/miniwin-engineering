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
