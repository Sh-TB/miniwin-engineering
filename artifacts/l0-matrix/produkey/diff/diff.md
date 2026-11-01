# Wine vs MiniWin differential

- generated: 2026-11-01T06:44:43.514169+00:00
- wine events: 37136 (682 unique functions)
- miniwin events: 174 (31 unique functions)

## First meaningful divergence

- none found (every Wine app-attributed function was also called by MiniWin)

## Missing calls (Wine observed, MiniWin never)

COVERAGE NOTE: MiniWin traces only the calls its mw_*
implementations log — a function listed here may be
implemented-but-unlogged rather than absent. Verify against
the compat database before treating it as a gap.

| function | wine calls | app-attributed |
|---|---|---|
| shell32.188 | 3 | False |
| msvcrt.??2@YAPEAX_K@Z | 17 | False |
| msvcrt.__getmainargs | 1 | False |
| ucrtbase.__p___argc | 3 | False |
| ucrtbase.__p___argv | 2 | False |
| ucrtbase.__p___wargv | 1 | False |
| msvcrt.__set_app_type | 1 | False |
| ucrtbase.__stdio_common_vsscanf | 1 | False |
| ucrtbase.__stdio_common_vswprintf | 16 | False |
| imm32.__wine_ime_wnd_proc | 24 | False |
| ucrtbase._close | 2 | False |
| ucrtbase._configure_narrow_argv | 2 | False |
| ucrtbase._configure_wide_argv | 1 | False |
| ucrtbase._fstat64i32 | 1 | False |
| ucrtbase._get_initial_narrow_environment | 2 | False |
| ucrtbase._get_initial_wide_environment | 1 | False |
| ucrtbase._initialize_narrow_environment | 2 | False |
| ucrtbase._initialize_wide_environment | 1 | False |
| msvcrt._initterm | 2 | False |
| msvcrt._onexit | 10 | False |
| ucrtbase._read | 1 | False |
| ucrtbase._set_app_type | 3 | False |
| msvcrt._strcmpi | 1 | False |
| ucrtbase._strdup | 27 | False |
| ucrtbase._stricmp | 1 | False |
| ntdll._vsnwprintf | 27 | False |
| ucrtbase._wcsdup | 18 | False |
| ntdll._wcsicmp | 205 | False |
| ucrtbase._wcsnicmp | 19 | False |
| ucrtbase._wgetenv | 3 | False |
| KERNEL32.AddAtomW | 10 | False |
| user32.AdjustWindowRect | 1 | False |
| KERNEL32.BaseThreadInitThunk | 2 | False |
| gdi32.BitBlt | 33 | False |
| ucrtbase.calloc | 53 | False |
| user32.ChangeDisplaySettingsExW | 1 | False |
| user32.CheckMenuRadioItem | 1 | False |
| user32.ClipCursor | 1 | False |
| KERNEL32.CloseHandle | 16 | False |
| advapi32.CloseServiceHandle | 1 | False |
| combase.CoInitializeEx | 2 | False |
| user32.CopyImage | 1 | False |
| combase.CoRegisterClassObject | 2 | False |
| combase.CoTaskMemAlloc | 2 | False |
| KERNEL32.CreateActCtxW | 1 | False |
| gdi32.CreateBitmap | 14 | False |
| gdi32.CreateCompatibleBitmap | 2 | False |
| gdi32.CreateCompatibleDC | 24 | False |
| gdi32.CreateDIBSection | 7 | False |
| KERNEL32.CreateEventW | 12 | False |
| KERNEL32.CreateFileA | 2 | False |
| KERNEL32.CreateFileW | 2 | False |
| gdi32.CreateFontIndirectW | 3 | False |
| KERNEL32.CreateIoCompletionPort | 1 | False |
| KERNEL32.CreateJobObjectW | 1 | False |
| KERNEL32.CreateMutexW | 4 | False |
| KERNEL32.CreateNamedPipeA | 3 | False |
| kernelbase.CreateNamedPipeW | 3 | False |
| gdi32.CreatePatternBrush | 7 | False |
| kernelbase.CreateProcessW | 1 | False |
| gdi32.CreateSolidBrush | 1 | False |
| comctl32.CreateStatusWindowA | 1 | False |
| KERNEL32.CreateThread | 5 | False |
| KERNEL32.CreateThreadpoolCleanupGroup | 1 | False |
| comctl32.CreateToolbarEx | 1 | False |
| user32.CreateWindowExW | 10 | False |
| user32.DdeCreateStringHandleW | 7 | False |
| user32.DdeFreeStringHandle | 6 | False |
| user32.DdeInitializeW | 1 | False |
| user32.DdeNameService | 7 | False |
| user32.DdeUninitialize | 1 | False |
| KERNEL32.DeleteAtom | 10 | False |
| gdi32.DeleteDC | 19 | False |
| gdi32.DeleteObject | 21 | False |
| user32.DestroyIcon | 1 | False |
| KERNEL32.DisableThreadLibraryCalls | 30 | False |
| user32.DispatchMessageA | 1 | False |
| user32.DispatchMessageW | 16 | False |
| DllMain[OLEAUT32.dll] | 1 | False |
| user32.DrawIconEx | 2 | False |
| DllMain[winspool.drv] | 1 | False |
| user32.EnumDisplayDevicesW | 2 | False |
| user32.EnumDisplaySettingsExW | 1 | False |
| KERNEL32.FindActCtxSectionGuid | 2 | False |
| kernelbase.FindClose | 1 | False |
| kernelbase.FindFirstFileW | 1 | False |
| KERNEL32.FindResourceA | 2 | False |
| kernelbase.FindResourceExW | 2 | False |
| kernelbase.FindResourceW | 152 | False |
| KERNEL32.FlsAlloc | 4 | False |
| KERNEL32.FlsSetValue | 10 | False |
| KERNEL32.FlushFileBuffers | 2 | False |
| msvcrt.free | 2 | False |
| ws2_32.freeaddrinfo | 1 | False |
| KERNEL32.FreeEnvironmentStringsA | 8 | False |
| KERNEL32.FreeEnvironmentStringsW | 2 | False |
| KERNEL32.FreeLibrary | 1 | False |
| kernelbase.FreeResource | 84 | False |
| KERNEL32.GetACP | 8 | False |
| ws2_32.getaddrinfo | 1 | False |

