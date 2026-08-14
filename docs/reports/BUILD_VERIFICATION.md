# Build Verification Report

**Date**: 2026-08-14
**Platform**: Linux x86_64, GCC 14.2.0
**Commit**: 296c0af (pre-fix)

---

## 1. Clean Build from Source

### Command
```bash
make clean && make
```

### Result: **FAIL** — 3 compilation errors

### Errors

```
src/loader/loader.c:1177:13: error: 'g_cap_er' undeclared (first use in this function);
    did you mean 'g_cap_args'?
 1177 |     memcpy(&g_cap_er, &er, sizeof(EXCEPTION_RECORD));

src/loader/loader.c:3114:16: error: redefinition of 'EH_UNWINDING'
 3114 | const uint32_t EH_UNWINDING = 0x02;

src/loader/loader.c:3115:16: error: redefinition of 'EH_UNWINDING'
 3115 | const uint32_t EH_UNWINDING = 0x02;
```

### Analysis

1. **g_cap_er undeclared**: A global variable `g_cap_er` is used at line 1177
   inside `seh_dispatch_exception` but is never declared. This appears to be
   an incomplete edit from the last development session. The similar variable
   `g_cap_args` exists and is properly declared.

2. **EH_UNWINDING redefined**: The constant `EH_UNWINDING = 0x02` is defined
   three times (lines 3113, 3114, 3115). This is a copy-paste error at the
   end of the file.

### Warnings (non-fatal)

- Format string warnings: `%d` / `%lx` argument type mismatches in MW_TRACE calls
- Unused variable: `prolog_size` in `mw_RtlVirtualUnwind`
- Unused but set variable: `hint` in `load_pe`

### Makefile Issue (fixed during audit)

The Makefile referenced `src/loader.c` but the file is at `src/loader/loader.c`.
Additionally, the Makefile had spaces instead of tabs for the recipe lines.
Both issues were fixed. The `-Iinclude` flag was added to resolve the
`#include "../include/pe.h"` path.

---

## 2. Build from Baseline Backup

### Command
```bash
gcc -O2 -g -Wall -Wno-unused-function -no-pie -Iinclude \
  -o minwin_loader_baseline src/loader/loader.c.baseline_2134 \
  -no-pie -ldl -Wl,-Ttext-segment=0x2000000
```

### Result: **SUCCESS** (with warnings only)

### Output Binary
```
minwin_loader_baseline: ELF 64-bit LSB executable, x86-64, version 1, statically linked, not stripped
Size: 216,992 bytes
```

### Note
The baseline backup has 2134 lines and does NOT include the exception
dispatcher, RtlUnwindEx, or the naked RaiseException stub. It can load PE,
resolve imports, and run CRT initialization, but cannot handle exceptions.

---

## 3. Regression Test Suite

### Command
```bash
./tests/regression/test_pe_loader_regression.sh /path/to/working/binary
```

### Prerequisites
- Pre-built `minwin_loader` binary (from pre-error source)
- `samples/upx_decompressed.exe` test binary

### Result (with pre-built binary): **12/12 PASS**

```
=======================================
MiniWin Regression Test Suite
Loader: /home/z/my-project/minwin/minwin_loader
=======================================
Test: UPX --version execution
  PASS: Entry point reached
  PASS: Imports resolved (157 entries)
  PASS: CRT init (_initterm) executed
  PASS: __getmainargs executed
  PASS: SetUnhandledExceptionFilter called
  PASS: RaiseException(0x20474343) reached
  PASS: .pdata parsed (3030 entries)
  PASS: RUNTIME_FUNCTION lookup succeeded
  PASS: UNWIND_INFO parsed
  PASS: Exit code 139 (SIGSEGV after unhandled exception)
  PASS: malloc calls succeed
  PASS: InitializeCriticalSection works

Results: 12 passed, 0 failed
=======================================
```

### Result (from clean clone without binary): **CANNOT RUN**
No pre-built binary exists in the repository. Source does not compile.

---

## 4. EXP-NEXT-2 Harness Build

### Command
```bash
gcc -o exp_next2_harness tests/exp_next2/exp_next2_harness.c -O2 -no-pie -g
```

### Result: **Not tested** (harness expects specific PE binary and runtime environment)

---

## 5. Dispatch Test Harness Build

### Command
```bash
gcc -o test_dispatch_harness tests/dispatch_tests/test_dispatch_harness.c -O2 -g -no-pie
```

### Result: **Not tested** (requires test PE binaries)

---

## 6. Rust Runtime Build

### Command
```bash
cd runtime && cargo build --release
```

### Result: **NOT TESTED** — The Rust runtime is a separate project.
It has an unused `nix` dependency in error.rs and an unused `goblin`
crate in Cargo.toml. Building it is not required for the C loader.

---

## Summary

| Step | Command | Result |
|------|---------|--------|
| Clean build | `make` | FAIL (3 errors) |
| Baseline build | `gcc ... loader.c.baseline_2134` | SUCCESS |
| Regression (pre-built) | `test_pe_loader_regression.sh` | 12/12 PASS |
| Regression (clean clone) | N/A | CANNOT RUN |
| EXP-NEXT-2 harness | `gcc ... exp_next2_harness.c` | Not tested |
| Rust build | `cargo build` | Not tested (separate project) |

**Conclusion**: A clean clone cannot build or test. The new agent must either
fix the 3 compile errors or start from the baseline backup and port
the exception subsystem forward.
