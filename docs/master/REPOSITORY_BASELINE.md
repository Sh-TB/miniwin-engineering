# REPOSITORY BASELINE — Master Engineering Record

**Observed:** 2026-10-14 (UTC+8 session) by the master engineering agent
**Environment:** Debian GNU/Linux (kernel 5.10.134, x86_64), gcc 14.2.0 (Debian 14.2.0-19), git 2.47.3, Python 3.12.15
**Clone used for observation:** `/home/z/my-project/workspace/miniwin-engineering` (fresh, clean clone — zero uncommitted changes)

---

## 1. Repository Identity

| Item | Value | Verification status |
|---|---|---|
| URL | https://github.com/Sh-TB/miniwin-engineering | VERIFIED (cloned + `git ls-remote`) |
| Visibility | Public (anonymous clone succeeded, no credentials) | VERIFIED |
| Default branch | `main` (`origin/HEAD → origin/main`) | VERIFIED |
| HEAD SHA at observation | `39181aa9239216ce740b8d89ba990437f1d5e8ae` | VERIFIED via `git ls-remote` AND local `git rev-parse` — **identical to the SHA quoted in the mission brief; no new upstream commits** |
| HEAD commit date | 2026-08-16 04:14:20 +0000 | VERIFIED |
| HEAD commit message | "docs: add BUG-025 and subsequent work changelog" | VERIFIED |
| Tags | **none** | VERIFIED (`git tag` empty) |
| Branches | `main` only (no other local or remote branches) | VERIFIED |
| Tracked files | 210 | VERIFIED (`git ls-files \| wc -l`) |
| Pack size | 2.54 MiB, 263 objects | VERIFIED |
| Uncommitted changes | none (fresh clone) | VERIFIED |

### Commit history (oldest → newest, `git log --oneline`)

```
5e1eefd Create t
e2e32d9 Initial check
296c0af feat: complete MiniWin runtime knowledge transfer
1e233df docs: add archive verification reports and audit
c910434 Restore clean build before developer transition
4f19c00 fix(loader): clean build recovery - 3 compile errors fixed
18e63ce exp-033: dalvik architecture research (research-only, no code)
7c20d7b exp-034: dalvik register frame model prototype (24/24 tests pass)
d36fc5e sync: latest loader.c (MAP_FIXED fix, 3481 lines) + pe.h
8d2a5be docs: merge knowledge-transfer content into engineering repo
6881b7e docs: add working tree artifacts - scripts, tests, crash analysis
39181aa docs: add BUG-025 and subsequent work changelog   ← HEAD
```

⚠️ Per engineering rules: these commit messages are **claims**, not proof. Each is assessed in
`ENGINEERING_HISTORY_RECONCILIATION.md`.

---

## 2. Toolchain Observed

| Tool | Version |
|---|---|
| gcc | 14.2.0-19 (Debian) |
| git | 2.47.3 |
| python3 | 3.12.15 |
| OS | Debian, Linux 5.10.134 x86_64 |
| Rust/cargo | **not yet verified** — required if `runtime/` (Rust) is to be built; check in TASK-002 |

---

## 3. Authoritative Source Files (hashes at HEAD 39181aa)

| File | Lines | SHA-256 | Role |
|---|---|---|---|
| `src/loader/loader.c` | 3481 | `502ae5880350aaa325c28c49291d96fc8bc0521d2cdd51da9edff15a410d1953` | **Authoritative loader** (Makefile input) |
| `src/loader/loader.c.baseline_2134` | 2134 | `cbc04da625befd56c443e947372e2b57f1dc3bf9ffb7ee3e5d7a917990c49c59` | Historical backup (name implies 2134-line baseline) |
| `src/loader/loader.c.exp_next2_backup` | 2348 | `3e9d08e0f32bc6606c5ecf04aebfdcbefc53460c9f82ea61946c9c80dbfafb34` | Historical backup (EXP-NEXT-2 era) |
| `include/pe.h` | 269 | `928a780c688f5e4136a53caf2c22e3bb469f264570838fd2439c5f028291a9f2` | **Authoritative header** (Makefile `-Iinclude`) |
| `src/pe/pe.h` | 269 | `d14651c20847a27828f7efbfba3cb205ec75d9c1e5ee7e8ebc54d26fb532ef06` | Duplicate; differs from `include/pe.h` by **one comment line only** (UNWIND_INFO flag bit-position note). Stale mirror — candidate for removal after reconciliation |
| `include/pe.h.exp_next2_backup` | 254 | `302f2fabc1494d203eff1343cb69d20fb73510bdfb20a0074a49d9812b090c80` | Historical header backup |
| `Makefile` | — | `309b668732e57d94e35169fe9c465840c08b8953da2dc943ef33f95d47318dac` | Build: `gcc -O2 -g -Wall -Wno-unused-function -no-pie -Iinclude -o minwin_loader src/loader/loader.c -ldl -Wl,-Ttext-segment=0x2000000` |
| `src/dalvik/frame.c/.h` | 304 | (unrelated scope) | Dalvik register-frame prototype — inventory only, out of MiniWin scope |

**Duplicate-content directories (same doc content mirrored):**
- `knowledge-transfer/` — mirrors root docs, bugs, experiments, reports, evidence logs
- `working-tree/` — mirrors scripts, tests (with compiled Linux ELF artifacts), extra docs incl. `BUG-025-upx-crash-missing-import-stubs.md`, `EXCEPTION_DISPATCH_CURRENT_STATE.md`, `worklog_archive_latest.md`

