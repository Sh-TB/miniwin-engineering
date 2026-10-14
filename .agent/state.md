# Engineering State — CANONICAL COMPACT SNAPSHOT

**Updated:** 2026-10-14 (BUG-026/EXP-036 session — CONTINUE #2). Supersedes the
prior snapshot; prior versions remain in git history.

| Field | Value |
|---|---|
| Current HEAD (local) | `BUG-026` commit (child of `08d94ac`, which is child of `39181aa`) — see `git log --oneline -3` |
| Origin/main | `39181aa...` (push still pending credentials; now 2 local commits ahead) |
| Source SHA-256 (loader.c) | `e1caa5d7b0915750e49322c303cf144960245c7d95a840bb0df48c6c9f689864` (BUG-026 operand fix applied) |
| Source SHA-256 (include/pe.h) | `928a780c688f5e4136a53caf2c22e3bb469f264570838fd2439c5f028291a9f2` (unchanged) |
| Build status | CLEAN — 0 errors; binary `c8472d04…` (post-fix) |
| Test status | Regression **12/12** (restored by BUG-026); EXP-NEXT-2 PASS; unwind units 35/35; dispatch tests BROKEN/VACUOUS (TEST-004 pending); dalvik 24/24 (out-of-scope) |
| Current target | UPX 4.2.4 decompressed (`samples/upx_decompressed.exe`, sha256 `14e07e29…`) — current result: **exit 139, SIGSEGV at heap RIP=0x2004167 AFTER a full 4-frame walk + real RtlUnwindEx + landing-pad resume** |
| Exact current blocker | **BUG-027**: `mw_RtlUnwindEx` synthesizes the landing-pad CONTEXT — RSP = establisher − alloc_total where ALLOC_SMALL decode misses `+8` (0x40 vs true 0x48 → RSP 0x7ffe60f7deb8 instead of 0x7ffe60f7deb0), and RDX (C++ selector) is not propagated from the personality's ContextRecord. Live-proven in EXP-036 §4 |
| Active task ID | TASK-BUG-027 (READY — evidence complete, fix designed, not implemented) |
| Last completed task ID | TASK-BUG-026 + TASK-EXP-036 (this session, closed together) |
| Next task ID | TASK-BUG-027 |
| Blocking dependencies | none for BUG-027 |
| Evidence links | docs/master/experiments/EXP-036-bug026-gcap-rip-fix-live-rerun.md; docs/bugs/BUG-026.md; docs/bugs/BUG-027.md; evidence/master/bug026/*; evidence/master/bug026_results.json |
| Known repository inconsistencies | (1) regression 12/12 is TRUE AGAIN as of this session (fresh build) — historical docs and this file's earlier "stale" note both superseded; (2) sample hash mis-stated as 254ac80d in BUG-001/NEW_AGENT_START (true: 14e07e29); (3) two undocumented EH hacks still in source (EH_FIX L~1607, CXX-EH-FIX L~3263) — EH_FIX override fired live during dispatch-1 (EXP-036 §3.4); (4) push to GitHub pending credentials; (5) dead register loads in RaiseException stub (BUG-028 candidate, benign) |

**Rule honored:** BUG-026 was closed only after fresh-build verification (12/12
+ trace evidence), not merely after editing code. UPX acceptance (G1) remains
NOT MET and is now blocked by BUG-027, not by dispatch capture.
