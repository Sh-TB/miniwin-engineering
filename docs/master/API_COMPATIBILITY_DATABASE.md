# API COMPATIBILITY DATABASE — Master Record

**Produced:** TASK-005, 2026-10-14, at HEAD `39181aa`. Source of truth: `g_import_table[]` / `g_data_imports[]` in `src/loader/loader.c` (extracted programmatically — NOT copied from older docs).
**Measured resolution on UPX-decompressed:** 159 code imports resolved (trace, TASK-002) — some table entries are unused by this target.

## Classification legend
- **REAL** — implements actual behavior (may be Linux-backed)
- **PARTIAL** — real for a documented subset
- **STUB-FAKE** — returns fabricated success/handle; safe only within documented scope
- **NOOP** — accepts and does nothing
- **UNEXERCISED** — present in table, no evidence any target called it

Counts: KERNEL32.DLL = 70 code imports; msvcrt.dll = 91 code imports; msvcrt data imports = 4 (`_acmdln`, `__initenv`, `_commode`, `_fmode`). Total = 165.

## Mission-flagged APIs (deep classification)

| API | DLL | Class | Implementation (loader.c) | Contract notes & risks |
|---|---|---|---|---|
| LoadLibraryA | K32 | **STUB-FAKE** | L468-474: logs, returns `0xdead0001` regardless of name | Documented fake handle. Callers using it for real DLL loading will misbehave. GetProcAddress special-cases `0xdead0001` and `g_image_base` (L3000). Risk: any target that loads a real DLL and calls its exports gets nonsense. Acceptable ONLY while no target exercises real loading |
| GetProcAddress | K32 | **PARTIAL** | L2996-3003 via `g_resolve_proc_fn` → searches dispatch table by name for the fake module handle | Works for names that exist in our table; returns NULL otherwise (correct failure). Cannot resolve symbols from DLLs we don't contain |
| ExitProcess | K32 | **REAL** (scope: process exit) | L476-478: `_exit(code)` | noreturn; flushes are caller's duty (does not flush trace!) — note: `mw_ExitProcess` calls `_exit` WITHOUT flushing `g_trace_log` → trace tail loss possible on ExitProcess paths (BUG-026-adjacent hygiene item) |
| VirtualAlloc | K32 | **ABSENT** | not in table | Historical docs claim a VirtualAlloc stub existed; current table has none. UPX-decompressed does not import it (uses msvcrt heap) |
| VirtualFree | K32 | **ABSENT** | not in table | same as above |
| VirtualProtect | K32 | **PARTIAL** | L2224-2235: mprotect with Win32→Linux prot mapping; `*old` always PAGE_READWRITE | Does not validate addr/size range against known mappings; old-protection is fabricated |
| VirtualQuery | K32 | **PARTIAL** | L2237+: /proc/self/maps parse | Depends on maps format; allocation-base fields likely approximated |
| HeapAlloc/HeapFree/HeapReAlloc/GetProcessHeap | K32 | **ABSENT** | not in table | Historical "bump allocator" claims refer to an older table snapshot; UPX-decompressed uses msvcrt malloc/free/realloc (host libc backed — REAL) |
| File I/O (`_open/_read/_write/_close/_lseeki64/_stat64/…`) | msvcrt | **REAL (host fd passthrough)** | implemented wrappers over host libc fds; handles 0/1/2 map to stdin/stdout/stderr via params | `CreateFileA/ReadFile/WriteFile` (KERNEL32-style) absent from table |
| Thread APIs (`_beginthreadex/_endthreadex/SuspendThread/ResumeThread/SetThreadContext/GetThreadContext`) | K32/msvcrt | **STUB-FAKE / NOOP (mostly)** | `_beginthreadex` present (needs audit), Suspend/Resume/Context stubs return success without effect | Single-threaded runtime; per-thread TEB absent — any target creating threads will silently misbehave |
| Exception APIs | | | | |
| — RaiseException | K32 | **REAL** (with BUG-026 defect) | naked stub + `seh_dispatch_exception` | Currently broken capture (UPX_FAILURE_FORENSICS §2) |
| — RtlLookupFunctionEntry / RtlVirtualUnwind | K32 | **REAL** (dual SysV+ms_abi implementations; latent ALLOC_LARGE bug N-1) | binary search + opcode simulation | Unit-tested (35/35); ms_abi versions used by PE's own unwinder |
| — RtlUnwindEx | K32 | **PARTIAL** | longjmp bridge + EXP-035 RSP calc; ignores OriginalContext; no intermediate cleanup, no UHANDLER phase, no RDX selector (N-4) | Documented simplification (D-003) |
| — AddVectoredExceptionHandler / Remove… | K32 | **REAL** (registry) | L251-277 array; called in reverse order | UPX registers one; trace shows 0 handlers at dispatch — registration-not-persisting question open (R-31) |
| — SetUnhandledExceptionFilter | K32 | **STUB-FAKE** | L~2200: stores pointer, returns old | **Never invoked** when walk exhausts (structural gap S3) — UPX's filter at 0x49c9c0 never runs |
| — __C_specific_handler | msvcrt | **NOOP** | returns 0 (ContinueSearch) unconditionally | Documented no-op; blocks MSVC-style SEH targets |
| — UnhandledExceptionFilter | K32 | **STUB-FAKE** | minimal | see above |
| — TlsAlloc/TlsGetValue/TlsSetValue | K32 | **PARTIAL** | global slots (single-threaded semantics) | trace shows UPX using TlsGetValue(0) successfully |
| — Critical sections (Initialize/Enter/Leave/…) | K32 | **NOOP** | success without synchronization | acceptable single-threaded; regression test 12 |
| — RtlCaptureContext | K32 | present | capture helper | unexercised by UPX? verify in Phase-6 |
| GetLastError/SetLastError | K32 | **PARTIAL** | global storage (single-threaded) | trace shows consistent 0s |
| GetModuleHandleA | K32 | **STUB-FAKE** | returns fake handle | same risk class as LoadLibraryA |
| printf/fprintf/fwrite/fputs/… (variadic) | msvcrt | **REAL** (via SysV trampolines, L208-231) | va_list ABI translation, routes to host stdio | printf-family is the trickiest ABI surface; verified by UPX output ("terminate called…" printed through it) |

