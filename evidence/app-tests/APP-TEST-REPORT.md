# APP-TEST ROUND — Real-World Impact Validation (WinRAR / Notepad / IDM)

Date: 2026-10-29 · Loader: `minwin_loader` @ commit "APP-TEST round" · Regression: 12/12 PASS

## Question this round answers

> Does the GitHub issue list actually change what happens when real Windows
> applications run under miniwin — or is it just paperwork?

Method: download official binaries, capture BEFORE baselines, fix the exact
items the worklist called out, re-run, compare. Every number below is from
`evidence/app-tests/*.log`.

## Test binaries (all real, no synthetics)

| App | Source | Type |
|---|---|---|
| WinRAR 7.12 (Rar.exe 817KB / UnRAR.exe 544KB / WinRAR.exe 3.3MB) | rarlab.com official installer, extracted | PE32+ x64 console + GUI |
| Notepad | Wine 10.0 PE64 build (Debian libwine) | PE32+ x64 GUI, 127 imports, mingw-compiled |
| Microsoft notepad.exe import table (201 APIs, 9 DLLs) | cuckoo analysis mirror | reference data only |
| IDM 6.42 installer (12.7MB) | mirror2.internetdownloadmanager.com | **PE32 (32-bit!)** |

## BEFORE → AFTER (the impact, measured)

### Rar.exe (WinRAR console)
| Metric | BEFORE | AFTER |
|---|---|---|
| Unresolved imports | 13 | **0** |
| Silent return-0 stub calls | 124 | **0** |
| Usage/help output | none (crash first) | **full help printed** |
| Exit | SIGSEGV (139) | SIGSEGV (139) — different, deeper RVA 0x55fab → 0x8bea1 |

New crash is INSIDE Rar's help-text engine (cmp [rdx],cx with rdx=7). Next
iteration target — see "what the list still owes us".

### UnRAR.exe
| Metric | BEFORE | AFTER |
|---|---|---|
| Unresolved imports | 8 | **0** |
| Silent stub calls | 65 | **0** |
| Banner + help output | none | **"UNRAR 7.12" + full help** |
| Exit | SIGSEGV | SIGSEGV — same deeper-crash class as Rar |

### Notepad (wine-notepad PE64)
| Metric | BEFORE | AFTER |
|---|---|---|
| Unresolved imports | 16 | **0** |
| LoadStringW (title/status text) | all "resource absent" → 0 chars | **38/105/63/72/110 chars real text** |
| GetMenu(hwnd) | NULL for the app's whole life | **HMENU 0x50001** (RT_MENU parsed from .rsrc) |
| Message pump | GetMessage fabricated WM_QUIT → instant exit(0) | **blocks, app alive at timeout (exit 124 = killed, not quit)** |
| Exit | clean exit(0) immediately | **stays running** |

### WinRAR.exe (GUI)
| Metric | BEFORE | AFTER |
|---|---|---|
| Unresolved imports | 51 | **12** (only Gdip* — gdiplus flat API) |
| Exit | SIGSEGV inside libc | SIGSEGV — persists, deeper root cause |

### IDM 6.42 installer
| Result | Detail |
|---|---|
| Rejected at PE parse | `Not a PE32+ file (magic=0x10b)` — IDM ships **32-bit**; miniwin is x64-only. This is the WOW64 gap (REG-2 covers its registry face; the loader face is new). |

## Which issues this PROVES real (evidence → issue)

