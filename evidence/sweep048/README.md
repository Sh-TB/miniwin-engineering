# SWEEP-048 «چهار قطار گرافیک» — the four GUI trains

User directive: test FOUR GUI programs — (1) an ODF viewer, (2) a Windows
calculator, (3) a program that does not exist on Linux at all (free choice),
(4) a game from the Windows XP games list. ادامه قطار.

## The four trains

| Train | Program | Source (sha-pinned in provenance_sha256.txt) | Result |
|---|---|---|---|
| A — calculator | **calc.exe 5.2.3790 x64** (XP Pro x64 SP2) | archive.org crmpxoem-en ISO → AMD64/CALC.EX_ expanded **by the engine's own 7za** | **exit 0 x3**, dialog DLGTEMPLATEEX cdit=30 resolved (SW-1206 plane), 30 children created (id=403 EDIT + buttons), scripted clicks delivered as WM_COMMAND id=131/132 into the guest's own wndproc (trace: [GUI-EVENT] lines) |
| B — ODF viewer | **soffice.bin x64 26.8.1** (LibreOffice) | documentfoundation.org official MSI → CAB carved via olefile → 19,427 files extracted **through the engine's 7za** | import forest FULLY RESOLVED (mergedlo.dll 152MB / 50,142 export names / 2,862 unresolved → **0**), dies in msvcp140 CRT boot = **SW-1252** frontier |
| C — not on Linux | **everything.exe 1.4.1.1028 x64** (voidtools) + bonus **mspaint.exe x64** (XP x64 SP2) | voidtools.com zip / XP x64 ISO | Everything: message-loop **park 124** (27 unresolved, no crash); mspaint: real **MFC42U.DLL x64** loaded + 100% import resolution (was 633 unresolved → 0), abort at MFC AfxInit = **SW-1247** frontier |
| D — XP game | **winmine.exe x64** (XP Pro x64 SP2 Minesweeper) | XP x64 ISO → AMD64/WINMINE.EX_ | **exit 0 x3, ZERO stub calls**, 1 unresolved (ShellAboutW only), 3 MessageBox dialogs RENDERED (BMP evidence screenshots/messagebox_N.bmp) + GDI frame (frame_001.bmp) |

Bonus corpus (all XP x64 SP2, all sha-pinned): spider.exe, freecell.exe,
mshearts.exe, notepad.exe, pinball.exe (+ PINBALL.DAT).

## Engine wagons LANDED this round

- **SW-1244** w3 handle table: MAX_HANDLES 64→8192 + CloseHandle file-slot
  recycling (7za create: 63/874 files + 811× "Error #80070004" → **874/874
  rc=0**; unlocked both LibreOffice and FreeOffice extraction). MINWIN_HEAP_TRAP=warn
  diagnostic mode added (default trap unchanged).
- **SW-1245** case-insensitive disk DLL search (mspaint imports "MFC42U.DLL",
  disk had "mfc42u.dll"; ext4 stat missed it → all MFC ordinals stub-0).
- **SW-1246** crash_handler out-of-PE backtrace walker bounded (libc crash
  frames killed the handler itself before; now stack-plausibility-gated).
- **SW-1247** MFC42U msvcrt plane: _ltow/_ultow/labs/__CppXcptFilter + 20 more
  msvcrt names + __stdio_common_vsprintf_s/onexit/locale/pctype UCRT batch
  (SW-1251) + __argc/__wargv data plane (real UTF-16 argv) + 19 gdi32/user32/
  kernel32 tail (metafile cookies, PolyBezierTo, wvsprintfW, GetProcessVersion
  0x00050002, lstrcatW...). Fixed "malloc(): corrupted top size" abort class.
- **SW-1250** load_pe_dll recursively disk-loads its OWN import descriptors
  (mergedlo → libxml2/libxslt/sal3/clucene/msvcp140/vcruntime140 now load);
  msvcp140/vcruntime140 dropped from the known-dll pseudo table (the real
  DLLs are better than the pseudo-handle).

## Frontiers registered (open)

- **SW-1243** 7za-under-miniwin SIGSEGV on `l -tISO` (617MB ISO) and on
  cards.ex_ LZX expand (10/11 cabs expand; only CARDS.DL_ traps).
- **SW-1247-open** mspaint/mshearts: MFC AfxInit host-heap corruption
  ("malloc(): corrupted top size") AFTER 100% import resolution; init-term
  initializer MFC42U RVA 0xa3d60 → 0x4b400 chain.
- **SW-1248** MINWIN_HEAP_GUARD=1 regresses the DLL disk-load path (MFC42U
  ordinals go unresolved under guard mode; loader-side, guard is diagnostic-only).
- **SW-1252** soffice.bin: msvcp140.dll DllMain zeroes exactly 8 bytes of its
  own .text at RVA 0x1110 during early CRT boot (section verify: image==file
  pre-DllMain; __acrt_iob_func/malloc churn traces right before).

## Measurement (final binary dce5784c...)

- G1 PIN 12/12 (evidence/master/g1/): exit 0, 'upx 4.2.4' exact, no crash,
  regression suite 12/12.
- Corpus census x3 DETERMINISTIC: 14× exit-0 + pageant 124 park, ZERO
  regressions (artifacts/sweep047/census.tsv).
- New-guest census x3 DETERMINISTIC (artifacts/sweep048/census/census.tsv):
  calc/winmine/spider/freecell/notepad/pinball = 0, everything = 124 park,
  mspaint+mshearts = 134 (SW-1247 frontier), soffice = 139 (SW-1252 frontier).
- Acceptance batteries ALL GREEN on the final binary: snake 30 frames
  byte-identical x3, calc accept 9/9 + battery 15/15, breakout 381 frames /
  40→13 bricks, BUG-054b 3/3.
