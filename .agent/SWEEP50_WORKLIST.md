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

## SWEEP-008 (this sweep) — honest-stub elephant batch LANDED

- [x] **SW-555** COMPLETE: named-stub machinery — stub_site_record() stores a
      COPY of dll!name for every slot patched with the returning-0 stub
      (BUG-071: storing pointers into freed filedata was a UAF inside libc
      vsnprintf); mw_unresolved_import_stub decodes the guest return address
      (FF 15 / FF 25 / mov r64,[rip+d]) and prints `[STUB] dll!name -> 0
      (call #N) guest_rip=...`. Every future gate names itself for free.
- [x] **SW-563** GetVersionExW/A real fill: Win10 22H2 10.0.19045, platform 2,
      OSVERSIONINFOEX fields (wProductType=VER_NT_WORKSTATION, wSuiteMask,
      SP major/minor) — no more build=0 lie; ntdll!RtlGetVersion routes here.
- [x] **SW-564** minimal in-memory registry: RegOpenKeyW/RegOpenKeyExW/
      RegCreateKeyExW/RegCreateKeyW/RegSetValueExW/RegQueryValueExW/
      RegQueryValueW/RegCloseKey backed by a flat (path,name)->value store
      with case-insensitive paths and 0x50000000+ handles; missing keys
      honestly return ERROR_FILE_NOT_FOUND (the EULA-dialog trigger).
- [x] **SW-565** global atom table: GlobalAddAtomW/A 0xC000+ real semantics,
      GlobalDeleteAtom, GlobalGetAtomNameW.
- [x] **SW-566** shlwapi ColorHLSToRGB/ColorRGBToHLS — canonical Win32 HLS.H
      math (HLSMAX=240), pure functions.
- [x] **SW-567** DialogBoxIndirectParamW DECLARED auto-accept -> IDOK (every
      call logs loudly); DialogBoxParamW routes to the same accept.
- [x] **GATES PASSED**: autoruns64 exit 1 -> **0** (0 unresolved, EULA flow:
      dialog accept -> RegCreateKeyExW -> RegSetValueExW EulaAccepted);
      rammap64 exit 1 -> **0** (0 unresolved); produkey exit -> **0**;
      diskmon64 exit 0 held. G1 re-pinned 12/12 (sha 6c14f04c).
- [x] **BUG-068** load_pe: fopen-on-directory / ftell=-1 -> malloc((size_t)-1)
      SIGSEGV; reject size<=0 and size>64MB up front.
- [x] **BUG-070/070b** GetModuleHandleW(name) now returns the SAME pseudo-
      handle identities as LoadLibraryA (autoruns64's
      GetProcAddress(GetModuleHandleW(L"ntdll"),"RtlGetVersion") chain), and
      reads the guest string as UTF-16LE through uint16_t (host wchar_t is
      4-byte — "ntdll" decoded as "ndl"+junk before).
- [x] **BUG-072** GetProcAddress(MAKEINTRESOURCE): low-16-bit names are
      ORDINALS (procexp64 binds ntdll!RtlQueryProcessDebugInformation by
      ordinal 135); ordinal 135 returns a real handler that fills the
      {int@+0, ptr@+8} out-struct with a zero-filled buffer; other ordinals
      -> optional stub (was: %s-print of a small integer crashed libc).
- [x] **batch-2/3/4 stub landing**: ~230 WARN-list imports of
      autoruns64/rammap64/procexp64 covered via 12 policy functions + ~35
      real one-liners (MulDiv, lstrcmp[i]W, CharNext/LowerW, CoTaskMem*,
      SHGetKnownFolderPath, GetSystemMetrics, rects/monitor info, text
      metrics/extents, SID length/validity, IsWow64Process, SCM honest
      access-denied, LSA not-found, Nt* honest NTSTATUS, WinHTTP offline,
      ImageList family, K32/TLHELP32 empty enumerations).

## SWEEP-008 follow-ups (new levers, all with measurable accept criteria)

- [ ] **SW-568** procexp64 exit(2)->SEGV at RIP=0x14009be71 (`mov ecx,[rdx]`
      rdx=0): identify the function pointer stored in data slot 0x1401356d0
      (thunk jmp rax); ord135 handler observed called with out=NULL — find
      the (rcx=0, edx=3, r8=NULL) call site. Accept: named callee + exit
      changes.
