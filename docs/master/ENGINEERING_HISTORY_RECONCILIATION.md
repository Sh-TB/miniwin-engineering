# ENGINEERING HISTORY RECONCILIATION — Master Record

**Produced:** TASK-001, 2026-10-14 session, at repo HEAD `39181aa9239216ce740b8d89ba990437f1d5e8ae`
**Method:** Every claim below was checked against (a) actual source at HEAD, (b) checkpoint archives, (c) recorded hash manifests, or (d) is explicitly marked unverified. No contradictory evidence was resolved by picking the most optimistic account.

---

## 1. Reconstructed Timeline (from commits, checkpoints, worklogs, hash manifests)

| # | Period | Event | Evidence |
|---|--------|-------|----------|
| S0 | ≤2026-07-30 | Pre-MiniWin research phase: Wine relay tracing for API discovery (`docs/KNOWN_ISSUES.md`, `research/`) | docs/KNOWN_ISSUES.md dated 2026-07-30 |
| S1 | 2026-08-06 | Initial PE loader: parse, map 10 sections, imports, TEB/PEB, CRT init. UPX reaches CRT init. Checkpoint `miniwin_checkpoint_20260806` (loader.c `c0d143f5...`) | CHANGELOG.md S1, checkpoint_hashes_20260806.txt |
| S2 | 2026-08-07 | BUG-023: SEH infra (RtlLookupFunctionEntry binary search, RtlVirtualUnwind 9 opcodes). Naked-stub decision. Checkpoint before_cpp_exception | worklog Task 1, checkpoint_evidence_rsp.md (2026-08-07) |
| S3 | 2026-08-07 | "Truncation" misdiagnosis resolved; 2134-line loader = working baseline; 12-test regression suite created; RaiseException(0x20474343) reached; exit 139 = expected UEF crash | worklog Task 2, checkpoint bug023_recovery (loader.c `cbc04da6...`) |
| S4 | 2026-08-07 | EXP-NEXT: prolog-corrected walk; REX 0x48 classification bug fixed; EHANDLER reachable at Frame[3]; 236/3030 malformed UNWIND_INFO (static analysis of 601 entries) | BUG-023-expnext-report.md |
| S5 | — | EXP-NEXT-2: synthetic 3-frame PE; handler discovery PROVEN. loader.c `3e9d08e0...` (2348 lines) backed up | EXP-NEXT-2 report, checkpoint before_rtl_dispatch |
| S6 | 2026-08-14 | EXP-NEXT-3: real dispatcher (`seh_dispatch_exception`) on UPX; 5-frame walk; Frame[3] ContinueSearch; Frame[4] calls RtlUnwindEx; longjmp bridge works; context restore incomplete | EXP-NEXT-3 report, checkpoint exp_next3_before_dispatch |
| S7 | 2026-08-14 | Exception-dispatch audit & 6-bug fix pass (C1 g_cap_er order, M1 CHAININFO est-frame, M2 ContextFlags, CRITICAL flags mask 0x03→0x07, dup EH_UNWINDING removed); 35-assertion unit tests created | working-tree worklog "exception-1-through-5", EXCEPTION_DISPATCH_CURRENT_STATE.md (against 3144-line source) |
| S8 | 2026-08-14 | Knowledge transfer repo assembled (9 commits: 5e1eefd..296c0af) | FINAL_ENGINEERING_TRANSFER_REPORT.md |
| S9 | 2026-08-14 | Build-verification audit: 3 compile errors found (g_cap_er undeclared, 2× EH_UNWINDING dup) → fixed; Makefile path/tab fix; mkdir fix; 12/12 regression | BUILD_VERIFICATION.md, CLEAN_BUILD_RECOVERY.md, commits c910434, 4f19c00 |
| S10 | 2026-08-15 | BUG-025 (separate packed `upx.exe` target): crash at IAT call of unresolved LoadLibraryA/ExitProcess (IAT slots still held name RVAs). Fixed with stubs (LoadLibraryA → fake handle 0xdead0001). Packed UPX then decompresses and reaches exception dispatch; new crash at RVA 0x1561 | working-tree/docs/BUG-025-*.md, worklog "bug-025" |
| S11 | 2026-08-15 | MAP_FIXED_NOREPLACE → MAP_FIXED change (fallback mapping without relocations was corrupting absolute addresses); EXP-035 RSP investigation; loader.c grows to 3481 lines and is synced in commit d36fc5e | changelogs/BUG-025_and_subsequent_work.md, source lines 3075-3092, 1841-1924 |
| S12 | 2026-08-15 | `.agent/state.md` written: landing pad 0x40331c reached; SIGSEGV at RIP=0x401561; H-001 (NV regs) / H-002 (stack contents) hypotheses open | .agent/state.md |
| S13 | 2026-08-16 | Final sync commits (8d2a5be, 6881b7e, 39181aa) merge working-tree docs/changelog into repo. **HEAD.** | git log |

