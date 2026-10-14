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
