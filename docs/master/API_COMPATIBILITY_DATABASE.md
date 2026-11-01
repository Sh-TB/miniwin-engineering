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

## Full inventory (188 code imports; 162 at G1, +26 wave-2 appcompat 2026-10-15)


### Wave 1+2 additions (2026-10-15, EXP-040 appcompat sweep)
- msvcrt: atexit, _exit, setvbuf (mode mapping 0/64/4 -> glibc), _XcptFilter (CONTINUE_SEARCH stub), _aligned_malloc/_aligned_free, _assert, _strtoi64/_strtoui64, wcstol/wcstoul/_wcstoi64/_wcstoui64 (ASCII-safe UTF-16 passthrough), fputwc, fwprintf (wide fmt repack), FlsAlloc/FlsGetValue/FlsSetValue (__thread slot array).
- kernel32: GetCPInfo (SBCS honest stub), SRW locks (Acquire/Try/Release Exclusive via pthread_rwlock registry), condition variables (Sleep/Wake/WakeAll, timed-wait semantics), InitOnceExecuteOnce, RtlRestoreContext (full GPR+XMM+RIP restore), GetModuleHandleW, GetThreadId, SwitchToThread, WaitForSingleObjectEx.
- Native TLS (TASK-TLS-001): TEB+0x58 ThreadLocalStoragePointer, static TLS slot 0 (+ template copy, zero-fill, __tls_index, DLL_PROCESS_ATTACH callbacks), TlsAlloc/GetValue/SetValue share the slot array (dynamic from slot 1).


**KERNEL32.DLL (70):** AddVectoredExceptionHandler, CloseHandle, CreateEventA, CreateSemaphoreA, DebugBreak, DeleteCriticalSection, DuplicateHandle, EnterCriticalSection, GetConsoleCursorInfo, GetConsoleMode, GetConsoleScreenBufferInfo, GetCurrentProcess, GetCurrentProcessId, GetCurrentThread, GetCurrentThreadId, GetFileTime, GetHandleInformation, GetLastError, GetModuleHandleA, GetProcAddress, GetProcessAffinityMask, GetStartupInfoA, GetStdHandle, GetSystemTimeAsFileTime, GetThreadContext, GetThreadPriority, GetTickCount, InitializeCriticalSection, IsDBCSLeadByteEx, ExitProcess, IsDebuggerPresent, LoadLibraryA, LeaveCriticalSection, MultiByteToWideChar, OpenProcess, OutputDebugStringA, QueryPerformanceCounter, QueryPerformanceFrequency, RaiseException, ReleaseSemaphore, RemoveVectoredExceptionHandler, ResetEvent, ResumeThread, RtlCaptureContext, RtlLookupFunctionEntry, RtlUnwindEx, RtlVirtualUnwind, ScrollConsoleScreenBufferA, SetConsoleCursorInfo, SetConsoleCursorPosition, SetConsoleTextAttribute, SetEvent, SetFileTime, SetLastError, SetProcessAffinityMask, SetThreadContext, SetThreadPriority, SetUnhandledExceptionFilter, Sleep, SuspendThread, TlsAlloc, TlsGetValue, TlsSetValue, TryEnterCriticalSection, VirtualProtect, VirtualQuery, WaitForMultipleObjects, WaitForSingleObject, WideCharToMultiByte, WriteConsoleOutputA

