# Issue #1 — Phase 0/1 Engineering Report (per §19 required format)

Date (UTC): 2026-10-15 · Executor: MiniWin engineering agent · Scope: full read of Issue #1, Phase 0 audit, Phase 1 six-target execution, Phase 2 first general fix, merge of parallel upstream work.

## A. Verified repository state

- Repository URL: https://github.com/Sh-TB/miniwin-engineering.git
- Branch: `main`
- HEAD (local, post-merge): `60a6f3b93ef031ba9b3567573d26fd6c8b3bd9ac` (merge commit "Merge parallel upstream … with ISS1 slice")
- Remote HEAD (origin/main): identical (push verified `9d0af32..60a6f3b main -> main`, exit 0)
- Build SHA-256 (minwin_loader, final merged tree): `0630d3ce2228c93f69cf504a2cc0022af058844b0398f0f06791dce2914fcfec`
- Working-tree status: clean after merge commit; no reset/force-push used at any point; parallel upstream commits (709b9ee, 9d0af32) fully integrated and preserved.

## B. Work completed

- Root cause 1 (fixed): entire stdio file-I/O family missing (t12: `fopen fclose fread fseek feof fgetc remove _strtoi64 _strtoui64` all UNRESOLVED → SEGV RIP=0x166a2 outside PE).
- Root cause 2 (fixed): FILE* ABI contract — guest static CRT calls `InitializeCriticalSection(fp+0x30)`; an opaque or raw-glibc handle corrupts host heap (SIGSEGV inside libc during fputc). Semantic rule: fopen returns an MSVCRT-strided registry handle with ≥0x40 zeroed slack; host FILE* side-table; every FILE* API resolves via `map_crt_stream` (registry → iob window → pass-through).
- Root cause 3 (fixed): `ungetc` stub dropped the pushback char → every `fscanf` token shifted by one char (probe: `d=42 word=[xt] dbl=0.75 hex=15` → after fix `d=-42 word=[txt] dbl=3.75 hex=255`).
- Root cause 4 (fixed): errno mirror — guest `_errno()` reads `g_errno_storage`; fopen/remove failures now sync host errno (t12 ENOENT path).
- Files changed: `src/loader/loader.c` (stdio family + map_crt_stream routing + fseek/ftell/feof + errno sync), new `tests/corpus/` (5 fixtures + 2 probes + SHA256SUMS), `tests/appcompat/*.exe` rebuilt, `scripts/build/build_issue1_corpus.sh`, `scripts/run_issue1_targets.sh`, `scripts/iss1_final_run.sh`, `artifacts/audit/*`, `artifacts/research/*`, `artifacts/results/*`, `.agent/*` updates, evidence dirs.
- Commit(s): `d115e87` (state sync), `d9d2dc4` (ISS1 Phase-1 + WS-D fix), `60a6f3b` (merge + unified stdio contract).
- Push verification: successful (exit 0; remote ref advanced `9d0af32..60a6f3b`).

## C. Tests

- Focused regression: my 6-target Phase-1 suite (Issue #1 §16 Phase 1 list), 3× each.
- Previous result: t12 SEGV (no output); t08 exit 1 (159/255); t09/t10/t11 not present as binaries before this slice (historical IDs MISSING upstream — verified by grep).
- Current result: **upx_424 PASS ×3** (exit 0, stdout hash f4f1a161534f…), **t09 PASS ×3** (0a411bcda3f6…), **t10 PASS ×3** (637f1d0e3f9f…), **t11 PASS ×3** (303386c7e643…), **t12 PASS ×3** (134fa5e66f06…), **t08 PARTIAL ×3** (exit 1, 159/255, 26c74acfacd5…).
- Repeat count: 3 per target (byte-identical stdout each; no nondeterminism; timeout guard 90 s).
- Full-suite result: appcompat sweep **10/12 PASS** (t01–t07, t10, t11, t12 PASS; t08 FAIL + t09_big_cpp_iostream TIMEOUT — both C++ EH family); repository regression suite **12/12**; G1 pin (clean build, UPX exact banner, exit 0, no crash) OK.
- x86 result: PE32/i386 binary (i686-mingw-built) → deterministic explicit refusal `[ERROR] Not a PE32+ file (magic=0x10b)`, exit 1 — classified UNSUPPORTED (§4-compliant), not a runtime defect.
- Skipped tests and reasons: none skipped; t09_big_cpp_iostream TIMEOUT is a classified failure, not a skip.

## D. Actual runtime evidence

- Original failure: t12 `Signal 11 at RIP=0x166a2` (outside PE; 9 unresolved imports) — `evidence/master/issue1_run_20261015_084438/`.
- First divergence (merged-tree crash): `fclose(0x36d46a0)` freed the registry handle; the guest's subsequent CRT lock used freed memory → SIGSEGV inside libc (`RIP=0x7fd9a875394a`, `RCX=0x36d46a0`) — proved the fp+0x30 lock contract.
- Fixed behavior: unified map_crt_stream routing + MSVCRT-strided registry handles → t12 full PASS (255/255) on the merged tree.
- Application behavior: UPX prints exact `upx 4.2.4` banner + warranty lines and exits 0; t12 writes/reads/seeks/scans/removes real files; t08 STL checks + cross-frame exception + 5 destructor executions verified correct.
- Exit status: as listed in C (recorded per run in evidence dirs).
- Logs and artifacts: `evidence/master/issue1_final_*/`, `evidence/master/issue1_run_20261015_084438/`, `evidence/master/g1/20261015_083825/`, `evidence/master/appcompat/run-20261015_091659/`, `miniwin-results/*/api_trace.json`.

## E. Remaining work

- Open root causes: **BUG-038 family — C++ exception handling**: (a) multi-clause trivial-type catch selector delivers wrong handler values inside same-frame catches (t08: r0/r1 fail deterministically; probe shows stack slot `ok` clobbered to heap-pointer value 46681368); (b) `t09_big_cpp_iostream` TIMEOUT. Cross-frame `std::exception` catch, destructor unwinding, and `what()` are verified CORRECT.
- Blockers: none environmental for x64 work. Native Windows oracle unavailable (documented prerequisite, §7.5 note).
- Known limitations: x86/PE32 execution unsupported by design today (explicit refusal path); errno mirror is single-threaded; MW_FILE_MAX=32 concurrent files; UPX coverage limited to the 4.2.4 x64 sample.
- Related issue numbers: Issue #1 (master), BUG-038 (registered upstream 9d0af32), BUG-037 (CLOSED upstream 9d0af32; integrated here by merge).

## F. Next continuation step

- Highest-priority next root: **C++ catch-clause selector for multi-clause LSDAs (trivial types)** — BUG-038.
- Exact reproduction command: `./minwin_loader tests/corpus/bin/t08_probe_exc.exe` (expect p2 len=0 + clobbered `ok`) and `./minwin_loader tests/corpus/bin/t08_big_cpp_stl.exe` (expect deterministic `score=159`).
- Expected evidence: landing-pad catch-switch value (RDX per API-005 4-param protocol) for `throw 42` / `throw std::string` vs the LSDA clause table; stack-write trace that clobbers `ok`.
- Acceptance criteria: t08 exit 0 with `score=255` (and upstream t08/t09 equivalents green), 3× deterministic, regression 12/12 + UPX G1 stay green, sweep ≥ 11/12. References: MS x64 EH + LLVM Win64EH.h (Issue #1 §7), Wine `dlls/ntdll/unwind.c`.
