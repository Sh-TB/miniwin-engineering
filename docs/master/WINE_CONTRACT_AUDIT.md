# Wine Contract Audit — source-to-runtime map (2026-10-30)

Scope: every API whose contract was consulted against Wine source (or
equivalent Microsoft headers) and changed/verified in the engine during this
round. Statuses: VERIFIED / IMPLEMENTED / TESTED / PARTIAL / PENDING / BLOCKED
(see `artifacts/wine-tracker/finding-ledger.json` for claim-level statuses).

| # | API / mechanism | Wine reference | Engine location (src/loader/loader.c) | Contract point fixed/verified | Status | Test |
|---|---|---|---|---|---|---|
| 1 | IMalloc vtable layout (9 slots) | objidl.h/objidlbase.idl; dlls/ole32/imalloc.c | `g_shmalloc_vtbl`, `mw_CoGetMalloc` | Order = QI/AddRef/Release/Alloc/**Realloc**/**Free**/GetSize/DidAlloc/HeapMinimize (Free/Realloc were swapped; Co table was 8-slot NULL-QI no-GetSize) | VERIFIED | `tests/imalloc_abi/` 34/34 through real vtable memory (ms_abi pointers = guest calling convention) |
| 2 | IMalloc::QueryInterface identity | objidl.h (IID_IUnknown 00000000-…-46, IID_IMalloc 000000DA-…-46) | `mw_shmalloc_qi`, `mw_ole_malloc_qi` | S_OK + same object for both IIDs; E_NOINTERFACE else; E_POINTER for NULL out | VERIFIED | same ABI test |
| 3 | IMalloc::GetSize | IMalloc::GetSize contract (usable-size semantics) | `mw_shmalloc_getsize`, `mw_ole_malloc_getsize` | malloc_usable_size for owned blocks; 0 for foreign/NULL (never lie) | VERIFIED | same ABI test |
| 4 | IMalloc foreign-pointer safety | Wine imalloc ownership model | cotask ownership registry | Free(foreign)/DidAlloc(foreign) refused, never host-freed | VERIFIED | same ABI test (stack + host-malloc pointers) |
| 5 | LoadMenuW/A resource parse | dlls/user32/menu.c MENU_ParseMenu; winuser.h MENUITEMTEMPLATE | `mw_menu_load_from_res`, `mw_menu_parse_items` | MENUTEMPLATE v0/v1: WORD ver/offset; per item: WORD option, WORD id (non-popup), wide title; MF_POPUP nesting; MF_END terminates list; option→state uses MF_CHECKED=8/MF_GRAYED=1/MF_DISABLED=2 per the SWEEP-019 menu model | VERIFIED | notepad merged-engine trace: HMENU 0x50001, 5 top items == binary's template (`scripts/dump_menu.py`) |
| 6 | Class menu (lpszMenuName) | CreateWindowEx contract (hMenu NULL + class menu ⇒ load class menu resource) | `mw_class_menu_store_w/a` + `mw_class_menu_attach` | WNDCLASS/EX x64 menu@56; i386 W@32/ExW@36; capture at RegisterClass, attach at CreateWindow when hMenu==NULL | VERIFIED | `[GUI] class menu -> HMENU 0x50001 (lpszMenuName)`, GetMenu readback |
| 7 | FindResource result deref | PE resource tree: HRSRC = IMAGE_RESOURCE_DATA_ENTRY (DataRVA@0, Size@4) | `mw_menu_load_from_res` | Menu loader derefs entry like `mw_LoadStringW` (SW-1277) — first port fed the 16-byte header to the parser (false malformed) | VERIFIED | notepad trace before/after |
| 8 | GetCPInfoExW | kernelbase GetCPInfoExW; CPINFOEXW layout | `mw_GetCPInfoExW` | MaxCharSize/DefaultChar/LeadByte/UnicodeDefaultChar/CodePage/CodePageName fill; ERROR_INVALID_PARAMETER(87) for unsupported CPs | TESTED | resolves in Rar.exe load (0 unresolved) |
| 9 | GetDpiForWindow | user32 GetDpiForWindow | `mw_GetDpiForWindow` | 96 (USER_DEFAULT_SCREEN_DPI) — honest for the engine's fixed-DPI surface | TESTED | ENTRY resolves; notepad load |
| 10 | wnsprintfW/A | shlwapi wnsprintf (truncate + always NUL-terminate, return written chars) | `mw_wnsprintfW/A` | routes the shared wide engine `mw_vfmt_w16` (same as UCRT __stdio_common_vswprintf row) | TESTED | ENTRY resolves in app loads |
| 11 | StrRStrIW | shlwapi ordinal.c StrRStrIW | `mw_StrRStrIW` | reverse case-insensitive search over [start,end); empty needle ⇒ start | TESTED | notepad load (was 1 unresolved) |
| 12 | OemToCharBuffW | winuser.h OemToCharBuff (W dst) | `mw_OemToCharBuffW` | cp437 ASCII identity for W-twin (A-only coverage left the name unresolved) | TESTED | Rar.exe load (was 1 unresolved) |
| 13 | SetThreadExecutionState | kernel32 SetThreadExecutionState | `mw_SetThreadExecutionState` | returns PREVIOUS state (real contract) — old stub absent entirely | TESTED | UnRAR/Rar load |
| 14 | LsaAddAccountRights | advapi32 LsaAddAccountRights | `mw_LsaAddAccountRights` | STATUS_SUCCESS 0 (no LSA store; grant-and-forget honest) | TESTED | UnRAR load |
| 15 | LoadStringW RT_STRING | user32 LoadString (block=(id>>4)+1, len-prefixed) | upstream `mw_LoadStringW` (SW-1475) | verified equivalent to our independent implementation | VERIFIED | notepad 7 LoadStringW calls, real text |
| 16 | GetMessageW blocking | user32 GetMessage/PeekMessage pump contract | upstream `mw_gui_pump_getmsg` (SWEEP-035) | workers park, WM_QUIT via PostThreadMessage/PostQuitMessage respected; headless main-thread exit-gate | VERIFIED | corpus regression 12/12 + winmergeu worker evidence |
| 17 | CW_USEDEFAULT top-level sizing | CreateWindowEx CW_USEDEFAULT contract | upstream SW-1471 | 1024×768 work-area metric, first top-level only | VERIFIED | WinRAR GUI/NPP window creation (SWEEP-094 evidence) |
| 18 | PE32/0x10b guests | (engine-internal) SW-1281 x86 interpreter | `load_pe32`/`cpu32_run` | NOT a Wine contract — recorded because it superseded the "PE32 honestly refused" finding (F-08) | TESTED | IDM installer: 76,125 steps, exit 0 |

## Deliberately NOT changed

- `runtime/src/dispatch_mod.rs` — not in the production link (Makefile builds
  `minwin_loader` from `src/loader/loader.c` only). Untouched.
- Existing app-tested implementations that passed their tests (Char* family,
  FoldStringW, IsTextUnicode, GetFileTitleW, …) — audited, no blind edits.

## Contract sources note

Wine source references name the behavior contract (function, file, expected
return); no Wine code was copied. Where the exact Wine file is cited, the
engine implementation remains the repo's own (verified by the tests above,
which exercise OUR vtable memory, OUR parser, OUR menu model).