## Full inventory (161 code imports)

**KERNEL32.DLL (70):** AddVectoredExceptionHandler, CloseHandle, CreateEventA, CreateSemaphoreA, DebugBreak, DeleteCriticalSection, DuplicateHandle, EnterCriticalSection, GetConsoleCursorInfo, GetConsoleMode, GetConsoleScreenBufferInfo, GetCurrentProcess, GetCurrentProcessId, GetCurrentThread, GetCurrentThreadId, GetFileTime, GetHandleInformation, GetLastError, GetModuleHandleA, GetProcAddress, GetProcessAffinityMask, GetStartupInfoA, GetStdHandle, GetSystemTimeAsFileTime, GetThreadContext, GetThreadPriority, GetTickCount, InitializeCriticalSection, IsDBCSLeadByteEx, ExitProcess, IsDebuggerPresent, LoadLibraryA, LeaveCriticalSection, MultiByteToWideChar, OpenProcess, OutputDebugStringA, QueryPerformanceCounter, QueryPerformanceFrequency, RaiseException, ReleaseSemaphore, RemoveVectoredExceptionHandler, ResetEvent, ResumeThread, RtlCaptureContext, RtlLookupFunctionEntry, RtlUnwindEx, RtlVirtualUnwind, ScrollConsoleScreenBufferA, SetConsoleCursorInfo, SetConsoleCursorPosition, SetConsoleTextAttribute, SetEvent, SetFileTime, SetLastError, SetProcessAffinityMask, SetThreadContext, SetThreadPriority, SetUnhandledExceptionFilter, Sleep, SuspendThread, TlsAlloc, TlsGetValue, TlsSetValue, TryEnterCriticalSection, VirtualProtect, VirtualQuery, WaitForMultipleObjects, WaitForSingleObject, WideCharToMultiByte, WriteConsoleOutputA

**msvcrt.dll (91):** __C_specific_handler, __getmainargs, __iob_func, __lconv_init, ___lc_codepage_func, ___mb_cur_max_func, __set_app_type, __setusermatherr, _amsg_exit, _beginthreadex, _cexit, _chmod, _close, _dup, _endthreadex, _errno, _fstat64, _fileno, _get_osfhandle, _gmtime64, _initterm, _isatty, _kbhit, _localtime64, _lock, _lseeki64, _onexit, _open, _read, _rename, _rmdir, _sopen, _setmode, _setjmp, _stat64, _strdup, _stricmp, _time64, _ultoa, _unlink, _unlock, _wopen, _write, abort, atoi, calloc, clock, exit, fflush, fprintf, fputc, fputs, free, fwrite, getc, getenv, islower, isspace, isupper, isxdigit, localeconv, longjmp, malloc, memchr, memcmp, memcpy, memmove, memset, printf, puts, qsort, rand, realloc, rename, signal, srand, strchr, strcmp, strcpy, strcspn, strerror, strlen, strncmp, strrchr, strstr, strtol, strtoul, tolower, ungetc, vfprintf, wcstombs, wcslen

**Unresolved-import policy:** slot left unchanged (holds name RVA) + stderr WARN. This policy turned BUG-025's missing stubs into a jump-to-name-RVA crash for the packed target. The general rule should be: on unresolved import, patch the IAT with a trap stub that reports and aborts loudly (Phase-6 backlog item).

## Data imports (4): `_acmdln`, `__initenv`, `_commode`, `_fmode` — pointer patching to loader-owned storage (REAL). `_errno` explicitly removed (function, not data).

## Ownership & memory-effect notes (where established)
- malloc/free/realloc/calloc → host libc heap: ownership real; realloc use-after-free *trace* smell logged (build warnings)
- LoadLibraryA/GetModuleHandleA handles → fabricated constants: ownership meaningless by design
- Exception object lifetime: allocated by PE's libgcc via malloc; `_Unwind_DeleteException` not implemented (backlog)
- Trace side effects: every mw_* writes MW_TRACE → file + stderr; use-after-free smell in realloc trace path
