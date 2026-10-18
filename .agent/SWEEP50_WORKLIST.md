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
      UPDATE (SWEEP-008, REA+Ghidra): faulting instruction DECODED =
      `CMP dword ptr [R11+0xb8], R13D` — a DATA read of an invalid structure
      pointer (R11) during process-enum, not a control-flow transfer. Next:
      find what fills R11 (xrefs to the struct init) and why it is invalid
      under MiniWin. Evidence: evidence/sweep008_rea/cports_crash_*.json.
- [ ] **SW-552** isolate puttygen RIP=0x0 (call through NULL — bound-import/ordinal
      slot left NULL despite SWEEP-001?) — one BUG-*.
      UPDATE (SWEEP-008, REA+Ghidra): hypothesis REWRITTEN — the NULL is NOT an
      IAT/bound-import slot. Chain: WinMain FUN_1400028b0 `call 0x14000be30`
      (0x1400028dd; return addr 0x1400028e2 = stack top at fault) -> FUN_14000be30
      `call *%rdi` at 0x14000be56 where RDI = [.data 0x14008a8e8] — a
      FUNCTION-POINTER VARIABLE, never initialized under MiniWin (pefile: not in
      IAT). Writer candidates: refs to 0x14008a8e8 at 0x14002c787..0x14002cc57
      (FUN_14002c730 region). Next: decompile the writers, find the init stub
      that no-ops instead of storing the pointer. Evidence:
      evidence/sweep008_rea/puttygen_*.json.
- [ ] **SW-553** isolate adexplorer64/zoomit64 SIGTRAP (133) after UnhandledExceptionFilter —
      DebugBreak/int3 in fail-fast path; decide honest mapping (one BUG-* or WONTFIX note).
      UPDATE (SWEEP-007): adexplorer64 is UNOBTAINABLE — Microsoft retired
      ADExplorer.zip from download.sysinternals.com (404). zoomit64 TRAP133 re-confirmed
      3x deterministic; SW-553 is now zoomit64-only.
- [ ] **SW-554** putty-gui + pageant exit(1) gate: find the stub-call that returns the
      fatal FALSE (trace shows CRT-exit sequence after mscoree probe) — needs named
      stub-call logging (per-import thunk identity) as tooling prerequisite.
      UPDATE (SWEEP-008, REA+Ghidra): pageant fatal path MAPPED — string trace
      "security APIs" -> FUN_1400017e0, gate = main-init else-branch ->
      MessageBoxA("Unable to access security APIs", "Pageant Fatal Error") ->
      return 1 (the exit(1)); teardown probes (api-ms-win-appmodel /
      mscoree CorExitProcess) confirmed post-decision. Candidate guard
      FUN_1400132a0 = trivial `return 1`. Remaining hunt: the early-init
      security chain inside FUN_1400017e0 before the message loop. Evidence:
      evidence/sweep008_rea/pageant_*.json.
