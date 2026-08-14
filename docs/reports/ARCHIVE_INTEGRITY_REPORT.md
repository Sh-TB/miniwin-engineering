# Archive Integrity Report

**Date**: 2026-08-14
**Repository**: Sh-TB/miniwin-engineering
**Commit**: 296c0af
**Auditor**: Automated verification + manual review

---

## Summary

| Metric | Value |
|--------|-------|
| Total files (non-.git) | 92 |
| Total source lines (all types) | 22,010 |
| Documentation files (.md) | 22 |
| Documentation lines | 6,322 |
| Experiment reports | 3 |
| Bug reports | 4 |
| Test files (harness + scripts + binaries) | 15 |
| Analysis scripts (.py) | 11 |
| Build/patch scripts (.py) | 10 |
| Checkpoint archives | 5 |
| C source lines (loader.c only) | 3,115 |
| Rust source lines (all .rs) | 6,159 |
| C header lines (pe.h) | 269 |

---

## File Inventory

### Source Code

| File | Lines | Size | Status |
|------|-------|------|--------|
| src/loader/loader.c | 3,115 | 124KB | Present (has compile errors — see BUILD_VERIFICATION) |
| src/loader/loader.c.baseline_2134 | 2,134 | 80KB | Present |
| src/loader/loader.c.exp_next2_backup | 2,348 | 89KB | Present |
| include/pe.h | 269 | 8KB | Present |
| include/pe.h.exp_next2_backup | 254 | 7KB | Present |
| src/pe/pe.h | 269 | 8KB | Duplicate of include/pe.h |
| runtime/src/*.rs | 6,159 | 235KB | 15 files, Rust reimplementation |

### Root Documentation

| File | Lines | Status |
|------|-------|--------|
| README.md | 273 | Present, comprehensive |
| ARCHITECTURE.md | 403 | Present, complete pipeline + ABI bridge |
| ENGINEERING_RULES.md | 197 | Present, 12 rules |
| KNOWLEDGE_BASE.md | 394 | Present, 9 sections |
| ROADMAP.md | 199 | Present, 5 phases + milestones table |
| CHANGELOG.md | 110 | Present, 7 session entries |

### Detailed Documentation

| File | Status |
|------|--------|
| docs/ARCHITECTURE.md | Present (509 lines — Rust-era architecture) |
| docs/BUILD_GUIDE.md | Present (349 lines — Rust/Wine toolchain) |
| docs/KNOWN_ISSUES.md | Present (256 lines — 16 known issues) |
| docs/PROJECT_HISTORY_AND_STATUS.md | Present (881 lines — Rust-era history) |
| docs/TRACE_FORMAT.md | Present (386 lines — trace spec) |
| docs/architecture/project_overview.md | Present (144 lines) |
| docs/windows_abi/windows_x64_complete.md | Present (472 lines) |
| docs/reports/FINAL_ENGINEERING_TRANSFER_REPORT.md | Present (192 lines) |
| docs/reports/current_status.md | Present (157 lines) |

### Bug Reports

| File | Status |
|------|--------|
| docs/bugs/BUG-001.md | Present — SIGABRT root cause |
| docs/bugs/BUG-023.md | Present — Missing GCC unwinding (detailed) |
| docs/bugs/BUG-023-expnext-report.md | Present — EXP-NEXT evidence |
| docs/bugs/BUG-024-rtl-dispatch-exception.md | Present — Dispatcher plan |

### Experiment Reports

| File | Status |
|------|--------|
| docs/experiments/EXP-NEXT-correct-seh-first-frame-unwind.md | Present |
| docs/experiments/EXP-NEXT-2-synthetic-exception-dispatch-validation.md | Present |
| docs/experiments/EXP-NEXT-3-real-binary-exception-dispatch.md | Present |

### Tests

| File | Status |
|------|--------|
| tests/regression/test_pe_loader_regression.sh | Present — 12 tests, 178 lines |
| tests/dispatch_tests/test_dispatch_harness.c | Present — 467 lines |
| tests/exp_next2/exp_next2_harness.c | Present — 796 lines |
| tests/dispatch_tests/test_a.exe | Present — 7KB PE binary |
| tests/dispatch_tests/test_b.exe | Present — 7KB PE binary |
| tests/dispatch_tests/test_c.exe | Present — 7KB PE binary |
| tests/dispatch_tests/test_d.exe | Present — 7KB PE binary |
| tests/exp_next2/synthetic_test.exe | Present — 3.5KB PE binary |
| tests/synthetic_pe/hello.c | Present — 11 lines, WinMain template |

### Checkpoints

| File | Size | Status |
|------|------|--------|
| miniwin_checkpoint_before_cpp_exception.zip | 759KB | Present |
| miniwin_checkpoint_before_master_exception_runtime.zip | 799KB | Present |
| miniwin_checkpoint_before_rtl_dispatch.zip | 947KB | Present |
| miniwin_checkpoint_bug023_recovery.zip | 842KB | Present |
| miniwin_checkpoint_exp_next3_before_dispatch.zip | 299KB | Present |

### Scripts

| Category | Files | Total Lines |
|----------|-------|-------------|
| scripts/analysis/ | 11 (.py + .c) | 1,577 |
| scripts/build/ | 10 (.py) | 2,966 |

---

## Missing Files

| Item | Expected Location | Status | Impact |
|------|-------------------|--------|--------|
| Evidence logs (28 execution logs) | evidence/execution_logs/ | Not present (in .gitignore) | Medium — historical debug output not available |
| API trace JSON | evidence/api_trace/ | Not present (in .gitignore) | Low — trace content is quoted in docs |
| Crash logs | evidence/crash_logs/ | Not present (in .gitignore) | Low — crash info quoted in docs |
| Hash files | evidence/hashes/ | Not present | Low — checkpoint zips provide integrity |
| Sample binary (upx_decompressed.exe) | samples/ | Not present (in .gitignore) | HIGH — regression tests need this binary |
| Pre-built loader binary | minwin_loader | Not present (in .gitignore) | HIGH — source has compile errors (see BUILD_VERIFICATION) |
| worklog_archive.md | Root | Not present | Low — CHANGELOG.md covers this |
| docs/architecture/prolog_analysis_mw_RaiseException.txt | docs/architecture/ | Not present | Medium — referenced in transfer report |

### Critical Missing Item Details

**upx_decompressed.exe**: The regression test script (`test_pe_loader_regression.sh`) expects
the test binary at `./samples/upx_decompressed.exe`. This 2.1MB binary is excluded by .gitignore
because it is a pre-compiled Windows executable. A new agent must obtain this binary
independently (e.g., download UPX 4.2.4 and decompress it) or the regression tests
cannot run.

**Pre-built loader binary**: The last known-good binary exists at `/home/z/my-project/minwin/minwin_loader`
(268KB, commit hash cbc04da6...) but was not committed to the repository. The current
source code has 3 compilation errors (see BUILD_VERIFICATION.md). The new agent must
either fix the compile errors or obtain the pre-built binary.

---

## Broken References

### Stale File Paths

3 documents reference the old monolithic path `src/loader.c` instead of the
restructured path `src/loader/loader.c`:

| Document | Line | Old Reference | Correct Path |
|----------|------|---------------|--------------|
| docs/bugs/BUG-024-rtl-dispatch-exception.md | 100 | `src/loader.c` | `src/loader/loader.c` |
| docs/bugs/BUG-023.md | 218 | `src/loader.c` | `src/loader/loader.c` |
| docs/bugs/BUG-023-expnext-report.md | 114 | `src/loader.c` | `src/loader/loader.c` |

### Inaccurate Claims

| Document | Claim | Reality |
|----------|-------|--------|
| FINAL_ENGINEERING_TRANSFER_REPORT.md | "Repository builds (make produces minwin_loader)" | Source has 3 compile errors; Makefile needed path fix |
| FINAL_ENGINEERING_TRANSFER_REPORT.md | "Total commits: 9" | Repository has 2 commits (test + main) |
| README.md | "12/12 tests pass" | Tests pass only with pre-built binary, not from clean build |

### Dual-Architecture Documentation

The `docs/` directory contains two sets of documentation from different project eras:

1. **C loader era** (root .md files): Describes the working C monolithic loader
2. **Rust runtime era** (docs/*.md): Describes a Rust-based reimplementation with
   Wine-based trace collection

These are NOT the same codebase. The Rust runtime (`runtime/`) is a separate
reimplementation that was never completed. A new agent should be aware that
`docs/BUILD_GUIDE.md`, `docs/KNOWN_ISSUES.md`, `docs/PROJECT_HISTORY_AND_STATUS.md`,
and `docs/TRACE_FORMAT.md` describe the Rust project, not the C loader that is
the active development target.

---

## Placeholder Directories

These directories exist with only `.gitkeep` files, indicating planned but
unimplemented modules:

- src/exception/
- src/imports/
- src/memory/
- src/runtime/
- src/unwind/
- src/win32/
- tests/integration/
- evidence/binary_analysis/
- tools/

This is intentional — the ROADMAP.md Phase 3.1 describes the planned module split.

---

## Integrity Verdict

| Aspect | Status |
|--------|--------|
| All source files present | PASS |
| All documentation present | PASS |
| All experiments present | PASS |
| All checkpoints present | PASS |
| All tests present | PASS |
| All scripts present | PASS |
| Build from clean clone | **FAIL** — 3 compile errors + Makefile path issue |
| Regression tests from clean clone | **FAIL** — needs pre-built binary + sample PE |
| Documentation cross-references | PARTIAL — 3 stale paths, some inaccurate claims |
| Knowledge completeness | PASS (with caveats — see KNOWLEDGE_GAP_REPORT.md) |

**Overall**: The archive is substantially complete for knowledge transfer purposes.
A new agent can understand the entire project history, architecture, and current
state. However, a clean clone cannot build or run tests without (a) fixing 3 compile
erros in loader.c, and (b) providing the upx_decompressed.exe test binary.
