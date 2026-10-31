# WinRAR.exe 7.12 — TRAIN-10 precise tracker (Wine-source grounded)

Guest: artifacts/app3/winrar/WinRAR.exe — PE32+ (magic 0x20B), 3,316,880 bytes
Result this train: exit 0 x3 deterministic; imports 100% resolved (0 unresolved,
was 25+); main window 1024x768; framed paint evidence (frame BMP 1032x795);
clean CRT exit path (DeleteCriticalSection -> mscoree probe -> ExitProcess(0)).

## Per-problem ledger (fix -> Wine root-cause reference -> verification)

| # | Problem | Before | Fix (Wine ref) | Verified |
|---|---------|--------|----------------|----------|
| 1 | LoadLibraryExA returned NULL+126 for MSVC delay-load (__delayLoadHelper2, .didat) -> 0xC06D007E | SHLWAPI delay-load dead | delegate to LoadLibraryA known-DLL path (Wine dlls/kernel32/module.c LoadLibraryExA forwards to LoadLibraryExW) | delay-load resolves |
| 2 | SHGetMalloc IMalloc vtable wrong slot order (Free/Realloc swapped) + SysV ABI callbacks (self read from RDI) | SIGSEGV in SHGetMalloc users | rebuilt 9-slot COM vtable QI/AddRef/Release/Alloc/Realloc/Free/GetSize/DidAlloc/HeapMinimize, all ms_abi, 1024-bucket live-pointer registry; foreign ptr -> Free=no-op, Realloc=NULL (Wine ole32 CMalloc magic-header guard) | SHGetMalloc->S_OK; guest frees pass |
| 3 | CharLowerW(0x1) single-character contract (high word 0) dereferenced as string | SIGSEGV RCX=1 | dual contract: (v>>16)==0 -> returns lowered char (Wine dlls/user32/text.c) | chars '1'..'9' pass |
| 4 | LoadStringW stub returned 0 | empty UI strings | real RT_STRING: block id/16+1, entry id&0xF, [len][WCHARs] (Wine user32/resource.c load_stringW) | ids 100,450,763,873,925-928,1014-1018 real lengths |
| 5 | LoadIconW constant 0x50002 | no resource walk | RT_GROUP_ICON walk; IDI_*/OIC_* -> default object (Wine user32/cursoricon.c) | LoadIconW(stock id=101) -> 0x53001 |
| 6 | LoadImageW(IMAGE_ICON) blank pool fallback | icon never decoded | RT_GROUP_ICON -> best entry (prefer 32x32) -> RT_ICON decode (BITMAPINFOHEADER biHeight=2h + XOR + AND -> 32bpp ARGB); LR_LOADFROMFILE parses .ico/.cur (Wine CURSORICON_FindBestId) | IconFromGroup(ICON name=0x65) -> 32x32 HICON (entry id=6) |
| 7 | CreateIcon/CreateIconIndirect/GetIconInfo/DestroyIcon/ExtractIconExW/CreateIconFromResourceEx absent or cookies | no icon objects | all build/consume real MWIcon pixel objects; GetIconInfo returns fresh HBITMAPs | CreateIcon resolved and functional |
| 8 | DrawIconEx/DrawIcon no pixels; WM_SETICON dropped | icons invisible | alpha-blend into mw_dc_surface; WM_SETICON/WM_GETICON engine state (Wine gdi32) | blit + state active |
| 9 | no non-client frame (kadr) | bare white rect | frame composer: navy caption + icon + title + buttons + borders, WS_CAPTION-gated, client bytes exact | frame BMP shows title bar + borders |
| 10 | GetSystemMetrics misindexed (SM_CXSMICON(49)=0, SM_CXFRAME(32)=0) | icons sized 0 | full rebuild vs Wine include/winuser.h indices | 49/50 -> 16/16 |
| 11 | api_trace.json corrupted by worker-thread lines after JSON terminator (fclose flush race) | unparseable | mutex-serialized write+shutdown, NULL under lock | JSON valid (8021 lines) |
| 12 | CreateWindowExW CW_USEDEFAULT (INT_MIN) clamped 32x32 | postage stamp | default 1024x768 centered (Wine resolves from metrics) | 1024x768@448,156 |
| 13 | CreateDialogIndirectParamW ignored template -> GetDlgItem all NULL | dialogs unbuilt | DLGTEMPLATE + DLGTEMPLATEEX walkers create children before WM_INITDIALOG (Wine defdlg.c) | walker in place |
| 14 | SHGetFolderPathW(csidl=0x10) E_FAIL | folder bookkeeping aborted | full CSIDL map (Desktop/APPDATA/FONTS/PROFILE/...) | Desktop resolves |
| 15-17 | GDI+ cookie-only objects | no pixels | graphics remembers HDC; pen/brush ARGB; Draw/Fill Rect/Ellipse/Polygon rasterize; bitmaps own 32bpp ARGB store (Wine gdiplus) | Gdip* real objects |
| 18-33 | 25 unresolved imports: CreateIcon, CharToOem*/OemToChar*, keybd_event/SendInput, GetScrollBarInfo, TzSpecificLocalTimeToSystemTime, FoldStringW, GetCPInfoExW, EnumResourceLanguagesW, UpdateResource family, BackupRead/Seek, change notifications, CreateNamedPipeW/WaitNamedPipeW, OpenSemaphoreW/CreateSemaphoreExW, SHCreateItemFromParsingName/SHGetFolderLocation, OleSetClipboard, FindExecutableW, DragAcceptFiles, IsCharAlpha*, SetWinEventHook/UnhookWinEvent, SetUserObjectInformationW, SetProcessDefaultLayout/SetThreadExecutionState/GetPriorityClass/GetPhysicallyInstalledSystemMemory | STUB->0 | each typed per Wine semantics (loader.c TRAIN-10 batch comments) | grep unresolved = 0 |
| 34/37 | [STUB] log lacked import name | unknown callees | IAT slot->name registry; stub decodes call [rip+disp32] from guest RA | names reported |
| 35 | GetMenu(hwnd) -> NULL | menu bar missing | WNDCLASS.lpszMenuName (+56) stored, loaded at create (Wine win.c) | class menu attach wired |
| 36 | FreeLibraryAndExitThread called mw_ExitProcess | worker killed whole process pre-pump | ExitThread semantics — calling thread only (Wine kernel32/thread.c) | process survives to clean exit |

## Frontier (next train)
- Pump delivers 1 WM_PAINT then WM_QUIT (headless budget design).
- Guest C++ EH unwinding (SW-1201 class) not exercised by WinRAR's exit path.