**Loader source lineage:** 1888 (truncated checkpoint) → 2134 `cbc04da6` (S3 baseline backup `loader.c.baseline_2134`) → 2348 `3e9d08e0` (S5 backup `loader.c.exp_next2_backup`) → 3144 (S7 audit reference, not archived) → **3481 `502ae588` (current HEAD)**.

---

## 2. Claim Reconciliation Table

Statuses: VERIFIED / IMPLEMENTED_UNVERIFIED / PARTIAL / STALE / CONTRADICTED / BLOCKED / UNKNOWN

| ID | Claim | Source | Verification | Status |
|----|-------|--------|--------------|--------|
| R-01 | HEAD is 39181aa on main, no later commits | mission brief + git | `git ls-remote` == local == brief | **VERIFIED** |
| R-02 | Clean build succeeds (0 errors, warnings only) | CLEAN_BUILD_RECOVERY.md, commits c910434/4f19c00 | Not yet re-run in this session → TASK-002 | **IMPLEMENTED_UNVERIFIED** |
| R-03 | Regression suite 12/12 PASS | state.md, CLEAN_BUILD_RECOVERY.md | Not yet re-run → TASK-002. Note: test #10 asserts exit code 139 — i.e. "expected crash", not functional success | **IMPLEMENTED_UNVERIFIED** |
| R-04 | 162 imports resolved (K32:68, msvcrt:94); BUILD_VERIFICATION log says "157 entries" | README vs BUILD_VERIFICATION.md | Both numbers appear in docs; trace shows 157 in one run, 162 claimed elsewhere. Re-measure in TASK-002 | **CONTRADICTED (157 vs 162)** — to be re-measured |
| R-05 | MAP_FIXED present in loader | changelog + commit d36fc5e | Source line 3084 `MAP_PRIVATE\|MAP_ANONYMOUS\|MAP_FIXED` — present | **VERIFIED** (source); safety audit owed → Phase 5 of mission |
| R-06 | g_cap_er declared and ER copy after field fill (C1 fix) | worklog | Source lines 608, 1290-1291 | **VERIFIED** |
| R-07 | ContextFlags 0x100007 (M2 fix) | worklog | To re-check in source during TASK-003 | **IMPLEMENTED_UNVERIFIED** |
| R-08 | CHAININFO establisher frame computed (M1 fix) | worklog | To re-check in source during TASK-003 | **IMPLEMENTED_UNVERIFIED** |
| R-09 | UNWIND_INFO flags mask 0x03→0x07 (critical fix) | worklog | `mw_RtlVirtualUnwind` line 1978 uses `& 0x07` | **VERIFIED** |
| R-10 | LoadLibraryA/ExitProcess/GetProcAddress stubs added (BUG-025) | worklog + BUG-025 doc | Source lines 468/476/2839/2841; fake handle 0xdead0001 | **VERIFIED (present)**; correctness is a Phase-6 concern (fabricated handle — documented, must not silently pretend to be real) |
| R-11 | Packed UPX root cause = unresolved IAT slots holding name RVAs | BUG-025 doc | Internally consistent + trace quoted; sample `upx.exe` NOT in repo (only `upx_decompressed.exe` recovered) → cannot re-verify directly | **PARTIAL** (evidence internally consistent, target binary missing) |
| R-12 | "PE loads at correct ImageBase (0x140000000)" | changelogs/BUG-025_and_subsequent_work.md | Conflicts with every other record (0x400000) and with the recovered decompressed sample header. Likely refers to a different packed-UPX build | **CONTRADICTED / STALE** — needs re-measurement if packed target is ever recovered |
| R-13 | UPX `--version` crash chain: raise at RIP=0x49d5b1 → 5 frames → Frame[3] ContinueSearch → Frame[4] RtlUnwindEx(0x40331c) → landing pad → SIGSEGV RIP=0x401561, RAX=0 | .agent/state.md | Historical trace evidence (exp_next3 logs) supports the flow **as of that build**. Current source contains EH_FIX LSDA override which may divert the flow at Frame[3] (LSDA `ff ff 01 08 20 03 35 00` → pc_offset 0x22 ∈ [0x08,0x28), action=0x35>0, lp=3>0 → override to LP 0x401573). Fresh reproduction REQUIRED → TASK-004 | **STALE/UNCERTAIN for current source** |
| R-14 | "RtlUnwindEx sets only Rip/Rsp/Rax" | state.md, CURRENT_ENGINEERING_STATE.md | Current source: Rip/Rsp/Rax **plus** EXP-035 post-prologue-RSP computation (lines 1871-1928). The described 3-field behavior no longer matches source | **STALE** |
| R-15 | H-001: landing-pad NV registers not restored | .agent/hypotheses.md | Mechanism analysis: dispatcher copies **all 16** regs from ctx to g_unwind_regs (lines 1350-1365) and naked stub restores all 16 (lines 1722-1740). ctx NV values come from unwind-code simulation. Whether they are the CORRECT values is unproven. H-001 as stated ("not restored") is likely **imprecise**; "restored but possibly wrong values" is the accurate open question | **CONTRADICTED as stated; underlying question OPEN** |
| R-16 | H-002: stack contents at [RSP+0x38] zero/uninitialized | .agent/hypotheses.md | Plausible; consistent with RtlUnwindEx DIAG dumps of [tgt_frame+0x30..0x40] and crash RAX=0. NOT yet proven; also note DIAG reads at `tgt_frame` while landing pad reads at `post_prologue_rsp` — diag may probe the wrong window | **UNKNOWN** — experiment designed in TASK-004 |
| R-17 | EXP-035 fix: post_prologue_rsp = target_frame − alloc_total | changelog (pre-EXP-035 checkpoint only) + source comment | Code present (lines 1841-1924). No experiment report exists for EXP-035. Its correctness (and whether alloc_total from possibly-malformed UNWIND_INFO is trustworthy) is unproven | **IMPLEMENTED_UNVERIFIED** |
| R-18 | EH_FIX: dispatcher-side DWARF LSDA override of personality ContinueSearch | not mentioned in ANY doc (found in source review, lines 1607-1644) | Present in source; undocumented — undocumented mechanism that bypasses the personality's type-matching decision. Does not check whether the action is a CATCH or a CLEANUP. HIGH-RISK shortcut | **CONTRADICTED with "no app-specific hacks" engineering rules; undocumented** |
| R-19 | 236/3030 malformed UNWIND_INFO in UPX | EXP-NEXT report | Static-analysis evidence recorded; sample binary recovered and re-checkable | **VERIFIED (historical static analysis)** |
| R-20 | .pdata has gaps (3030 entries, not fully contiguous) | EXP-NEXT report, S1/S2/S3 structural issues | Static evidence recorded; re-checkable | **VERIFIED (historical static analysis)** |
| R-21 | upx_decompressed.exe hash = 254ac80d... | BUG-001.md, NEW_AGENT_START.md | Manifest assigns 254ac80d to **minwin_loader**, sample is **14e07e29** (4-way cross-verified + manifest) | **CONTRADICTED** — docs conflate loader hash with sample hash |
| R-22 | "3 compile errors exist in current loader.c" | NEW_AGENT_START.md, BUILD_VERIFICATION.md | Fixed in S9 (CLEAN_BUILD_RECOVERY). Doc retains stale warning + Option B fallback advice | **STALE** |
| R-23 | "SIGSEGV at 0x49c9c6 UEF path" as current blocker | NEW_AGENT_START.md | Pre-dispatcher-era state (S3); superseded by landing-pad-era evidence (S12) | **STALE** |
| R-24 | 12 regression tests exist & are meaningful | BUILD_VERIFICATION.md | Script present; content review shows several tests assert crash-continuation behavior (exit 139) — valid as regression pins, NOT as functional success. Full review in TASK-002 | **PARTIAL** |
| R-25 | EXP-NEXT-2 synthetic proof PASS | EXP-NEXT-2 report + results file | Harness + synthetic_test.exe + results committed; re-runnable → TASK-002 | **IMPLEMENTED_UNVERIFIED** (re-run owed) |
| R-26 | 35/35 unwind unit assertions pass | worklog S7 | test_unwind_unit.c present in working-tree; re-runnable → TASK-002 | **IMPLEMENTED_UNVERIFIED** (re-run owed) |
| R-27 | 4 dispatch tests (test_a-d.exe) validate BUG-024 behavior | BUG-024 doc | Binaries + harness committed; whether they exercise real dispatch through the loader (vs standalone harness re-implementation) must be audited in TASK-002/Phase-7 | **PARTIAL** |
| R-28 | Rust `runtime/` is a parallel implementation | file tree | ~5,600 lines Rust (loader, PE, win32, dispatch, trace, replay); not referenced by Makefile; BUILD_VERIFICATION says "separate project, not tested" | **UNKNOWN role** — classify: out of active C-loader critical path; do not delete; no effort without owner decision |
| R-29 | Dalvik/MiniAndroid artifacts (src/dalvik, research/, exp-033/034) | commits | Present; "24/24 tests pass" claim; OUT OF SCOPE per mission (inventory only) | **VERIFIED as present; OUT OF SCOPE** |
| R-30 | knowledge-transfer/ and working-tree/ are mirrors | commit messages | Duplicated doc trees; working-tree contains UNIQUE docs (BUG-025, EXCEPTION_DISPATCH_CURRENT_STATE, worklog_archive_latest) | **VERIFIED — working-tree is the newer record; knowledge-transfer is an older mirror. No content silently discarded** |
| R-31 | VEH chain works; UPX registers a VEH handler | BUG-001 trace + audit doc | Code present; UPX trace shows AddVectoredExceptionHandler called; 0 handlers active at dispatch in one trace — inconsistency flagged in audit doc §3.4 | **PARTIAL/UNCERTAIN** |
| R-32 | TEB/PEB, heap (bump), Virtual*, CRT stubs working | multiple docs | Code present; exercised by UPX run traces | **IMPLEMENTED_UNVERIFIED** (re-run in TASK-002) |
| R-33 | "UPX --version: unhandled 'unsigned long long' exception" post-BUG-025 | changelog S10 | Refers to packed-UPX flow (different binary); cannot reproduce without packed target | **BLOCKED** (missing target binary) |
| R-34 | 28 execution logs, api_trace.json, crash logs "in repo" | README/FINAL_TRANSFER report | Actually excluded by .gitignore; only copies exist under knowledge-transfer/evidence/ (subset) | **PARTIAL** — evidence subset preserved in knowledge-transfer/ |

