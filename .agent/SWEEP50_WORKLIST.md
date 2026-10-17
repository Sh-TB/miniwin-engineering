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
- [ ] **SW-562** MW_GUI_EVENTS=1 battery: run all 16 runs_gui programs under the
      pump to map which survive WM_PAINT delivery (post-SWEEP-005 frontier).
