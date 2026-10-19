# SWEEP-50 WORKLIST — 50 items (user mandate 2026-10-18: «۵۰ مورد تسک باید بزاری تو لیست کاری»)

**Source:** mass-sweep of 100 downloaded programs (72 executed: 70 x64 + 2 x86 probes).
Every item below cites its blocking evidence from `artifacts/sweep50/STAGE_TABLE_100.tsv`.
Rules unchanged: 3x acceptance, irreversible status labels, no fake success.

**Post-sweep baseline (SWEEP-001/002 landed, G1 12/12 green):**
4×RAN-OUTPUT(exit 0) · 12×RAN-EXIT2/4/9/126/255 · 15×RAN-EXIT1(usage) · 35×CRASH-GUEST ·
7×CRASH-LOADER · 2×LOAD-REJECT(x86 by design) · 28×ARCH-OUT-OF-SCOPE(never run).

## P0 — user mandate

- [ ] **SW-501 (MICRO-GAME, MUST-100%)**: run a micro-game 100% — VERIFIED 3×3.
      Candidates (pick smallest obtainable first): 2048-class win64 exe; Minesweeper-class
      win64; fallback famous: Chocolate-Doom win64 + Doom shareware WAD (both direct
      download). Any blocker becomes BUG-* and this item stays open until exit-0 + real
      game output. This is the user's 100% mandate item.

## P0 — loader-side crashes still open (7 programs)

- [ ] **SW-502** CRASH-LOADER isolate: go-bombardier + go-mkcert (only 4 unresolved
      imports yet loader dies) — gdb the fault, one BUG-*, fix.
- [ ] **SW-503** CRASH-LOADER isolate: rust-fd + rust-hexyl (13-25 unresolved) —
      same fault class? one BUG-*.
- [ ] **SW-504** CRASH-LOADER isolate: gui-winmergeu (515 unresolved) + si-B-bginfo64
      (513) + putty-puttygen (78) — high-volume unresolved + GUI init fault.