---

## 3. Key Contradictions Summary (owner-visible)

1. **Hash conflation (R-21):** BUG-001.md and NEW_AGENT_START.md state the UPX sample hash as `254ac80d...`, which the recorded manifest assigns to the *loader binary*. The cross-checkpoint-verified sample hash is `14e07e29...`. Download instructions in NEW_AGENT_START.md would produce/verify the WRONG hash.
2. **State snapshot vs. source drift (R-13/R-14/R-17/R-18):** `.agent/state.md` describes behavior (3-field RtlUnwindEx, Frame[3] ContinueSearch) that the current 3481-line source no longer implements (EXP-035 RSP adjustment; undocumented EH_FIX LSDA override capable of diverting dispatch at Frame[3]). **No document describes the current source's actual control flow.** A fresh reproduction (TASK-004) is the only trustworthy source of current behavior.
3. **Import counts (R-04):** 157 vs 162 across docs — re-measure.
4. **ImageBase 0x140000000 (R-12):** single-source claim, contradicts everything else; likely belongs to the missing packed `upx.exe` target.
5. **Undocumented EH_FIX bypass (R-18):** violates the spirit of ENGINEERING_RULES (no app-specific hacks / no fake success) and is undocumented. It must be either documented-with-evidence or isolated behind an env-var/flag and tested. **Decision owed in Phase B (UPX forensics).**

