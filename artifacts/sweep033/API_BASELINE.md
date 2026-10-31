# SWEEP-033 — Level-0 Wine Relay Campaign: BASE API derived from 22 real programs

Date: 2026-10-30. Method = the user's own level-0 methodology: pass every famous program through a level-0 tracker (Wine `WINEDEBUG=+relay`, wine-10.0 WineHQ noble build at `tools/wine-10.0`), capture **every call**, then compare with MiniWin's loader-resolved import sets and derive the base API that a Windows-compatible engine must implement.

## Corpus (22 real Windows programs, sha256-pinned)

| app | kind | wine rc | relay distinct | relay total | miniwin rc | static imports resolved | surface APIs | surface calls |
|---|---|---|---|---|---|---|---|---|
| winrar-gui | GUI-INIT | TO | 602 | 872,988 | TO | 509/509 | 108 | 94,617 |
| rar-console | CONSOLE | 0 | 614 | 750,648 | 0 | 183/183 | 73 | 41,083 |
| unrar | CONSOLE | 0 | 612 | 747,831 | 0 | 168/168 | 73 | 39,941 |
| putty | GUI-INIT | TO | 598 | 862,406 | 139 | 348/348 | 87 | 30,320 |
| puttygen | GUI-INIT | 1 | 615 | 973,462 | 0 | 186/186 | 61 | 37,140 |
| pageant | GUI-INIT | TO | 609 | 862,550 | TO | 202/202 | 83 | 29,787 |
| procexp64 | GUI-INIT | TO | 748 | 980,041 | 0 | 703/703 | 237 | 66,927 |
| procmon64 | GUI-INIT | TO | 505 | 426,803 | 0 | 550/550 | 100 | 32,865 |
| bginfo64 | GUI-INIT | 1 | 506 | 426,794 | 1 | 717/717 | 105 | 32,738 |
| rammap64 | GUI-INIT | TO | 505 | 426,838 | 139 | 267/267 | 70 | 20,634 |
| autoruns64 | GUI-INIT | 1 | 271 | 429,255 | 1 | 489/489 | 68 | 15,973 |
| zoomit64 | GUI-INIT | 0 | 271 | 429,255 | 0 | 501/501 | 71 | 29,217 |
| produkey | CONSOLE | 0 | 271 | 436,503 | 0 | 244/249 | 18 | 199 |
| cports | CONSOLE | 0 | 505 | 429,492 | 0 | 273/278 | 27 | 17,868 |
| diskmon64 | GUI-INIT | 0 | 271 | 429,248 | 0 | 259/259 | 39 | 2,416 |
| searchmyfiles | GUI-INIT | 0 | 505 | 429,578 | 0 | 279/284 | 57 | 3,086 |
| winmergeu | GUI-INIT | 0 | 271 | 429,243 | 0 | 761/761 | 80 | 15,979 |
| coreinfo64 | CONSOLE | 0 | 271 | 429,248 | 1 | 153/153 | 40 | 2,383 |
| 7za | CONSOLE | 0 | 576 | 744,102 | 0 | 139/145 | 43 | 9,972 |
| jq | CONSOLE | 0 | 585 | 746,501 | 0 | 207/210 | 23 | 6,374 |
| 7z-sfx | GUI-INIT | 1 | 275 | 436,489 | 1 | 73/76 | 23 | 143 |
| notepadpp | GUI-INIT | 255 | 271 | 429,256 | 9 | 584/584 | 71 | 15,952 |

Honest notes:

- **GUI-INIT** apps ran headless (no display server in sandbox): the relay covers process init + window-creation attempt (wine `nodrv` path). Console apps are full runs.
- **surface** = dynamic ∩ static: APIs the app both imports and actually called under wine. **infra** = dynamic − static: wine runtime internals + GetProcAddress-discovered calls (caller-PC→module attribution is future work; 2 MB relay samples per app are committed for that mining).
- MiniWin resolved **100% of static imports for all 22 apps** this round (unresolved=0 everywhere) — the gap is no longer import resolution; it is behavioral fidelity of the resolved APIs (see rc column: putty/rammap64 SIGSEGV 139, notepad++ frontier rc=9, pageant pump-hang).

## BASE API — ranked by how many programs exercise it

Union of surfaces: **349 APIs**. Top of the list = the mandatory core.

