# EXP-040 — Application Compatibility Sweep (2026-10-15)

**Question:** how far does the loader actually get on real compiled Windows
applications — a few small ones and two large ones — when they are built with
a mainstream toolchain (llvm-mingw, clang 23.1.3, classic msvcrt target)?

**Method:** cross-compile deterministic self-verifying programs with
llvm-mingw (`x86_64-w64-mingw32-gcc/g++`, `-O1`), inspect every import with
pefile, run each on the loader, capture stdout/stderr/exit code, and
root-cause every non-PASS through the API trace. Suite lives in
`tests/appcompat/`; runner `tests/appcompat/run_sweep.sh`; evidence in
`evidence/master/appcompat/run-20261015_053015/`.

## Test matrix (9 programs)

| Test | Size | Exercises | Result (final) |
|---|---|---|---|
| t01_hello | small | printf formats incl. %f/%e/%g, puts, fflush, return codes | **PASS** |
| t02_math | small | 64-bit arithmetic, primes, GCD, fib(50), shifts | **PASS** |
| t03_mem | small | malloc/calloc/realloc/free patterns, 1 MiB block, stress loop | **PASS** |
| t04_string | small | strings, memmove overlap, strtol/strtoul, qsort int+str, ctype | **PASS** |
| t05_file | small | _open/_write/_read/_lseeki64/_close/_unlink, seek+read-back | **PASS** |
| t06_setjmp | small | setjmp/longjmp across 4 frames, jmp_buf reuse, volatiles | **PASS** |
| t07_big_c_app | **BIG** | inventory manager: multi-key qsort, binary persistence round-trip, FNV-1a integrity, journal file, setjmp error handling, reports | **PASS** (18/18 checks) |
| t08_big_cpp_stl | **BIG** | C++17: vector/string/map, lambdas, sort, exception hierarchy, rethrow, RAII unwind counting, dynamic_cast, new[] | CRASH at BUG-037 frontier (0 unresolved imports; C++ EH machinery runs: handler called, dispositions correct, dies decoding CRT-startup UNWIND_INFO) |
| t09_big_cpp_iostream | probe | iostream formatting | CRASH at import resolution (44 missing APIs — wave 3 list recorded) |

Gates held after every loader change: regression **12/12**, UPX `--version`
**exit 0** (G1).

## Results by sweep stage (evidence trail)

1. Initial run (loader at G1 state, before any change): **0/9** — every PE
   died at `call [iat]` → RIP = raw hint/name RVA (e.g. 0x92f8), SIGSEGV
   before `main`. Unresolved per test: C=5, C++=31, iostream=75.
2. Wave 1 (atexit, _exit, setvbuf, GetCPInfo, _XcptFilter) → 0/9 still, but
   crash moved inside glibc (setvbuf on a non-FILE pointer).
3. BUG-032 fix → t02, t03 PASS.
4. BUG-033 fix (printf repack) → t01 prints floats; crash inside PE dtoa.
5. BUG-034 fix → t01 VERIFY-only (test expectation bug), t04 crash moved.
6. BUG-035 fix → t04 PASS; t05/t06/t07 fall to their own causes.
7. BUG-036 fix → t01, t02, t03, t04, t06 PASS; t05/t07 = test bugs.
8. Wave 2 (26 APIs) + TASK-TLS-001 → t05, t07 PASS (7/9); t08 = BUG-037
   frontier with **0 unresolved imports**; t09 = 44 missing APIs (wave 3).

## Findings

- **APP-F-01 (recorded, not fixed by design):** loader maps `_O_NOINHERIT`
  (0x0080) to O_APPEND — msvcrt _O_APPEND is 0x0008. Tests avoid it; fix
  queued as API-0xx (see backlog TASK-API-012).
- **APP-F-02 (BUG-032, FIXED+TESTED):** mingw treats `__iob_func()`'s return
  as a FILE* into an array of 48-byte FILE structs (stdout = base+0x30,
  stderr = base+0x60); the loader forwarded those synthetic pointers to
  glibc → SIGSEGV (observed setvbuf(stream=base+0x60)). Fix:
  `map_crt_stream()` recognizes the window (stride 0x30) and maps to the
  real streams; also fixed stderr going to stdout.
- **APP-F-03 (BUG-033, FIXED+TESTED):** printf-family crossed the ABI with a
  raw MS slot pointer handed to SysV vfprintf (vprintf read gp_offset = low
  bits of a stack address → libc crash on any float format). Fix:
  fmt-aware repack — classify every argument GP/FP by parsing the format
  string, materialize uniform 8-byte MS slots, synthesize a faithful SysV
  va_list (gpv[0]=fmt, gpv[1..5], xmm0-7 low 64 bits, overflow in call
  order). Applied to printf/fprintf/vfprintf/fwprintf.
