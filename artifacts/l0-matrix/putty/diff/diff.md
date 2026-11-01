# Wine vs MiniWin differential

- generated: 2026-11-01T08:07:20.083967+00:00
- wine events: 27860 (641 unique functions)
- miniwin events: 6364 (40 unique functions)

## First meaningful divergence

- `KERNEL32.LoadLibraryExW` — app-attributed call with zero MiniWin calls (wine calls: 5)

## Missing calls (Wine observed, MiniWin never)

COVERAGE NOTE: MiniWin traces only the calls its mw_*
implementations log — a function listed here may be
implemented-but-unlogged rather than absent. Verify against
the compat database before treating it as a gap.

| function | wine calls | app-attributed |
|---|---|---|
| shell32.188 | 3 | False |
| ucrtbase.__p___argc | 3 | False |
| ucrtbase.__p___argv | 2 | False |
| ucrtbase.__p___wargv | 1 | False |
| ucrtbase.__stdio_common_vsscanf | 1 | False |
| ucrtbase.__stdio_common_vswprintf | 16 | False |
| imm32.__wine_ime_wnd_proc | 32 | False |
| ucrtbase._close | 2 | False |
| ucrtbase._configure_narrow_argv | 2 | False |
| ucrtbase._configure_wide_argv | 1 | False |
| ucrtbase._fstat64i32 | 1 | False |
| ucrtbase._get_initial_narrow_environment | 2 | False |
| ucrtbase._get_initial_wide_environment | 1 | False |
| ucrtbase._initialize_narrow_environment | 2 | False |
| ucrtbase._initialize_wide_environment | 1 | False |
| ucrtbase._read | 1 | False |
| ucrtbase._set_app_type | 3 | False |
| ucrtbase._strdup | 27 | False |
| ucrtbase._stricmp | 1 | False |
| ntdll._vsnwprintf | 27 | False |
| ucrtbase._wcsdup | 23 | False |
| ntdll._wcsicmp | 8 | False |
| ucrtbase._wcsnicmp | 28 | False |
| ucrtbase._wgetenv | 3 | False |
| msgsm32.acm.DriverProc | 11 | False |
| DllMain[msgsm32.acm] | 2 | False |
| KERNEL32.AddAtomW | 10 | False |
| user32.AdjustWindowRect | 1 | False |
| KERNEL32.AreFileApisANSI | 1 | True |
| KERNEL32.BaseThreadInitThunk | 3 | False |
| ucrtbase.calloc | 53 | False |
| user32.ChangeDisplaySettingsExW | 1 | False |
| user32.CharLowerW | 6 | False |
| user32.ClipCursor | 1 | False |
| KERNEL32.CloseHandle | 16 | False |
| advapi32.CloseServiceHandle | 1 | False |
| combase.CoInitializeEx | 2 | False |
| combase.CoRegisterClassObject | 2 | False |
| combase.CoTaskMemAlloc | 2 | False |
| KERNEL32.CreateActCtxW | 1 | False |
| KERNEL32.CreateEventW | 13 | False |
| KERNEL32.CreateFileA | 2 | False |
| KERNEL32.CreateFileW | 2 | False |
| gdi32.CreateFontA | 1 | False |
| KERNEL32.CreateIoCompletionPort | 1 | False |
| KERNEL32.CreateJobObjectW | 1 | False |
| KERNEL32.CreateMutexW | 4 | False |
| KERNEL32.CreateNamedPipeA | 3 | False |
| kernelbase.CreateNamedPipeW | 3 | False |
| gdi32.CreatePatternBrush | 1 | False |
| kernelbase.CreateProcessW | 1 | False |
| KERNEL32.CreateThread | 5 | False |
| KERNEL32.CreateThreadpoolCleanupGroup | 1 | False |
| user32.CreateWindowExW | 8 | False |
| user32.DdeCreateStringHandleW | 7 | False |
| user32.DdeFreeStringHandle | 6 | False |
| user32.DdeInitializeW | 1 | False |
| user32.DdeNameService | 7 | False |
| user32.DdeUninitialize | 1 | False |
| KERNEL32.DeleteAtom | 10 | False |
| gdi32.DeleteObject | 1 | False |
| user32.DestroyWindow | 1 | False |
| KERNEL32.DisableThreadLibraryCalls | 38 | False |
| user32.DispatchMessageW | 18 | False |
| DllMain[sspicli.dll] | 1 | False |
| msacm32.DriverProc | 10 | False |
| DllMain[winspool.drv] | 1 | False |
| user32.EnumDisplayDevicesW | 2 | False |
| user32.EnumDisplaySettingsExW | 1 | False |
| KERNEL32.FindActCtxSectionGuid | 2 | False |
| kernelbase.FindClose | 1 | False |
| kernelbase.FindFirstFileW | 1 | False |
| KERNEL32.FindResourceA | 3 | False |
| kernelbase.FindResourceExW | 3 | False |
| kernelbase.FindResourceW | 114 | False |
| KERNEL32.FlsGetValue | 91 | True |
| KERNEL32.FlsSetValue | 12 | True |
| KERNEL32.FlushFileBuffers | 2 | False |
| ucrtbase.free | 212 | False |
| ws2_32.freeaddrinfo | 1 | False |
| KERNEL32.FreeEnvironmentStringsA | 8 | False |
| KERNEL32.FreeEnvironmentStringsW | 3 | True |
| KERNEL32.FreeLibrary | 17 | False |
| kernelbase.FreeResource | 68 | False |
| KERNEL32.GetACP | 9 | True |
| ws2_32.getaddrinfo | 1 | False |
| user32.GetAncestor | 16 | False |
| KERNEL32.GetAtomNameW | 15 | False |
| KERNEL32.GetCommandLineW | 18 | True |
| KERNEL32.GetComputerNameA | 1 | False |
| kernelbase.GetComputerNameExW | 2 | False |
| KERNEL32.GetComputerNameW | 1 | False |
| KERNEL32.GetCPInfo | 18 | True |
| user32.GetDC | 1 | False |
| KERNEL32.GetEnvironmentStringsA | 8 | False |
| KERNEL32.GetEnvironmentStringsW | 3 | True |
| kernelbase.GetEnvironmentVariableA | 2 | False |
| KERNEL32.GetEnvironmentVariableW | 1 | False |
| kernelbase.GetFileSize | 6 | False |
| kernelbase.GetFileTime | 6 | False |

