# Wine vs MiniWin differential

- generated: 2026-11-01T08:06:26.154143+00:00
- wine events: 30788 (422 unique functions)
- miniwin events: 1539 (8 unique functions)

## First meaningful divergence

- `msvcrt.__set_app_type` — app-attributed call with zero MiniWin calls (wine calls: 1)

## Missing calls (Wine observed, MiniWin never)

COVERAGE NOTE: MiniWin traces only the calls its mw_*
implementations log — a function listed here may be
implemented-but-unlogged rather than absent. Verify against
the compat database before treating it as a gap.

| function | wine calls | app-attributed |
|---|---|---|
| msvcrt.__getmainargs | 1 | True |
| ucrtbase.__p___argc | 2 | False |
| ucrtbase.__p___argv | 2 | False |
| msvcrt.__set_app_type | 1 | True |
| ucrtbase.__stdio_common_vswprintf | 8 | False |
| ucrtbase._close | 2 | False |
| ucrtbase._configure_narrow_argv | 2 | False |
| ucrtbase._fstat64i32 | 1 | False |
| ucrtbase._get_initial_narrow_environment | 2 | False |
| ucrtbase._initialize_narrow_environment | 2 | False |
| msvcrt._initterm | 2 | True |
| msvcrt._isatty | 3 | True |
| msvcrt._onexit | 7 | True |
| ucrtbase._read | 1 | False |
| ucrtbase._set_app_type | 2 | False |
| ucrtbase._strdup | 3 | False |
| ucrtbase._stricmp | 1 | False |
| ntdll._vsnwprintf | 25 | False |
| ucrtbase._wcsdup | 3 | False |
| msvcrt._wcsicmp | 60 | False |
| ucrtbase._wcsnicmp | 12 | False |
| ucrtbase._wgetenv | 3 | False |
| advapi32.AdjustTokenPrivileges | 3 | True |
| KERNEL32.BaseThreadInitThunk | 2 | False |
| ucrtbase.calloc | 6 | False |
| KERNEL32.CloseHandle | 26 | True |
| combase.CoGetMalloc | 1 | False |
| KERNEL32.CompareFileTime | 1 | True |
| combase.CoTaskMemAlloc | 4 | False |
| KERNEL32.CreateEventW | 13 | True |
| KERNEL32.CreateFileW | 6 | True |
| KERNEL32.CreateIoCompletionPort | 1 | False |
| KERNEL32.CreateJobObjectW | 1 | False |
| KERNEL32.CreateMutexW | 1 | False |
| KERNEL32.CreateNamedPipeA | 1 | False |
| kernelbase.CreateNamedPipeW | 1 | False |
| kernelbase.CreateProcessW | 1 | False |
| KERNEL32.CreateSemaphoreW | 5 | True |
| KERNEL32.CreateThread | 5 | False |
| KERNEL32.CreateThreadpoolCleanupGroup | 1 | False |
| KERNEL32.DeleteFileW | 1 | True |
| KERNEL32.DisableThreadLibraryCalls | 14 | False |
| DllMain[OLEAUT32.dll] | 2 | False |
| msvcrt.fflush | 1 | True |
| KERNEL32.FindClose | 6 | True |
| KERNEL32.FindFirstFileW | 6 | True |
| KERNEL32.FlsAlloc | 3 | False |
| KERNEL32.FlsSetValue | 8 | False |
| msvcrt.fputc | 19 | True |
| msvcrt.fputs | 33 | True |
| ws2_32.freeaddrinfo | 1 | False |
| KERNEL32.FreeEnvironmentStringsA | 6 | False |
| KERNEL32.FreeEnvironmentStringsW | 1 | False |
| kernelbase.FreeLibrary | 2 | False |
| KERNEL32.GetACP | 6 | False |
| KERNEL32.GetActiveProcessorCount | 1 | True |
| KERNEL32.GetActiveProcessorGroupCount | 1 | True |
| ws2_32.getaddrinfo | 1 | False |
| KERNEL32.GetCommandLineA | 6 | False |
| KERNEL32.GetCommandLineW | 13 | True |
| kernelbase.GetComputerNameExW | 1 | False |
| KERNEL32.GetComputerNameW | 1 | False |
| KERNEL32.GetCPInfo | 12 | False |
| KERNEL32.GetCurrentDirectoryW | 2 | True |
| KERNEL32.GetEnvironmentStringsA | 6 | False |
| KERNEL32.GetEnvironmentStringsW | 1 | False |
| KERNEL32.GetEnvironmentVariableW | 1 | False |
| KERNEL32.GetFileAttributesW | 2 | True |
| KERNEL32.GetFileInformationByHandle | 1 | True |
| KERNEL32.GetFileType | 21 | False |
| ws2_32.gethostname | 1 | False |
| kernelbase.GetLogicalProcessorInformationEx | 4 | False |
| KERNEL32.GetModuleFileNameA | 6 | False |
| KERNEL32.GetModuleFileNameW | 7 | False |
| KERNEL32.GetModuleHandleA | 1 | True |
| KERNEL32.GetModuleHandleW | 21 | True |
| KERNEL32.GetPrivateProfileSectionW | 1 | False |
| KERNEL32.GetProcessAffinityMask | 1 | True |
| KERNEL32.GetStartupInfoA | 6 | False |
| KERNEL32.GetStdHandle | 18 | False |
| KERNEL32.GetStringTypeW | 12 | False |
| KERNEL32.GetSystemDirectoryW | 3 | False |
| kernelbase.GetSystemFirmwareTable | 2 | False |
| KERNEL32.GetUserDefaultLocaleName | 6 | False |
| advapi32.GetUserNameW | 4 | False |
| KERNEL32.GetVersion | 1 | True |
| KERNEL32.GetVersionExW | 6 | False |
| kernelbase.GetWindowsDirectoryW | 2 | False |
| KERNEL32.GlobalMemoryStatusEx | 1 | True |
| KERNEL32.HeapCreate | 3 | False |
| KERNEL32.HeapFree | 4191 | False |
| KERNEL32.InitializeCriticalSectionEx | 38 | False |
| KERNEL32.IsBadStringPtrW | 13 | False |
| KERNEL32.IsProcessorFeaturePresent | 9 | True |
| KERNEL32.IsValidCodePage | 6 | False |
| kernelbase.IsWow64Process | 1 | False |
| kernelbase.KernelBaseGetGlobalData | 3 | False |
| KERNEL32.LCMapStringW | 24 | False |
| ntdll.LdrAddRefDll | 3 | False |
| ntdll.LdrDisableThreadCalloutsForDll | 24 | False |

## Return-value mismatches (deterministic functions)

Pointer-valued returns are class-compared (each runtime maps
its own address space); only null-vs-non-null and numeric
differences count.

| function | wine | miniwin |
|---|---|---|
| KERNEL32.GetLastError | 00000000 | 1300 |

## Frequency deltas

| function | wine | miniwin |
|---|---|---|
| msvcrt._beginthreadex | 3 | 1 |
| msvcrt.free | 1904 | 1 |
| KERNEL32.GetLastError | 3 | 4 |
| kernelbase.GetProcAddress | 14 | 2 |
| msvcrt.malloc | 1529 | 1517 |
| KERNEL32.ReadFile | 8 | 5 |
