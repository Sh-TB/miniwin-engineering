# EXP-040 — PARALLEL-PATH RECORD (2026-10-17): Wave-11 real-program unblock (7-Zip 26.04, jq 1.7.1)

> **SUPERSEDED by origin/main (OPENSRC waves, BUG-044/050/052..060).** This session ran in
> parallel and converged on the same findings via an independent path. Bug IDs used here
> were LOCAL to this session and were later remapped to the canonical ledger:
>   local local-BUG-032 (import trap stubs)   -> alternative design; origin solved via registry +
>                                          LoadLibraryW api-ms-win pseudo-DLLs (BUG-057)
>   local local-BUG-034 (4-byte wchar_t/UTF-16) -> canonical **BUG-050** (guest wide-string ABI)
>   local local-BUG-035 (console struct 48>22)  -> canonical **BUG-044** (0xC0000409 stack canary)
> Historical value: independent confirmation of BUG-044/BUG-050 root causes, the RTTI
> throw-type decoder, the UTF-16 unit-dump diagnostic, and the 7z2604 source-verified
> argument flow. Do NOT reuse the local IDs below; the ledger's canonical records govern.


## 1. Scope
First real-program wave after G1 (UPX). Targets: **7-Zip 26.04 x64 console** (user-required)
and **jq 1.7.1 x64**. Method: acquire official x64 PEs → 3× baseline runs → local-BUG-032 trap
→ iterate API waves guided by trap frontier → regression + G1 pin after each wave.

## 2. Acquisition
| Program | Source | SHA-256 (first 16) | Format |
|---|---|---|---|
| 7za.exe (26.04) | 7z2604-extra.7z (7-zip.org), extracted via 7zz Linux x64 (py7zr lacks BCJ2) | 15d4c788c148e367 | PE32+ console x86-64, 1,335,808 B |
| jq-windows-amd64.exe (1.7.1) | GitHub jqlang/jq release | 7451fbbf37feffb9 | PE32+ console x86-64, 985,088 B |
Identity records: samples/real/SHA256SUMS.txt. (7zr.exe from 7-zip.org is 32-bit — out of Path-A scope.)

## 3. Baseline (pre-fix)
- 7za: exit 139, RIP=0x134346; 56/145 imports resolved; crash at 3rd `_initterm` initializer.
- jq: exit 139, RIP=0xf3798; 39/145 resolved.
- **Both crash addresses are import hint/name RVAs executed as absolute addresses.**

## 4. local-BUG-032 — unresolved IAT slots hold ILT name-RVA (FIXED)
The resolver left unresolved IAT slots untouched (slot value = ILT entry = name RVA);
`call [iat]` jumped to the RVA as an absolute address. Fix: every unresolved slot is
patched with a synthesized 22-byte stub (`movabs rcx,idx; movabs rax,tramp; jmp rax` in an
RWX page); the ms_abi trampoline names the API once and returns 0; crash handler now
prints the last-called unimplemented API. Verified: 7za/jq no longer fault at name-RVA;
first frontier APIs named in trace (7za: IsProcessorFeaturePresent → GetModuleHandleW →
SetConsoleCtrlHandler → _CxxThrowException).

## 5. local-BUG-034 — host wchar_t (4 bytes) used where the PE needs UTF-16LE (FIXED)
7za printed usage for EVERY command line despite correct argv. Ground truth via a
loader-side probe: `GetCommandLineW` buffer bytes = `0037 0000 007a 0000 …` — g_cmdline_wide
was built with mbstowcs into Linux 4-byte wchar_t; 7za's SplitCommandLine read "7\0" →
1 token → usage. 7za's `main(void)` (Windows) ignores argv entirely and parses
GetCommandLineW (confirmed in 7z2604 source, CPP/7zip/UI/Console/Main.cpp L870-900).
Fix: g_cmdline_wide is uint16_t[4096] filled by manual widening; all PE-boundary
converters rewritten for explicit UTF-16 (MultiByteToWideChar / WideCharToMultiByte /
CreateFileW / GetFileAttributesW). The old mbstowcs converter wrote 2× the caller's
buffer (host 4-byte units) — a live corruption vector.
Aftermath: 7za parses commands (`i`, `a`, invalid-command), reaches the scan.