## Return-value mismatches (deterministic functions)

Pointer-valued returns are class-compared (each runtime maps
its own address space); only null-vs-non-null and numeric
differences count.

| function | wine | miniwin |
|---|---|---|
| ole32.CoInitialize | 00000000 | S_OK |
| user32.CreateWindowExA | 00000000 | HWND 0x10001 |
| user32.SetActiveWindow | 00000000 | prev (nil) |
| KERNEL32.SetDefaultDllDirectories | 00000001 | TRUE |
| user32.SetWindowLongPtrW | 6ffffff478f0 | 0x0 |
| user32.SetWindowPos | 00000001 | TRUE |
| user32.ShowWindow | 00000000 | TRUE |

## Frequency deltas

| function | wine | miniwin |
|---|---|---|
| ole32.CoInitialize | 2 | 1 |
| KERNEL32.FlsAlloc | 5 | 2 |
| KERNEL32.GetCommandLineA | 9 | 1 |
| kernelbase.GetProcAddress | 31 | 65 |
| KERNEL32.GetStdHandle | 27 | 3 |
| secur32.GetUserNameExA | 3 | 1 |
| user32.LoadImageW | 6 | 1 |
| KERNEL32.ReadFile | 1 | 2 |
| user32.RegisterClassW | 25 | 1 |
| user32.SetWindowLongPtrW | 1 | 2 |
| user32.SetWindowPos | 1 | 5 |
| user32.ShowWindow | 2 | 3 |
| KERNEL32.SizeofResource | 1 | 2 |
