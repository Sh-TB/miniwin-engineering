# REPRODUCIBLE BASELINE — Master Record

**Produced:** TASK-002, 2026-10-14, at repo HEAD `39181aa9239216ce740b8d89ba990437f1d5e8ae` (clean clone, no local modifications to tracked files)
**Environment:** Debian GNU/Linux x86_64 (kernel 5.10.134), gcc 14.2.0 (Debian 14.2.0-19), GNU Make, Python 3.12.15
**Machine-readable results:** `evidence/master/baseline_results.json`
**Raw build log:** `evidence/master/build_log_head_39181aa.txt`

---

## 1. Prerequisites Recovery

| Requirement | Status |
|---|---|
| `samples/upx_decompressed.exe` (gitignored) | **RECOVERED** from `checkpoints/miniwin_checkpoint_bug023_recovery.zip`; SHA-256 `14e07e294cb8aac61603c95e31b2c364ebd7d0887506a7e156044b262c0d147b`; cross-verified identical in 4 checkpoints and matches manifest `checkpoint_hashes_20260806.txt` |
| Test PE binaries (test_a–d.exe, synthetic_test.exe) | Present in repo (committed) |
| Harness sources | Present |
| Prebuilt loader binary | NOT used (gitignored by design); everything built from source |

---

## 2. Clean Build

```
$ make clean && make          # exit code 0
gcc -O2 -g -Wall -Wno-unused-function -no-pie -Iinclude -o minwin_loader \
    src/loader/loader.c -no-pie -ldl -Wl,-Ttext-segment=0x2000000
```

**Result: SUCCESS — 0 errors, warnings only.** Claim R-02 (VERIFIED).

Warnings inventory (non-fatal, exact classes):
- `-Wformat=` mismatches in `MW_TRACE` expansions: L702 (`%d` missing arg), L1448 (`%x` vs uint64_t), L1593 (unknown conversion `=` — literal `%1` in format string), L1630/L1799/L1805 (`%lx` vs uint32_t), L3334-3340 (`%lx` vs `greg_t`)
- `-Wunused-variable`: `pre_unwind_rsp` (L1444), `prolog_size` (L1979), `page_off` (L3273), `g_errno_ptr` (L2283)
- `-Wunused-but-set-variable`: `hint` (L3173)
- `-Wuse-after-free` (WARNING — real smell, matches old backlog item): `mw_realloc` passes freed `ptr` into `MW_TRACE` after `realloc` (L2468-2469)

**Binary:**
- Path: `minwin_loader`
- Size: 291,872 bytes
- SHA-256: `afa7744c90a3eaabe88bdfe9eb0da1953998cbaf9ef69184959072d95fbcdf3c`
- Format: ELF 64-bit LSB executable, x86-64, dynamically linked (interpreter /lib64/ld-linux-x86-64.so.2), BuildID `39667a6a10110d935954fcb5618ef9c4e3ac3d14`, with debug_info, not stripped
- Note: older docs describe "268KB statically linked" — cosmetic doc drift (build flags unchanged; gcc 14 links differently than the toolchain that produced the 268KB binary)

---

## 3. Test Execution Results (ALL from source builds — no prebuilt binaries)

### 3.1 Regression suite — `tests/regression/test_pe_loader_regression.sh .` → **10/12 PASS, 2 FAIL**

| # | Test | Result |
|---|------|--------|
| 1 | Entry point reached | PASS |
| 2 | Imports resolved | PASS — **159 entries** (docs claim 157 and 162; fresh measurement = 159) |
| 3 | CRT init (_initterm) | PASS |
| 4 | __getmainargs | PASS |
| 5 | SetUnhandledExceptionFilter | PASS |
| 6 | RaiseException(0x20474343) reached | PASS |
| 7 | .pdata parsed (3030) | PASS |
| 8 | RUNTIME_FUNCTION lookup | **FAIL** — no lookup trace occurs (see §5) |
| 9 | UNWIND_INFO parsed | PASS |
| 10 | Exit code 139 | **FAIL — actual exit code 134 (SIGABRT)** |
| 11 | malloc calls succeed | PASS |
| 12 | InitializeCriticalSection | PASS |

⚠️ Historical claims of "12/12 PASS" are **not reproducible from current source**. The 2 failures are fully explained by the newly identified `g_cap_rip` capture bug (see UPX_FAILURE_FORENSICS.md §RootCause).

### 3.2 EXP-NEXT-2 harness (source-built) — **PASS (exit 0)**

Frame 0 (func_C, no handler) → Frame 1 (func_B, EHANDLER at RVA 0x1060) — all checks pass.
Caveat recorded: this harness embeds its **own** lookup/unwind/dispatch code (compiled from `exp_next2_harness.c`), so it validates the *algorithms*, not the loader's integrated dispatch path.

### 3.3 Dispatch tests test_a–d.exe (source-built harness) — **BROKEN / VACUOUS**