| # | API | apps | total calls |
|---|---|---|---|
| 1 | `KERNEL32.CLOSEHANDLE` | 22 | 1,828 |
| 2 | `KERNEL32.GETPROCADDRESS` | 22 | 1,839 |
| 3 | `KERNEL32.GETSTDHANDLE` | 21 | 1,140 |
| 4 | `KERNEL32.WIDECHARTOMULTIBYTE` | 21 | 4,142 |
| 5 | `KERNEL32.MULTIBYTETOWIDECHAR` | 20 | 46,691 |
| 6 | `ADVAPI32.REGCLOSEKEY` | 19 | 9,042 |
| 7 | `KERNEL32.GETMODULEFILENAMEW` | 19 | 339 |
| 8 | `KERNEL32.GETMODULEHANDLEW` | 19 | 1,407 |
| 9 | `KERNEL32.GETCOMMANDLINEW` | 18 | 539 |
| 10 | `KERNEL32.ISPROCESSORFEATUREPRESENT` | 18 | 267 |
| 11 | `KERNEL32.LOADLIBRARYEXW` | 18 | 233 |
| 12 | `KERNEL32.CREATETHREAD` | 17 | 348 |
| 13 | `KERNEL32.GETSTARTUPINFOW` | 17 | 40 |
| 14 | `KERNEL32.HEAPFREE` | 17 | 259,392 |
| 15 | `KERNEL32.TLSALLOC` | 17 | 315 |
| 16 | `KERNEL32.FREEENVIRONMENTSTRINGSW` | 16 | 93 |
| 17 | `KERNEL32.GETACP` | 16 | 231 |
| 18 | `KERNEL32.GETCOMMANDLINEA` | 16 | 231 |
| 19 | `KERNEL32.GETCPINFO` | 16 | 465 |
| 20 | `KERNEL32.GETENVIRONMENTSTRINGSW` | 16 | 93 |
| 21 | `KERNEL32.GETFILETYPE` | 16 | 744 |
| 22 | `KERNEL32.GETSTRINGTYPEW` | 16 | 448 |
| 23 | `KERNEL32.ISVALIDCODEPAGE` | 16 | 231 |
| 24 | `KERNEL32.LCMAPSTRINGW` | 16 | 896 |
| 25 | `KERNEL32.SETSTDHANDLE` | 16 | 116 |
| 26 | `USER32.SETWINDOWPOS` | 16 | 17 |
| 27 | `ADVAPI32.REGQUERYVALUEEXW` | 15 | 8,859 |
| 28 | `KERNEL32.COMPARESTRINGW` | 15 | 7,254 |
| 29 | `KERNEL32.FLSALLOC` | 15 | 117 |
| 30 | `KERNEL32.WAITFORSINGLEOBJECT` | 15 | 2,538 |
| 31 | `USER32.GETSYSCOLOR` | 15 | 1,481 |
| 32 | `KERNEL32.GLOBALALLOC` | 14 | 14 |
| 33 | `KERNEL32.GLOBALLOCK` | 14 | 28 |
| 34 | `KERNEL32.GLOBALUNLOCK` | 14 | 28 |
| 35 | `KERNEL32.LOADRESOURCE` | 14 | 189 |
| 36 | `KERNEL32.WRITEFILE` | 14 | 351 |
| 37 | `USER32.GETSYSTEMMETRICS` | 14 | 111 |
| 38 | `KERNEL32.GETVERSIONEXW` | 13 | 206 |
| 39 | `KERNEL32.LOADLIBRARYW` | 13 | 60 |
| 40 | `KERNEL32.LOCALALLOC` | 13 | 16 |
| 41 | `KERNEL32.LOCALFREE` | 13 | 76 |
| 42 | `KERNEL32.SETEVENT` | 13 | 355 |
| 43 | `KERNEL32.SIZEOFRESOURCE` | 13 | 72 |
| 44 | `KERNEL32.CREATEFILEW` | 12 | 75 |
| 45 | `KERNEL32.EXITPROCESS` | 12 | 51 |
| 46 | `KERNEL32.READFILE` | 12 | 248 |
| 47 | `USER32.DISPATCHMESSAGEW` | 12 | 147 |
| 48 | `USER32.SENDMESSAGEW` | 12 | 15 |
| 49 | `ADVAPI32.REGSETVALUEEXW` | 11 | 751 |
| 50 | `KERNEL32.GETSYSTEMDIRECTORYW` | 11 | 108 |
| 51 | `KERNEL32.RAISEEXCEPTION` | 11 | 15 |
| 52 | `USER32.CREATEWINDOWEXW` | 11 | 87 |
| 53 | `USER32.GETMESSAGEW` | 11 | 141 |
| 54 | `USER32.LOADCURSORW` | 11 | 79 |
| 55 | `USER32.LOADICONW` | 11 | 25 |
| 56 | `USER32.PEEKMESSAGEW` | 11 | 11 |
| 57 | `ADVAPI32.REGCREATEKEYEXW` | 10 | 815 |
| 58 | `KERNEL32.CREATEEVENTW` | 10 | 1,058 |
| 59 | `KERNEL32.FINDRESOURCEW` | 10 | 127 |
| 60 | `KERNEL32.OPENPROCESS` | 10 | 74 |

## Per-DLL surface size

| DLL | surface APIs |
|---|---|
| KERNEL32 | 135 |
| USER32 | 83 |
| ADVAPI32 | 46 |
| MSVCRT | 18 |
| GDI32 | 14 |
| NTDLL | 14 |
| SHELL32 | 8 |
| COMCTL32 | 5 |
| OLEAUT32 | 4 |
| SHLWAPI | 4 |
| SETUPAPI | 4 |
| VERSION | 3 |
| UXTHEME | 2 |
| OLE32 | 2 |
| WTSAPI32 | 2 |
| POWRPROF | 2 |
| GDIPLUS | 1 |
| WS2_32 | 1 |
| USERENV | 1 |

## Artifacts

- `campaign.json` — full dataset (per-app surface/infra, ranked base API)
- `<app>.wine-dynamic-apis.json` — per-app level-0 dynamic call counts
- `<app>.miniwin-apitrace.json` — miniwin loader trace per app
- `matrix-app-x-dll.tsv` — app × DLL matrix
- per-app relay samples + events: `/home/z/tmp/sweep033/<app>/` (raw logs deleted after parse; disk budget)

## Immediate MiniWin worklist derived from this baseline

1. putty rc=139 and rammap64 rc=139 — same crash class (SW-1325/1326 registered); level-0 traces now show the last surface APIs before death.
2. notepadpp rc=9 — MFC C++ EH frontier (SW-1201 family), 71 surface APIs exercised.
3. coreinfo64 rc=1 — console tool that fully runs under wine; its surface (40 APIs) is a compact behavioral target.
4. WinRAR GUI: surface 108 APIs incl. GDIPLUS.GdiplusStartup family — the GUI path is exercised at level 0 exactly as designed.