## Return-value mismatches (deterministic functions)

Pointer-valued returns are class-compared (each runtime maps
its own address space); only null-vs-non-null and numeric
differences count.

| function | wine | miniwin |
|---|---|---|
| ole32.CoInitialize | 00000000 | S_OK |
| user32.CreateWindowExA | 00000000 | HWND 0x10001 |
| user32.GetClientRect | 00000001 | 1024x768 |
| gdi32.GetObjectW | 00000020 | BITMAP 112x16 |
| user32.GetWindowRect | 00000001 | 0x0@0,0 |
| comctl32.ImageList_ReplaceIcon | 00000000 | 0 (blit 16x16) |
| comctl32.ImageList_SetImageCount | 00000001 | TRUE |
| comctl32.InitCommonControlsEx | 00000001 | TRUE |
| user32.LoadAcceleratorsA | 00030050 | 0xac0001 (n=14, REAL) |
| user32.RegisterClassA | 0000c036 | ATOM 0xc001 |
| user32.RegisterWindowMessageA | 0000c03b | 0xc000 |
| user32.SetMenu | 00000001 | TRUE |
| user32.SetWindowPos | 00000001 | TRUE |
| user32.ShowWindow | 00000000 | TRUE |

## Frequency deltas

| function | wine | miniwin |
|---|---|---|
| user32.CheckMenuItem | 16 | 32 |
| ole32.CoInitialize | 2 | 1 |
| user32.CreateWindowExA | 3 | 1 |
| user32.EnableMenuItem | 10 | 20 |
| user32.GetClientRect | 10 | 1 |
| user32.GetMenu | 27 | 54 |
| gdi32.GetObjectW | 8 | 3 |
| kernelbase.GetProcAddress | 30 | 5 |
| user32.GetSysColor | 20 | 3 |
| user32.GetWindowRect | 3 | 2 |
| user32.LoadImageW | 6 | 4 |
| user32.LoadStringA | 16 | 17 |
| user32.SetFocus | 1 | 2 |
| user32.SetWindowPos | 4 | 3 |
| kernelbase.SizeofResource | 85 | 1 |