**Parallel implementation (scope decision required):** `runtime/` is a complete **Rust** implementation (~5,600 lines: loader, PE parser, win32, dispatch, trace, replay). It is NOT referenced by the Makefile. Its relationship to the C loader must be classified in TASK-001 (competing implementation vs. tooling vs. abandoned experiment).

---

## 4. Sample Target Recovery (CRITICAL)

`samples/` is gitignored (`.gitignore`: "Sample binaries (too large for git, download separately)").
The UPX target was **recovered from checkpoint archives**:

- Recovered from: `checkpoints/miniwin_checkpoint_bug023_recovery.zip::samples/upx_decompressed.exe` (copied into working tree `samples/`)
- Cross-checks: byte-identical copies extracted from 4 independent checkpoints (`before_cpp_exception`, `before_master_exception_runtime`, `before_rtl_dispatch`, `bug023_recovery`)
- SHA-256: `14e07e294cb8aac61603c95e31b2c364ebd7d0887506a7e156044b262c0d147b`
- Size: 2,140,672 bytes
- Matches the independently recorded manifest `knowledge-transfer/evidence/hashes/checkpoint_hashes_20260806.txt` → **VERIFIED authentic**
- PE structure verified: `PE\0\0` at e_lfanew=0x80, machine=0x8664 (x64), 10 sections, AddressOfEntryPoint=0x14f0, ImageBase=0x400000, SizeOfImage=0x213000, Subsystem=3 (console)

⚠️ Note: the same manifest records `minwin_loader` sha256 `254ac80d...` and `include/pe.h` sha256 `883d5630...` from the 2026-08-06 era — both differ from current HEAD values, consistent with later edits (pe.h comment update; loader 2134→3481 growth). This lineage is traced in TASK-001.

---

## 5. Test Entry Points Found

| Test | Path | Type |
|---|---|---|
| PE loader regression | `tests/regression/test_pe_loader_regression.sh` | shell script (needs built loader) |
| EXP-NEXT-2 harness | `tests/exp_next2/exp_next2_harness.c` + `synthetic_test.exe` | C harness + committed PE binary |
| Dispatch harness | `tests/dispatch_tests/test_dispatch_harness.c` + `test_a..d.exe` | C harness + 4 committed PE binaries |
| Unwind unit test | `working-tree/tests/exception_dispatch/test_unwind_unit.c` (+ prebuilt ELF) | C unit test |
| Dalvik frame test | `tests/dalvik_frame/test_frame.c` | C unit test (out-of-scope artifact, run only to preserve) |
| Synthetic PE source | `tests/synthetic_pe/hello.c` | source for generating test PEs |

**Missing for reproduction:** nothing blocking — all test sources are present; UPX sample recovered (§4); `minwin_loader` must be built from source (binaries are gitignored by design).

---

## 6. Checkpoint Inventory

| Archive | Size | Notable contents |
|---|---|---|
| `checkpoint_before_clean_build_fix.zip` | 36K | loader.c, pe.h, Makefile (no samples) |
| `miniwin_checkpoint_before_cpp_exception.zip` | 744K | loader.c, pe.h, Makefile, **samples/upx_decompressed.exe**, BUG-001/023 docs |
| `miniwin_checkpoint_before_master_exception_runtime.zip` | 784K | + regression test, exp_next2 tests, dispatch tests, worklog.md, sample |
| `miniwin_checkpoint_before_rtl_dispatch.zip` | 928K | + loader backups, prebuilt minwin_loader, exp_next2 artifacts, sample |
| `miniwin_checkpoint_bug023_recovery.zip` | 824K | + recovery hash manifest, **api_trace.json for UPX run**, sample |
| `miniwin_checkpoint_exp_next3_before_dispatch.zip` | 296K | loader backups, 3 prebuilt loaders, exp_next3 logs, worklog.md |

---

## 7. Findings Requiring Verification (fed to TASK-001/TASK-002)

1. Does current `src/loader/loader.c` (3481 lines) compile clean with gcc 14.2? (commit `4f19c00` claims 3 compile errors were fixed — verify)
2. Does the regression test pass with a fresh source build?
3. Is the "MAP_FIXED fix" actually present in the current loader source? (commit `d36fc5e` claims so)
4. What is the Rust `runtime/` — competing implementation, tooling, or dead experiment?
5. Do the committed test PE binaries (`test_a..d.exe`, `synthetic_test.exe`) still exercise the intended paths in the current loader?
6. Which of the exception-path claims (BUG-023/024/025, landing pad 0x40331c, crash RIP=0x401561) hold against the current source?
7. Is there any Git history beyond `main` (stash, dangling objects) worth recovering? (none found in fresh clone — objects fully packed, 263 objects)

---

## 8. Observation Provenance Statement

Everything in §1–§5 was directly observed in this session's fresh clone at HEAD `39181aa`.
Nothing was accepted from commit messages or documentation without independent observation.
Claims found in documentation are handled by TASK-001 with statuses
(VERIFIED / IMPLEMENTED_UNVERIFIED / PARTIAL / STALE / CONTRADICTED / BLOCKED / UNKNOWN).
