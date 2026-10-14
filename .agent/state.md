# Engineering State — CANONICAL COMPACT SNAPSHOT

**Updated:** 2026-10-14 (TASK-005 session) — this file supersedes all prior state descriptions. Historical state (2026-08-15) is preserved in git history and was factually stale (see docs/master/ENGINEERING_HISTORY_RECONCILIATION.md R-13/R-14).

| Field | Value |
|---|---|
| Current HEAD (local) | `miniwin-master-foundation-001` (child of `39181aa9239216ce740b8d89ba990437f1d5e8ae`) |
| Origin/main | `39181aa...` (foundation commit NOT pushed — no credentials) |
| Source SHA-256 (loader.c) | `502ae5880350aaa325c28c49291d96fc8bc0521d2cdd51da9edff15a410d1953` (unchanged this session — no source edits yet) |
| Source SHA-256 (include/pe.h) | `928a780c688f5e4136a53caf2c22e3bb469f264570838fd2439c5f028291a9f2` |
| Build status | CLEAN — 0 errors, 24 warnings (gcc 14.2.0); binary `afa7744c…`, 291,872 bytes |
| Test status | Regression **10/12** (tests 8 & 10 fail — root cause known); EXP-NEXT-2 PASS; unwind units 35/35; dispatch tests BROKEN/VACUOUS; dalvik 24/24 (out-of-scope) |
| Last confirmed working checkpoint | `miniwin_checkpoint_bug023_recovery.zip` (2026-08 era, pre-dispatcher baseline); no NEW verified-working checkpoint yet |
| Current target | UPX 4.2.4 decompressed (`samples/upx_decompressed.exe`, sha256 `14e07e29…`) — last observed result: **exit 134, no output, "terminate called after throwing an instance of 'unsigned long long'"** |
| Exact current blocker | **BUG-026**: `mw_RaiseException` stub L1697-1702 loads `[rsp+0x00]` into `g_cap_rip` (stack address) instead of saved rip at `[rsp+0x08]` → CONTEXT.Rip outside PE → walk ends at Frame[0] → unhandled → terminate/abort. Probe-proven (forensics §1 F8) |
| Active task ID | TASK-BUG-026 (READY — not started; this session ended before implementation by design) |
| Last completed task ID | TASK-006 (foundation selection & handoff, this session) |
| Next task ID | TASK-BUG-026 → then EXP-036 |
| Blocking dependencies | none for BUG-026 (evidence complete) |
| Evidence links | docs/master/{REPOSITORY_BASELINE, ENGINEERING_HISTORY_RECONCILIATION, REPRODUCIBLE_BASELINE, EXECUTION_PIPELINE, UPX_FAILURE_FORENSICS, IMAGE_MAPPING_AND_RELOCATION_AUDIT, API_COMPATIBILITY_DATABASE, TESTING_STRATEGY, MASTER_PLAN}.md; evidence/master/* |
| Known repository inconsistencies | (1) historical docs claim 12/12 + exit 139 + landing-pad reached — NOT true for current source; (2) sample hash mis-stated as 254ac80d in BUG-001/NEW_AGENT_START (true: 14e07e29); (3) two undocumented EH hacks present in source (EH_FIX L1607-1644, CXX-EH-FIX L3263-3287); (4) import count discrepancies (157/162 historical vs 159 measured); (5) push to GitHub pending credentials |

**Rule honored:** no task is marked complete merely because code was edited. BUG-026 is diagnosed (VERIFIED root cause) but its fix is IMPLEMENTED=NO.