## 4. What Must Be Done to Resolve Remaining Uncertainty

| Uncertainty | Resolution path |
|---|---|
| Clean build + regression status at HEAD | TASK-002 (this session) |
| Actual current UPX failure mode | TASK-004 fresh reproduction with full machine-state capture |
| H-001 vs H-002 vs EH_FIX interference | TASK-004 differential run: (a) current source as-is; (b) EH_FIX disabled — compare landing-pad register/stack state |
| EXP-035 alloc_total correctness | Static: decode target-function UNWIND_INFO from the recovered sample; dynamic: compare computed post_prologue_rsp against landing-pad expectations |
| R-04 import count | Read trace from TASK-002 run |
| Packed-UPX claims | BLOCKED: `upx.exe` not in repo; recovering it is optional Phase-B work (owner may provide) |

## 5. Superseded / Dead Artifacts (classified, NOT deleted)

- `src/loader.c` copies inside checkpoints — historical only.
- `mw_RaiseException_baseline` in current source (lines ~1757+) — dead code, superseded by naked stub. Candidate for removal in Phase F (not now).
- `docs/NEW_AGENT_START.md` Option A/B advice — stale (build fixed); Option C reading list still valid.
- `working-tree/tests/**` prebuilt ELF artifacts — regenerated on demand; binaries kept for provenance.