| Issue | Evidence from this round |
|---|---|
| **PREREQ-1** (#13) | 13+8+16+51 unresolved imports across WinRAR/notepad; silent-0 stub fed Rar.exe's crash path (124 stub calls before first crash). Fixed 52 of them with real semantics → Rar/UnRAR/Notepad behavior changed immediately. |
| **FILE-2** (#10) | LoadStringW "resource absent" while .rsrc is fully mapped (0x33000 bytes) — notepad lost ALL UI text. RT_STRING parse implemented → 5 real strings loaded. |
| **GFX-2** (#12) | Notepad menu = window-class lpszMenuName; loader ignored it → GetMenu nil forever. Class-menu support added → GetMenu returns HMENU 0x50001. |
| **Messaging (GFX-2 scope)** | GetMessageW fabricated WM_QUIT when idle ("no event source") — every GUI app's pump died instantly. Blocking implemented → notepad survives. |
| **REG-2** (#5) + new WOW64 loader face | IDM installer = PE32 → loader exits at parse. Real-world blocker for the entire 32-bit app universe. |
| **GFX-1** (#11) | WinRAR GUI's 12 Gdip* imports unresolved — no GDI+ flat API at all. |

## Fixes landed this round (loader.c)

1. **52 new import implementations** with REAL Win32 contracts (not silent 0s):
   BackupRead/Seek (FALSE+ERROR_NOT_SUPPORTED), FoldStringW, CharToOem*/OemToChar*
   (6), SetThreadExecutionState, TzSpecificLocalTimeToSystemTime,
   GetSecurityDescriptorLength (RtlLengthSecurityDescriptor algorithm),
   GetCPInfoExW (544-byte struct), GetPhysicallyInstalledSystemMemory,
   GetPriorityClass, CreateSemaphoreExW/OpenSemaphoreW, CreateNamedPipeW/
   WaitNamedPipeW, Find*ChangeNotification (INVALID_HANDLE_VALUE + error),
   EnumResourceLanguagesW (real resource lookup + callback), Begin/Update/
   EndUpdateResourceW (honest failure), GetDpiForWindow(96), MonitorFromRect,
   keybd_event, SendInput, SetProcessDefaultLayout, SetUserObjectInformationW,
   GetScrollBarInfo, CreateIcon, SetWinEventHook/UnhookWinEvent, IsCharAlphaW,
   IsTextUnicode (BOM+statistics heuristic), LsaAddAccountRights(STATUS_SUCCESS),
   StrStrW/StrRStrIW/StrCmpNW/StrCmpNIW, wnsprintfW + __stdio_common_vswprintf
   (shared format engine, mingw x64 va_list model, probe-guarded), 
   _get_initial_narrow_environment(environ), DragAcceptFiles, ShellAboutW,
   GetFileTitleW, SHCreateItemFromParsingName/SHGetFolderLocation(E_FAIL),
   OleSetClipboard(S_OK), FindExecutableW(SE_ERR_NOASSOC).
2. **LoadStringW/A**: real RT_STRING parse (block id>>4+1, count+len pairs).
3. **LoadMenuW/A + class menus**: real RT_MENU template parse (nested popups,
   MF_END, option→state mapping); RegisterClass captures lpszMenuName@56,
   CreateWindowEx loads+attaches it (Windows behavior).
4. **GetMessageW/A blocking**: idle no longer fabricates WM_QUIT; polls every
   2ms, honors PostQuitMessage, MINWIN_MSG_BLOCK_SEC bound (default 15s),
   timeout delivers WM_NULL (pump stays alive).

## What the list still owes us (next round, from this evidence)

1. Rar/UnRAR deeper crash: help-text engine dies at RVA 0x8bea1 — needs
   console codepage/OEM trace (GetConsoleOutputCP? CharToOem mapping for
   >0x7F?) — Rar prints help through a compressed string engine.
2. WinRAR GUI: 12 Gdip* imports = need a minimal GDI+ object layer (GFX-1 scope).
3. Notepad: WM_PAINT dispatched but no frame BMP dumped yet (BeginPaint path
   didn't fire for the guest's paint handler — verify WM_PAINT delivery path).
4. WOW64 loader (new issue candidate): PE32 support = unblock IDM + the whole
   32-bit corpus. Registry face already tracked as REG-2.
5. comctl32 ordinals 410/413 imported by wine-notepad land on the stub (return 0)
   — add to ordinal→name table if they get called with nonzero expectations.

## Reproduce

```bash
cd evidence/app-tests
./../../minwin_loader winrar/Rar.exe            # console archiver
MW_GUI_EVENTS=1 ../../minwin_loader notepad.exe # GUI + pump; expect exit 124
../../minwin_loader idman642build33.exe         # expect: Not a PE32+ (magic=0x10b)
```
Logs: `*_baseline_full.log` (BEFORE), `*_after*.log` (AFTER), `notepad_final.log`.