## 6. local-BUG-035 — CONSOLE_SCREEN_BUFFER_INFO 48-byte memset overflow (FIXED)
Real struct = 22 bytes on x64. The 48-byte memset wrote 26 bytes past the caller's
object, corrupting adjacent PE globals: 7za's `g_BreakCounter` became nonzero mid-scan →
`TestBreakSignal()` true → `throw CSystemException(E_ABORT)` → abort. Fixed to exact
22-byte struct copy. (E_ABORT persists post-fix — see §8 open frontier.)

## 7. Wave-11 API additions (all verified by trap/trace/regression)
kernel32: VirtualAlloc/VirtualFree (mmap-based + reservation table),
IsProcessorFeaturePresent, GetSystemInfo, GlobalMemoryStatusEx, GetVersion, GetOEMCP,
SetFileApisToOEM/ANSI, GetModuleHandleW, SetConsoleCtrlHandler, SetConsoleMode, LocalFree,
FormatMessageW, GetCommandLineW, ReadFile, WriteFile, CreateFileW, SetFilePointer,
GetFileSize, SetEndOfFile, GetFileAttributesW, FindFirstFileW/FindNextFileW/FindClose,
FindFirstStreamW/FindNextStreamW (honest empty enumeration), GetCurrentDirectoryW.
msvcrt: wcscmp, wcsstr, fgetc (MSVC _iobuf layout, _file@28), _purecall,
?terminate@@YAXXZ, ??1type_info@@UEAA@XZ, __dllonexit, _exit, _c_exit, _XcptFilter,
__CxxFrameHandler (ContinueSearch=1), _CxxThrowException (honest terminate + RTTI
decoder: ThrowInfo→CatchableTypeArray→TypeDescriptor.name, prints the thrown class),
_set_app_type, __p__fmode/__p__commode/__p___argc/__p___argv/__p___wargv/__p__environ/
__p__wenviron (valid empty arrays, not NULL), _get_initial_narrow_environment,
_set_new_mode, _set_invalid_parameter_handler, _configure_wide_argv/_initialize_wide_environment
(no-ops — UCRT internals self-serve from PEB; jq passes garbage RCX=0xb/0x2).
Data imports: _iob (3×48-byte MSVC layout, _file=0/1/2).

## 8. Results after Wave 11
| Program | Command | Before | After |
|---|---|---|---|
| UPX 4.2.4 (G1) | --version | exit 0 | **exit 0** (12/12 regression, G1 PIN OK 3×) |
| 7za 26.04 | (no args) | exit 139 at RVA-jump | **exit 0 — full usage banner** |
| 7za 26.04 | i / a / bad-cmd | n/a | args parsed; banner+copyright; scan starts ("Scanning the drive:"), progress dots |
| 7za 26.04 a | archive add | n/a | **frontier: CSystemException(0x80004004 E_ABORT)** — g_BreakCounter still nonzero post-local-BUG-035; needs runtime tooling (no gdb in env) |
| jq 1.7.1 | --version | exit 139 at RVA-jump | CRT init reaches `malloc(24)` post-SetUnhandledExceptionFilter; **frontier: in-libc crash at jq startup** |

7za unresolved imports: 83 → **49**; jq: 106 → **90**.

## 9. Evidence
- miniwin-results/7za.exe/api_trace.json, miniwin-results/jq-windows-amd64.exe/api_trace.json
- samples/real/SHA256SUMS.txt, samples/real/unresolved-7za.txt
- /home/z/my-project/download/miniwin_execution_proof_20261017.png (execution-proof sheet,
  TASK-PNG-002/003; renderer script /home/z/my-project/scripts/render_proof.py)
- evidence/master/g1/20261017_041439/ (G1 pin after all Wave-11 changes)

## 10. Falsifiable predictions
- P-1: implementing MSVC C++ EH (TASK-API-030) converts 7za's E_ABORT abort into a normal
  caught error path ("Error: …" + exit code) without touching the scan APIs.
- P-2: 7za archive-add completes once (a) E_ABORT source resolved and (b) OpenProcessToken
  family returns valid handles (privilege path) or is skipped via error-tolerant stubs.
- P-3: jq's in-libc crash disappears once its CRT's `_configure_wide_argv` gets a real
  PEB-derived wide-argv implementation (PEB ProcessParameters already carries CmdBuffer).

## 11. Notes on method
7z2604 source used as ground truth for the Windows argument flow (SplitCommandLine +
Delete(0) + usage-on-empty); objdump + pefile used for IAT/RTTI ground truth. All temp
debug instrumentation removed before commit; regression 12/12 + G1 pin re-run after.