**msvcrt.dll (91):** __C_specific_handler, __getmainargs, __iob_func, __lconv_init, ___lc_codepage_func, ___mb_cur_max_func, __set_app_type, __setusermatherr, _amsg_exit, _beginthreadex, _cexit, _chmod, _close, _dup, _endthreadex, _errno, _fstat64, _fileno, _get_osfhandle, _gmtime64, _initterm, _isatty, _kbhit, _localtime64, _lock, _lseeki64, _onexit, _open, _read, _rename, _rmdir, _sopen, _setmode, _setjmp, _stat64, _strdup, _stricmp, _time64, _ultoa, _unlink, _unlock, _wopen, _write, abort, atoi, calloc, clock, exit, fflush, fprintf, fputc, fputs, free, fwrite, getc, getenv, islower, isspace, isupper, isxdigit, localeconv, longjmp, malloc, memchr, memcmp, memcpy, memmove, memset, printf, puts, qsort, rand, realloc, rename, signal, srand, strchr, strcmp, strcpy, strcspn, strerror, strlen, strncmp, strrchr, strstr, strtol, strtoul, tolower, ungetc, vfprintf, wcstombs, wcslen

**Unresolved-import policy:** slot left unchanged (holds name RVA) + stderr WARN. This policy turned BUG-025's missing stubs into a jump-to-name-RVA crash for the packed target. The general rule should be: on unresolved import, patch the IAT with a trap stub that reports and aborts loudly (Phase-6 backlog item).


## wine-l0 W8 additions (2026-11-02, tooltip/scroll/icon/control wave — SW-1591..1595)

Contract: wine-9.0 `dlls/comctl32/tooltips.c` (L360-470 GetDispInfo, L836 Show, L852 Hide, L1486-1545 RelayEvent), `dlls/comctl32/toolbar.c` (L1995-2060 per-button tools, L6355 TOOLBAR_TTGetDispInfo), `dlls/user32/scroll.c`, `include/commctrl.h` (TTM_/TTN_/TTF_ x64 values), `winuser.h` (SCROLLINFO/MSG/LB_/CB_/SS_/BS_ x64 layouts). Evidence: `artifacts/wine-l0-w8/` (15 programs, 137 checks, 3x determinism, G1 12/12).