- **APP-F-04 (BUG-034, FIXED+TESTED):** `struct MwCritSec {pthread_mutex_t
  mutex(40); int initialized;}` wrote an 'initialized' flag at offset 40 —
  beyond the 40-byte Windows CRITICAL_SECTION contract. Observed live: mingw
  dtoa places its CRITICAL_SECTION flush against `freelist[]` (CS at
  0x14000b178, freelist at +0x28) → flag write corrupted freelist[0] → 1 →
  `__Balloc_D2A` deref → SIGSEGV. Fix: mutex overlays the CS itself;
  sentinel lives in the Windows SpinCount slot (offset 36).
- **APP-F-05 (BUG-035, FIXED+TESTED):** glibc qsort invokes the comparator
  SysV-style (RDI,RSI) but the PE's comparator is ms_abi (RCX,RDX) → NULL
  deref in cmp (t04: `mov eax,[rcx]`, rcx=0). Fix: `qsort_r` bridge whose
  callee re-dispatches through an ms_abi-typed function pointer.
- **APP-F-06 (BUG-036, FIXED+TESTED):** setjmp/longjmp stored a glibc
  sigjmp_buf inside mw__setjmp's own frame: RDI/RSI/XMM6-15 (ms_abi
  callee-saved) were not covered, and resumption flowed through the loader
  frame (t06 counter corruption; t07 infinite `longjmp(val=1)` loop). Fix:
  loader-owned jmp_buf format (30 slots): rbx/rbp/rdi/rsi/r12-r15, xmm6-15,
  entry rsp, return address, magic; naked-asm setjmp; longjmp restores all
  and jumps with rsp = entry_rsp + 8 (16-byte alignment preserved).
- **APP-F-05b:** `_open/_wopen` were variadic ms_abi entry points —
  converted to fixed third argument `mode` (only consumed with _O_CREAT).
- **APP-F-07 (TASK-API-008 wave 2, FIXED+TESTED):** 26 new APIs for the C++
  runtime surface: SRW locks (pthread_rwlock registry keyed by the PE's
  8-byte lock slots), condition variables (Sleep/Wake/WakeAll with
  timed-wait semantics), FlsAlloc/GetValue/SetValue (__thread index array),
  InitOnceExecuteOnce, RtlRestoreContext (full GPR+XMM+RIP restore from a
  Windows CONTEXT), GetModuleHandleW, GetThreadId, SwitchToThread,
  WaitForSingleObjectEx, _aligned_malloc/_aligned_free, _assert,
  _strtoi64/_strtoui64, wcstol/wcstoul/_wcstoi64/_wcstoui64 (ASCII-safe
  UTF-16 passthrough, documented limitation), fputwc, fwprintf (wide fmt
  repack through vfwprintf). Table grew 162 → 188 entries.
- **APP-F-08 (TASK-TLS-001, FIXED+TESTED):** native Windows TLS implemented:
  x64 TEB+0x58 is ThreadLocalStoragePointer (pointer to slot-pointer array),
  previously zeroed (32-bit-style misinterpretation) → libc++abi's
  `__cxa_get_globals` (gs:0x58 indexing) crashed. Loader now: allocates the
  TLS template+zero-fill as slot 0, writes __tls_index=0, publishes the
  block through gs:0x58, runs TLS callbacks with DLL_PROCESS_ATTACH, and
  TlsAlloc/GetValue/SetValue share the same slot array (dynamic from 1).

## Remaining frontier (registered, not fixed here)

- **BUG-037:** during t08's first C++ throw, the 4-frame walk reaches the
  CRT-startup frame (RF begin=0x1470 end=0x23d9, UNWIND_INFO @ RVA 0x50458,
  version=1, flags=0x03 EH+UH, 13 code slots, frame byte 0x85) and the
  interpreter faults at `mov (%r9),%rbx` with R9=0x27 — a raw slot offset
  reached the save-path where a stack address was expected. The FPREG /
  SAVE_XMM128 / EPILOGUE-heavy UNWIND_INFO of the mingw CRT startup is the
  first structure the decoder has met that UPX's frames did not contain.
  Next task: reference-decode that structure (rev-order slot walk) and fix
  the opcode interpreter; predictions and byte dump in evidence dir.
- **t09 wave 3:** 44 unresolved APIs (fopen/fclose/fread/fseek/fsetpos/
  fgetpos, setbuf, setlocale, strftime, isdigit/toupper, _vsnprintf/
  _vscprintf, _sys_nerr, __pctype_func, _filelengthi64, _wfopen, fgetwc/
  ungetwc, the whole `_isw*_l`/`_tolower_l`/`_strcoll_l`/`_strtod_l`
  locale family, AreFileApisANSI, FormatMessageA, GetACP, GetLocaleInfoEx,
  LocalFree). Queued as TASK-API-009 (wave 3).

## Reproduction

```
export PATH=/home/z/my-project/tools-mingw/toolchain/bin:$PATH
bash tests/appcompat/run_sweep.sh          # builds nothing; runs binaries
make clean && make                          # loader rebuild
bash tests/regression/test_pe_loader_regression.sh .
```

Toolchain pinned at `/home/z/my-project/tools-mingw/toolchain`
(llvm-mingw 20261006 msvcrt, clang 23.1.3) — NOT in the repo (downloaded;
Ubuntu 22.04 tarball from mstorsjo/llvm-mingw releases).
