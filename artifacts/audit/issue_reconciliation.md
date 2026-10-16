# Issue Reconciliation — Issue #1 Phase 0/1

Date (UTC): 2026-10-15

## Master issue

| Field | Value |
|---|---|
| Number / URL | **#1** — https://github.com/Sh-TB/miniwin-engineering/issues/1 |
| Title | MiniWin Master Engineering Campaign — Evidence-Driven Windows x86/x64 PE Execution, UPX Unpacking, Exception Unwinding, Runtime APIs, and Reproducible Compatibility |
| State | OPEN, created 2026-10-15T08:31:12Z |
| Read | FULL body read line-by-line (1095 lines of cleaned text) before any implementation |
| Role | Coordination hub; no duplicate sub-issues created (per §15) |

## Historical identifiers (§2.2) resolved against evidence

| ID | Meaning found | Status after this slice |
|---|---|---|
| BUG-025 | UPX progress / missing import stubs (working-tree doc `working-tree/docs/BUG-025-upx-crash-missing-import-stubs.md`) | SUPERSEDED — UPX `--version` now passes end-to-end (G1) |
| BUG-026 | Incorrect capture of exception return address | VERIFIED FIXED earlier; loader source contains the corrected prologue walk; UPX G1 depends on it |
| BUG-027 | Exception context restoration / stack unwind / C++ catch | PARTIAL — cross-frame `std::exception` + destructor unwinding now proven working by t08 (dtors=5, what() correct); trivial-type catch selection still divergent |
| BUG-030 / API-005 | Catch selector delivered in RDX, flag-guarded EH hacks (`MINWIN_EH_HACKS`, default OFF, ADR-010) | Present in source; hacks remain OFF during this slice's runs (runs were performed without EH hacks) |
| BUG-031 | ADR-011 unwind chain; G1 acceptance MET (`upx --version` exit 0) | Re-verified by this slice: G1 pin PASS, 3× deterministic |
| BUG-037 | UNWIND_INFO decoding (slot counting, frame-register nibbles) | Not re-audited in this slice (PENDING, tracked in backlog) |
| BUG-038 | Subsequent C++ EH landing-pad/resume failure | **CONFIRMED STILL OPEN as the t08 residual**: same-function catch of trivial types (`int`, `std::string`) delivers wrong handler values; stack corruption observed in probe (`ok` clobbered to 0x2C9xxxx) |
| t08/t09/t10/t11/t12 | Historical test IDs | MISSING upstream (0 grep hits). Recreated: t08_big_cpp_stl, t09_recon_c_smoke (explicitly NOT claiming continuity with historical t09), t10_float_math, t11_abi_deep, t12_libc_io — all PE32+/x64, sources in `tests/corpus/` |

## Root causes found and fixed in this slice

1. **WS-D: entire stdio file-I/O family unresolved** — t12 imported `fopen fclose fread fseek feof fgetc remove _strtoi64 _strtoui64`; all 9 UNRESOLVED → SEGV (RIP=0x166a2 outside PE). Fixed by implementing the family host-backed in `src/loader/loader.c`.
2. **WS-D: FILE* must be MSVCRT-layout, not opaque** — observed: guest's statically-linked CRT introspects `_file` (CRT lock indexing) and `_flag`; the naive wrapper caused silent heap corruption (`InitializeCriticalSection` on garbage heap pointer 0x3a3bd60). Fixed: `_iobuf`-compatible struct, real small fd, zero-extended allocation.
3. **WS-D: `ungetc` stub lost the pushed-back char** — observed via probe: every `fscanf` conversion consumed one extra char (`d=42 word=[xt] dbl=0.75 hex=15` — exactly one char shifted). Fixed: standard single-char pushback via `_charbuf`, EOF-flag clearing.

## Root causes confirmed and left open (evidence-backed)

4. **C++ catch clause selection for trivial types (BUG-038 lineage)** — t08: `throw 42`/`throw std::string` caught by the WRONG clause values inside a multi-catch function (score 159/255, deterministic 3×); minimal probe shows int-catch alone works, string arrives empty, and a stack slot (`ok`) gets clobbered with a heap pointer value 46681368. Cross-frame `std::runtime_error` catch + 5 destructor executions + `what()` are CORRECT. Next investigation: landing-pad catch-switch selector value delivery for multi-clause LSDAs.

## Status vocabulary used

IMPLEMENTED / TESTED / OBSERVED / PARTIAL / BLOCKED / PENDING / SUPERSEDED per Issue #1 §3.1. No test was counted as passed when skipped, timed out, or run against a stale binary (fixture EXEs rebuilt and re-hashed after every source change; hashes in results manifest).