| Test | Result | Assessment |
|---|---|---|
| test_a.exe | exit 139 (SIGSEGV inside PE entry, before any dispatch output) | BROKEN |
| test_b.exe | exit 0, "Handler called: NO / Exception handled: YES (PASS)" | **VACUOUS** |
| test_c.exe | exit 0, same vacuous verdict | **VACUOUS** |
| test_d.exe | exit 0, same vacuous verdict | **VACUOUS** |

Root observations (evidence in `/tmp` logs copied to `evidence/master/dispatch_test_logs/`):
1. Harness `resolve_imports()` runs but never logs `[IMPORT] RaiseException -> raise_stub_entry` — the import walk finds no RaiseException entry, so the IAT is **never patched** (slot value remains 0 from the builder script).
2. test_a: PE calls through the unpatched IAT → jump to address 0 → SIGSEGV.
3. test_b/c/d: no exception is ever raised; `g_trace_fail` stays 0; the harness prints PASS unconditionally.
4. Verdict logic flaw: "Exception handled: YES" is the default when no dispatch occurs — precisely the false-success pattern forbidden by the engineering rules.

**Consequence:** The claim that test_a–d validate BUG-024 dispatch behavior is CONTRADICTED. They are retained as historical artifacts; Phase 7 (test strategy) owns their repair or replacement.

### 3.4 Unwind unit tests — `working-tree/tests/exception_dispatch/test_unwind_unit.c` — **35/35 PASS (source rebuild)**

The committed prebuilt ELF also reports 35/35, but the source rebuild was required (include-path fix: compile from a directory where `../../include/pe.h` resolves; done under `/tmp/unitbuild` with `include` symlinked to repo `include`). Covers: PUSH_NONVOL restore, ALLOC_SMALL/LARGE, SET_FPREG, SAVE_NONVOL, CHAININFO establisher frame, RF lookup edge cases (begin/end-1/end/before-first/after-last). Same caveat as 3.2: unit tests exercise embedded copies of the algorithms.

### 3.5 Dalvik frame tests (out-of-scope artifacts, preserved) — **24/24 PASS (source rebuild)**

`gcc tests/dalvik_frame/test_frame.c src/dalvik/frame.c` — 24/24. Matches commit 7c20d7b claim. Run only to preserve unrelated artifacts per mission scope rule. No MiniWin effort redirected.

### 3.6 Target application run — `./minwin_loader samples/upx_decompressed.exe --version`

| Item | Value |
|---|---|
| Exit code | **134 (SIGABRT)** |
| stdout | `[MiniWin] Loading samples/upx_decompressed.exe...` then `terminate called after throwing an instance of 'unsigned long long'` |
| stderr | loader command-line trace only |
| Version output | **NONE** — acceptance command NOT met |
| Trace | `miniwin-results/upx_decompressed.exe/api_trace.json`: two RaiseException dispatches, both ending `Frame[0]: RIP outside PE — walk ends` → `No handler handled the exception` → PE-side `abort()` |

---

## 4. Environment Prerequisites (for reproduction)

1. Linux x86_64, gcc ≥ 14 (tested with 14.2.0), GNU Make, bash, python3
2. `samples/upx_decompressed.exe` — recover via documented checkpoint procedure (REPOSITORY_BASELINE.md §4) or rebuild from UPX 4.2.4 per NEW_AGENT_START.md instructions **but verify against hash `14e07e29...`, not the `254ac80d...` stated there (known doc error)**
3. No network access required; no Rust toolchain required (C loader path)

## 5. Gap List (blocking full green baseline)

| Gap | Effect | Owner |
|---|---|---|
| `g_cap_rip` capture bug (stub asm loads `[rsp+0x00]` instead of saved rip) | Regression tests 8 & 10 fail; UPX run aborts early; entire dispatch machinery unreachable | TASK-004/006 — fix task queued |
| Dispatch tests' import patching never engages | No synthetic Tier-B exception coverage | Phase 7 (TESTING_STRATEGY) |
| Dispatch-harness verdict logic defaults to PASS | False-success risk | Phase 7 |
| `MW_TRACE` format warnings + realloc use-after-free smell | Noise; latent UB in trace path | Backlog (low) |

## 6. Baseline Verdict

The **authoritative, reproducible baseline at HEAD 39181aa is: clean build OK; regression 10/12; UPX target fails with SIGABRT before any exception frame walk.** All historical "12/12 + exit 139 + landing-pad-reached" claims describe an older binary, not the current source. This baseline is the reference point for every subsequent task.

## 7. Post-fix baseline update (2026-10-14, BUG-026 session)

After the BUG-026 operand fix (loader.c `e1caa5d7…`, binary `c8472d04…`, fresh
source build): **regression 12/12** (tests 8 & 10 restored — the §6 verdict's
"10/12" describes pre-fix HEAD `08d94ac`/`39181aa` and remains the recorded
baseline for those commits). UPX target moved from "SIGABRT, walk never starts"
to "SIGSEGV at landing-pad resume" — machine-readable:
`evidence/master/bug026_results.json`; raw: `evidence/master/bug026/`.
Current blocker: BUG-027 (`docs/bugs/BUG-027.md`).
