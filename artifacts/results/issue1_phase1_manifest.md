# Issue #1 Phase-1 Test Manifest & Compatibility Matrix

Date (UTC): 2026-10-15 · Loader sha256: dea39e83d01295a16d112921803a7df0d55b6402ff08344955f096bff1494013 · Runs performed WITHOUT EH hacks (MINWIN_EH_HACKS unset, default).

## Manifest (§11.1)

| ID | Binary | SHA-256 | Arch/Format | Expected | Result (3×) | Notes |
|---|---|---|---|---|---|---|
| upx_424 | samples/upx_decompressed.exe | 14e07e294cb8aac61603c95e31b2c364ebd7d0887506a7e156044b262c0d147b | PE32+/x64, UPX-packed, 10 sections | banner `upx 4.2.4`, exit 0 | **PASS exit 0 ×3**, identical stdout hash f4f1a161534f… | G1 acceptance; unpacking + CRT + API layers |
| t08_big_cpp_stl | tests/corpus/bin/t08_big_cpp_stl.exe | 188ae4708dfc21762cc8500005dfbefe929f34ac02420ce795e4755e3624673d | PE32+/x64 (static, C++) | score 255, exit 0 | **PARTIAL exit 1 ×3**, score 159/255, stdout 26c74acfacd5… | STL+algorithms PASS; cross-frame exception+destructors PASS; multi-clause trivial catch FAIL (BUG-038 lineage) |
| t08_probe_exc | tests/corpus/bin/t08_probe_exc.exe | 56f429363fbf81d63d36a41936ccdc31eb4bde0ca90d0a0ade7ed5f5c10312eb | PE32+/x64 (diagnostic) | 3/3 probes | 1/3 + stack-slot clobber observed | pinpoints catch-selector divergence |
| t09_recon_c_smoke | tests/corpus/bin/t09_recon_c_smoke.exe | 9a4c55ed89c6981fd569181b69e31fc8a96c2785955c6f710196121c9b87ce4e | PE32+/x64 | score 127, exit 0 | **PASS exit 0 ×3**, stdout 0a411bcda3f6… | historical t09 semantics unknown → new fixture, no false continuity |
| t10_float_math | tests/corpus/bin/t10_float_math.exe | 5234eced2cb92e4e318e134c4da645b85baa6f6f3718a33049275edc4d3ae793 | PE32+/x64 | score 255, exit 0 | **PASS exit 0 ×3**, stdout 637f1d0e3f9f… | FP32 exact bit values, sqrt/sin/cos, x87/SSE |
| t11_abi_deep | tests/corpus/bin/t11_abi_deep.exe | a54c0423ec126cce85fe4020f64f7949ba26501637d7da6c21255900d6c7d671 | PE32+/x64 | score 255, exit 0 | **PASS exit 0 ×3**, stdout 303386c7e643… | struct-by-value (reg+stack), varargs, fn pointers, deep recursion, 11-frame probe |
| t12_libc_io | tests/corpus/bin/t12_libc_io.exe | 783f763a3be130411bbdbb7599a3b312b9f06ee1477770d555d678ec3110de6d | PE32+/x64 | score 255, exit 0 | **PASS exit 0 ×3** (was SEGV pre-fix), stdout 134fa5e66f06… | write/read/seek/feof/printf-scan round-trip/ENOENT/remove all verified |
| t12_probe_scan | tests/corpus/bin/t12_probe_scan.exe | 32e91e69dba64a3d6d86b6eb09e785f566166261cc5c65591fb49e439d4c23d4 | PE32+/x64 (diagnostic) | got=4, values exact | PASS after ungetc fix | was `d=42 word=[xt] dbl=0.75 hex=15` before |
| x86_pe32_class | /tmp/t32.exe (i686 mingw, return 86) | not retained (classification probe) | **PE32/x86 i386**, magic 0x10b | deterministic classification | exit 1, `[ERROR] Not a PE32+ file (magic=0x10b)` | §4 honest classification: UNSUPPORTED, clean refusal, not a runtime defect |

Regression suite (`tests/regression/test_pe_loader_regression.sh`): **12 passed / 0 failed** (pre- and post-change).
G1 pin (`scripts/g1_accept.sh`): **OK** @ 2026-10-15T08:38:25Z (sha256 40646f4b…), evidence `evidence/master/g1/20261015_083825/`.
Final 3× evidence dir: `evidence/master/issue1_final_*/` (stdout/stderr per run). Earlier diagnostics: `evidence/master/issue1_run_20261015_084438/`.

## Compatibility matrix (§18)

| Target family | x64 PE32+ | x86 PE32 |
|---|---|---|
| Packed (UPX) execution end-to-end | VERIFIED (upx 4.2.4 --version, 3×) | not applicable (UPX 4.x x64 sample) |
| C runtime + stdio console | VERIFIED (t09, t10) | UNSUPPORTED — explicit refusal (magic 0x10b) |
| stdio FILE I/O + fscanf/fprintf | VERIFIED after WS-D fix (t12) | same |
| x64 ABI: structs/varargs/fn-ptrs/deep frames | VERIFIED (t11) | — |
| C++ STL (vector/map/set/string/sort) | VERIFIED (t08, STL portion) | — |
| C++ exceptions: cross-frame + destructors | VERIFIED (t08: dtors=5, what() correct) | — |
| C++ exceptions: multi-clause trivial catch | **DIVERGENT (open root, BUG-038 lineage)** | — |

## Repeatability statement (§11.4)

Every result above was executed 3 times from the same loader build; all three runs per target produced byte-identical stdout (hashes recorded). No run timed out (timeout 90s guard). No nondeterminism observed.