## 6. Files Audited in This Phase

Read in full or substantial part (TASK-001): README.md, CHANGELOG.md, .agent/mission.md, .agent/state.md, .agent/backlog.md, .agent/decisions.md, .agent/hypotheses.md, docs/NEW_AGENT_START.md, docs/reports/CURRENT_ENGINEERING_STATE.md, docs/reports/BUILD_VERIFICATION.md, docs/reports/CLEAN_BUILD_RECOVERY.md, docs/reports/FINAL_ENGINEERING_TRANSFER_REPORT.md, docs/reports/current_status.md, docs/bugs/BUG-001.md, docs/bugs/BUG-023-expnext-report.md, docs/bugs/BUG-024-rtl-dispatch-exception.md, working-tree/docs/BUG-025-upx-crash-missing-import-stubs.md, working-tree/docs/EXCEPTION_DISPATCH_CURRENT_STATE.md, working-tree/docs/checkpoint_evidence_rsp.md, working-tree/worklog_archive_latest.md, changelogs/BUG-025_and_subsequent_work.md, docs/KNOWN_ISSUES.md (phase-scoped), Makefile, .gitignore, src/loader/loader.c (exception/unwind/unwind-restore/import-stub/mapping sections, ~1,000 of 3,481 lines in detail), include/pe.h (diff vs src/pe/pe.h), checkpoint manifests and 6 checkpoint listings. Plus TASK-000 full-file inventory (210 tracked files).

Reference-only (read structure, not fully read yet): ARCHITECTURE.md, KNOWLEDGE_BASE.md, ROADMAP.md, docs/SOURCE_MAP.md, docs/EXPERIMENT_INDEX.md, docs/BUILD_GUIDE.md, docs/TRACE_FORMAT.md, docs/windows_abi/windows_x64_complete.md, docs/bugs/BUG-023.md, docs/experiments/EXP-NEXT*.md (3), docs/reports/ARCHIVE_INTEGRITY_REPORT.md, docs/reports/KNOWLEDGE_GAP_REPORT.md, working-tree/docs/EXP_EXCEPTION_DISPATCH_REPORT.md, runtime/src/*.rs (structure only). These are consultative references; none is a competing source of truth for current state except as flagged in §3.
