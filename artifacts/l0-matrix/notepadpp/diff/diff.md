# Wine vs MiniWin differential

- generated: 2026-11-01T06:44:48.862082+00:00
- wine events: 86447 (672 unique functions)
- miniwin events: 18633 (63 unique functions)

## First meaningful divergence

- `KERNEL32.LoadLibraryExW` — app-attributed call with zero MiniWin calls (wine calls: 8)

## Missing calls (Wine observed, MiniWin never)

COVERAGE NOTE: MiniWin traces only the calls its mw_*
implementations log — a function listed here may be
implemented-but-unlogged rather than absent. Verify against
the compat database before treating it as a gap.

| function | wine calls | app-attributed |
|---|---|---|
| uxtheme.132 | 1 | True |
| uxtheme.135 | 2 | True |
| uxtheme.136 | 1 | True |
| shell32.188 | 3 | False |
| ucrtbase.__p___argc | 3 | False |
| ucrtbase.__p___argv | 2 | False |
| ucrtbase.__p___wargv | 1 | False |
| ucrtbase.__stdio_common_vsscanf | 1 | False |
| ucrtbase.__stdio_common_vswprintf | 18 | False |
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
| ucrtbase._strdup | 30 | False |
| ucrtbase._stricmp | 1 | False |
| ntdll._vsnwprintf | 27 | False |
| ucrtbase._wcsdup | 23 | False |
| ucrtbase._wcsicmp | 278 | False |
| ucrtbase._wcsnicmp | 35 | False |
| ucrtbase._wgetenv | 3 | False |
| KERNEL32.AddAtomW | 10 | False |
| user32.AdjustWindowRect | 1 | False |
| kernelbase.AppPolicyGetProcessTerminationMethod | 1 | True |
| KERNEL32.BaseThreadInitThunk | 3 | False |
| ucrtbase.calloc | 58 | False |
| user32.ChangeDisplaySettingsExW | 1 | False |
| user32.CharLowerW | 3 | False |
| advapi32.CheckTokenMembership | 1 | True |
| user32.ClipCursor | 1 | False |
| KERNEL32.CloseHandle | 33 | True |
| advapi32.CloseServiceHandle | 1 | False |
| combase.CoInitializeEx | 2 | False |
| combase.CoMarshalInterface | 1 | False |
| KERNEL32.CopyFileW | 5 | True |
| combase.CoRegisterClassObject | 2 | False |
| combase.CoTaskMemAlloc | 2 | False |
| combase.CoUninitialize | 1 | False |
| KERNEL32.CreateActCtxW | 1 | False |
| gdi32.CreateBitmap | 1 | False |
| KERNEL32.CreateDirectoryW | 5 | True |
| KERNEL32.CreateEventW | 12 | False |
| KERNEL32.CreateFileA | 2 | False |
| KERNEL32.CreateFileW | 13 | True |
| KERNEL32.CreateIoCompletionPort | 1 | False |
| KERNEL32.CreateJobObjectW | 1 | False |
| KERNEL32.CreateMutexW | 8 | True |
| KERNEL32.CreateNamedPipeA | 4 | False |
| kernelbase.CreateNamedPipeW | 4 | False |
| gdi32.CreatePatternBrush | 1 | False |
| gdi32.CreatePen | 36 | True |
| kernelbase.CreateProcessW | 1 | False |
| KERNEL32.CreateSemaphoreW | 1 | False |
| gdi32.CreateSolidBrush | 73 | True |
| combase.CreateStreamOnHGlobal | 1 | False |
| KERNEL32.CreateThread | 28 | True |
| KERNEL32.CreateThreadpoolCleanupGroup | 1 | False |
| user32.DdeCreateStringHandleW | 7 | False |
| user32.DdeFreeStringHandle | 6 | False |
| user32.DdeInitializeW | 1 | False |
| user32.DdeNameService | 7 | False |
| user32.DdeUninitialize | 1 | False |
| KERNEL32.DeleteAtom | 10 | False |
| gdi32.DeleteObject | 108 | True |
| user32.DestroyWindow | 1 | False |
| KERNEL32.DisableThreadLibraryCalls | 31 | False |
| user32.DispatchMessageW | 20 | False |
| DllMain[OLEAUT32.dll] | 2 | False |
| DllMain[winspool.drv] | 2 | False |
| dwmapi.DwmGetColorizationColor | 2 | True |
| dwmapi.DwmSetWindowAttribute | 1 | True |
| user32.EnumDisplayDevicesW | 2 | False |
| user32.EnumDisplaySettingsExW | 1 | False |
| KERNEL32.FindActCtxSectionGuid | 4 | False |
| kernelbase.FindClose | 1 | False |
| KERNEL32.FindFirstFileW | 1 | True |
| kernelbase.FindResourceExW | 3 | False |
| kernelbase.FindResourceW | 127 | False |
| KERNEL32.FlsGetValue | 33 | True |
| KERNEL32.FlsSetValue | 36 | True |
| KERNEL32.FlushFileBuffers | 2 | True |
| ucrtbase.free | 221 | False |
| ws2_32.freeaddrinfo | 1 | False |
| KERNEL32.FreeEnvironmentStringsA | 8 | False |
| KERNEL32.FreeEnvironmentStringsW | 3 | True |
| kernelbase.FreeLibrary | 5 | False |
| kernelbase.FreeResource | 76 | False |
| KERNEL32.GetACP | 9 | True |
| ws2_32.getaddrinfo | 1 | False |
| user32.GetAncestor | 17 | False |
| KERNEL32.GetAtomNameW | 15 | False |
| user32.GetCaretBlinkTime | 1 | True |
| KERNEL32.GetCommandLineW | 18 | True |
| KERNEL32.GetComputerNameA | 1 | False |