- [ ] **SW-555** procexp64 exit(2) + autoruns64/rammap64 exit(1) gates: same named-stub
      prerequisite, then fix top blocker each.
      UPDATE (SWEEP-008b, REA+Ghidra): prerequisite DELIVERED — loader stub now
      logs the guest call site (`guest_rip=`), static decode (FF 15 + IAT map)
      names every stub call, 0 unmapped across the 3 programs (2 runs
      identical). GATES IDENTIFIED: autoruns64 exit(1) AND rammap64 exit(1) =
      the Sysinternals EULA gate — last stub call is user32!
      DialogBoxIndirectParamW (autoruns 0x140062d1c, rammap 0x14000438f)
      inside the in-memory-DLGTEMPLATE EULA check ("&Decline"/"&Print"/
      RICHEDIT; on success RegSetValueExW("EulaAccepted")); stub 0 -> "not
      accepted" -> return 0 -> exit(1). procexp64 exit(2): GlobalAddAtomW +
      GetVersionExW, then shlwapi!ColorRGBToHLS/ColorHLSToRGB alternating 9x
      inside FUN_14007baf0 (22%-luminance shading helper, 36 callers — stubs
      black out the palette); the exit(2) itself comes from a RESOLVED API
      (no stub after call #20) — open. Cheap honest-stub fixes queued:
      GetVersionExW (real OSVERSIONINFOW — lied 0 eleven times in rammap),
      RegOpenKeyW (ERROR_FILE_NOT_FOUND instead of lying SUCCESS+NULL handle),
      GlobalAddAtomW, ColorRGBToHLS/ColorHLSToRGB (pure math). EULA-dialog fix
      needs DialogBoxIndirectParamW/DLGTEMPLATE — SW-509/SW-559 family.
      Evidence: evidence/sweep008_rea/ (3 gate-function YAMLs,
      sweep008b_stub_callers.json), scripts/sweep008b_*.py.
      UPDATE (SWEEP-009, honest-stub batch LANDED): 9 imports implemented —
      GetVersionExW (real OSVERSIONINFOW/EXW fill), RegOpenKeyW/RegCreateKeyW
      (wrappers, honest-failure contract), GlobalAddAtomW/FindAtomW/
      DeleteAtom/GetAtomNameW (REAL refcounted atom table 0xC000..0xFFFF),
      ColorRGBToHLS/ColorHLSToRGB (REAL 0..240 HLS; exhaustive 2^24 roundtrip
      test: L error EXACTLY 0, channel dev <=4 inherent to Win32 scale; caught
      + fixed a hue-truncation bug pre-commit). Post-batch 3x runs all
      deterministic: autoruns64 6->2 stub calls, rammap64 16->1, procexp64
      20->9 with ALL-NEW sites (GetSystemMetrics x4, ImageList_Create/Destroy
      x4, SetDefaultDllDirectories — progressed past color init into
      UI-resource init; exit(2) decision downstream, SW-509/556 family).
      NEW INSIGHT: with RegOpenKeyW honest, autoruns64 exits via the
      open-fail branch 0x140062976 WITHOUT showing the EULA dialog — the
      EULA fix needs DialogBoxIndirectParamW AND a reachable key-open (minimal
      in-memory hive for HKCU\Software\Sysinternals\<tool>); EulaAccepted
      auto-seed shim considered and NOT taken (policy). G1 re-pinned
      sha c06c5eb8 12/12. Evidence: evidence/sweep009_honest_stubs/README.md,
      artifacts/sweep50/runs_stubrip9/, scripts/sweep009_color_roundtrip.c.
- [ ] **SW-556** SW-509 (PE-DLL loader) now blocks 4 GUI heavyweights
      (bginfo64/winmergeu C++ runtime, searchmyfiles mfcu, procexp64) — raise priority.
      UPDATE (SWEEP-010, UI-init batch LANDED): procexp64's named UI-init
      stubs implemented — user32!GetSystemMetrics (REAL Win10 1920x1080 table;
      procexp asks EXACTLY SM_CYSMCAPTION=17 / SM_CYSMICON=16, decoded from
      the 4 call sites), comctl32!ImageList_Create/Destroy (REAL handle
      lists) + Add/GetImageCount (strip-add, real pixel copy), kernel32!
      SetDefaultDllDirectories (documented policy no-op -> TRUE). Self-test
      links the REAL loader TU: 29/29 PASS (ms_abi declaration lesson
      recorded). Result: procexp64 9 -> 1 stub calls AND exit(2) -> exit(1) —
      the SWEEP-008b "decision from a resolved API" frontier is broken
      through; cleanup ImageList_Destroy now real. ALL THREE gate programs
      (procexp64/autoruns64/rammap64) now converge on ONE named stub import:
      DialogBoxIndirectParamW (EULA gate needs the in-memory mini-hive too,
      per SWEEP-009 policy note). G1 re-pinned sha df28de0f 12/12. Evidence:
      evidence/sweep010_ui_init/ (README + 2 JSONs + trimmed traces),
      artifacts/sweep50/runs_stubrip10/, scripts/sweep010_*.py/.c/.sh.

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
- [x] **SW-011** REA-powered speed sweep — DONE (SWEEP-011): user directive
      «از rea نهایت استفاده برای کمتر شدن زمان اجرا برنامه‌ها، سرعت دیباگر
      بهتر بشه». Landed in loader.c (all env-gated, defaults unchanged):
      (1) MINWIN_FUNC_MAP=<json>[,<json>] O(log n) function-map symbolizer —
      auto-names [STUB] caller=, [CRASH] [FUNC], [SCAN] stack values,
      backtrace frames, profiler output; maps built ONCE per PE via
      persistent Ghidra headless project (scripts/sweep011_funcmap_export.py,
      Java postScript — Ghidra 12 .py needs PyGhidra) + nm engine map
      (scripts/sweep011_engine_funcmap.sh, 1070 fns); 10 guest maps in
      artifacts/sweep011_perf/funcmaps/ (procexp64 2938 fns, ZoomIt64 4958).
      (2) MINWIN_PROFILE=1 SIGPROF 1 ms sampler -> profile.json top-16
      symbolized (breakout verdict: mwb_fill/mw_BitBlt pixel loops, not guest
      code). (3) api_trace.json 64 KiB buffered (was fflush PER line;
      breakout 21608 lines/run), MINWIN_TRACE=0 opt-out,
      MINWIN_SCREENSHOT=0 opt-out (381 BMPs/run). (4) SIGTRAP -> full crash
      report, exit stays 133 (SW-553 zoomit64 int3 now diagnosed:
      RIP=0x140054c56); backtrace walker follows return chain, stack-bounded.
      Speeds (median-of-3, artifacts/sweep011_perf/COMPARISON.md): breakout
      1228->896 ms fast-mode (-27%), snake 102->86 (-15%), 7za 4.7->2.7
      (-43%), sqlite3 7.0->3.9 (-44%), t08 3.4->1.7 (-50%); parity battery
      13/13 exit-identical. rea --snapshot: cold 2m29s -> warm 2.0s (74x).
      G1 re-pinned sha 5e81f847 12/12 (evidence/master/g1/20261018_153005).
      SW-552 delta: puttygen crash stack machine-named (FUN_1400028b0+0x32
      WinMain / FUN_140041140+0x106 / entry+0x9). NOTE: toolchain restore
      after home reset — rea 6.7.0 + Ghidra 12.1.4 + JDK 21.0.12.1 back in
      place; engine funcmap must be regenerated after every engine rebuild.

---
## SWEEP-013 (2026-10-19) — DONE block
- [x] **SW-013** "5x multiplication" sweep — DONE: user directives
      «حداقل یک برنامه گرافیکی ساده ماشین حساب» + «تسک رو حداقل ۵ برابر بکن».
      (1) GUI calculator tests/gui/calc.c delivered: real Win32 EDIT+16 BUTTON
      children, WM_COMMAND via MINWIN_CLICKS, 3x deterministic exit 0,
      stdout "[CALC] 12+45 = 57", guest-drawn screenshot
      evidence/sweep013/calc_screenshot.bmp. (2) Honest batch #3: ~65 APIs
      real-ified (mini window manager + dialog family + NT-query family +
      SID/token + registry A-variants + monitors/fonts + IsBad*Ptr) — see
      evidence/sweep013/README.md. (3) MEASURED PROGRESS (51+2 pool):
      mean_score 2.71->2.92, RAN-OUTPUT 16->22, STUB-GATE 10->5, zero-stub
      clean runs for Autoruns64/ZoomIt64/Diskmon64/7z2604/jq64/sqlite3/gui-calc.
      (4) Harness fix: stdin </dev/null (stdin-blockers were misclassified).
      (5) G1 re-pinned 700d83ee 12/12.

---

## SWEEP-014 QUEUE — 3x continuation multiplication (SW-651..SW-800, 150 items)

**Source:** artifacts/sweep012/runs-AFTER3 (mean_score 3.08, RAN-OUTPUT 26,
STUB-GATE 0 — batch #4 closed every named stub gate; the frontier is now
0-stub guest crashes + deep API families). Rules unchanged: 3x acceptance,
irreversible status labels, no fake success, PROGRESS over activity.

- [ ] **SW-651** Bginfo64.exe exit 139 0-stub guest crash: MINWIN_FUNC_MAP symbolize RIP (SWEEP-011 method), one BUG-*, re-run 3x (evidence artifacts/sweep012/runs-AFTER3/Bginfo64.exe/)
- [ ] **SW-652** Coreinfo64.exe exit 139 0-stub guest crash: MINWIN_FUNC_MAP symbolize RIP (SWEEP-011 method), one BUG-*, re-run 3x (evidence artifacts/sweep012/runs-AFTER3/Coreinfo64.exe/)
- [ ] **SW-653** SearchMyFiles.exe exit 139 0-stub guest crash: MINWIN_FUNC_MAP symbolize RIP (SWEEP-011 method), one BUG-*, re-run 3x (evidence artifacts/sweep012/runs-AFTER3/SearchMyFiles.exe/)
- [ ] **SW-654** WinMergeU.exe exit 139 0-stub guest crash: MINWIN_FUNC_MAP symbolize RIP (SWEEP-011 method), one BUG-*, re-run 3x (evidence artifacts/sweep012/runs-AFTER3/WinMergeU.exe/)
- [ ] **SW-655** arm64_7za.exe exit 139 0-stub guest crash: MINWIN_FUNC_MAP symbolize RIP (SWEEP-011 method), one BUG-*, re-run 3x (evidence artifacts/sweep012/runs-AFTER3/arm64_7za.exe/)
- [ ] **SW-656** curl.exe exit 139 0-stub guest crash: MINWIN_FUNC_MAP symbolize RIP (SWEEP-011 method), one BUG-*, re-run 3x (evidence artifacts/sweep012/runs-AFTER3/curl.exe/)
- [ ] **SW-657** hashdeep64.exe exit 139 0-stub guest crash: MINWIN_FUNC_MAP symbolize RIP (SWEEP-011 method), one BUG-*, re-run 3x (evidence artifacts/sweep012/runs-AFTER3/hashdeep64.exe/)
- [ ] **SW-658** md5deep64.exe exit 139 0-stub guest crash: MINWIN_FUNC_MAP symbolize RIP (SWEEP-011 method), one BUG-*, re-run 3x (evidence artifacts/sweep012/runs-AFTER3/md5deep64.exe/)
- [ ] **SW-659** procexp64.exe exit 139 0-stub guest crash: MINWIN_FUNC_MAP symbolize RIP (SWEEP-011 method), one BUG-*, re-run 3x (evidence artifacts/sweep012/runs-AFTER3/procexp64.exe/)
- [ ] **SW-660** puttygen.exe exit 139 0-stub guest crash: MINWIN_FUNC_MAP symbolize RIP (SWEEP-011 method), one BUG-*, re-run 3x (evidence artifacts/sweep012/runs-AFTER3/puttygen.exe/)
- [ ] **SW-661** sha1deep64.exe exit 139 0-stub guest crash: MINWIN_FUNC_MAP symbolize RIP (SWEEP-011 method), one BUG-*, re-run 3x (evidence artifacts/sweep012/runs-AFTER3/sha1deep64.exe/)
- [ ] **SW-662** sha256deep64.exe exit 139 0-stub guest crash: MINWIN_FUNC_MAP symbolize RIP (SWEEP-011 method), one BUG-*, re-run 3x (evidence artifacts/sweep012/runs-AFTER3/sha256deep64.exe/)
- [ ] **SW-663** sqlite3_analyzer.exe exit 139 0-stub guest crash: MINWIN_FUNC_MAP symbolize RIP (SWEEP-011 method), one BUG-*, re-run 3x (evidence artifacts/sweep012/runs-AFTER3/sqlite3_analyzer.exe/)
- [ ] **SW-664** tigerdeep64.exe exit 139 0-stub guest crash: MINWIN_FUNC_MAP symbolize RIP (SWEEP-011 method), one BUG-*, re-run 3x (evidence artifacts/sweep012/runs-AFTER3/tigerdeep64.exe/)
- [ ] **SW-665** whirlpooldeep64.exe exit 139 0-stub guest crash: MINWIN_FUNC_MAP symbolize RIP (SWEEP-011 method), one BUG-*, re-run 3x (evidence artifacts/sweep012/runs-AFTER3/whirlpooldeep64.exe/)
- [ ] **SW-666** ws2_32 ladder: socket->fd real alloc (cports/ProduKey/sqlite3_analyzer now resolve by ordinal — SWEEP-014 BUG-071; first CALLER gate applies)
- [ ] **SW-667** ws2_32 ladder: bind/listen real (cports/ProduKey/sqlite3_analyzer now resolve by ordinal — SWEEP-014 BUG-071; first CALLER gate applies)
- [ ] **SW-668** ws2_32 ladder: accept real (cports/ProduKey/sqlite3_analyzer now resolve by ordinal — SWEEP-014 BUG-071; first CALLER gate applies)
- [ ] **SW-669** ws2_32 ladder: connect non-blocking (cports/ProduKey/sqlite3_analyzer now resolve by ordinal — SWEEP-014 BUG-071; first CALLER gate applies)
- [ ] **SW-670** ws2_32 ladder: send/recv real fd (cports/ProduKey/sqlite3_analyzer now resolve by ordinal — SWEEP-014 BUG-071; first CALLER gate applies)
- [ ] **SW-671** ws2_32 ladder: select real fd_set (cports/ProduKey/sqlite3_analyzer now resolve by ordinal — SWEEP-014 BUG-071; first CALLER gate applies)
- [ ] **SW-672** ws2_32 ladder: getsockopt/setsockopt honest levels (cports/ProduKey/sqlite3_analyzer now resolve by ordinal — SWEEP-014 BUG-071; first CALLER gate applies)
- [ ] **SW-673** ws2_32 ladder: shutdown + closesocket fd free (cports/ProduKey/sqlite3_analyzer now resolve by ordinal — SWEEP-014 BUG-071; first CALLER gate applies)
- [ ] **SW-674** ws2_32 ladder: getsockname/getpeername real sockaddr (cports/ProduKey/sqlite3_analyzer now resolve by ordinal — SWEEP-014 BUG-071; first CALLER gate applies)
- [ ] **SW-675** ws2_32 ladder: inet_addr/inet_ntoa real (cports/ProduKey/sqlite3_analyzer now resolve by ordinal — SWEEP-014 BUG-071; first CALLER gate applies)
- [ ] **SW-676** ws2_32 ladder: ntohl/htonl no-op identity (cports/ProduKey/sqlite3_analyzer now resolve by ordinal — SWEEP-014 BUG-071; first CALLER gate applies)
- [ ] **SW-677** ws2_32 ladder: ioctlsocket FIONBIO real (cports/ProduKey/sqlite3_analyzer now resolve by ordinal — SWEEP-014 BUG-071; first CALLER gate applies)
- [ ] **SW-678** ws2_32 ladder: WSAPoll subset (cports/ProduKey/sqlite3_analyzer now resolve by ordinal — SWEEP-014 BUG-071; first CALLER gate applies)
- [ ] **SW-679** ws2_32 ladder: WSAAsyncSelect event queue real (cports/ProduKey/sqlite3_analyzer now resolve by ordinal — SWEEP-014 BUG-071; first CALLER gate applies)
- [ ] **SW-680** ws2_32 ladder: getaddrinfo host resolver (cports/ProduKey/sqlite3_analyzer now resolve by ordinal — SWEEP-014 BUG-071; first CALLER gate applies)
- [ ] **SW-681** GUI frontier: WM_CHAR path: pump delivers typed digit chars to EDIT children (SW-616)
- [ ] **SW-682** GUI frontier: child-control PAINT: draw BUTTON/EDIT frames + labels into the raster (SW-615)
- [ ] **SW-683** GUI frontier: WM_CHAR +/- operators: full keyboard calculator scenario E 1+2+3=
- [ ] **SW-684** GUI frontier: second GUI guest: Notepad clone (multiline EDIT + WM_CHAR typing)
- [ ] **SW-685** GUI frontier: third GUI guest: analog/digital clock (SetTimer + WM_TIMER + paint)
- [ ] **SW-686** GUI frontier: comdlg32 GetOpenFileNameW/GetSaveFileNameW modal-lite + file pick
- [ ] **SW-687** GUI frontier: menus: SetMenu/DrawMenuBar/TrackPopupMenu + WM_COMMAND ids
- [ ] **SW-688** GUI frontier: scrollbars: SetScrollInfo/ShowScrollBar + WM_VSCROLL delivery
- [ ] **SW-689** GUI frontier: clipboard: OpenClipboard/SetClipboardData/GetClipboardData (text)
- [ ] **SW-690** GUI frontier: ListView first slice: LVM_INSERTCOLUMN/ITEM + LVM_GETITEMCOUNT
- [ ] **SW-691** GUI frontier: TreeView first slice: TVM_INSERTITEM + TVM_GETCOUNT
- [ ] **SW-692** GUI frontier: TrackPopupMenu real WM_COMMAND return
- [ ] **SW-693** GUI frontier: SetCursor/ShowCursor + WM_SETCURSOR routing
- [ ] **SW-694** GUI frontier: GetUpdateRect/GetRgnBox honest rect fill
- [ ] **SW-695** GUI frontier: RedrawWindow superset of InvalidateRect (RDW flags decode)
- [ ] **SW-696** GUI frontier: dialog units MapDialogRect real font metric
- [ ] **SW-697** GUI frontier: IsDialogMessage/DlgDirListlite for dialog keyboards
- [ ] **SW-698** GUI frontier: WM_GETDLGCODE + IsDialogMessage focus walk
- [ ] **SW-699** GUI frontier: Calc scenario F: chained 12+45-7= via clicks
- [ ] **SW-700** GUI frontier: Calc paint assertion: golden-BMP hash pin of frame_006
- [ ] **SW-701** oleaut32 family: audit full OLEAUT32 ordinal map vs pefile db (BUG-072: 8 was wrong) (Bginfo64 imports 19 ordinals; SysAllocStringByteLen=150 fixed this sweep)
- [ ] **SW-702** oleaut32 family: VariantChangeType real VT_I4/VT_R8/VT_BSTR (Bginfo64 imports 19 ordinals; SysAllocStringByteLen=150 fixed this sweep)
- [ ] **SW-703** oleaut32 family: VariantClear deep VT_BSTR|VT_ARRAY (Bginfo64 imports 19 ordinals; SysAllocStringByteLen=150 fixed this sweep)
- [ ] **SW-704** oleaut32 family: SafeArrayCreate/DestroyDescriptor/Data (Bginfo64 imports 19 ordinals; SysAllocStringByteLen=150 fixed this sweep)
- [ ] **SW-705** oleaut32 family: SafeArrayGetElement/PutElement 1D (Bginfo64 imports 19 ordinals; SysAllocStringByteLen=150 fixed this sweep)
- [ ] **SW-706** oleaut32 family: SafeArrayGetUBound/LBound real dims (Bginfo64 imports 19 ordinals; SysAllocStringByteLen=150 fixed this sweep)
- [ ] **SW-707** oleaut32 family: VariantTimeToSystemTime real conversion (Bginfo64 imports 19 ordinals; SysAllocStringByteLen=150 fixed this sweep)
- [ ] **SW-708** oleaut32 family: SystemTimeToVariantTime inverse (Bginfo64 imports 19 ordinals; SysAllocStringByteLen=150 fixed this sweep)
- [ ] **SW-709** oleaut32 family: VarBstrFromDate honest locale format (Bginfo64 imports 19 ordinals; SysAllocStringByteLen=150 fixed this sweep)
- [ ] **SW-710** oleaut32 family: OleLoadPicture honest E_FAIL + stream probe (Bginfo64 imports 19 ordinals; SysAllocStringByteLen=150 fixed this sweep)
- [ ] **SW-711** GDI depth: CreateDIBSection 32bpp real 4B/px mode + alpha (CreateDIBSection real landed SWEEP-014 r3)
- [ ] **SW-712** GDI depth: CreateDIBSection BI_BITFIELDS masks decode (CreateDIBSection real landed SWEEP-014 r3)
- [ ] **SW-713** GDI depth: StretchBlt real scale loop on MWBmp (CreateDIBSection real landed SWEEP-014 r3)
- [ ] **SW-714** GDI depth: GdiFlush/batch no-op contract (CreateDIBSection real landed SWEEP-014 r3)
- [ ] **SW-715** GDI depth: GetDIBits real from MWBmp pool (CreateDIBSection real landed SWEEP-014 r3)
- [ ] **SW-716** GDI depth: SetDIBitsToDevice direct raster write (CreateDIBSection real landed SWEEP-014 r3)
- [ ] **SW-717** GDI depth: PatBlt rop PATCOPY/PATINVERT (CreateDIBSection real landed SWEEP-014 r3)
- [ ] **SW-718** GDI depth: ExtCreatePen/ExtCreateRegion honest handles (CreateDIBSection real landed SWEEP-014 r3)
- [ ] **SW-719** GDI depth: GetTextExtentPoint32W real font metric est (CreateDIBSection real landed SWEEP-014 r3)
- [ ] **SW-720** GDI depth: EnumFonts metrics round-trip pin (CreateDIBSection real landed SWEEP-014 r3)
- [ ] **SW-721** GDI depth: SetWorldTransform/GetWorldTransform honest identity (CreateDIBSection real landed SWEEP-014 r3)
- [ ] **SW-722** GDI depth: CreatePalette/RealizePalette honest handles (CreateDIBSection real landed SWEEP-014 r3)
- [ ] **SW-723** GDI depth: GetPixel/SetPixel real MWBmp accessors (CreateDIBSection real landed SWEEP-014 r3)
- [ ] **SW-724** GDI depth: RoundRect/Ellipse real raster paths (CreateDIBSection real landed SWEEP-014 r3)
- [ ] **SW-725** GDI depth: TransparentBlt color-key loop (CreateDIBSection real landed SWEEP-014 r3)
- [ ] **SW-726** msvcrt/advapi tail: _itoa/_ltoa/_ultoa real (Bginfo64 355-gate list order)
- [ ] **SW-727** msvcrt/advapi tail: _strlwr/_strupr real (Bginfo64 355-gate list order)
- [ ] **SW-728** msvcrt/advapi tail: _mbsicmp/_memicmp (Bginfo64 355-gate list order)
- [ ] **SW-729** msvcrt/advapi tail: _mbschr/_mbsrchr byte semantics (Bginfo64 355-gate list order)
- [ ] **SW-730** msvcrt/advapi tail: _snprintf real vararg (Bginfo64 355-gate list order)
- [ ] **SW-731** msvcrt/advapi tail: _strcmpi/_strnicmp real (Bginfo64 355-gate list order)
- [ ] **SW-732** msvcrt/advapi tail: WNetOpenEnum/EnumResource/CloseEnum honest ERROR_NO_NETWORK (Bginfo64 355-gate list order)
- [ ] **SW-733** msvcrt/advapi tail: CreateEnvironmentBlock/DestroyEnvironmentBlock honest NULL (Bginfo64 355-gate list order)
- [ ] **SW-734** msvcrt/advapi tail: NetWkstaUserGetInfo/NetServerGetInfo NERR base (Bginfo64 355-gate list order)
- [ ] **SW-735** msvcrt/advapi tail: NetApiBufferFree/NetApiBufferAllocate real (Bginfo64 355-gate list order)
- [ ] **SW-736** TASK-LOADER-DLL slice: PE DLL: LoadLibraryW image map + DllMain (unblocks 13+ sweep programs)
- [ ] **SW-737** TASK-LOADER-DLL slice: PE DLL: export table walk (unblocks 13+ sweep programs)
- [ ] **SW-738** TASK-LOADER-DLL slice: PE DLL: GetProcAddress by name/ordinal (unblocks 13+ sweep programs)
- [ ] **SW-739** TASK-LOADER-DLL slice: PE DLL: TLS callbacks (unblocks 13+ sweep programs)
- [ ] **SW-740** TASK-LOADER-DLL slice: PE DLL: DllMain detach + refcount (unblocks 13+ sweep programs)
- [ ] **SW-741** TASK-LOADER-DLL slice: PE DLL: 7z.dll case study (unblocks 13+ sweep programs)
- [ ] **SW-742** TASK-LOADER-DLL slice: PE DLL: libcrypto.dll case study (unblocks 13+ sweep programs)
- [ ] **SW-743** TASK-LOADER-DLL slice: PE DLL: dependencies-of-dependencies (unblocks 13+ sweep programs)
- [ ] **SW-744** TASK-LOADER-DLL slice: PE DLL: resource-only DLLs (unblocks 13+ sweep programs)
- [ ] **SW-745** TASK-LOADER-DLL slice: PE DLL: G1 extension with 7z.exe full ops (unblocks 13+ sweep programs)
- [ ] **SW-746** kernel32/user32 tail: ReadProcessMemory honest fail (self-probe)
- [ ] **SW-747** kernel32/user32 tail: WriteProcessMemory honest fail
- [ ] **SW-748** kernel32/user32 tail: GetComputerNameA real hostname
- [ ] **SW-749** kernel32/user32 tail: ExpandEnvironmentStringsW real env map
- [ ] **SW-750** kernel32/user32 tail: WritePrivateProfileStringW in-memory win.ini + GetPrivateProfileStringW round-trip
- [ ] **SW-751** kernel32/user32 tail: GetPrivateProfileIntW (Bginfo64 pairs with GetProfileIntW)
- [ ] **SW-752** kernel32/user32 tail: SearchPathW real module dir search
- [ ] **SW-753** kernel32/user32 tail: GetModuleFileNameExW + EnumProcessModules honest set
- [ ] **SW-754** kernel32/user32 tail: WaitForMultipleObjects full WAIT_FAILED paths
- [ ] **SW-755** kernel32/user32 tail: SetUnhandledExceptionFilter + UnhandledExceptionFilter contract
- [ ] **SW-756** kernel32/user32 tail: GetExitCodeProcess/TerminateProcess cross-check
- [ ] **SW-757** kernel32/user32 tail: CreateToolhelp32Snapshot honest empty snapshot
- [ ] **SW-758** kernel32/user32 tail: Process32First/Next honest ERROR_NO_MORE_FILES
- [ ] **SW-759** kernel32/user32 tail: HeapWalk honest end-of-heap
- [ ] **SW-760** kernel32/user32 tail: GetProcessWorkingSetSize honest quota
- [ ] **SW-761** kernel32/user32 tail: SetInformationJobObject honest reject
- [ ] **SW-762** kernel32/user32 tail: AssignProcessToJobObject honest fail
- [ ] **SW-763** kernel32/user32 tail: GetLogicalDrives + GetDriveTypeA honest C: only
- [ ] **SW-764** kernel32/user32 tail: GetVolumeInformationW honest volume
- [ ] **SW-765** kernel32/user32 tail: SetLocalTime honest ignore
- [ ] **SW-766** kernel32/user32 tail: GetTimeZoneInformation real host TZ
- [ ] **SW-767** kernel32/user32 tail: GetSystemInfo real cores/page size
- [ ] **SW-768** kernel32/user32 tail: GlobalMemoryStatusEx real
- [ ] **SW-769** kernel32/user32 tail: GetProcessAffinityMask pin
- [ ] **SW-770** kernel32/user32 tail: SetErrorMode honest store
- [ ] **SW-771** infra/verify: decoder ordinal audit: sweep012_progress vs pefile db full sweep
- [ ] **SW-772** infra/verify: api_trace repair: buffered writer close-on-crash (parse-err in AFTER3)
- [ ] **SW-773** infra/verify: sweep pool: add collision-free meta names (AFTER2 dup RAMMap64)
- [ ] **SW-774** infra/verify: G1: add jq.exe exit-parity case
- [ ] **SW-775** infra/verify: prog-sweep: parallel runs (nproc workers)
- [ ] **SW-776** infra/verify: prog-sweep: per-program wall-time column
- [ ] **SW-777** infra/verify: progress.json: track cumulative zero-stub count
- [ ] **SW-778** infra/verify: stdin </dev/null harness: document in sweep README
- [ ] **SW-779** infra/verify: MINWIN_FUNC_MAP: auto-build for all 52 pool programs
- [ ] **SW-780** infra/verify: crash banner: include last-3 APIs from api_trace
- [ ] **SW-781** infra/verify: stub rate-limiter: MINWIN_STUB_LIMIT env (200 default)
- [ ] **SW-782** infra/verify: loader: assert-dispatch audit script (defined-but-unwired check, BUG-070 class)
- [ ] **SW-783** infra/verify: docs: ADR-015 ordinal-import policy
- [ ] **SW-784** infra/verify: docs: BUG-069/070/071 postmortems
- [ ] **SW-785** infra/verify: 100-list: x86 LOAD-REJECT column formalized (11 programs)
- [ ] **SW-786** infra/verify: games: 2048-class win64 obtain+run (SW-501 mandate check-in)
- [ ] **SW-787** infra/verify: 7zFM/7zG: click-through GUI scenario via MINWIN_CLICKS
- [ ] **SW-788** infra/verify: procexp64: 0-stub crash RIP symbolize (SW-602 close-out)
- [ ] **SW-789** infra/verify: produkey: RegSetValueExW persistence path (EulaAccepted pattern)
- [ ] **SW-790** infra/verify: zoomit64 SW-553 int3 re-check after batch #4
- [ ] **SW-791** infra/verify: curl: OPENSRC-CURL-1 UCRT state pointer next slice (SW-610)
- [ ] **SW-792** infra/verify: puttygen: SW-552 stack machine-named frames -> fix RIP=0 path
- [ ] **SW-793** infra/verify: md5deep64 suite: shared CRT-start crash root-cause (one BUG-*)
- [ ] **SW-794** infra/verify: Coreinfo64: 0-stub crash — IsWow64Process now real, re-trace (SW-608)
- [ ] **SW-795** infra/verify: SearchMyFiles: 0-stub crash re-trace after batch #4 (SW-609)
- [ ] **SW-796** infra/verify: Bginfo64: libc-side NULL deref — last-API correlation (new frontier)
- [ ] **SW-797** infra/verify: WinMergeU: guest NULL store at RVA 0x12d0c2 (REA funcmap)
- [ ] **SW-798** infra/verify: sqlite3_analyzer: byte-store crash at RVA 0xaa652 (REA funcmap)
- [ ] **SW-799** infra/verify: procmon64: exit 1 banner path audit after batch #4
- [ ] **SW-800** infra/verify: lz4/zstd/trurl: exit-code parity re-pin (3x each)

## SWEEP-015 continuation queue (SW-801..SW-820 — evidence-cited 2026-10-19)

Source: SWEEP-015 session (reconciliation with upstream f64290e + fresh
static analysis). Port patches preserved in `.agent/patches/sweep015_resource_version_port.patch`
(ready-to-apply diff against f64290e; each item below names its regression
gate that must be re-accepted when the port lands).

- [ ] **SW-801a** resource/fix: IMAGE_RESOURCE_DIRECTORY NamedEntries/IdEntries
      read at +12/+14 (not +8/+10) — old walk saw 0 entries and EVERY
      FindResource honestly failed. Port patch hunk `w3_res_cmp_name/w3_res_find`.
      Regression gate: Bginfo64 must re-accept exit0 after the flow deepens.
- [ ] **SW-801b** resource/fix: resource string-name compare in real 2-byte
      UTF-16 units (old 4-byte wchar_t cast only matched even-length names;
      L"MAIN_WINDOW" never matched). Same hunk; same gate.
- [ ] **SW-801c** resource/fix: language level (depth 2) accepts any entry;
      IS_INTRESOURCE for numeric type/name (MAKEINTRESOURCE(5) RT_DIALOG).
      Proven on rammap64: FindResourceW(L"MAIN_WINDOW", RT_DIALOG) then
      DLGTEMPLATEEX parse -> main window dialog image.
- [ ] **SW-801d** resource/fix: LockResource returns IMAGE_BASE +
      DATA_ENTRY.OffsetToData (verified: rammap64 0x5e640 -> 01 00 ff ff).
      Upstream policy (entry VA) kept this sweep to avoid Bginfo64 flow
      regression; land together with SW-801a-c in one batch.
- [ ] **SW-801e** version/real: VERSION.dll six-pack VS_VERSIONINFO parser
      (GetFileVersionInfoSize/W + A, VerQueryValue\, \VarFileInfo\Translation,
      \StringFileInfo\<lc>\<key>) reading RT_VERSION from disk. Replaces the
      honest-0 stubs (loader.c:12034 f64290e). Gate: Coreinfo64 + autorunsc64
      go deeper (version gate passes) — needs SW-801f-class topology stubs
      (GetNumaHighestNodeNumber/SetThreadGroupAffinity real) or an
      honest GetLogicalProcessorInformationEx to keep exit0.
- [ ] **SW-801f** pump/fix LANDED: gate-bypass — keys/clicks remaining but
      pacing gate closed + nothing else deliverable -> deliver anyway
      (real GetMessage blocks, never quits). Timerless paint-driven guests
      died after their first key. Acceptance: tests/games/miniwin_calc.c
      3x exit0, 4 frames, all-frames SHA 1b92b95f873029f0 x3.
- [ ] **SW-801g** uxtheme LANDED: dark-mode family named (ordinals
      104/132/133/135 = RefreshImmersiveColorPolicyState /
      ShouldAppsUseDarkMode(FALSE: light host) / AllowDarkModeForWindow /
      AllowDarkModeForApp) in the IAT ordinal map + GetProcAddress
      ordinal path + ENTRYs. autoruns64 ord 0x87=135 now self-describing.
- [ ] **SW-801h** infra/env: environment-sensitive exits — procexp64/
      Coreinfo64/Bginfo64/autorunsc64/WinMergeU measured exit0 in the
      SWEEP-014 pool but 139 (deterministic x3) in today's 2-CPU/4GB
      sandbox, pure-upstream binary reproduced (sha 8fda4302) — record
      host geometry with every pool run (nproc/cpu.max/mem) and re-baseline.
- [ ] **SW-801i** CRT/bug: zig/mingw static-CRT snprintf builds a fake FILE
      (heap) that map_crt_stream refuses -> vfprintf(NULL) SIGSEGV
      (tests/games/miniwin_calc.c first build). Either recognize the mingw
      fake-FILE window in map_crt_stream or document wsprintfA-only contract.
- [ ] **SW-801j** calc/accept: second calculator genre tests/games/
      miniwin_calc.c (pure GDI + WM_KEYDOWN keyboard calc, no child
      controls) — 3x exit0, display "1+2=" result "3", frame SHAs
      byte-identical x3. Wire into scripts/ as a permanent acceptance.