| API / message family | Dll | Status | Notes |
| --- | --- | --- | --- |
| TTM_ADDTOOL A(0x404)/W(0x432) + TOOLINFO x64 parse | comctl32 (engine) | **REAL** | replace-by-(hwnd,uId); direct text / INTRESOURCE→LoadStringW / LPSTR_TEXTCALLBACK deferred; bad cbSize honestly rejected |
| TTM_DELTOOL / NEWTOOLRECT / GETTOOLINFO / SETTOOLINFO / UPDATETIPTEXT / GETTEXT / ENUMTOOLS / GETCURRENTTOOL / HITTEST / GETTOOLCOUNT / ADJUSTRECT / GETBUBBLESIZE / WINDOWFROMPOINT / SET-GETMARGIN / SET-GETTIPCOLOR / SET-GETDELAYTIME / SET-GETMAXTIPWIDTH / SETTITLE / ACTIVATE / POP / POPUP / UPDATE | comctl32 (engine) | **REAL** | full TTM protocol on Tooltips_Class32 children; deterministic immediate ID_TIMERSHOW (traced) |
| TTM_RELAYEVENT (0x407) | comctl32 (engine) | **REAL** | winuser.h MSG x64 layout: pt@36 (v1 read `time`); WM_MOUSEMOVE→tool-from-point→show, button-down→hide |
| TTN_NEEDTEXT (GETDISPINFO A −520 / W −530) | comctl32 (engine) | **REAL** | NMTTDISPINFO x64 136B delivered to tool hwnd; szText/direct/INTRESOURCE/recursive-callback/parent-fallback reply forms per wine L360-470 |
| TTN_SHOW (−521) / TTN_POP (−522) | comctl32 (engine) | **REAL** | delivered to the tool hwnd; toolbar-owned tools consume internally (wine toolbar.c L6355 parity) |
| Toolbar button tips (TBSTYLE_TOOLTIPS) | comctl32 (engine) | **REAL** | TOOLBAR_TTGetDispInfo model: szText filled from the TB_ADDSTRINGW pool by idCommand |
| SetScrollInfo / GetScrollInfo | user32 | **REAL** (was STUB-FAKE) | SCROLLINFO x64 offsets fixed (nPos@20 not nPage@16); SIF_RANGE/PAGE/POS/TRACKPOS honored; unified (hwnd,bar) store — v1 GetScrollInfo always returned zeros |
| SetScrollRange/GetScrollRange/SetScrollPos/GetScrollPos | user32 | **REAL** | unified with the g_scroll store (v1 had a parallel pair the Info family never saw) |
| WM_HSCROLL/WM_VSCROLL on SCROLLBAR ctl + EDIT | user32 | **REAL** | SB_LINEUP..SB_ENDSCROLL codes adjust pos (thumb param, clamp); SCROLLBAR re-notifies parent (hWnd lParam), EDIT scrolls silently |
| SCROLLBAR control painting (SBS_HORZ/SBS_VERT/SBS_SIZEGRIP) | user32 | **REAL** | arrows/track/thumb geometry per scroll.c DrawArrows/DrawInterior; thumb sized by nPage, positioned by nPos |
| BM_SETCHECK (0xF1) | user32 | **REAL** (was missing) | checkbox/radio state stored; returns 0 (wine button.c) |
| WM_SETIMAGE (0x172) on STATIC/BUTTON | user32 | **REAL** | IMAGE_ICON/BITMAP stored, returns PREVIOUS image (wine static.c/button.c) |
| SS_ICON creation param + SS_*FRAME/SS_*RECT/ETCHED* painting | user32 | **REAL** | full SS_ decode in the compositor; icon blit via the mw_icon plane (same source as WinRAR art) |
| BS_GROUPBOX/BS_ICON/checkbox/radio painting | user32 | **REAL** | etched group frames, real check states, icon buttons |
| LB_ADDSTRING/INSERTSTRING/DELETESTRING/RESETCONTENT/SET-GETCURSEL/GETTEXT/GETTEXTLEN/GETCOUNT | user32 | **REAL** | LB_ADDSTRING reads lParam (v1 read wParam); 32x48 UTF-16 row store + painter |
| CB_SETCURSEL/GETCURSEL (plain COMBOBOX + ComboBoxEx32) | user32 | **REAL** (was hardcoded 0) | cb_cursel store |
| PBM_SETRANGE/SETRANGE32/SETPOS/GETPOS/DELTAPOS/SETSTEP/STEPIT/GETRANGE | comctl32 (engine) | **REAL** | range/pos/step model + segmented painter |
| TBM_SETRANGE/SETRANGEMIN/MAX/GETRANGEMIN/MAX/SETPOS/GETPOS | comctl32 (engine) | **REAL** | class msctls_trackbar32 added to W+A tables (0x91) + painter |
| UDM_SETRANGE/GETRANGE/SETRANGE32/SETPOS32/GETPOS32 | comctl32 (engine) | **REAL** | class msctls_updown32 added (0x92); MAKELONG(max,min) getrange contract |
| MwChild.cls 16→24 | engine | **FIX** | "tooltips_class32"(16)/"msctls_progress32"(17)/"msctls_trackbar32"(17) were snprintf-truncated to 15+NUL — every cls_is/strcmp silently failed |
| SendMessageA WM_SETTEXT widen | user32 | **FIX** | self-clobbering tmp loop stored "l" for "line one…"; separate byte buffer now |

## Data imports (4): `_acmdln`, `__initenv`, `_commode`, `_fmode` — pointer patching to loader-owned storage (REAL). `_errno` explicitly removed (function, not data).

## Ownership & memory-effect notes (where established)
- malloc/free/realloc/calloc → host libc heap: ownership real; realloc use-after-free *trace* smell logged (build warnings)
- LoadLibraryA/GetModuleHandleA handles → fabricated constants: ownership meaningless by design
- Exception object lifetime: allocated by PE's libgcc via malloc; `_Unwind_DeleteException` not implemented (backlog)
- Trace side effects: every mw_* writes MW_TRACE → file + stderr; use-after-free smell in realloc trace path