## Return-value mismatches (deterministic functions)

Pointer-valued returns are class-compared (each runtime maps
its own address space); only null-vs-non-null and numeric
differences count.

| function | wine | miniwin |
|---|---|---|
| advapi32.AllocateAndInitializeSid | 00000001 | SID 0x4753bc0 |
| ole32.CoInitialize | 00000000 | S_OK |
| advapi32.FreeSid | 00000000 | NULL |
| kernelbase.GetFileVersionInfoW | 00000001 | TRUE (724 bytes) |
| ole32.OleInitialize | 00000000 | S_OK |
| user32.RemoveMenu | 00000001 | TRUE count->16 |
| user32.SetWindowPos | 00000001 | TRUE |
| shell32.SHGetFolderPathW | 00000000 | 'C:\Users\MiniWin\AppData\Roaming' S_OK |
| kernelbase.VerQueryValueW | 00000001 | FIXEDFILEINFO 52B |

## Frequency deltas

| function | wine | miniwin |
|---|---|---|
| user32.CreateWindowExW | 9 | 1 |
| KERNEL32.FlsAlloc | 5 | 2 |
| KERNEL32.GetCommandLineA | 9 | 1 |
| KERNEL32.GetFileAttributesExW | 23 | 2 |
| kernelbase.GetFileVersionInfoSizeW | 2 | 1 |
| kernelbase.GetFileVersionInfoW | 2 | 1 |
| KERNEL32.GetLastError | 118 | 972 |
| user32.GetMessageW | 21 | 1 |
| kernelbase.GetProcAddress | 30 | 51 |
| KERNEL32.GetStdHandle | 27 | 3 |
| user32.GetSysColor | 29 | 70 |
| user32.GetWindowLongPtrW | 10 | 268 |
| user32.LoadIconW | 4 | 2 |
| user32.LoadImageW | 6 | 54 |
| ole32.OleInitialize | 1 | 4 |
| user32.RegisterClassExW | 6 | 4 |
| user32.RegisterClassW | 27 | 5 |
| user32.RegisterClipboardFormatW | 4 | 16 |
| user32.RegisterWindowMessageW | 17 | 3 |
| user32.SendMessageW | 1 | 217 |
| user32.SetWindowLongPtrW | 4 | 43 |
| user32.SetWindowPos | 1 | 6 |
| shell32.SHGetFolderPathW | 4 | 1 |
| kernelbase.SizeofResource | 76 | 9 |
| user32.SystemParametersInfoW | 4 | 23 |
| kernelbase.VerQueryValueW | 4 | 1 |
