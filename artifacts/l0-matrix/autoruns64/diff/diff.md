# Wine vs MiniWin differential

- generated: 2026-11-01T06:45:24.846931+00:00
- wine events: 313847 (719 unique functions)
- miniwin events: 199 (28 unique functions)

## First meaningful divergence

- `KERNEL32.LoadLibraryExW` — app-attributed call with zero MiniWin calls (wine calls: 6)

## Missing calls (Wine observed, MiniWin never)

COVERAGE NOTE: MiniWin traces only the calls its mw_*
implementations log — a function listed here may be
implemented-but-unlogged rather than absent. Verify against
the compat database before treating it as a gap.

| function | wine calls | app-attributed |
|---|---|---|
| uxtheme.132 | 1 | True |
| uxtheme.135 | 2 | False |
| shell32.188 | 3 | False |
| ucrtbase.__p___argc | 3 | False |
| ucrtbase.__p___argv | 2 | False |
| ucrtbase.__p___wargv | 1 | False |
| ucrtbase.__stdio_common_vsscanf | 1 | False |
| ucrtbase.__stdio_common_vswprintf | 16 | False |
| imm32.__wine_ime_wnd_proc | 28 | False |
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
| ucrtbase._wcsdup | 18 | False |
| ucrtbase._wcsicmp | 365 | False |
| ucrtbase._wcsnicmp | 19 | False |
| ucrtbase._wgetenv | 3 | False |
| KERNEL32.AddAtomW | 10 | False |
| user32.AdjustWindowRect | 1 | False |
| atlthunk.AtlThunk_AllocateData | 8 | True |
| atlthunk.AtlThunk_DataToCode | 5 | True |
| atlthunk.AtlThunk_InitData | 11 | True |
| KERNEL32.BaseThreadInitThunk | 3 | False |
| gdi32.BitBlt | 42 | False |
| user32.CallNextHookEx | 7 | False |
| ucrtbase.calloc | 53 | False |
| user32.CallWindowProcW | 37 | True |
| user32.ChangeDisplaySettingsExW | 1 | False |
| user32.ClipCursor | 1 | False |
| KERNEL32.CloseHandle | 17 | True |
| advapi32.CloseServiceHandle | 1 | False |
| ole32.CoInitialize | 1 | False |
| shcore.CommandLineToArgvW | 1 | False |
| user32.CopyImage | 42 | False |
| combase.CoRegisterClassObject | 2 | False |
| combase.CoTaskMemAlloc | 2 | False |
| KERNEL32.CreateActCtxW | 1 | False |
| gdi32.CreateBitmap | 146 | False |
| gdi32.CreateCompatibleBitmap | 162 | False |
| gdi32.CreateCompatibleDC | 477 | True |
| gdi32.CreateDCW | 2 | False |
| user32.CreateDialogParamW | 2 | True |
| gdi32.CreateDIBSection | 192 | True |
| KERNEL32.CreateEventW | 12 | False |
| KERNEL32.CreateFileA | 2 | False |
| KERNEL32.CreateFileW | 2 | False |
| gdi32.CreateFontIndirectW | 7 | True |
| gdi32.CreateFontW | 3 | False |
| KERNEL32.CreateIoCompletionPort | 1 | False |
| KERNEL32.CreateJobObjectW | 1 | False |
| KERNEL32.CreateMutexW | 4 | False |
| KERNEL32.CreateNamedPipeA | 3 | False |
| kernelbase.CreateNamedPipeW | 3 | False |
| gdi32.CreatePatternBrush | 5 | False |
| kernelbase.CreateProcessW | 1 | False |
| comctl32.CreateStatusWindowW | 1 | True |
| KERNEL32.CreateThread | 5 | False |
| KERNEL32.CreateThreadpoolCleanupGroup | 1 | False |
| user32.DdeCreateStringHandleW | 7 | False |
| user32.DdeFreeStringHandle | 6 | False |
| user32.DdeInitializeW | 1 | False |
| user32.DdeNameService | 7 | False |
| user32.DdeUninitialize | 1 | False |
| KERNEL32.DeleteAtom | 10 | False |
| gdi32.DeleteDC | 475 | True |
| user32.DeleteMenu | 1 | True |
| gdi32.DeleteObject | 350 | False |
| user32.DestroyIcon | 40 | False |
| user32.DialogBoxIndirectParamW | 1 | True |
| KERNEL32.DisableThreadLibraryCalls | 38 | False |
| user32.DispatchMessageW | 18 | False |
| DllMain[atlthunk.dll] | 1 | False |
| comctl32.DllGetVersion | 3 | True |
| user32.DrawTextW | 1 | True |
| DllMain[winspool.drv] | 1 | False |
| user32.EnumDisplayDevicesW | 2 | False |
| user32.EnumDisplaySettingsExW | 1 | False |
| KERNEL32.FindActCtxSectionGuid | 2 | False |
| kernelbase.FindClose | 1 | False |
| kernelbase.FindFirstFileW | 1 | False |
| kernelbase.FindResourceW | 459 | False |
| KERNEL32.FlsGetValue | 3 | True |
| KERNEL32.FlsSetValue | 12 | True |
| KERNEL32.FlushFileBuffers | 2 | False |
| ucrtbase.free | 192 | False |
| ws2_32.freeaddrinfo | 1 | False |
| KERNEL32.FreeEnvironmentStringsA | 8 | False |
| KERNEL32.FreeEnvironmentStringsW | 3 | True |
| KERNEL32.FreeLibrary | 3 | True |
| kernelbase.FreeResource | 245 | False |
| gdi32.GdiAlphaBlend | 29 | False |

## Return-value mismatches (deterministic functions)

Pointer-valued returns are class-compared (each runtime maps
its own address space); only null-vs-non-null and numeric
differences count.

| function | wine | miniwin |
|---|---|---|
| combase.CoInitializeEx | 00000000 | S_OK |
| user32.GetClientRect | 00000001 | 1024x768 |
| comctl32.InitCommonControlsEx | 00000001 | TRUE |
| user32.LoadMenuW | 00020052 | HMENU 0x50001 (7 top items) |
| ntdll.RtlGetVersion | 00000000 | 10.0.19045 (Win10 22H2) |
| user32.ShowWindow | 00000000 | TRUE |

## Frequency deltas

| function | wine | miniwin |
|---|---|---|
| combase.CoInitializeEx | 2 | 1 |
| user32.CreateWindowExW | 15 | 1 |
| KERNEL32.FlsAlloc | 5 | 2 |
| user32.GetClassInfoExW | 5 | 1 |
| user32.GetClientRect | 6 | 2 |
| KERNEL32.GetCommandLineA | 9 | 1 |
| user32.GetMessageW | 19 | 1 |
| kernelbase.GetProcAddress | 31 | 12 |
| KERNEL32.GetStdHandle | 28 | 3 |
| user32.GetSysColor | 24 | 7 |
| user32.LoadImageW | 49 | 2 |
| user32.LoadStringW | 154 | 1 |
| ucrtbase.malloc | 309 | 1 |
| user32.RegisterClassExW | 7 | 1 |
| user32.RegisterWindowMessageW | 17 | 1 |
| advapi32.RegQueryValueExW | 32 | 2 |
| ntdll.RtlGetVersion | 7 | 1 |
| user32.SetWindowLongPtrW | 12 | 1 |
| user32.SetWindowsHookExW | 2 | 1 |
| kernelbase.SizeofResource | 245 | 1 |