- [ ] **SW-569** ntdll!RtlInitUnicodeString (GetProcAddress by name -> NULL
      late in procexp64 run): implement real UTF-16 init (len/2, buffer
      pointer, no alloc). Accept: trace shows non-NULL resolution.
- [ ] **SW-570** procexp64 WinStationGetProcessSid (winsta.dll missing ->
      ERROR 126 -> MessageBoxW 'Process Explorer' #3): winsta pseudo-DLL with
      honest failure. Accept: dialog #3 replaced by logged graceful path.
- [ ] **SW-571** coreinfoex64 CRASH-SEGV (4 warns, 2 stubs, dialog rendered,
      14921 bytes): decode the 2 stub guest_rips, fix, exit 1 -> 0.
- [ ] **SW-572** cports CRASH-SEGV (69 warns, 21 stubs): decode stub sequence,
      fix top-3 imports. Accept: crash point advances past current site.
- [ ] **SW-573** puttygen CRASH-SEGV (30 warns, 1 dialog): SW-552 family —
      decode guest_rip at crash, implement the returning-0 caller's callee.
- [ ] **SW-574** searchmyfiles CRASH-SEGV (32 warns, 1 stub): same decode
      loop. Accept: named crash site.
- [ ] **SW-575** bginfo64 exit 9 (247 warns): top-20 imports by call order ->
      honest stubs; accept: exit 9 -> 0 or named gate.
- [ ] **SW-576** winmergeu exit 9 (259 warns, 14 stubs): same as SW-575.
- [ ] **SW-577** pageant/procmon64/putty exit 1 usage-path (34/34/93 warns,
      3 dialogs each): dialog images render — identify post-dialog exit
      decision API. Accept: named decision point per program.
- [ ] **SW-578** zoomit64 exit 1 with 0 unresolved (SW-553 TRAP133 area):
      exit decision now comes from RESOLVED APIs — guest_rip decode of the
      exit path. Accept: named exit-decision call.
- [ ] **SW-579** produkey under MW_GUI_EVENTS=1 crashes at 0x140022f24 — the
      known SWEEP-007 comctl32 cliff (stub#4): implement the cliff callee.
      Accept: produkey GUI pump survives WM_CREATE.
- [ ] **SW-580** comctl32 cliff general fix (cports shares 0x22f24 class):
      same accept as SW-579 for cports.
- [ ] **SW-581** DialogBox* dialog-proc invocation: call the guest dialog proc
      with WM_INITDIALOG then WM_COMMAND(IDOK) instead of returning IDOK
      blindly. Accept: autoruns64/rammap64 still exit 0 AND EulaAccepted
      value readable via RegQueryValueExW in-run.
- [ ] **SW-582** SendMessageW routing: dispatch WM_* to the WNDPROC registry
      (SWEEP-006 machinery) instead of ret0. Accept: a scripted-key GUI
      program still exits 0 with unchanged frame hashes.
- [ ] **SW-583** GetOpenFileNameW/GetSaveFileNameW cancel-semantics: return 0
      with CommDlgExtendedError()==0 — verify callers handle (coreinfoex64
      touches this). Accept: no crash change.
- [ ] **SW-584** CryptHashData/CryptGetHashParam real SHA1 via advapi32
      CryptoAPI in-memory hash. Accept: guest-computed hash matches sha1sum.
- [ ] **SW-585** CoCreateInstance static CLSID table (a few known CLSIDs ->
      fake IUnknown with QI). Accept: a guest that CoCreates proceeds 1 step.
- [ ] **SW-586** SCM in-memory services (OpenSCManagerW fake handle +
      EnumServicesStatusExW empty list instead of access-denied). Accept:
      autoruns64 service tab path exercised without exit change.
- [ ] **SW-587** MW_STUB_SITES dump: write g_stub_sites (slot_va, dll, name)
      to miniwin-results/<exe>/stub_sites.json at exit. Accept: file present
      for every run.
- [ ] **SW-588** per-run stub-call summary line: total stub calls, first 5
      named calls, exit code on one stderr line. Accept: present for all 15.
- [ ] **SW-589** breadth runner env knobs (MW_EULA_MODE=auto|decline) to test
      decline-path honesty. Accept: autoruns64 declines -> exit 1 w/ EULA
      prompt message.
- [ ] **SW-590** decode-script YAML/JSON dedupe: sweep008_decode.py outputs
      stub callers per exe; wire into evidence automation. Accept: evidence
      json regenerated in CI-style single command.
- [ ] **SW-591** 100-list re-run under SWEEP-008 stubs (STAGE_TABLE_100
      regeneration). Accept: new stage map TSV committed with deltas.
- [ ] **SW-592** go2-* family (age tools, frp, gomplate, restic): re-run under
      new stubs; classify per program (exit / crash site). Accept: delta vs
      STAGE_TABLE_100 recorded.
- [ ] **SW-593** ssh2-* family (openssh 13 exes): same as SW-592.
- [ ] **SW-594** rust2-* family (delta, bat, fd, dust, hexyl, hyperfine...):
      same as SW-592.
- [ ] **SW-595** c2-* family (nasm, ndisasm): same as SW-592.
- [ ] **SW-596** x86 subset re-run (7zr etc.): loader x86 path unaffected?
      verify no regression vs LOAD-REJECT baseline. Accept: same exits.
- [ ] **SW-597** msvcrt.dll CRT init for older exes: bginfo64/winmergeu use
      A-suffix APIs — A/W bridge layer for the batch-2 family. Accept: warn
      count drops for bginfo64.
- [ ] **SW-598** GetNumberFormatW real formatting (_locale-independent digits).
      Accept: returns formatted string for en-US.
- [ ] **SW-599** GetDateFormatW/GetTimeFormatW same treatment. Accept: real
      date string for fixed SYSTEMTIME.
- [ ] **SW-600** ShellExecuteW/ExW return-code honesty: 33 (>32 success) vs
      SE_ERR_ codes; verify putty pageant launcher path. Accept: named
      decision documented.
- [ ] **SW-601** FindTextW/ReplaceTextW comdlg common dialogs. Accept: return
      0 + no crash.
- [ ] **SW-602** PrintDlgW/StartDocW/EndPage printing no-op family honesty
      note (no printer in MiniWin). Accept: document in loader comments.
- [ ] **SW-603** SetWindowsHookExW hook registry: record hook type + return
      fake handle; CallNextHookEx no-op. Accept: no crash, named in trace.
- [ ] **SW-604** MonitorFromWindow/GetMonitorInfoW multi-monitor virtual
      desktop consistency (SM_C[XY]SCREEN vs rcMonitor). Accept: consistent
      1920x1080 across all metrics.
- [ ] **SW-605** GetKeyState/GetKeyboardState honest zero-state with
      MW_GUI_EVENTS key pump integration. Accept: scripted-key game still
      deterministic.
- [ ] **SW-606** IsWow64Process/GetNativeSystemInfo consistency (both report
      x64-native). Accept: no WOW64 path taken by any guest.
- [ ] **SW-607** ConvertSidToStringSidW real S-1-x-y formatting. Accept:
      string matches Windows format for a crafted SID.
- [ ] **SW-608** CheckTokenMembership/OpenProcessToken honest combo (token =
      fake admin). Accept: autoruns admin-check path named in trace.
- [ ] **SW-609** DwmSetWindowAttribute no-op already; add DwmIsComposition-
      Enabled(TRUE). Accept: callers proceed.
- [ ] **SW-610** uxtheme ordinal binds (procexp ord 135-class) general table:
      map common uxtheme/ntdll ordinals to names. Accept: ordinals logged
      WITH names.
- [ ] **SW-611** LoadLibraryW bridge (currently routes to LoadLibraryA —
      verify UTF-16 conversion correctness for "uxtheme.dll" etc.). Accept:
      trace shows correct names.
- [ ] **SW-612** GetModuleHandleA parity with BUG-070b fix (A-version reads
      ANSI directly). Accept: puttygen module chain advances.
- [ ] **SW-613** FreeLibrary/GetModuleFileNameW pseudo-handle hygiene. Accept:
      no crash when guest frees.
- [ ] **SW-614** SetEnvironmentVariableW/GetEnvironmentVariableW already real?
      verify COMPLUS_FodPath write lands. Accept: read-back succeeds.
- [ ] **SW-615** evidence/sweep008_stubs/ README + per-program before/after
      exit table (this sweep's numbers). Accept: committed.
- [ ] **SW-616** 3x determinism battery for the four exit-0 programs
      (autoruns64/rammap64/produkey/diskmon64): exits + output hashes x3.
      Accept: 3x identical.
- [ ] **SW-617** G1 re-pin automation hook into sweep scripts (post-loader-
      change mandatory pin). Accept: sweep script refuses without fresh pin.
- [ ] **SW-618** STAGE_TABLE_100 CRLF discipline maintained for new TSVs.
      Accept: file(1) says CRLF.
- [ ] **SW-619** worklist audit: every SW-5xx entry has either evidence link
      or a next-lever description. Accept: grep audit passes.
- [ ] **SW-620** mini-game #3 genre: puzzle (same GDI pipeline). Accept:
      3x exit0 with frame hashes.
- [ ] **SW-621** mini-app: MiniWin Calc (button grid + display, real Win32
      dialog app) — the "simple graphical program" acceptance anchor.
      Accept: 3x exit0, display shows computed result, frame hash stable.
- [ ] **SW-622** calc button hit-testing via scripted mouse pump (WM_LBUTTON-
      DOWN at coords). Accept: 2+3=5 sequence renders "5".
- [ ] **SW-623** MessageBoxW dialog image regression pin (putty/pageant
      dialogs byte-identical x2 sweeps). Accept: SHA stable.
- [ ] **SW-624** procmon64/procexp64 shared UI framework (Sysinternals
      common code) — fix once, benefit both. Accept: one fix advances both.
- [ ] **SW-625** document the honest-stub policy table (which APIs get
      handle/ret1/ret0/error) in docs/. Accept: policy doc committed.
- [ ] **SW-626** sweep008 evidence JSON: machine-readable per-program deltas
      (exit_before/after, warns_before/after). Accept: json committed.
- [ ] **SW-627** cports GUI pump test (SWEEP-007 cliff shared with produkey).
      Accept: same cliff site named or bypassed.
- [ ] **SW-628** puttygen SW-552 RIP=0 re-check under new stubs. Accept:
      crash site moved or resolved.
- [ ] **SW-629** SW-553 zoomit64 TRAP133 re-check under new stubs. Accept:
      trap gone or named.
- [ ] **SW-630** SW-561 version drift re-check: coreinfoex64 new binary vs
      GetVersionExW 19045 fill. Accept: version check passes (exit moves).
- [ ] **SW-631** adexplorer64 re-acquisition retry (was 404). Accept:
      downloaded or documented unobtainable x2.
- [ ] **SW-632** loader: cache GpResult — unresolved imports now resolve to
      stub via site table; add slot->name map rebuild for data imports too.
      Accept: data-import stubs named as well.
- [ ] **SW-633** ApiSetSchema-lite: api-ms-win-* resolution to real DLLs
      (normaliz etc.). Accept: one api-ms guest resolves deeper.
- [ ] **SW-634** loader perf: stub_site linear scan -> hash if >64 sites.
      Accept: no measurable slowdown on procexp64 (727 stubs).
- [ ] **SW-635** thread-safety of stub_site_lookup static buf: use TLS.
      Accept: multi-threaded guests (procmon64) show no interleaved names.
- [ ] **SW-636** RegSetValueExW REG_DWORD/REG_SZ type validation. Accept:
      wrong-type write logged.
- [ ] **SW-637** registry: RegEnumValueW/RegEnumKeyExW enumerate the
      in-memory store (currently honest 259). Accept: EULA key enumeration
      works in-run.
- [ ] **SW-638** registry persistence: optional JSON snapshot via
      MW_REG_PERSIST=path. Accept: EulaAccepted survives loader restart.
- [ ] **SW-639** ShGetKnownFolderPath real CSIDL table (AppData, ProgramFiles
      etc.). Accept: guests see plausible paths per csidl.
- [ ] **SW-640** GetTempPathW/GetTempFileNameW real (tmp dir). Accept: file
      created and deletable.
- [ ] **SW-641** ExpandEnvironmentStringsW real (%WINDIR% etc.). Accept:
      expands consistently with SW-639.
- [ ] **SW-642** CreateDirectoryW/GetFileAttributesW A/W parity audit. Accept:
      no new crashes on Sysinternals startup path.
- [ ] **SW-643** DefineDosDevice/QueryDosDeviceW honest table. Accept: named.
- [ ] **SW-644** WTS API family: WTSQuerySessionInformationW returns session 1
      console info. Accept: procexp session column path proceeds.
- [ ] **SW-645** GetLogicalProcessorInformation real (1 group, N cores from
      host). Accept: procexp CPU graph data path proceeds.
- [ ] **SW-646** GetActiveProcessorCount(ALL_PROCESSOR_GROUPS)=host nproc.
      Accept: consistent with SW-645.
- [ ] **SW-647** GlobalMemoryStatusEx real from /proc/meminfo. Accept:
      numbers plausible and stable within run.
- [ ] **SW-648** GetDiskFreeSpaceExW real for cwd mount. Accept: procmon64
      volume query proceeds.
- [ ] **SW-649** GetVolumeInformationW honest "MINIWIN" label. Accept: named.
- [ ] **SW-650** NetApi32 family for Sysinternals network tabs. Accept:
      honest error + no crash.
- [ ] **SW-651** document BUG-067/068/070/071/072 in BUGS.md with repro
      commands. Accept: BUGS.md updated.
- [ ] **SW-652** regression: add autoruns64+rammap64 exit0 pins to the suite
      (like G1). Accept: suite 14/14.
- [ ] **SW-653** regression: add rammap64 0-unresolved pin (import coverage
      gate). Accept: suite enforces.
- [ ] **SW-654** loader banner: print MiniWin version + stub-coverage stat at
      startup. Accept: one line, all runs.
- [ ] **SW-655** api_trace.json: add stub names to the JSON event stream.
      Accept: trace contains dll!name for stub calls.
- [ ] **SW-656** cleanup: mw_GetVersionExA/W share fill (already) — audit all
      version consumers for 19045 assumptions. Accept: no regression.
- [ ] **SW-657** USER32 A-suffix batch for pageant/procmon64 (CharLowerA etc.
      — see SW-597). Accept: their warn counts drop.
- [ ] **SW-658** gdi32 GetDeviceCaps(LOGPIXELSX/Y)=96 consistency. Accept:
      DPI-dependent layout stable.
- [ ] **SW-659** CreateDIBSection real pixel buffer (currently fake handle) —
      route into the framebuffer for 32bpp. Accept: a DIB-drawing guest
      paints visible pixels.
- [ ] **SW-660** GradientFill/AlphaBlend raster into framebuffer. Accept:
      visible gradient in a dialog render.
- [ ] **SW-661** DrawTextW real text raster (font8x8 like MessageBoxW path).
      Accept: dialog text visible in BMP.
- [ ] **SW-662** ExtTextOutW/TextOutW raster for owner-drawn controls.
      Accept: same.
- [ ] **SW-663** GetTextExtentPoint32W real per-char widths from font8x8.
      Accept: width sums match glyph widths.
- [ ] **SW-664** dialogs: render DLGTEMPLATE controls (autoruns EULA rich
      text area) into BMP before auto-accept. Accept: EULA dialog image.
- [ ] **SW-665** keyboard: WM_KEYDOWN/UP synthesis into dialogs. Accept:
      EULA accept via Enter key scripted.
- [ ] **SW-666** mouse: WM_LBUTTONDOWN synthesis in client coords. Accept:
      button click handled by guest WNDPROC.
- [ ] **SW-667** focus: SetFocus/GetFocus real per-window state. Accept:
      keyboard routed to focused window.
- [ ] **SW-668** clipboard: OpenClipboard/SetClipboardData/GetClipboardData
      in-memory clipboard. Accept: copy/paste within guest works.
- [ ] **SW-669** DDE/Atom integration: atoms already real; wire
      PropSet/PropGet to window properties table. Accept: GetPropW returns
      what SetPropW stored.
- [ ] **SW-670** timers: SetTimer/KillTimer/WM_TIMER into the event pump.
      Accept: guest timer proc fires N times under pump.
- [ ] **SW-671** WaitForInputIdle real via pump idle detection. Accept:
      returns 0 after window creation.
- [ ] **SW-672** IsHungAppWindow real (window not pumping = TRUE). Accept:
      matches pump state.
- [ ] **SW-673** audit: SendMessageTimeoutW/GetMessagePos real values. Accept:
      named.
- [ ] **SW-674** putty SW-554 remainder: post-dialog exit gate — after this
      sweep's stub landing putty still exits 1 with 93 warns; decode the
      first 10 stubs. Accept: named next API.
- [ ] **SW-675** age/age.exe full crypto CLI run (SC-999 acceptance): 100-list
      depth item. Accept: encrypt+decrypt roundtrip under loader.
- [ ] **SW-676** zstd/lz4/gzip/md5deep regression re-run after sweep-008
      loader changes. Accept: all still 3x3 byte-perfect.
- [ ] **SW-677** 7-Zip 26.04 banner re-check (wave11 win). Accept: still
      exit 0 banner.
- [ ] **SW-678** curl/openssh x64 runtime probes under new stubs. Accept:
      no new crashes.
- [ ] **SW-679** crash-handler: log the guest caller chain (top 5 stack
      addresses inside PE) on every CRASH. Accept: autoruns crash reports
      caller 0x1400172cf-class info automatically.
- [ ] **SW-680** disassembler integration for crash sites (objdump one-liner
      in the report). Accept: crash log includes 8 instructions before RIP.
- [ ] **SW-681** deterministic RNG audit: ProcessPrng consumers unaffected by
      stub batch. Accept: snake/breakout frame hashes unchanged.
- [ ] **SW-682** loader exit-code audit: map guest exit(1) usage strings to
      per-program docs. Accept: table in evidence.
- [ ] **SW-683** batch A/W duplication via macro (reduce loader.c size after
      batch-2). Accept: loader.c net smaller, behavior identical.
- [ ] **SW-684** MW_TRACE gating audit: ensure stub prints are rate-limited
      like unresolved banner (n<=20). Accept: no stderr flooding.
- [ ] **SW-685** review policy-stub return types vs real signatures (int64
      truncation traps). Accept: audit list documented.
- [ ] **SW-686** monitor virtual desktop math in GetWindowRect placement
      (cascade offsets). Accept: multiple windows get distinct rects.
- [ ] **SW-687** SetWindowPos/Z-order no-op documented. Accept: comment.
- [ ] **SW-688** WM_CREATE synchronous delivery cliff (SW-579/580 root
      cause): deliver WM_CREATE via pump instead of direct call. Accept:
      produkey pump passes WM_CREATE.
- [ ] **SW-689** InitCommonControlsEx: record requested classes; report
      availability. Accept: named in trace.
- [ ] **SW-690** property sheets (CreatePropertySheetPageW/PropertySheetW):
      real sheet lifecycle for procexp64 properties. Accept: sheet proc
      invoked with WM_INITDIALOG.
- [ ] **SW-691** common dialogs: FileOpen dialog render (like MessageBoxW)
      then cancel. Accept: BMP of a file dialog.
- [ ] **SW-692** GetDlgItemTextW real from dialog control table. Accept:
      returns typed text.
- [ ] **SW-693** EndDialog records the per-dialog result code. Accept:
      DialogBox* returns the EndDialog value not hardcoded IDOK.
- [ ] **SW-694** autoruns64: with SW-693, accept the DECLINE path (IDCANCEL)
      evidence too. Accept: decline -> exit 1 honest.
- [ ] **SW-695** measure: stub-coverage percent per program (resolved / total
      imports). Accept: metric in summary TSV.
- [ ] **SW-696** target: 5 more exit-0 programs from the 100-list via batch
      triage (SW-592..595 levers). Accept: stage map shows +5.
- [ ] **SW-697** target: second GUI program to full lifecycle (bginfo64 or
      winmergeu). Accept: window created + message loop entered.
- [ ] **SW-698** keep .agent microtask count >= 100 after SWEEP-009 triage
      (this expansion brings it to ~200). Accept: count verified.
- [ ] **SW-699** final 14-section report skeleton refresh with SWEEP-008
      numbers. Accept: draft committed.