- [ ] **SW-505** Crash-report gap: several runs die SIGSEGV with NO [CRASH] banner
      (strings64 after stub-call #15, rg after #2) — sigaltstack/SA_ONSTACK audit;
      the handler itself must survive guest-stack overflow. One BUG-*.

## P0 — runtimes (unblocks whole spectra)

- [ ] **SW-506** Go boot beyond osinit: age dies at RVA 0x869a5 during runtime boot
      (post timeBeginPeriod fix). Needs: guest-thread + TLS + EH interplay under Go
      runtime. One BUG-* per stage gate.
- [ ] **SW-507** Go optional syscalls (9/program): WerSetFlags, WerGetFlags,
      SetWaitableTimer, SetProcessPriorityBoost, RaiseFailFastException, GetErrorMode,
      CreateWaitableTimerExW, GetQueuedCompletionStatusEx, AddVectoredContinueHandler.
- [ ] **SW-508** Rust std remainder (rg list): CompareStringOrdinal, lstrlenW,
      GetUserProfileDirectoryW, ReadFileEx, WriteFileEx, SetThreadStackGuarantee,
      IsThreadAFiber — each → stub-with-real-semantics or honest fail-fast.
- [ ] **SW-509** TASK-LOADER-DLL promote to P0: 16+ sweep programs blocked on real DLL
      deps (openssh×13 needs libcrypto.dll, wimlib needs libwim-15.dll, 7z.exe needs
      7z.dll). PE-DLL loader is the single biggest unblock left.
- [ ] **SW-510** PyInstaller path (yt-dlp): 30 unresolved then CRASH-GUEST — map the
      PyInstaller bootstrap API set; document as its own mini-ADR.

## P1 — easy API wins (exit-1 usage → exit-0 banner class)

- [ ] **SW-511** version.dll six-pack: VerQueryValueW/A + GetFileVersionInfoW/A +
      GetFileVersionInfoSizeW/A (real VS_VERSIONINFO parse from guest image) — kills
      the "(null) v(null)" banners on strings64/sigcheck/psinfo class.
- [ ] **SW-512** GetLocalTime (real host time) — plink/sigcheck/psinfo date paths.
- [ ] **SW-513** GetWindowsDirectoryA/W + GetSystemDirectoryA/W (honest C:\Windows).
- [ ] **SW-514** GlobalMemoryStatus (+64-bit variant) — Sysinternals startup.
- [ ] **SW-515** GetThreadTimes + GetProcessTimes (getrusage mapping exists at loader.c:9009).
- [ ] **SW-516** GetUserNameA/W + GetUserProfileDirectoryW (USER env + getpwuid mapping).
- [ ] **SW-517** Registry A-family read path: RegOpenKeyA/RegQueryValueExA backed by an
      in-memory hive file (GUI tools persist settings; A-family first, W later).
- [ ] **SW-518** Registry A-family write path: RegCreateKeyExA/RegSetValueExA/RegCloseKey.
- [ ] **SW-519** ninja exit 4 → exit 0: trace which missing API breaks its usage path
      (12 unresolved incl. GetFileAttributesExW class) — quick G1-style pin.
- [ ] **SW-520** si-A-du64 exit 255 → diagnose; si-A-psinfo64 exit 9 → diagnose;
      si-A-sigcheck64 exit 126 → diagnose (three one-hour triage tickets).

## P1 — winsock-lite (T2 ladder feed)

- [ ] **SW-521** WSAStartup/WSACleanup minimal socket state (plink/pscp/psftp deep paths).
- [ ] **SW-522** socket/connect/send/recv/closesocket on Linux fd mapping.
- [ ] **SW-523** getaddrinfo/gethostbyname (use host resolver; log DNS honestly).
- [ ] **SW-524** select/WSAPoll subset for ssh-keyscan connect scan.
- [ ] **SW-525** ioctlsocket/FIONBIO + WSAEventSelect stub-class for netcat-class rungs.

## P1 — GUI frontier (needs SW-504 first)

- [ ] **SW-526** user32 window class minimals: RegisterClassExA/W + CreateWindowExA/W
      returning a real HWND handle + DestroyWindow.
- [ ] **SW-527** GetMessage/DispatchMessage/PostMessage queue (single-thread message
      pump over an internal ring buffer).
- [ ] **SW-528** DialogBoxIndirectParamA/W + GetDlgItem + EndDialog (Sysinternals
      EULA/progress dialogs block every GUI rung's startup).
- [ ] **SW-529** GDI minimal: BeginPaint/EndPaint + FillRect/DrawTextW offscreen-only.
- [ ] **SW-530** comctl32 8.0-era: InitCommonControlsEx + trackbar/progress subset
      (NirSoft cports/produkey/searchmyfiles import surface).
- [ ] **SW-531** Clipboard: OpenClipboard/GetClipboardData/CloseClipboard via X11/WL
      bridge or honest empty-clipboard semantics.

## P2 — named pipes / serial / security (plink & Sysinternals deep)

- [ ] **SW-532** CreateNamedPipeA/W + ConnectNamedPipe (PLINKpipe protocol plink uses).
- [ ] **SW-533** Serial comm six: ClearCommBreak/SetCommBreak/GetCommState/SetCommState/
      SetCommTimeouts + BuildCommDCB (plink -serial).
- [ ] **SW-534** SID family: AllocateAndInitializeSid/CopySid/EqualSid/GetLengthSid +
      descriptor init/set (accesschk64/psinfo64 security columns).
- [ ] **SW-535** CheckTokenMembership/IsUserAnAdmin (accesschk64 admin path).

## P2 — old-era msvcrt hardening (path-B proxy evidence)

- [ ] **SW-536** msvcrt 8.0-era NirSoft import audit (comctl32/mpr.dll/version.dll/
      comdlg32 triple) — produce the missing-exports TSV like the 7-Zip census.
- [ ] **SW-537** mpr.dll WNet* stub family (cports/produkey/searchmyfiles).
- [ ] **SW-538** wevtapi + netapi32 stubs (accesschk/winsta class).
- [ ] **SW-539** pdh.dll minimal (psinfo64 CPU counters) — PdhOpenQuery/PdhCollectQueryData.
- [ ] **SW-540** wintrust+winhttp pair (sigcheck64 signature path).

## P2 — printing & GDI deeper

- [ ] **SW-541** comdlg32 PrintDlgA → detect-no-printer honest failure (strings64 print menu).
- [ ] **SW-542** gdi32 print path: StartDocA/StartPage/EndPage/EndDoc/SetMapMode/
      GetDeviceCaps (offline EMF-to-PDF later; honesty first).

## P2 — batch hygiene

- [ ] **SW-543** sweep runner 2.0: merge A/B scripts, fix classification (unresolved-
      grep false positives), add per-run stage tags directly.
- [ ] **SW-544** census→worklist automation: script that diffs two sweep runs and
      auto-drafts BUG-* candidates from new unresolved names.
- [ ] **SW-545** api_trace capture fix for batch B (parse-err rate-limiter audit).
- [ ] **SW-546** ARM64 probe pair (accesschk64a, coreinfo64a): confirm clean
      LOAD-REJECT messaging = honest "unsupported arch" contract.
- [ ] **SW-547** x86 path-B feasibility note fed by probes: 7zr.exe + strings.exe
      reject messages recorded as the boundary contract (ADR addendum).
- [ ] **SW-548** G1 evidence: pin the four RAN-OUTPUT programs (ndisasm, procs,
      testlimit64, coreinfoex64) as regression pins (exit + first-line exact).
- [ ] **SW-549** Ladder cross-link: map each SWEEP-50 program to LADDER-0xx rungs
      (PuTTY→LADDER-022..024, Sysinternals→028..030, busybox64→007).
- [ ] **SW-550** Sweep report doc: evidence/sweep50/REPORT.md with before/after
      SWEEP-001/002 stage deltas + SHA-256 of every binary (export from census).

---
**Status vocabulary:** PLANNED → TRIAGED → IN-PROGRESS → TESTED → VERIFIED (irreversible).
**Micro-game SW-501 is the user's 100% mandate — nothing else outranks it.**

## SWEEP-003 wave (2026-10-18) — GUI skeleton + BUG-066, user mandate «برنامه‌های گرافیکی متوسط»

**Landed (code, VERIFIED 3×3 + G1 12/12 re-pinned):**
- SWEEP-003 GUI skeleton v1: RegisterClass{,Ex}{W,A}→ATOM, CreateWindowEx{W,A}→HWND,
  ShowWindow/UpdateWindow/DestroyWindow→TRUE, GetMessage{W,A}→WM_QUIT (no event source),
  TranslateMessage/DispatchMessage/DefWindowProc{W,A}, PostQuitMessage,
  COMCTL32 InitCommonControlsEx→TRUE + InitCommonControls.
- BUG-066: seh_dispatch_exception C++ object dump dereferenced Param[0]=0x19930520
  (MSVC EH magic, NOT a pointer) → loader SIGSEGV inside its own dispatcher.
  Fix: x64 C++ layout (param[1]=object) + process_vm_readv probe, dump from local
  buffer only. zoomit64/bginfo64/winmergeu: 139→133/9/9, zero [CRASH] banners.

**GUI stage map now (16 programs, all 3×3 deterministic — runs_gui/):**
4×RAN-EXIT0 (produkey=full GUI lifecycle InitCommonControlsEx→CreateWindowExA→
ShowWindow→GetMessage→exit(0); coreinfoex64; procmon64; diskmon64) ·
4×RAN-EXIT1 (putty-gui, pageant, autoruns64, rammap64) · 1×RAN-EXIT2 (procexp64) ·
2×RAN-TERM9-CXX-UNHANDLED (bginfo64, winmergeu: 14 personality calls, walk works,
unhandled→fail-fast 0xC0000409) · 2×RAN-TRAP133 (adexplorer64, zoomit64) ·
3×CRASH-GUEST (cports 0x14001b338 past init gate; searchmyfiles 0x140028001;
puttygen RIP=0x0).

- [x] **SW-511 (partial → VERIFIED)** comctl32 init gate: InitCommonControlsEx
      was the exit(1) cause of cports+produkey (GetProcAddress→FALSE). Landed.
- [ ] **SW-551** isolate cports SIGSEGV 0x14001b338 (post-init, process-enum phase,
      psapi via GetProcAddress optional-stub) — one BUG-*.
- [ ] **SW-552** isolate puttygen RIP=0x0 (call through NULL — bound-import/ordinal
      slot left NULL despite SWEEP-001?) — one BUG-*.
- [ ] **SW-553** isolate adexplorer64/zoomit64 SIGTRAP (133) after UnhandledExceptionFilter —
      DebugBreak/int3 in fail-fast path; decide honest mapping (one BUG-* or WONTFIX note).
      UPDATE (SWEEP-007): adexplorer64 is UNOBTAINABLE — Microsoft retired
      ADExplorer.zip from download.sysinternals.com (404). zoomit64 TRAP133 re-confirmed
      3x deterministic; SW-553 is now zoomit64-only.
- [ ] **SW-554** putty-gui + pageant exit(1) gate: find the stub-call that returns the
      fatal FALSE (trace shows CRT-exit sequence after mscoree probe) — needs named
      stub-call logging (per-import thunk identity) as tooling prerequisite.
- [ ] **SW-555** procexp64 exit(2) + autoruns64/rammap64 exit(1) gates: same named-stub
      prerequisite, then fix top blocker each.
- [ ] **SW-556** SW-509 (PE-DLL loader) now blocks 4 GUI heavyweights
      (bginfo64/winmergeu C++ runtime, searchmyfiles mfcu, procexp64) — raise priority.

## SWEEP-004 wave (2026-10-18) — GDI-lite dialog raster, image mandate DELIVERED

**Landed (code, VERIFIED 3×3 identical SHA + G1 12/12 re-pinned):**
- MessageBoxW/MessageBoxA now RENDER a classic Win95 dialog to
  <results>/<exe>/screenshots/messagebox_N.bmp (font8x8, public domain).
- First image from a real graphical program: putty.exe --version ->
  MessageBoxA("PuTTY Fatal Error", "Unable to load any WinSock library")
  -> dialog #1 (evidence/sweep004_gdi/putty_messagebox_1.bmp, 604x122,
  3 runs byte-identical SHA f953a2f3ec131b3a*).

**2 NEW programs tested (user mandate «یکی دوتا برنامه جدید»):**
- DriverView.exe (NirSoft x64 GUI): CRASH-GUEST 139 @ 0x14000cd1d, after
  advapi32 privilege phase (LookupPrivilegeValueA/AdjustTokenPrivileges
  resolved, ERROR 1300 honest path) — SW-557.
- HeapMemView.exe (NirSoft x64 GUI): CRASH-GUEST 139 @ 0x14000c581, same
  class — SW-558.

- [ ] **SW-557** isolate DriverView 0x14000cd1d (post-privilege, stub call
      #1/#2 unnamed — named-stub logging prerequisite stands).
- [ ] **SW-558** isolate HeapMemView 0x14000c581 (same class — likely one
      shared BUG with SW-557).
- [ ] **SW-559** extend GDI-lite: TextOutA/W + FillRect + real WM_PAINT
      surface so programs that draw their OWN window content produce images
      (produkey window render = end goal).

## SWEEP-005 wave (2026-10-18) — SW-501 micro-game 100% DELIVERED + real paint surface

**Landed (loader.c, VERIFIED 3×3 byte-identical + G1 12/12 re-pinned):**
- SWEEP-005 paint surface + event pump v2 (gated by MW_GUI_EVENTS=1; default v1
  contract untouched): persistent framebuffer from guest's own CreateWindowEx
  size; RegisterClass stores lpfnWndProc (offset 8) and DispatchMessage CALLS it;
  WM_PAINT on demand (UpdateWindow/InvalidateRect), WM_TIMER (SetTimer), scripted
  WM_KEYDOWN (MW_GUI_KEYS, paced MW_GUI_KEY_EVERY), budget guard, WM_QUIT with
  wParam=exit code filled into MSG (Win32 semantics).
- GDI32 pseudo-module (24 entries): BeginPaint/EndPaint/GetDC/FillRect/DrawTextA/
  GetClientRect/InvalidateRect/ValidateRect/GetSysColor/LoadCursor/LoadIcon/
  CallWindowProc/wsprintf(A|W)/lstrlen(A|W)/SetTimer/KillTimer/PeekMessage + GDI32:
  TextOutA/W, ExtTextOutA, Rectangle, Ellipse, MoveToEx, LineTo, SetPixel,
  CreateSolidBrush, CreatePen, GetStockObject, SelectObject, DeleteObject,
  SetTextColor, SetBkColor, SetBkMode, SetMapMode, GetDeviceCaps, SaveDC,
  RestoreDC, DeleteDC, PatBlt. EndPaint dumps framebuffer ->
  <results>/<exe>/screenshots/frame_NNN.bmp (the guest's own rendered frames).
- BUG-067: zig/mingw ucrt startup imports __p__acmdln (api-ms-win-crt-runtime);
  unresolved stub returned 0 -> CRT deref NULL (mov rsi,[rax]) -> SIGSEGV before
  main. Fix: mw___p__acmdln returns &g_acmdln_ptr.
- BUG-067b: ms_abi variadic stub (wsprintfA) with SysV va_list read garbage
  (reg_save_area=0) -> SIGSEGV inside mw_vfmt_a. Fix: __builtin_ms_va_list
  (Wine pattern).
- BUG-067c: pump priority PAINT-before-KEY let the per-tick pending paint steal
  every key-parity slot -> scripted keys never delivered. Fix: KEY first.

**SW-501 ACCEPTANCE (user mandate «شبیه سازی بازی میکرو ... ۱۰۰ درصد»): DELIVERED.**
- tests/games/miniwin_snake.c — a REAL Win32 GDI game (RegisterClassA -> CreateWindowExA
  -> ShowWindow/UpdateWindow -> SetTimer loop -> WndProc WM_TIMER step /
  WM_KEYDOWN turn / WM_PAINT draw with FillRect/Ellipse/Rectangle/TextOutA via
  CreateSolidBrush+SelectObject -> ESC/PostQuitMessage clean exit). Compiled to
  PE x64 with zig cc (machine 0x8664, imports USER32/GDI32/KERNEL32+ucrt sets).
- scripts/sweep005_snake_accept.sh: 3 runs (MW_GUI_EVENTS=1, keys
  40,39,38,39,40,39,38,39,40,39,27): exit 0 ×3, 35 frames ×3, ALL 35 frames
  byte-identical SHA-256 across runs; HUD shows Score: 10 | Len: 9 (LCG seed
  12983 puts first food on the scripted path); food eaten -> grow -> respawn
  verified in pixels. STAGE_TABLE row: RAN-EXIT0-GAME-100%.

- [x] **SW-501** micro-game runs 100% under MiniWin — DONE (this wave).
- [x] **SW-559 (core)** real WM_PAINT surface + TextOut/FillRect landed; open
      remainder: produkey window content render (listview is comctl32-internal)
      -> needs child-control emulation or SW-509 PE-DLL loader.
- [x] **SW-560** games wave 2 — DONE (SWEEP-006): BitBlt/CreateCompatibleDC/
      CreateCompatibleBitmap REAL double buffering (draws routed by HDC;
      window framebuffer changes only via BitBlt SRCCOPY 172800 px/frame);
      WM_CHAR queued by real TranslateMessage (printable VKs only — snake
      cadence unchanged, re-verified 3x); synchronous WM_CREATE on
      CreateWindowEx{W,A}. Second genre = MiniWinBreakout
      (tests/games/miniwin_breakout.c): 3x exit0, 381 frames x3 ALL
      byte-identical SHA-256, 27/40 bricks destroyed (score 270, lives 3),
      evidence/sweep006_game/. Pre-commit bugs fixed: memDC handle decode
      off-by-one; SelectObject must not deselect bitmap on brush/pen;
      WM_CREATE was never delivered; guest auto-launch counter zeroed each
      tick (ball parked — caught by frozen-score frame diff). G1 12/12
      re-pinned; snake 3x re-pinned (30 frames x3, HUD Score:10 Len:9).
      Open remainder: GetAsyncKeyState (not needed for the genre, tracked
      for SW-562 battery), StretchBlt/transparent blits for sprites.
- [ ] **SW-561** Coreinfo64.exe (live.sysinternals, current version) exit(1):
      newer exe needs SetThreadGroupAffinity/GetProcessWorkingSetSize/
      version.dll trio — version drift vs SWEEP-003 coreinfoex64, one BUG-*.
      CONFIRMED by SWEEP-007 battery: re-downloaded Coreinfo64.exe exit 1 x3
      (SWEEP-003 binary exited 0) — version drift is real and reproducible.
- [x] **SW-562** MW_GUI_EVENTS=1 battery — DONE (SWEEP-007): 15 re-acquired GUI
      programs x3 under the pump, 15/15 3x-deterministic (exit+paints+frames).
      Map: produkey+cports ARM the pump and crash in the synchronous WM_CREATE
      (comctl32 child-control cliff — produkey 0x22f24 at stub #4; cports moves
      its old 0x1b338 site earlier to 0x2119); procmon64/diskmon64 exit 0 with
      armed=0 — NO runtime CreateWindowEx/GetMessage (SWEEP-003 GUI-lifecycle
      labels were overly generous, honest stage = PRE-WINDOW-CLEAN-EXIT);
      putty 2nd dialog-image confirm; pageant FIRST dialog image ("Unable to
      access security APIs"); coreinfoex64 exit1 = version drift (SW-561);
      adexplorer64 UNOBTAINABLE (retired 404, folded into SW-553 note).
      Named-stub gates (procexp64/autoruns64/rammap64/puttygen) unchanged —
      SW-554/555 named-stub-call logging is the highest-leverage next step.
      Evidence: evidence/sweep007_battery/ (README, battery_map.png, 2 dialog
      PNGs, battery_raw.tsv 105 rows); scripts/sweep007_redownload_gui.py +
      sweep562_battery.sh + sweep562_images.py; exe SHA manifest in
      artifacts/sweep50/sweep007_redownload_manifest.tsv.

---

# SWEEP-008 WORKLIST EXPANSION — x3 mandate («ادامه سه برابری», SWEEP-008 wave)

**Source:** SWEEP-008 honest-stub batch + named-stub guest_rip decoding +
in-memory registry + EULA-dialog automation + mass breadth sweep (23 programs,
`artifacts/sweep008/mass/sweep008_mass.tsv`). Every item cites its blocker.
Rules unchanged: 3x acceptance, irreversible labels, no fake success.

## P0 — SWEEP-008 open frontiers (from the gates that moved)

- [ ] **SW-600** COMCTL32 undocumented ordinal table: autoruns64 blocks on
      COMCTL32!#344 stub-0, rammap64 on COMCTL32!#17 (both named by SWEEP-008
      guest_rip decode; return 0 = init-fail -> graceful exit 1 after EULA).
      Build an ordinal->name map (Wine spec + public ABI lists), implement the
      Sysinternals-used subset, re-run both gates. Exit 1 -> non-1 = pass.
- [ ] **SW-601** procexp64 RVA 0x9be71 guest NULL-deref after 37 named stub
      calls (`mov ecx,[rdx]` RDX=0) — bisect the stored-NULL handle: candidates
      LoadStringW/StrStrIW/remaining Nt* stubs. One BUG-*, re-run gate.
- [ ] **SW-602** GetNumberFormatW/GetCurrencyFormatW real formatting (rammap64
      frontier): LCID 0x0409 integer/grouping format, honest LOCALE_SDECIMAL.
- [ ] **SW-603** _strlwr (cports frontier, 41 stubs deep): CRT string-case
      helper as data import; cports crash -> output = pass.
- [ ] **SW-604** StrStrIW/StrChrIW shlwapi family (procexp64 frontier).
- [ ] **SW-605** FlushInstructionCache (autoruns64 first stub — cache flush
      honest no-op TRUE on x64).
- [ ] **SW-606** CreateToolbarEx + ImageList_DragMove (diskmon64 exit 0 already;
      produkey frontier CreateToolbarEx — toolbar-window class registration).
- [ ] **SW-607** AllocateAndInitializeSid / FreeSid (pageant frontier): honest
      SID allocation — nonzero synthetic SID pointer, FreeSid no-op.
- [ ] **SW-608** GetSysColorBrush real palette handles (bginfo64/winmergeu
      frontier — returns brush handle, colors honored by GDI-lite fill).
- [ ] **SW-609** PathStripToRootW (winmergeu, 17 stubs deep) — real path math.
- [ ] **SW-610** SnmpUtilOidNCmp (bginfo64) — lexicographic OID compare.
- [ ] **SW-611** GetWindowsDirectoryA/GetCurrentObject (puttygen frontier).
- [ ] **SW-612** CreateBitmap (putty frontier — first stub; bitmap object with
      1bpp/24bpp backing so dialog-image paths stay alive).
- [ ] **SW-613** LoadStringW real resource loading (procexp64 first stub):
      PE .rsrc RT_STRING (type 6) walk — reuse SWEEP-008 resource walker.
- [ ] **SW-614** timeKillEvent/CreateEventExW (zoomit64 TRAP-133 family,
      SW-553 scoping unchanged) — timer-kill honest stop, event object real.
- [ ] **SW-615** SetThreadGroupAffinity/GetNumaHighestNodeNumber
      (coreinfoex64 -accepteula frontier — it now runs past the version-drift
      exit-1, SW-561 partially mitigated by build 19045; verify 3x).
- [ ] **SW-616** __wgetmainargs crash (searchmyfiles) — UCRT startup data
      import; likely same family as BUG-067 __p__acmdln.
- [ ] **SW-617** curl exit 139 (was RUNS-PARTIAL in SWEEP-50?): isolate the
      new crash site — if it is past the previous frontier, one BUG-*.
- [ ] **SW-618** MW_EULA_ACCEPT contract test: a scripted Win32 EULA-dialog
      PE compiled from tests/games/ template that exercises
      DialogBoxParamW/EndDialog/IsDlgButtonChecked/CheckDlgButton end-to-end,
      3x exit-0 with accepted+declined variants.
- [ ] **SW-619** Registry persistence option: optional file-backed RAM hive
      (MW_REG_FILE=path) so EulaAccepted survives loader restarts; document.

## P1 — corpus restoration and breadth

- [ ] **SW-620** The SWEEP-50 100-program corpus is no longer on disk (only
      the 15 SWEEP-007-reacquired GUI programs + opensource + games remain;
      23 executables total swept in SWEEP-008). Re-download the missing
      corpus from artifacts/sweep50/download_log.tsv + redownload manifest,
      verify SHAs where recorded, then re-run the mass sweep to regenerate
      the 100-row progress table.
- [ ] **SW-621** After SW-620: re-run mass sweep, diff vs STAGE_TABLE_100.tsv,
      produce a per-program delta report (exit-before -> exit-after, stub
      depth) into artifacts/sweep008/.
- [ ] **SW-622** Third GUI genre: a Win32 notepad-class editor (menu + EDIT
      control) as a third acceptance target after snake/breakout/calc.
- [ ] **SW-623** Fourth genre: Win32 common-dialog (GetOpenFileNameW mock)
      tester to exercise comdlg32 frontier honestly.
- [ ] **SW-624** putty full login-dialog lifecycle under MW_EULA_ACCEPT-style
      scripted dialog automation (pageant/pageant-dialog follow-up).

## P2 — generated per-program items (from SWEEP-008 mass sweep)

- [ ] **SW-625** `opensource/7zip/full/7z` — exit=0, stub-calls=0. Next: keep-green regression pin (3x exit-0 + output SHA) after every loader change.
- [ ] **SW-626** `opensource/7zip/x64/7za` — exit=0, stub-calls=0. Next: keep-green regression pin (3x exit-0 + output SHA) after every loader change.
- [ ] **SW-627** `opensource/curl/curl` — exit=139, stub-calls=0. Next: isolate the crash site (RVA/rip map), fix one BUG-*, re-run.
- [ ] **SW-628** `opensource/jq/jq` — exit=0, stub-calls=0. Next: keep-green regression pin (3x exit-0 + output SHA) after every loader change.
- [ ] **SW-629** `opensource/sqlite/sqlite3` — exit=0, stub-calls=0. Next: keep-green regression pin (3x exit-0 + output SHA) after every loader change.
- [ ] **SW-630** `sweep005/snake` — exit=0, stub-calls=0. Next: keep-green regression pin (3x exit-0 + output SHA) after every loader change.
- [ ] **SW-631** `sweep006/breakout` — exit=0, stub-calls=0. Next: keep-green regression pin (3x exit-0 + output SHA) after every loader change.
- [ ] **SW-632** `sweep008/calc` — exit=0, stub-calls=0, first unresolved `lstrcatA`. Next: keep-green regression pin (3x exit-0 + output SHA) after every loader change.
- [ ] **SW-633** `autoruns64/Autoruns64` — exit=1, stub-calls=1, first stub `CoInitializeEx`, first unresolved `FlushInstructionCache`. Next: the app exits 1 by its own decision — name the gate via guest_rip trace, implement the honest blocker (registry value, version check, or stub-0 API), re-run.
- [ ] **SW-634** `bginfo64/Bginfo64` — exit=9, stub-calls=3, first stub `GetSysColorBrush`, first unresolved `SnmpUtilOidNCmp`. Next: isolate the crash site (RVA/rip map), fix one BUG-*, re-run.
- [ ] **SW-635** `coreinfoex64/Coreinfo64` — exit=139, stub-calls=2, first stub `GetNumaHighestNodeNumber`, first unresolved `SetThreadGroupAffinity`. Next: isolate the crash site (RVA/rip map), fix one BUG-*, re-run.
- [ ] **SW-636** `cports/cports` — exit=139, stub-calls=41, first stub `<unknown>`, first unresolved `_strlwr`. Next: isolate the crash site (RVA/rip map), fix one BUG-*, re-run.
- [ ] **SW-637** `diskmon64/Diskmon64` — exit=0, stub-calls=3, first stub `RegOpenKeyA`, first unresolved `CreateToolbarEx`. Next: keep-green regression pin (3x exit-0 + output SHA) after every loader change.
- [ ] **SW-638** `pageant/pageant` — exit=1, stub-calls=0, first unresolved `AllocateAndInitializeSid`. Next: the app exits 1 by its own decision — name the gate via guest_rip trace, implement the honest blocker (registry value, version check, or stub-0 API), re-run.
- [ ] **SW-639** `procexp64/procexp64` — exit=139, stub-calls=41, first stub `LoadStringW`, first unresolved `StrStrIW`. Next: isolate the crash site (RVA/rip map), fix one BUG-*, re-run.
- [ ] **SW-640** `procmon64/Procmon64` — exit=1, stub-calls=0, first unresolved `ImageList_DragMove`. Next: the app exits 1 by its own decision — name the gate via guest_rip trace, implement the honest blocker (registry value, version check, or stub-0 API), re-run.
- [ ] **SW-641** `produkey/ProduKey` — exit=0, stub-calls=9, first stub `CoInitialize`, first unresolved `CreateToolbarEx`. Next: keep-green regression pin (3x exit-0 + output SHA) after every loader change.
- [ ] **SW-642** `putty/putty` — exit=1, stub-calls=1, first stub `CoUninitialize`, first unresolved `CreateBitmap`. Next: the app exits 1 by its own decision — name the gate via guest_rip trace, implement the honest blocker (registry value, version check, or stub-0 API), re-run.
- [ ] **SW-643** `puttygen/puttygen` — exit=1, stub-calls=4, first stub `GetWindowsDirectoryA`, first unresolved `GetCurrentObject`. Next: the app exits 1 by its own decision — name the gate via guest_rip trace, implement the honest blocker (registry value, version check, or stub-0 API), re-run.
- [ ] **SW-644** `rammap64/RAMMap64` — exit=1, stub-calls=0, first unresolved `GetNumberFormatW`. Next: the app exits 1 by its own decision — name the gate via guest_rip trace, implement the honest blocker (registry value, version check, or stub-0 API), re-run.
- [ ] **SW-645** `searchmyfiles/SearchMyFiles` — exit=139, stub-calls=1, first stub `__wgetmainargs`, first unresolved `__wgetmainargs`. Next: isolate the crash site (RVA/rip map), fix one BUG-*, re-run.
- [ ] **SW-646** `winmergeu/WinMergeU` — exit=9, stub-calls=17, first stub `GetSysColorBrush`, first unresolved `PathStripToRootW`. Next: isolate the crash site (RVA/rip map), fix one BUG-*, re-run.
- [ ] **SW-647** `zoomit64/ZoomIt64` — exit=133, stub-calls=3, first stub `CreateEventExW`, first unresolved `timeKillEvent`. Next: isolate the crash site (RVA/rip map), fix one BUG-*, re-run.

## P2 — API-surface completion sweep (one item per family, evidence-cited)

- [ ] **SW-648** KERNEL32 process/job family: CreateJobObjectW/SetInformationJobObject/IsProcessInJob — honest single-process job.
- [ ] **SW-649** KERNEL32 memory family: VirtualQueryEx/VirtualLock/VirtualUnlock/GetWriteWatch — page-walk backed, honest results.
- [ ] **SW-650** KERNEL32 file family: FindFirstFileExW filters (FIND_FIRST_EX_LARGE_FETCH), GetFileInformationByHandleEx.
- [ ] **SW-651** KERNEL32 thread family: SetThreadAffinityMask/SetThreadPriority/GetExitCodeThread per-thread real values.
- [ ] **SW-652** KERNEL32 sync family: WaitableTimer (SetWaitableTimerEx with APC), CreateMutexW ownership walk.
- [ ] **SW-653** KERNEL32 console family: SetConsoleTextAttribute/WriteConsoleOutputA real attribute buffer for the terminal backend.
- [ ] **SW-654** KERNEL32 locale family: GetLocaleInfoEx/GetDateFormatEx/GetTimeFormatEx — en-US fixed values, honest sizes.
- [ ] **SW-655** KERNEL32 env family: GetEnvironmentVariableW real W environment (build_wargv_wenv already builds A; add W parity test).
- [ ] **SW-656** KERNEL32 library family: LoadLibraryExW LOAD_LIBRARY_AS_DATAFILE honest rejection, AddDllDirectory/GetDllDirectoryW.
- [ ] **SW-657** KERNEL32 time family: QueryPerformanceFrequency real TSC-backed, GetSystemTimePreciseAsFileTime (currently GetProcAddress'd).
- [ ] **SW-658** KERNEL32 error family: SetErrorMode/GetErrorMode (Go runtime SW-507 list), RaiseFailFastException.
- [ ] **SW-659** KERNEL32 debug family: OutputDebugStringA/W -> stderr channel with [DBG] prefix, IsDebuggerPresent=0.
- [ ] **SW-660** USER32 window family: GetWindowRect/GetClientRect real 640x400 values, AdjustWindowRectEx math.
- [ ] **SW-661** USER32 text family: DrawTextA/DrawTextExA/GetTextExtentPoint32A — font8x8-backed metrics.
- [ ] **SW-662** USER32 caret/cursor family: CreateCaret/SetCaretPos/ShowCaret — state machine, no raster.
- [ ] **SW-663** USER32 menu family: CreateMenu/AppendMenuW/DrawMenuBar — menu-object table.
- [ ] **SW-664** USER32 scroll family: SetScrollInfo/GetScrollInfo/SetScrollPos — real bar state.
- [ ] **SW-665** USER32 clipboard family: OpenClipboard/EmptyClipboard/SetClipboardData — RAM clipboard with format registry.
- [ ] **SW-666** USER32 input family: GetKeyState/GetKeyboardState/MapVirtualKeyW — deterministic MW_GUI_KEYS-derived state.
- [ ] **SW-667** USER32 hook family: SetWindowsHookExW WH_CBT/WH_KEYBOARD — honest "not installed" OR minimal call-through.
- [ ] **SW-668** USER32 timer family: SetTimer with NULL hwnd (proc-exp style callback timers) + TIMERPROC dispatch.
- [ ] **SW-669** USER32 DDE/message family: PostMessageW real queue insertion + GetMessage ordering test.
- [ ] **SW-670** USER32 dialog family: GetDlgCtrlID/SetDlgItemInt/GetDlgItemInt — control-handle id decode (0xC3000000+id).
- [ ] **SW-671** GDI32 font family: GetTextMetricsA/EnumFontFamiliesExA — font8x8 metrics, one family listing.
- [ ] **SW-672** GDI32 bitmap family: CreateCompatibleBitmap real per-DC surfaces (SWEEP-006 has the mem-DC trio; extend to more formats).
- [ ] **SW-673** GDI32 region family: CreateRectRgn/CreateRectRgnIndirect/CombineRgn real rect-set algebra.
- [ ] **SW-674** GDI32 pen/brush family: CreatePen/ExtCreatePen/GetStockObject full 8-stock set with raster effects.
- [ ] **SW-675** GDI32 mapping family: SetMapMode/SetWindowExtEx/SetViewportExtEx — coordinate transform in the GDI-lite layer.
- [ ] **SW-676** GDI32 palette family: CreatePalette/RealizePalette/AnimatePalette — procexp color-gate end state.
- [ ] **SW-677** GDI32 metafile family: CreateEnhMetaFileW/PlayEnhMetaFile — honest NULL (no metafile raster) + SetLastError.
- [ ] **SW-678** GDI32 print family: StartDocW/StartPage/EndPage/EndDoc (rammap64 print path) — honest JOB abort or no-op success with counters.
- [ ] **SW-679** ADVAPI32 token family: GetTokenInformation/DuplicateTokenEx — real SID/attribute blobs on the SWEEP-008 token model.
- [ ] **SW-680** ADVAPI32 eventlog family: RegisterEventSourceW/ReportEventW — log to artifacts JSON, honest success.
- [ ] **SW-681** ADVAPI32 crypto family: CryptAcquireContextW/CryptGenRandom — route GenRandom to ProcessPrng source.
- [ ] **SW-682** ADVAPI32 service family: StartServiceW/QueryServiceStatusEx on the empty hive — honest 1060 everywhere (SWEEP-008 OpenServiceW base).
- [ ] **SW-683** SHELL32 family: SHGetFolderPathW real mapping for CSIDL_APPDATA etc. onto a MiniWin virtual home (MW_HOME env) — replaces SWEEP-008 E_FAIL stub.
- [ ] **SW-684** SHELL32 family: ShellExecuteW honest verb routing (open on a registered handler table; cports/rammap frontier).
- [ ] **SW-685** SHELL32 family: SHBrowseForFolderW/SHGetFileInfoW — icon-list shim, honest defaults.
- [ ] **SW-686** OLE32 family: CoInitializeEx apartment model — return S_OK + real apartment tag; CoCreateInstance CLSID registry (SW-608 follow-on from procmon/rammap frontier).
- [ ] **SW-687** OLE32 family: PropVariantClear/VariantClear real tag-based free (OLEAUT ordinals already mapped).
- [ ] **SW-688** COMCTL32 family: InitCommonControlsEx flags honored; property-page/trackbar classes as no-op window classes.
- [ ] **SW-689** COMDLG32 family: GetOpenFileNameW/GetSaveFileNameW/ChooseColorW — scripted-file-dialog tester (SW-623 sister item).
- [ ] **SW-690** VERSION.DLL family: real VS_VERSIONINFO parse from .rsrc RT_VERSION (type 16) — replaces honest-0 size (rammap trio SW-561).
- [ ] **SW-691** WINSPOOL family: EnumPrintersW — zero printers, honest success (bginfo64 SnmpUtil family neighbor).
- [ ] **SW-692** NETAPI32 family: NetWkstaGetInfo/NetApiBufferFree — workstation info fixed values (Sysinternals common probe).
- [ ] **SW-693** IPHLPAPI family: GetAdaptersAddresses/GetIfTable — zero-adapter honest success (procmon/procexp network probes).
- [ ] **SW-694** WS2_32 family: WSAStartup/GetAddrInfoW on the socket layer already present — external-net policy flag (MW_NET=0 default deny, MW_NET=1 pass-through).
- [ ] **SW-695** WINTRUST/winhttp family: WinHttp* honest session objects (procexp64 425-unresolved tail — WinHttpOpen/Connect/Request with no transport) — thin object model, ERROR_INTERNET_OFFLINE default.
- [ ] **SW-696** CRYPT32 family: CertGetNameStringW/CryptQueryObject — certificate blob honest-fail path (autoruns signature column).
- [ ] **SW-697** SETUPAPI/CFGMGR32 family: SetupDiGetClassDevsW — empty device set, honest success (device-enumeration class).
- [ ] **SW-698** POWRPROF family: CallNtPowerInformation/GetPwrCapabilities — fixed honest values.
- [ ] **SW-699** DBGHELP family: SymInitialize/SymFromAddr — loader-symbol backtrace bridge (maps guest RVA -> PE section name).

## P3 — engineering infrastructure (measurable-progress mandate)

- [ ] **SW-700** Sweep runner v3: unified scripts/sweep_run.py — per-program JSON result (exit, stubs named, first unresolved, output SHA, wall-time), replaces ad-hoc TSVs; 3x mode built in.
- [ ] **SW-701** Progress dashboard: render artifacts/sweep008/mass TSV + STAGE_TABLE delta into a single PNG/PDF chart per sweep commit.
- [ ] **SW-702** Named-stub decoder as a library: extract guest_rip FF15+disp32 decode + g_unres_names lookup into scripts/sweep_stub_decode.py so every sweep report gets named stubs automatically.
- [ ] **SW-703** Crash-triage harness: on SIGSEGV, auto-run the core backtrace (scripts/corebt.py symbolization) and append BUG-candidate to the run dir.
- [ ] **SW-704** Determinism checker: run every exit-0 program 3x automatically, flag non-identical stdout/frames (extend SWEEP-005/006 SHA discipline to the whole corpus).
- [ ] **SW-705** Registry dump tool: on exit, write the RAM hive to artifacts/<exe>/registry_dump.json — makes EULA/settings flows debuggable.
- [ ] **SW-706** Handle-table audit: synthetic handle ranges (0xC1.. registry, 0xC2.. dialog, 0xC3.. control, 0xC4.. imagelist, 0xC5.. gdiobj, 0xC6.. SCM, 0xC7.. symlink) documented + collision-checked in one header comment block.
- [ ] **SW-707** Loader unit tests for the SWEEP-008 subsystems: registry create/open/query/set round-trip, atom add/find/getname, Color roundtrip RGB->HLS->RGB <=2/channel, DLGTEMPLATE parser on synthetic templates.
- [ ] **SW-708** G1 extension: add MiniWinCalc exit-0 + frame-SHA to the G1 acceptance script (games + GUI regression pins, 12/12 -> 15/15).
- [ ] **SW-709** EULA-automation doc: MW_EULA_ACCEPT contract written into docs/ (env flags table: MW_GUI_EVENTS, MW_GUI_KEYS, MW_GUI_KEY_EVERY, MW_EULA_ACCEPT, MW_REG_FILE proposal).
- [ ] **SW-710** REA evidence regeneration: run rea function on the next 3 blocker functions (procexp64 RVA 0x9be71 caller chain, cports _strlwr caller, winmergeu PathStripToRootW caller) and store trimmed YAMLs under evidence/sweep008_rea/ — the REA-first workflow the user mandated.
- [ ] **SW-711** Corpus manifest hardening: sweep008 redownload manifest with per-exe SHA256 for ALL 23 swept programs (works even when upstream URLs die — SWEEP-007 ADExplorer lesson).
- [ ] **SW-712** BUG-068 candidate: mw_unresolved_import_stub guest_rip decode must fall back safely when the caller is NOT `FF 15 disp32` (e.g. jmp thunks) — currently prints <unknown>; add jmp-thunk decode.
- [ ] **SW-713** BUG-069 candidate: GetVersionExA/W size-validation is now strict — any guest passing garbage size gets honest 122; sweep the corpus for new exit-1s caused by this strictness and add a worklist per offender.
- [ ] **SW-714** Performance: mass sweep wall-time budget — 23 programs in <4 min with -P8; document and pin (regression gate for loader slowdowns).
- [ ] **SW-715** Docs: SWEEP-008 sweep report (this wave) in docs/master/ with the per-gate frontier table: autoruns64->SW-600, rammap64->SW-600/SW-602, procexp64->SW-601, calc 100%.

## P4 — game & GUI portfolio (SW-501 discipline: 100% or open)

- [ ] **SW-716** Fifth genre: Win32 Pong (real double-buffered GDI, MW_GUI_MOUSE-scripted paddle) — 3x exit-0 + frame SHA.
- [ ] **SW-717** Sixth genre: Win32 Tetris-class falling blocks (WM_TIMER cadence, rotation via WM_KEYDOWN) — 3x exit-0 + frame SHA.
- [ ] **SW-718** Seventh genre: Win32 paint/sketch (mouse-path drawing, WM_MOUSEMOVE stream under scripted events) — 3x exit-0 + frame SHA.
- [ ] **SW-719** Eighth genre: Win32 file-viewer (menu + listbox over the RAM filesystem) — exercises menus/list controls with honest content.
- [ ] **SW-720** Calculator v2: decimal point + division + clear entry (real long/int64 math, deterministic decimal print).
- [ ] **SW-721** Multi-window test: calculator spawning an About dialog (DialogBoxParamW from a real GUI app — closes the modal-dialog loop in-product).
- [ ] **SW-722** MW_GUI_MOUSE scripted click events feeding WM_LBUTTONDOWN/WM_LBUTTONUP with real client coordinates.
- [ ] **SW-723** WM_CHAR path for the calc display (TranslateMessage already delivers WM_CHAR — verify both message paths 3x).
- [ ] **SW-724** Frame-dump format option: PNG encoder in the loader (BMP today) for smaller evidence.
- [ ] **SW-725** Game acceptance runner: one script that runs snake/breakout/calc(+future) 3x and fails on any SHA drift.

## P5 — loader hardening & correctness backlog

- [ ] **SW-726** BUG-070 candidate: DLGTEMPLATEEX DS_SETFONT offset — verify the 6-byte font block against a real Sysinternals template dump; add a unit test with a captured template.
- [ ] **SW-727** BUG-071 candidate: mw_res_walk_lvl needs a cycle guard (malicious/corrupt .rsrc could loop the directory tree) — depth cap 16.
- [ ] **SW-728** Registry: RegOpenKeyExA/RegQueryValueExA real A-side on the RAM hive (currently honest-fail; GUI apps that use A-side dialogs stay blocked).
- [ ] **SW-729** Registry: REG_SZ/REG_MULTI_SZ/REG_EXPAND_SZ typed reads with buffer-size semantics (SWEEP-008 stores raw bytes only).
- [ ] **SW-730** Registry: RegDeleteTreeW/RegDeleteKeyW + key enumeration order stability (sorted paths) — determinism mandate.
- [ ] **SW-731** Atoms: GlobalAddAtomW MAX 128 -> dynamic table; GlobalFindAtomA + GlobalGetAtomNameA A-side completeness.
- [ ] **SW-732** HLS: document the rounding deltas vs real shlwapi with a side-by-side table for 16 reference colors (evidence/sweep008_rea/color_table.md).
- [ ] **SW-733** GetSystemMetrics: virtualized-monitor mode (MW_SCREEN_W/H env) instead of fixed 1920x1080; procexp UI layout adapts.
- [ ] **SW-734** ImageList: real raster backing (pixel buffers + DrawEx blit into the GDI-lite surface) so toolbar images appear in future GUI waves.
- [ ] **SW-735** NT symlink: enumerate the real drive set (C:..Z: via MW_DRIVES env) instead of the 4-volume rotation; return the same name twice in a row (currently seq-based).
- [ ] **SW-736** SCM: services table via env (MW_SERVICES=name,type,state) so service-dependent guests get honest non-empty enumeration.
- [ ] **SW-737** Dialog: real modal loop — pump WM_TIMER/WM_PAINT inside DialogBoxParamW until EndDialog (today: INITDIALOG + one WM_COMMAND; multi-message dialogs starve).
- [ ] **SW-738** Dialog: DialogBoxIndirectParamA + CreateDialogParamA A-side resource-name handling (ANSI templates).
- [ ] **SW-739** Dialog: capture Sysinternals EULA dialog templates (autoruns/rammap) into evidence/sweep008_rea/templates/ as hex dumps + parser unit fixtures.
- [ ] **SW-740** Control handles: GetDlgCtrlID real decode; SetFocus/GetFocus per-dialog focus model.
- [ ] **SW-741** SendMessageW: route WM_GETTEXT/WM_SETTEXT to the control-handle table (real text storage) — dialog text verification without raster.
- [ ] **SW-742** RtlGetVersion: honor OSVERSIONINFOEXW (write wServicePackMajor=0 etc. — currently zeros stay from caller memory; write them explicitly).
- [ ] **SW-743** Named stubs: g_unres_names cap 1024 -> dynamic growth (winmergeu 515+ unresolved programs can exhaust it).
- [ ] **SW-744** mw_dllname_valid: also reject pointer cookies >= 0xC0000000 (synthetic handle space) — defense in depth.
- [ ] **SW-745** Trace: MW_TRACE ring-buffer mode (MW_TRACE_RING=1) to bound api_trace.json size for long GUI runs (procexp 425 imports * init calls).
- [ ] **SW-746** Repro hygiene: pin make flags + record binary SHA in every sweep evidence README (SWEEP-008 does this manually — automate).
- [ ] **SW-747** Regression: keep 12/12 + calc/snake/breakout green through every future commit (script checks all four before push).
- [ ] **SW-748** Docs: engineering-rules update — "one BUG-* per frontier, named-stub evidence required" added to the acceptance rules the worklist header cites.
- [ ] **SW-749** Push hygiene: GITHUB_TOKEN rotation reminder stays visible in worklog until the user rotates the PAT (SWEEP-008 note).
- [ ] **SW-750** Final report: the 14-section engineering report mandate — add SWEEP-008 section (registry/dialog/atoms/color/module-handle subsystems + mass sweep table + calc 100%).
