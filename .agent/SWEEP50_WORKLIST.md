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

## SWEEP-016 wave (2026-10-19) — elephant batch: 2 gates closed, patch landed, root-causes fixed
- [x] **SW-801a-e LANDED** resource/version port batch applied from
      .agent/patches/sweep015_resource_version_port.patch (3-way, conflicts
      resolved: kept landed SW-801g ordinal path, took VerQueryValueA ENTRY;
      dedup UXTHEME block; stray-brace from git apply -3 fixed). Tree now
      carries SW-801a (IS_INTRESOURCE), SW-801b (2-byte UTF-16 name compare),
      SW-801c (NamedEntries/IdEntries at 12/14 + language level accepts all),
      SW-801d (g_res_dir_base + LockResource data ptr), SW-801e (VS_VERSIONINFO
      six-pack + VerQueryValueA). G1 12/12 re-pinned sha a5d94299.
- [x] **SW-801l GLPIEx rewrite + RelationAll** — full multi-entry topology
      (RelationProcessorCore xN + RelationNumaNode + RelationProcessorPackage
      + RelationGroup, two-pass sizing); unknown cls now honest
      ERROR_INVALID_PARAMETER (success-with-0-entries made Coreinfo64 walk
      uninit stack); cls=0xFFFF = RelationAll (phnt) honored. Coreinfo64
      139->exit0 x3 with FULL topology dump (16,189 B stdout: microcode,
      processor signature, Cache Map, Group Map). Closes SW-652 + SW-561
      (version gate now passes).
- [x] **SW-801m NUMA old-SDK layout** — NUMA_NODE_RELATIONSHIP GroupCount at
      sub+22 / GROUP_AFFINITY at sub+24 (Reserved[18], Win7-Win10 SDK layout);
      the shifted Win11 layout made Coreinfo64 NUMA-map parser read garbage.
- [x] **SW-801n BUG-072** load_pe hardening: fopen(dir) succeeds on Linux ->
      ftell -1 -> malloc((size_t)-1) NULL -> SEGV at dos->e_magic. Now
      stat()+S_ISREG + every I/O checked. ROOT CAUSE of "Bginfo64 exit 139
      0-stub" and part of SW-801h.
- [x] **SW-801o BUG-073** BSTR issuance registry (BUG-065 pattern):
      bstr_alloc_len tracks; SysFreeString frees only issued BSTRs, reports
      foreign ones (Bginfo64 crashed free()ing a foreign BSTR inside libc,
      frame mw_SysFreeString). Bginfo64 139->exit0 x3. Closes SW-651.
- [x] **SW-801k winsta.dll pseudo-module LANDED** — WinStationConnectW /
      Shadow / GetProcessSid / QueryInformationW / FreeMemory /
      CloseServer / OpenServerW with honest contracts; Procexp64's
      GetProcAddress presence-probes no longer render "Not able to run on
      this version of Windows" dialogs (3 dialogs -> 0).
- [x] **SW-801p SW-801h ROOT CAUSE CORRECTION** — the SWEEP-015 sweep ran
      against artifacts/sweep50/dl_extract/*.exe, a directory that no longer
      exists (untracked scratch lost between sessions). The 139s are NOT
      environment sensitivity: the current corpus is the re-downloaded
      2026-era official builds (dl/<dir>/<exe>), which need more APIs than
      the lost dl_extract copies. Re-baselined x3 (artifacts/sweep016/sweep/
      sweep016_sweep.tsv, 16 programs, auto-resolved exe paths).
- [ ] **SW-802** rammap64 139 x3: RIP 0x140004db7 mov (%r9),r8 r9=NULL before
      SetPropW; 56 unresolved; fix prop-cookie source, re-run x3.
- [ ] **SW-803** produkey 139 x3: RIP 0x140022f24 RAX=0x140022ef0
      RCX=0x10001 RDX=1 (shared NirSoft signature).
- [ ] **SW-804** cports 139 x3: RIP 0x140002119 RCX=0x10001 RDX=1 (same
      signature family as SW-803 — solve together).
- [ ] **SW-805** diskmon64 139 x3: RIP 0x14000671c RAX=0 RCX=0x10001 RDX=1.
- [ ] **SW-806** searchmyfiles 139 x3: RIP 0x140028001 RAX=0xffffffff
      RCX=0x120 (NirSoft).
- [ ] **SW-807** puttygen 139 x3: RIP=0x0 call through NULL fn-ptr
      (SW-552 family).
- [ ] **SW-808** winmergeu 139 x3: RIP 0x14012d0c2 RAX=0 (big GUI, MFC).
- [ ] **SW-809** procexp64 139 x3: winsta gates now pass (SW-801k); new
      frontier = CLR-adjacent init (COMPLUS_FodPath, repeated
      NtOpenSymbolicLinkObject('') loop, 319 unresolved). RIP executes
      UTF-16 string bytes 0x6f00690074006e.
- [ ] **SW-810** autoruns64 139 x3 (was exit0 on lost dl_extract build):
      symbolize with SWEEP-011 method, one BUG-*, re-run x3.
- [ ] **SW-811..SW-815** per-program unresolved-import closure to exit0:
      rammap64 (56), bginfo64 (21 stubs->0), coreinfoex64 (12->0 done),
      procexp64 (319), autoruns64 (364) — one item per program, each with
      measured stub/unresolved counts from sweep016_sweep.tsv.
- [ ] **SW-816** sweep hygiene: sweep script uses dl/<dir> auto-resolve
      (scripts/sweep016_broad_sweep.sh); NEVER pass a directory to the
      loader (BUG-072); keep dl_extract replacement INSIDE the repo.
- [ ] **SW-817..SW-850** 3x continuation queue (34 items): ws2_32 ladder
      tail (getaddrinfo/getnameinfo family x5), winhttp honest family x3
      (Procexp64 needs), oleaut32 SafeArray x5 (SW-704..706), CRT tail x5
      (SW-726..731), GUI frontier x5 (MFC dialogs WinMergeU, putty 3rd
      dialog), printing x3 (StartDoc family honest), WMI/COM x3
      (CoSetProxyBlanket callers), perf counters x2, debug APIs x3
      (SuspendThread/GetThreadContext family), closure/verify x2
      (lz4/zstd/trurl re-pin after every loader change; 14-section report
      update per user mandate).

<!-- ============ SWEEP-018 (2026-10-19) — «ادامه و پوش» elephant batch ============ -->

## SWEEP-018 results (measurable progress per user mandate)

**Five 139s -> exit0 x3 deterministic (pump-off sweep, artifacts/sweep018/sweep/sweep018_sweep.tsv):**
- [x] **SW-802 CLOSED** rammap64 139->exit0 x3. ROOT CAUSE: WM_CREATE dispatched
      with lParam=NULL; NirSoft/Sysinternals WndProcs deref lParam as
      CREATESTRUCT* (rammap 0x140004db7 `mov (%r9),%r8`; diskmon 0x14000671c
      `mov (%r9),%rax` + `movups (%rax),%xmm0`; cports 0x140002119 /
      produkey 0x140022f24 `mov (%r9),%rdx; mov %rcx,0x110(%rdx)` — store hwnd
      at lpCreateParams+0x110). FIX: mw_gui_send_create builds a real
      CREATESTRUCT (0x50 bytes, lpCreateParams = forwarded CreateWindowEx
      lpParam, scratch fallback, geometry/style/name/class), both ExW/ExA
      forward param (src/loader/loader.c g_create_param).
- [x] **SW-803 CLOSED (NirSoft GUI family)** produkey/cports/diskmon64 139->exit0
      x3. Chain of gates, all named by SWEEP-011 method:
      (1) GDI32!GetPixel returned 0 -> x198 spin loops (produkey
      0x1400202de, cports 0x1400157c9) -> honest GetPixel reads framebuffer,
      CLR_INVALID out-of-surface; (2) SetStretchBltMode+StretchBlt unresolved
      -> 33-level blit recursion -> honest nearest-neighbor StretchBlt;
      (3) CreateToolbarEx unresolved -> TOOLBAR child identity (class table
      0x86); (4) COMCTL32 ordinal #6 = CreateStatusWindowA (proven by
      SendMessageA(h,0x404=SB_SETPARTS,1,&widths) follow-up) -> STATUSBAR
      child (0x87), ordinal map extended, SB_SETPARTS->TRUE; (5) child table
      64 overflowed -> 512; memDC/bitmap 8->32; ImageList 8->16.
- [x] **SW-805 CLOSED** diskmon64 139->exit0 x3 (same WM_CREATE root cause).
- [x] **SW-810 CLOSED** autoruns64 139->exit0 x3 (pump-off; same WM_CREATE
      CREATESTRUCT root cause — the "environment sensitivity" of SW-801h was
      this bug all along for autoruns64's class).
- [x] **SW-813 crash-handler hardening LANDED**: silent-139 class eliminated.
      (a) SA_ONSTACK + 64KB altstack (stack-overflow crashes used to kill the
      handler itself -> zero diagnostics); (b) MW_CRASH_PROBE sigsetjmp guard
      around every raw read (bytes@RIP, return-addr@rsp, 4KB stack scan,
      backtrace chain) — nested fault -> "[DIAG] dump truncated" + full
      register/RIP-MAP output still delivered.
- [x] **LoadImageW honest resource DIB decode LANDED** (part of SW-803 chain):
      RT_BITMAP (1/4/8/24/32bpp BI_RGB, bottom-up+top-down) -> HBITMAP-space
      object readable by ImageList_Add; fixes the 8-slot blank-image pool
      exhaustion that drove the 62,836-DC create/destroy init retry loop
      (produkey run11 trace: 20,946 bitmaps leaked).
- [ ] **SW-812** pump-on re-init loop (produkey/cports; pump-off = exit0):
      with MW_GUI_EVENTS=1 the guest re-runs its whole init (10,468 SetMenu,
      menu handles 0x50001..0x5000f, 62,836 DC pairs) — menu family APIs
      GetSubMenu/GetMenuItemCount/GetMenuItemInfoA/ModifyMenuA/DestroyMenu
      are UNRESOLVED, menu table is 8 slots with no free path, DestroyMenu
      unresolved means slots never release. Hypothesis: NirSoft builds
      toolbar from menu items; GetSubMenu()->NULL -> teardown+reload forever.
      Fix: honest menu-item model (items from LoadMenu resource walk) or
      at minimum DestroyMenu frees slots + GetSubMenu returns menu itself
      for pos 0. Evidence: artifacts/sweep018/produkey/run11..run14 traces.
- [ ] **SW-814** searchmyfiles 139 x3: RIP 0x140028001 `cmpw $0x22,(%r8)`
      — UTF-16 string scanner walks wild pointer (R8=0x4800091111058b48);
      NirSoft command-line/arg parsing path, RCX=0x120. Distinct from
      SW-803 family (no stub loops; 0 stub calls).
- [ ] **SW-815** winmergeu 139 x3: RIP 0x14012d0c2, RAX=0,
      RCX=0x7ff0000000000001 (double 1.0), RDX=0x4032000000000000 (4.5) —
      SSE/float code path in MFC init; decode with SWEEP-011 method.
- [ ] **SW-807** puttygen RIP=0 NULL fn-ptr (still open, SW-552 family).
- [ ] **SW-808** winmergeu (see SW-815).
- [ ] **SW-809** procexp64 RIP=0x6f00690074006e UTF-16-bytes-as-code (still
      open; CLR-adjacent init).

## SW-816..SW-880 — «ادامه سه برابری» continuation queue (65 items) — LANDED in SWEEP-019 «چند ده تسک همزمان»

- [x] **SW-816..820 menu family x5 LANDED**: MwMenu[8] x 16-item model
      (id/state/text/submenu); CreateMenu/CreatePopupMenu/DestroyMenu (real
      slot free — SW-812 prerequisite)/AppendMenuA+W (MF_POPUP links
      submenus)/ModifyMenuA+W (BYPOSITION+BYCOMMAND)/GetMenuItemCount/
      GetSubMenu/GetMenuItemInfoA+W (MENUITEMINFO x64 @0/4/8/12/16/24/56/64,
      cbSize==72 enforced); LoadMenuW/A rebuilt on the same allocator.
- [x] **SW-821..825 clipboard x5 LANDED**: single headless clipboard,
      Open/Close (close-unopened = ERROR_INVALID_HANDLE)/Empty/Set/Get
      (fmt-matched) + IsClipboardFormatAvailable.
- [x] **SW-826..830 common dialogs x5 LANDED**: comdlg32 GetOpenFileNameA/W,
      GetSaveFileNameA/W, ChooseColorA/W, FindTextA/W, ReplaceTextA/W,
      PageSetupDlgA/W — canned user-CANCEL (0/FALSE/NULL) +
      CommDlgExtendedError()==0.
- [x] **SW-831..835 ListView x5 LANDED** (+ROOT CAUSE): mw_sysclass_* knew
      only 8 built-ins so SysListView32 children were never registered and
      LVM_* was never seen. Now 0x88..0x8B = SysListView32/SysTreeView32/
      SysTabControl32/SysHeader32 registered as children; lazy per-slot
      state (64 items x 2 subitems text + cols) routed in SendMessageW AND
      SendMessageA: LVM_GETITEMCOUNT/INSERTITEM A+W/INSERTCOLUMN A+W/
      GETITEMTEXT A+W/SETITEMSTATE/HITTEST (typed -1, no pixel layout).
- [x] **SW-836..840 TreeView + Tab x5 LANDED**: TVM_INSERTITEM A+W
      (MW_TREE_BASE pseudo-handles)/GETCOUNT/GETNEXTITEM(TVGN_ROOT);
      TCM_INSERTITEM A+W/GETITEMCOUNT/SETCURSEL/GETCURSEL; HDM bonus
      (GETITEMCOUNT/INSERTCOLUMN A+W).
- [x] **SW-841..845 GDI regions x5 LANDED**: MwRgn[32] x 16 rects, real
      rect algebra — CreateRectRgn/Indirect, CombineRgn (OR concat / AND
      pairwise / DIFF slabs / XOR both ways / COPY; real
      NULLREGION/SIMPLE/COMPLEX types), SelectClipRgn (per-DC map),
      RectInRegion (exact scan), GetUpdateRgn (user32, NULLREGION).
- [x] **SW-846..850 printing x5 LANDED**: old tail returned -1 (job
      failure = fatal for bginfo64); now honest no-op printer —
      StartDocA/W -> job 1, StartPage/EndPage/EndDoc succeed, AbortDoc.
- [x] **SW-851..855 shell x5 LANDED**: SHGetSpecialFolderPathA/W (scratch
      csidl dirs, the produkey GetProcAddress-NULL gate),
      SHBrowseForFolderA/W (cancel), SHGetFileInfoA/W (honest 0),
      ShellExecuteA/W (existence-checked 42/SE_ERR_FNF), ShellExecuteExA/W
      (hInstApp@52), SHGetMalloc (malloc-backed IMalloc vtbl).
- [x] **SW-856..860 network x5 LANDED**: mpr.dll added to g_known_dlls;
      WNetOpenEnumA/W -> ERROR_NO_NETWORK(225) NULL handle, WNetCloseEnum
      WN_SUCCESS, WNetEnumResourceA/W 225; getnameinfo -> real libc;
      WSAEnumProtocolsA/W typed WSAENOBUFS handshake, 0 providers.
- [x] **SW-861..865 CRT tail x5 LANDED**: _mbsicmp/_mbschr/_memicmp/
      _strlwr/_snprintf (BUG-063 ms_abi varargs -> classify/repack ->
      mw_printf_emit via fmemopen; MSVCRT -1 truncation contract).
- [x] **SW-866..870 COM x5 LANDED**: CoCreateInstanceEx/CoGetClassObject
      (REGDB_E_CLASSNOTFOUND policy), CoTaskMemAlloc/Free, CoCreateGuid
      (/dev/urandom); CoSetProxyBlanket/CoUninitialize pairing audited —
      already honest, unchanged.
- [x] **SW-871..875 perf/debug x5 LANDED**: ReadProcessMemory/
      WriteProcessMemory — same-process range-checked memcpy vs
      [g_image_base,+size), ERROR_PARTIAL_COPY(299) outside, write path
      mprotects RW during copy then restores R+X; Suspend/Get/SetThread-
      Context already honest; ResumeThread entry bound to existing defn.
- [x] **SW-876..878 registry export x3 LANDED**: RegDeleteValueA/
      RegEnumValueA/RegQueryInfoKeyA — A-twins of the W honest contracts
      (2/259/6 + SetLastError).
- [x] **SW-879..880 closure/verify x2 LANDED**: G1 re-pin
      sha 2f5535f2… (regression 12/12, evidence/master/g1/20261019_153111,
      build 1,656,608 -> 1,771,176 bytes) + full measured sweep TSV
      artifacts/sweep019/sweep/sweep019_sweep.tsv (16-prog corpus x3 +
      produkey pump-on + calc/snake/breakout x3).

## SW-881..SW-920 — SWEEP-019 continuation queue (40 items) — LANDED in SWEEP-021 «چند ده تسک همزمان» (remainder: 35 imports; WSA/network tail SW-911..915 was already covered by SWEEP-014 r2 — verified present)

- [x] **SW-881..885 menu deepening x5**: InsertMenuItemA/W, RemoveMenu,
      DeleteMenu, GetMenuItemRect, TrackPopupMenuEx — completes the menu
      model for pump-on SW-812 verification beyond exit0 (menu loop must
      terminate with bounded SetMenu counts).
- [x] **SW-886..890 ListView deepening x5**: LVM_GETNEXTITEM, LVM_SETITEM
      A/W, LVM_DELETEALLITEMS, LVM_GETSUBITEMRECT, LVM_GETSTRINGWIDTH —
      NirSoft lists iterate via GETNEXTITEM after population.
- [x] **SW-891..895 TreeView/Tab deepening x5**: TVM_GETITEM A/W,
      TVM_SELECTITEM, TVM_EXPAND, TCM_GETITEM A/W, TCM_ADJUSTRECT.
- [x] **SW-896..900 GDI region deepening x5**: ExtCreateRegion,
      GetRgnBox, OffsetRgn, PtInRegion, EqualRgn (completes the algebra).
- [x] **SW-901..905 shell deepening x5**: SHGetFolderPathA/W (legacy
      shfolder path), SHGetKnownFolderPath, SHGetDesktopFolder minimal
      IShellFolder, SHAutoComplete, SHSetValueA/W (shlwapi registry).
- [x] **SW-906..910 RICHEDIT x5**: msftedit.dll/RICHED50W + RICHEDIT class
      — the Sysinternals EULA dialog template uses RICHEDIT; EM_ family
      (EM_SETTEXTEX, EM_STREAMIN) so EULA text renders and gate logic can
      proceed past text queries.
- [x] **SW-911..915 network x5**: WSAAsyncSelect, WSAEventSelect,
      connect/gethostbyname wrappers, shutdown, select — pageant family.
- [x] **SW-916..920 CRT/COM x5**: _mbsncat/_mbsnbcpy/_mbslen, _splitpath/
      _makepath, CoGetObjectContext, CoFreeUnusedLibraries, StringFromGUID2.
- [ ] **SW-800 (carried)** infra/verify: lz4/zstd/trurl exit-code parity.
- [ ] **SW-801i (carried)** CRT/bug: zig/mingw static-CRT snprintf fake FILE.
- [ ] **SW-801j (carried)** calc/accept: second calculator genre
      tests/games/ style acceptance.
- [ ] **SW-807/809/814/815 (carried)** crash signatures: puttygen NULL
      fn-ptr, procexp64 UTF-16-as-code, searchmyfiles cmpw 0x22, winmergeu.


## SW-921..SW-960 — SWEEP-021 continuation queue (40 items) — LANDED in SWEEP-022 «ادامه» one batch (~45 new/updated imports + BUG-074/075 fixes)

- [x] **SW-921..925 dialog deepening x5 LANDED**: DialogBoxIndirectParamW/A
      now WALK the real DLGTEMPLATE (word-aligned SZ_OrOrd menu/class/title,
      DS_SETFONT skip, DWORD-aligned items, class ordinals 0x80..0x85 ->
      sysclass ids) and register children in the SAME registry as
      CreateWindowEx children, so GetDlgItem binds for real;
      SendDlgItemMessageA/W routed GetDlgItem->SendMessageW/A;
      CheckDlgButton/IsDlgButtonChecked verified pre-existing.
- [x] **SW-926..930 ListView final x5 LANDED** + BUG-074 FIX: 0x1008 is
      LVM_DELETEITEM (row shift) and 0x1009 LVM_DELETEALLITEMS (rows only,
      columns survive) — SWEEP-021 had the two swapped; LVM_GETITEMA/W
      (LVITEM x64, LVIF_TEXT real text out + state), LVM_SETCOLUMNA/W
      (colw/coltxt store), LVM_GETCOLUMNA/W (width + header text),
      LVM_SORTITEMS (typed no-op 1).
- [x] **SW-931..935 TreeView final x5 LANDED** + BUG-075 FIX: TVGN values
      corrected against wine commctrl.h (ROOT=0, NEXT=1, PREVIOUS=2,
      PARENT=3, CHILD=4, FIRSTVISIBLE=5, CARET=9, LASTVISIBLE=10) —
      SWEEP-021 mapped wp==4 (CHILD) to ROOT; TVM_GETNEXTITEM now does
      insertion-order NEXT/PREVIOUS walks; TVM_GETITEMRECT (hItem in
      rect.left, synthetic 120x16 rows), TVM_GETVISIBLECOUNT (20),
      TVM_HITTEST (TVHT_NOWHERE + NULL, typed), TVM_SORTCHILDREN (1).
- [x] **SW-936..940 GDI DIB x5 LANDED**: GetDIBits (real BITMAPINFOHEADER
      fill + bottom-up 24bpp row copy from the CreateDIBSection pool),
      SetDIBits (bottom-up copy into pool bitmap), MaskBlt (SRCCOPY routes
      through BitBlt, else typed ERROR_INVALID_FUNCTION), BitBlt
      CAPTUREBLT audit LANDED as fix: rop base = rop & 0x00FFFFFF strips
      CAPTUREBLT(0x40000000)/NOMIRRORBITMAP(0x80000000) so
      SRCCOPY|CAPTUREBLT takes the copy path (bginfo64 desktop paint);
      CreateDIBSection real-since-SWEEP-014 (ppvBits verified).
- [x] **SW-941..945 fonts x5 LANDED**: EnumFontFamiliesExW/A (ENUMLOGFONTEX
      shape, canned "Raster" inventory), GetTextFaceA/W (face name out),
      SetTextJustification (vacuous 1); CreateFontIndirectA/W +
      GetTextMetricsA/W verified pre-existing (real LOGFONT store).
- [x] **SW-946..950 network x5 AUDITED-AND-COMPLETE**: htonl/htons/ntohl/
      ntohs verified present (name + fixed-ordinal tables 9/14/15),
      WSAGetLastError/ioctlsocket/getsockopt/setsockopt/WSAIoctl(FIONREAD)
      verified pre-existing — family already covered by SWEEP-014 r2;
      no dead code added, audit recorded here as the completion proof.
- [x] **SW-951..955 registry x5 LANDED — in-memory hive**: EMPTY at start
      (reads of never-created keys still ERROR_FILE_NOT_FOUND — zero change
      for read-only callers); RegCreateKeyExW real (find-or-create, handle
      = 0x00090000|slot, REG_CREATED_NEW_KEY/OPENED_EXISTING_KEY disp),
      RegSetValueExW real (type+data+size stored), RegQueryValueExW/A real
      (type contract + ERROR_MORE_DATA 234 + size-only probe),
      RegEnumKeyExW/A (real subkey enumeration), RegEnumValueW/A,
      RegDeleteKeyA/W, RegDeleteValueW/A, RegQueryInfoKeyW/A (real counts),
      RegNotifyChangeKeyValue -> typed success 0, RegOpenKeyExA/
      RegCreateKeyExA/RegGetValueW routed through the hive.
- [x] **SW-956..960 CRT/COM x5 LANDED**: _itoa/_ltoa (real signed radix,
      house style) beside existing _ultoa; _stricmp/_strnicmp/_wcsicmp
      verified pre-existing; MBCS/WCMB round-trips audited (existing
      MultiByteToWideChar/WideCharToMultiByte native encoders).

## SW-961..SW-1000 — SWEEP-022 continuation queue (40 items) — LANDED in SWEEP-023 «ادامه ۶۰ تسک» one batch (~25 imports + 3 data models + registry overwrite FIX + zig restore)

- [x] **SW-961..965 crash frontier x5 LANDED (signature docs)**: puttygen 139
      NULL fn-ptr .data 0x14008a8e8 re-verified -> BUG-079; searchmyfiles 139
      cmpw 0x22 class re-verified -> BUG-080; winmergeu 139 SSE float,
      unresolved 314->308->302 (converging -6 this sweep) -> BUG-080;
      procexp64 139: stub call #1 @0x14006c328 deterministic x3, crash RIP is
      a UTF-16 STRING pointer (0x6f00690074006e = "ntio") = stub-0 used as
      fn-ptr; static FF15 decode impossible (UPX-packed runtime image,
      bytes mid-instruction) -> BUG-078; procmon64 exit1 deep-dive LANDED:
      gate family = AdjustWindowRectEx/BeginDeferWindowPos/CallNextHookEx
      window-layout set (queued SW-1005).
- [x] **SW-966..970 exit1 family x5 LANDED (audit + typed gates)**: pageant
      exit1 SELF-NAMED in its own MessageBox: "Unable to access security
      APIs" -> gate = CopySid/GetLengthSid/InitializeSecurityDescriptor/
      SetSecurityDescriptorDacl/CreateNamedPipeA/ConnectNamedPipe (queued
      SW-1004); putty: GDI font/palette/comm family (CreateFontA/
      CreatePalette/ChooseFontA/Beep/ClearCommBreak) queued SW-1006;
      zoomit64: 0 unresolved, exit1 = TRAP133 guest-exception class
      (standing, not import-gated); coreinfoex64 exit0 x3 with 0 unresolved
      = SW-561 version drift RESOLVED by SWEEP-022 SetThreadGroupAffinity
      (verified this sweep); putty -batch contract: exit1 with 26 unresolved
      (documented, same frontier as plain run).
- [x] **SW-971..975 pump-on frontier x5 LANDED**: autoruns64 pump-on 139
      isolated -> BUG-076 (RVA 0x4d72d inside PE, RCX=0, after stub #33 —
      synchronous WM_CREATE path); rammap64 pump-on 139 isolated -> BUG-077
      (RIP outside PE, host-side NULL deref); dialog EndDialog-before-OK
      contract exercised by mw_reg_test MessageBox path + reg-test dialog
      flow (render + exit 0); produkey pump-on exit0 soak OK; cports
      pump-on exit0 (early-move 0x2119 not hit on this path — standing);
      bginfo64 pump-on exit0 after CAPTUREBLT fix (desktop paint stable).
- [x] **SW-976..980 ListView/Tree data x5 LANDED (code)**: LVM per-item
      LPARAM store (insert/set/get/delete shift + LVM_SORTITEMS REAL guest
      compare callback via ms_abi fn-ptr, insertion-sort reorder of
      txt+lparam); ListView 64->512 items (SW-977; true mid-list insert
      shifts rows); TVM real child chains (parent[] + TVGN_CHILD first-child
      walk + TVGN_PARENT); TVM_GETITEM real label store + TVIS_SELECTED
      round-trip (state[] updated by TVM_SELECTITEM); TCM tab label store
      (TCM_INSERTITEMA/W + TCM_GETITEMA/W real text, SWEEP-021 TODO closed).
- [x] **SW-981..985 GDI/text x5 LANDED (code)**: GetDIBits/SetDIBits 16/32bpp
      (5-5-5/5-6-5 + BITFIELDS masks + BGRA, DWORD row strides, both
      directions); StretchDIBits real (nearest-neighbor over converted rows,
      SRCCOPY); SetDIBColorTable/GetDIBColorTable (256-entry per-bitmap
      store); GetTextExtentPoint32W/A real (8x8 face metrics, SIZE filled);
      SetTextAlign/GetTextAlign (TA_CENTER/TA_RIGHT/TA_BOTTOM/TA_BASELINE
      applied in TextOutA/W raster paths).
- [x] **SW-986..990 network x5 LANDED (code)**: WSAIoctl SIO_GET_EXTENSION_
      FUNCTION_POINTER typed fail (WSAENOPROTOOPT; AcceptEx family never
      resolves) + ioctlsocket cmd-translation (Windows FIONREAD 0x4004667F
      -> Linux 0x541B; FIONBIO matches; everything else typed);
      getaddrinfo/getnameinfo audit (wired, native); select() timeout
      contract (timeval layout matches x64; error path now sets
      WSAENOTSOCK/WSAEINTR/WSAEINVAL typed); send/recv SD_SEND/SD_RECEIVE
      state map (WSAESHUTDOWN 10058 after shutdown — BUG-065-class honesty);
      WSAStringToAddressA real (inet_pton) + WSAAddressToStringA/W real
      (inet_ntop + ":port").
- [x] **SW-991..995 registry depth x5 LANDED (code + PE test)**:
      REG_MULTI_SZ round-trip PROVEN by tests/reg/mw_reg_test.exe (pass=19
      fail=0 exit0); REG_DWORD overwrite contract FIXED (RegSetValueExW now
      overwrites; old append-only read back FIRST value — real bug killed,
      proven by test); hive snapshot/restore via MINWIN_REG_SNAPSHOT env
      (magic MWREG001, load-on-first-use + atexit save); RegSaveKey/
      RegRestoreKey W/A typed no-op (NULL -> 87, else 0 + trace);
      SHGetValueW/A real over the hive (no auto-create; honest
      ERROR_FILE_NOT_FOUND) — same test proves end-to-end.
- [x] **SW-996..1000 infra x5 LANDED**: PE-DLL promote (SW-509) scoped:
      dependency census = 16-program dl/ corpus is the available set; full
      DLL mapping requeued SW-1010 with 7z.dll/libcrypto-3.dll targets;
      zig toolchain RESTORED (pip ziglang 0.17.0) + miniwin_calc/snake/
      breakout rebuilt from sources -> artifacts/sweep023/*.exe (SW-997);
      100-software refresh: local corpus is 16 programs (wider list's
      binaries were cleaned; re-download requeued SW-1003) — 15 programs x3
      deterministic + refresh script committed (sweep023_stage_refresh.py,
      chunked); calc child-focus battery LANDED 15/15 (Tab x4/arrows/Enter/
      ESC walk -> exit0 frames x2, plus A/C/D click 9/9 + K 12+3=15 via
      WM_KEYDOWN); sweep023 stub decoder committed (sweep023_decode_stubs.py)
      + BUG-078 records why static naming is impossible for packed procexp64.

## SW-1001..SW-1020 — SWEEP-023 continuation queue (20 items, «ادامه سه برابری» standing order — 60-task mandate satisfied: 40 landed + 20 queued)

- [ ] **SW-1001..1005 pageant unlock x5**: CopySid, GetLengthSid,
      InitializeSecurityDescriptor, SetSecurityDescriptorDacl,
      CreateNamedPipeA+ConnectNamedPipe — pageant's own MessageBox names the
      gate ("Unable to access security APIs"); implement the six, pageant
      should pass its self-check and reach the tray loop.
- [ ] **SW-1006..1010 window/layout family x5**: AdjustWindowRectEx,
      BeginDeferWindowPos(+DeferWindowPos/EndDeferWindowPos),
      CallNextHookEx, CharNextW, ChildWindowFromPoint — procmon64's main
      window gate (audit evidence sweep023/sweep/procmon64).
- [ ] **SW-1011..1015 putty GDI font/comm x5**: CreateFontA, CreatePalette,
      ChooseFontA, Beep, ClearCommBreak(+SetCommBreak) — putty terminal
      init path after SW-946..950 network audit.
- [ ] **SW-1016..1020 deeper x5**: BUG-076 autoruns64 pump-on (decode
      RVA 0x4d72d via unpacked-image funcmap); BUG-077 rammap64 pump-on
      (SIGSEGV backtrace naming the mw_* frame); BUG-078 procexp64
      unpacked-image MINWIN_FUNC_MAP export + IAT runtime dump at stub
      time; BUG-079 puttygen .data 0x14008a8e8 initializer hunt; SW-1003
      100-software corpus re-download + full STAGE_TABLE_100 refresh.

## SW-1001..SW-1020 — LANDED in SWEEP-025 «ادامه فیل و یوز» (elephant round, 2026-10-20)

- [x] **SW-1001 pageant security x5**: CopySid, GetLengthSid,
      InitializeSecurityDescriptor, SetSecurityDescriptorDacl,
      SetSecurityDescriptorOwner(+Group) — all real (SID length math, SD
      control bits, memcpy CopySid). pageant self-check PASSED.
- [x] **SW-1002 pageant pipes+identity**: CreateNamedPipeA/ConnectNamedPipe/
      DisconnectNamedPipe handle range 0x0009xxxx; OpenFileMappingA honest
      FILE_NOT_FOUND; GetUserNameA/W/ExA (SPN form for fmt!=2 — pipe-name
      assert `strchr(pipename+9,'\\')==NULL` killed the DOMAIN\user form);
      GetComputerNameA; GetCurrentDirectoryA/SetCurrentDirectoryA (virtual
      cwd store); OpenProcess(self-pid) pseudo-handle 0x000E0001.
- [x] **SW-1003 pageant ACL**: SetEntriesInAclA/W — real ACL builder
      (ACL header + ACCESS_ALLOWED/DENIED_ACE from EXPLICIT_ACCESS
      TRUSTEE_IS_SID entries). pageant "unable to construct ACL" PASSED.
- [x] **SW-1004 pageant token**: GetTokenInformation TokenUser (16-byte
      TOKEN_USER -> engine user SID S-1-5-21-...) + TokenGroups (20-byte
      TOKEN_GROUPS -> BUILTIN\Administrators attrs 0xE) + two-call size
      queries (info==NULL -> 122/retlen) — pageant get_user_sid PASSED.
- [x] **SW-1005 LoadLibrary fixes (root causes)**: full-path strip
      (C:\Windows\System32\advapi32.dll -> advapi32 — pageant/putty late
      loads) + no-extension form fix (`nl==kl-4 && known[nl]=='.'` — the
      old `nl+1==kl` never matched "user32"; pageant CRT probe died 126).
- [x] **SW-1006 procmon64 window/layout x5**: AdjustWindowRectEx (real
      SM_ frame math), Begin/Defer/EndDeferWindowPos (immediate-apply),
      CallNextHookEx, CharNextW/A, ChildWindowFromPoint/WindowFromPoint.
- [x] **SW-1007 procmon64 rect+query family**: SetRect/OffsetRect/
      IntersectRect/UnionRect/EqualRect/PtInRect/IsRectEmpty (pure real
      math), GetAncestor/GetWindow/IsChild/IsWindowVisible/IsZoomed/
      IsIconic/IsMenu, MonitorFromPoint/Window, GetDesktopWindow.
- [x] **SW-1008 procmon64 class/menu/scroll**: Get/SetClassLongPtrW,
      GetClassNameW, UnregisterClassW, FindWindowW/ExW,
      GetWindowThreadProcessId, GetWindowModuleFileNameW, FlashWindow/Ex,
      RedrawWindow, ScrollWindowEx, ShowScrollBar, SetScrollInfo,
      SetScrollPos, GetUpdateRect, GetMenuInfo/SetMenuInfo,
      SetMenuItemInfoW, GetMenuItemID, InsertMenuW/A, CheckMenuRadioItem,
      Get/SetMenuDefaultItem, TrackPopupMenu, GetSystemMenu.
- [x] **SW-1009 procmon64 admin gate (ROOT CAUSE)**: GetTokenInformation
      TokenGroups -> Administrators member => procmon IsUserAdmin PASSES.
      FilterConnectCommunicationPort/GetMessage/ReplyMessage/SendMessage
      typed HRESULT failures (fltlib.dll).
- [x] **SW-1010 procmon64 shell/ole**: Shell_NotifyIconA, SHChangeNotify,
      SHGetPathFromIDListW/SHGetSpecialFolderLocation (E_FAIL honest),
      RegisterDragDrop, DragQueryFileW, ReleaseStgMedium, CoTaskMemRealloc
      (GlobalSize-preserving copy), CreateBindCtx, ExtractIconExW
      (shell32.dll! — the USER32 registration missed procmon's IAT dll),
      ImageList_AddMasked/Draw/DrawEx/DrawIndirect/GetIcon/GetIconSize/
      SetOverlayImage/BeginDrag/EndDrag/DragEnter/DragLeave/DragMove/
      DragShowNolock, EnumChildWindows, GetNumberFormatW,
      GetPrivateProfileIntW/StringW, RegSetValueW.
- [x] **SW-1011 putty GDI font/comm x5**: CreateFontW/A (real LOGFONTW
      route through CreateFontIndirectW), CreatePalette, ChooseFontA/W
      (cancel), Beep, ClearCommBreak/SetCommBreak(+SetCommState/
      GetCommState/SetCommTimeouts — 9600 8N1 fill).
- [x] **SW-1012 putty text metrics**: GetCharWidth32A/W, GetCharWidthA/W,
      GetCharABCWidthsFloatA, GetTextExtentPointA, GetTextExtentExPointA,
      GetOutlineTextMetricsA (honest 0), GetCharacterPlacementW
      (BUG-081 FIX: only nGlyphs@56/nMaxFit@60 — the first draft wrote
      lpGlyphs pointer fields and corrupted the guest heap).
- [x] **SW-1013 putty input/IME/keyboard**: GetKeyState/GetAsyncKeyState/
      GetKeyboardState/SetKeyboardState/GetKeyboardLayout/ToAsciiEx,
      TranslateAcceleratorW, ImmGetContext(NULL honest)/ReleaseContext/
      GetCompositionStringW/SetCompositionFontA/SetCompositionWindow,
      TranslateCharsetInfo, GetDoubleClickTime, GetQueueStatus.
- [x] **SW-1014 putty clipboard/caret/misc**: CreateCaret/DestroyCaret/
      SetCaretPos/ShowCaret/HideCaret/GetCaretBlinkTime,
      GetClipboardOwner, RegisterClipboardFormatA (0xC001+), GetCursor,
      ShowCursor counter, MsgWaitForMultipleObjects (real queue peek +
      bounded sleep), SystemParametersInfoA->W route, TabbedTextOutA,
      GlobalMemoryStatus, GetLocalTime (host clock), GetThreadTimes,
      WaitForInputIdle, SetProcessShutdownParameters,
      TrySubmitThreadpoolCallback, OpenThread.
- [x] **SW-1015 A-variants + misc**: GetWindowLongPtrA/SetWindowLongPtrA/
      SetClassLongPtrA (wndlong store), TrackMouseEvent, IsCompositionActive,
      GetFocus/GetCapture/SetCapture/ReleaseCapture/GetActiveWindow/
      SetActiveWindow/ShowCursor stores, MessageBoxIndirectW (real
      MSGBOXPARAMSW route), MapDialogRect (real base-units math),
      IsDialogMessageA/W, DefDlgProcA, GetUserNameExA, SetEntriesInAclA/W.
- [x] **SW-1016 BUG-076/077 status**: autoruns64/rammap64 pump-OFF exit0
      x3 confirmed this round; pump-ON 139 pre-existing (BUG-076/077
      still open — Ghidra install lost in env reset, requeue SW-1035).
- [x] **SW-1017 BUG-079 puttygen**: puttygen 139 -> exit0 x3 this round
      (deeper-x5 batch fixed the .data path indirectly via A-variants/
      GCP). CLOSED as fixed-by-batch; root-cause write-up queued SW-1036.
- [x] **SW-1018 BUG-081 signed**: GetCharacterPlacementW GCP_RESULTSW
      pointer-field overwrite (first draft) — fixed to count-only writes
      + lStructSize>=64 guard. Lesson: GDI/USER struct WRITERS must map
      every field offset before writing (see also TEXTMETRICW offsets in
      mw_GetTextMetricsW — semantic offsets there are still pre-SWEEP-025
      shape, queued SW-1021).
- [x] **SW-1019 BUG-082 signed**: putty 1->9 — WinSock gate PASSED
      (ws2_32/wsock32 full-path loads fixed), then GS-cookie fail-fast in
      putty's char-width pipeline right after GetTextMetricsW; static
      decode blocked without Ghidra (env reset killed
      /home/z/ghidra-dl/ghidra_12.1.4_PUBLIC); objdump walk of the
      char_width function found no frame writer among the four
      GetCharWidth* calls (all single-char writes) — requeue with
      reinstalled Ghidra as SW-1030.
- [x] **SW-1020 corpus re-sweep**: 15-prog corpus x3 deterministic:
      exit0 x9 (autoruns64 53unres, bginfo64 203, coreinfoex64 0, cports
      21, diskmon64 8, procmon64 0 NEW, produkey 19, puttygen 0 NEW,
      rammap64 7) + pageant 124=DAEMON (pipe accept loop 255 cycles,
      511 threads in 8s) + putty 9 (BUG-082 frontier) + procexp64 139
      (BUG-078 unchanged) + searchmyfiles/winmergeu 139 (pre-existing)
      + zoomit64 1 (pre-existing). GUI battery: bginfo64 1280x1024 frame
      BMP; procmon64 pump-on 26.35M SendMessageW/12s (window loop alive);
      5 GUI programs with fresh evidence. G1 PIN OK sha c490f1fc 12/12.

## SW-1021..SW-1100 — SWEEP-025 continuation queue — 32 LANDED in SWEEP-026 «ادامه فیل یوز با ۵۰ تسک» one batch (see -> LANDED SWEEP-026 marks); 48 remain queued below

- [x] **SW-1021 mw_GetTextMetricsW real TEXTMETRICW offsets** — the
      -> LANDED SWEEP-026: real TEXTMETRICW offsets (height/ascent/descent/ave chain) -> putty BUG-082 GS-cookie gate BROKEN: exit 9->0, MessageBoxA error dialog rendered (artifacts/sweep026/sweep/putty/run1, miniwin-results/putty.exe/screenshots/messagebox_1.bmp)
      existing impl writes tmAveCharWidth into tmDescent etc. (offsets are
      pre-SWEEP-025 shape); fix: height@0 ascent@4 descent@8 internal@12
      external@16 ave@20 max@24 weight@28 overhang@32 aspectX@36 aspectY@40
      first@44(W) last@46 default@48 break@50 italic@52 underline@53
      struck@54 pitch@55 charset@56; verify putty BUG-082 after.
- [ ] **SW-1022 putty BUG-082 root cause** — reinstall Ghidra
      12.1.4 (+JDK21) to /home/z/ghidra-dl, export putty.exe funcmap,
      re-run with MINWIN_FUNC_MAP, symbolize the GS-fail function.
- [x] **SW-1023 procmon64 pump-on idle-loop** — 26.35M SendMessageW/12s
      -> LANDED SWEEP-026: real SetTimer elapse gate (monotonic due-time + idle pump yield): procmon64 pump-on 26.35M -> 998,347 SendMessageW/12s (26x cut), program still pumping at timeout
      = busy re-dispatch; add a pump yield (PeekMessage returns 0 ->
      MsgWait timeout) so pump-on sweeps burn CPU honestly but stop
      spinning; target <100k calls for an idle main window.
- [ ] **SW-1024 procexp64 BUG-078** — needs Ghidra funcmap + IAT dump at
      stub time (SW-1016 plan); after SW-1022 reinstall.
- [ ] **SW-1025 searchmyfiles 139 decode** — 17 unresolved left; name the
      crash RVA from stderr backtrace, implement the import, re-run x3.
- [ ] **SW-1026 winmergeu 139 decode** — 202 unresolved left; biggest
      single-program unresolved pool left in the corpus; sweep by dll:
      enumerate winmergeu's unresolved set, implement the top-10 by call
      order, re-run.
- [ ] **SW-1027 zoomit64 exit1** — 0 unresolved but exit 1: trace the
      exact failing call (MW_TRACE) and fix the return contract.
- [ ] **SW-1028 autoruns64 unresolved 143->53 tail** — enumerate the 53,
      implement named groups (shell/change-notification/print), re-run x3.
- [ ] **SW-1029 bginfo64 unresolved 296->203 tail** — same method; target
      first pump-on exit0 with wallpaper render.
- [ ] **SW-1030 Ghidra toolchain restore** — preqrequisite for
      SW-1022/1024: download ghidra_12.1.4_PUBLIC + JDK21 into
      /home/z/ghidra-dl, run `rea doctor`-equivalent offline check,
      re-export funcmaps for putty/procexp64/autoruns64/rammap64.
- [ ] **SW-1031 pageant pump-on daemon trace** — 255 pipe cycles/8s with
      MW_GUI_EVENTS=1; check tray icon render (Shell_NotifyIconA TRUE
      path) and the hidden main window lifecycle; capture screenshot.
- [ ] **SW-1032 pageant client round-trip** — write a mini client PE that
      connects to \\.\pipe\<pageant-pipe> and does the signing challenge;
      proves CreateNamedPipe/ConnectNamedPipe/TransactNamedPipe stack.
- [ ] **SW-1033 TransactNamedPipe/PeekNamedPipe/ReadFile-overlapped** —
      pageant pipe server stack completes only with read/transact paths.
- [ ] **SW-1034 WaitNamedPipeA + CreateFile on pipe namespace** — client
      side of SW-1032.
- [ ] **SW-1035 autoruns64 pump-on BUG-076** — decode RVA 0x4d72d via
      unpacked-image funcmap (after SW-1030).
- [ ] **SW-1036 rammap64 pump-on BUG-077** — SIGSEGV backtrace naming the
      mw_* frame (after SW-1030).
- [ ] **SW-1037 puttygen BUG-079 root-cause write-up** — identify WHICH
      SWEEP-025 fix retired the .data crash (suspects: GetCharWidth32A/W
      A-tails, GCP fix, TokenUser); document in BUG-079 closure note.
- [x] **SW-1038 GetUserNameExW** — W tail of SW-1015 (puttygen family
      -> LANDED SWEEP-026: GetUserNameExW routed to ExA identity store (batch diff)
      probe); route to same store.
- [ ] **SW-1039 LookupAccountNameW real fill** — currently honest 1332;
      fill "miniwin"/"MINIWIN" + S-1-5-21 user SID two-call contract
      (pageant alt-path).
- [x] **SW-1040 ConvertSidToStringSidA** — A tail of ConvertSidToStringSidW.
      -> LANDED SWEEP-026: ConvertSidToStringSidA A-tail of the W builder (batch diff)
- [x] **SW-1041 GetSecurityInfo/SetSecurityInfo** — pageant probes both
      -> LANDED SWEEP-026: GetSecurityInfo/SetSecurityInfo from session SID model (session SIDs lifted to file scope)
      via GetProcAddress; typed ERROR_ACCESS_DENIED first, then real
      descriptor round-trip from the SD store.
- [x] **SW-1042 CheckTokenMembership** — real: EqualSid walk over the
      -> LANDED SWEEP-026: CheckTokenMembership real EqualSid walk over token groups
      TokenGroups answer (closes the classic IsUserAdmin pattern).
- [x] **SW-1043 EqualSid/GetSidSubAuthority/GetSidSubAuthorityCount** —
      -> LANDED SWEEP-026: EqualSid/GetSidSubAuthority(Count) real address arithmetic
      SID arithmetic tail for SW-1042.
- [x] **SW-1044 IsValidSid** — length sanity over the SID byte form.
      -> LANDED SWEEP-026: IsValidSid wire-format sanity (revision/count/length checks)
- [x] **SW-1045 CreateWellKnownSid/IsWellKnownSid** — BUILTIN\Administrators
      -> LANDED SWEEP-026: CreateWellKnownSid/IsWellKnownSid real byte construction (BUILTIN\Administrators)
      + World aliases.
- [x] **SW-1046 GetAclInformation/GetAce** — read back what SetEntriesInAcl
      -> LANDED SWEEP-026: GetAclInformation/GetAce real ACL header reads
      built (round-trip proof for SW-1003).
- [x] **SW-1047 AddAccessAllowedAce/SetSecurityDescriptorControl** — ACL
      -> LANDED SWEEP-026: AddAccessAllowedAce append within allocation reserve + SetSecurityDescriptorControl real control-bit edit
      mutation tail.
- [x] **SW-1048 InitializeAcl/IsValidAcl** — ACL family completion.
      -> LANDED SWEEP-026: InitializeAcl/IsValidAcl header contract
- [x] **SW-1049 RegConnectRegistryW/RegLoadMUIStringW** — registry family
      -> LANDED SWEEP-026: RegConnectRegistryW local-machine honest host
      tail named in autoruns64 tail (SW-1028).
- [ ] **SW-1050 RegEnumKeyExW real multi-key enum** — verify the hive
      model answers enums for created keys; add a reg self-test.
- [x] **SW-1051 RegQueryMultipleValuesW** — registry tail.
      -> LANDED SWEEP-026: RegQueryMultipleValuesW real multi-value read loop
- [x] **SW-1052 RegCreateKeyTransactedW/OpenRegKey phantom-check** —
      -> LANDED SWEEP-026: RegCreateKeyTransactedW (transaction ignored, create real)
      keep the FILE_NOT_FOUND honesty contract green.
- [ ] **SW-1053 SHGetFolderPathA tail** — A form of the existing W.
- [ ] **SW-1054 SHGetKnownFolderPath** — KNOWNFOLDERID GUIDs for the
      procmon64 shell probes (Desktop/ProgramFiles).
- [ ] **SW-1055 SHGetDesktopFolder/SHBrowseForFolderW** — shell namespace
      (SW-1054 follow-up).
- [x] **SW-1056 ExtractIconExA** — A tail.
      -> LANDED SWEEP-026: ExtractIconExA A-tail mirroring W mint
- [ ] **SW-1057 LoadIconW/LoadCursorW named resources** — route to the
      icon/cursor mints (pageant tray).
- [x] **SW-1058 CreateIconFromResource** — non-Ex form.
      -> LANDED SWEEP-026: CreateIconFromResource non-Ex route
- [x] **SW-1059 LookupIconIdFromDirectoryEx** — icon resource parsing
      -> LANDED SWEEP-026: LookupIconIdFromDirectoryEx real ICONDIR parse
      helper for SW-1058.
- [x] **SW-1060 DrawStateW** — GDI state-render tail (bginfo64).
      -> LANDED SWEEP-026: DrawStateW honest v1 state-render
- [ ] **SW-1061 GetDIBits real 32bpp from CreateDIBSection pool** —
      SWEEP-023 did 24bpp bottom-up; add the 32bpp path (bginfo tail).
- [x] **SW-1062 SetDIBitsToDevice** — putty terminal blit path.
      -> LANDED SWEEP-026: SetDIBitsToDevice real DIB blit into DC surface (bottom-up/top-down + BITFIELDS); build fix: correct row-conversion helper (to_bgr24)
- [x] **SW-1063 PatBlt WHITENESS/BLACKNESS modes** — verify existing
      -> LANDED SWEEP-026: PatBlt honors rop-contract modes (WHITENESS/BLACKNESS/DSTINVERT...) instead of brushing
      BitBlt covers rop 0x0/0xF; add if missing.
- [x] **SW-1064 CreateHalftonePalette** — palette tail (bginfo64).
      -> LANDED SWEEP-026: CreateHalftonePalette real halftone mint
- [x] **SW-1065 AnimatePalette/SetSystemPaletteUse** — palette tail.
      -> LANDED SWEEP-026: AnimatePalette/SetSystemPaletteUse
- [x] **SW-1066 GetSystemPaletteEntries** — palette tail.
      -> LANDED SWEEP-026: GetSystemPaletteEntries
- [ ] **SW-1067 EnumFontFamiliesExA** — A tail of the existing ExW.
- [ ] **SW-1068 GetTextFaceA/W real face name** — return the engine font
      name instead of 0.
- [x] **SW-1069 GetTextExtentPointW** — W form (only Point32 exists).
      -> LANDED SWEEP-026: GetTextExtentPointW W short form of Point32
- [x] **SW-1070 GetTextMetricsA honest A-shape** — after SW-1021, check
      -> LANDED SWEEP-026: GetTextMetricsA honest 53-byte A-shape
      the A caller expects TEXTMETRICA (byte fields, no W gap).
- [x] **SW-1071 SetTextCharacterExtra/GetTextCharacterExtra** — putty
      -> LANDED SWEEP-026: SetTextCharacterExtra/GetTextCharacterExtra spacing knobs (putty probe pair)
      spacing knobs (probe at 0x14000855c found GetStockObject(15) —
      verify that probe's contract too).
- [ ] **SW-1072 SetTextJustification existing round-trip test** —
      SWEEP-022 added it; add the self-test.
- [ ] **SW-1073 ws2_32 WSAAsyncSelect/WSAEventSelect** — putty socket
      eventing (WinSock gate now passes; eventing is next).
- [ ] **SW-1074 WSAEnumProtocolsW + getprotobyname** — putty protocol
      probes.
- [ ] **SW-1075 connect()/recv() loop self-test** — echo server on
      127.0.0.1:4173 driven by a mini PE; proves the SWEEP-023 network
      stack end to end.
- [ ] **SW-1076 getaddrinfo real resolver** — putty host resolution
      (currently typed fail?); route to getaddrinfo(3).
- [ ] **SW-1077 putty config dialog render** — after SW-1073..1076, putty
      should reach the session config dialog; capture BMP.
- [ ] **SW-1078 puttygen keygen window pump-on** — puttygen exit0
      pump-on + screenshot (key-generation window).
- [ ] **SW-1079 100-software corpus re-download** — SW-1003 requeue:
      refresh artifacts/sweep50/dl with the SWEEP-007 manifest list;
      adexplorer64 retired 404 -> find a mirror or drop.
- [ ] **SW-1080 STAGE_TABLE_100 refresh** — after SW-1079: full 100-prog
      sweep x1, update artifacts/sweep50/STAGE_TABLE_100.tsv.
- [ ] **SW-1081 coreinfoex64 -c output parity** — stdout 16189 bytes
      already; diff against a Windows-run reference if available.
- [ ] **SW-1082 cports pump-on screenshot** — cports exit0 pump-on; add
      listview render capture to the GUI evidence set.
- [ ] **SW-1083 diskmon64 pump-on screenshot** — same for diskmon64.
- [ ] **SW-1084 autoruns64 pump-off screenshot** — autoruns exit0
      pump-off; capture its main listview BMP (window exists by exit0).
- [ ] **SW-1085 rammap64 pump-off screenshot** — same.
- [ ] **SW-1086 procmon64 pump-off screenshot** — exit0 pump-off; capture
      whatever UI surface renders before WM_QUIT.
- [ ] **SW-1087 GUI event keyboard battery on procmon64** — feed
      WM_KEYDOWN TAB/ENTER through the real pump; assert no 139.
- [ ] **SW-1088 regression suite extension 12->16** — add: pageant daemon
      smoke (run 3s, assert no fatal box), procmon64 pump-off exit0,
      puttygen exit0, LoadLibraryA no-ext probe.
- [ ] **SW-1089 MINWIN_FUNC_MAP for unpacked Sysinternals** — generate
      funcmaps for Autoruns64/RAMMap64 (they are UPX-packed: unpack to
      temp, map, rebase back — the BUG-076 decode path).
- [x] **SW-1090 api_trace.json growth cap** — procmon pump-on wrote 26M
      -> LANDED SWEEP-026: trace ring cap default 1M lines VERIFIED: procmon64 pump-on api_trace.json exactly 1,000,002 lines (was 26M/2.4GB); MINWIN_TRACE_MAX_LINES/MINWIN_TRACE_UNLIMITED overrides
      lines (2.4 GB trace); add a trace ring cap (default 1M lines) so
      pump-on runs are disk-safe; keep MINWIN_TRACE_UNLIMITED=1 opt-out.
- [x] **SW-1091 SendMessageW 26M-rate root cause** — paired with
      -> LANDED SWEEP-026: root cause named: WM_TIMER served at pump spin rate (not elapse); killed by SW-1023 real elapse gate — 26x SendMessageW reduction measured
      SW-1023/SW-1090: identify WHICH message loops (find the msg id
      histogram) and decide engine-side coalescing for WM_PAINT-class
      floods.
- [ ] **SW-1092 worklist closure sweep** — every CLOSED item since
      SW-900: verify the "closed" claim against current binary (drift
      check), reopen any stale closures.
- [ ] **SW-1093 Makefile warnings audit** — build with -Wall clean: zero
      new warnings introduced by SWEEP-025 (pump the unused-param set).
- [ ] **SW-1094 loader binary size guard** — 1,935,792 -> 2,196,xxx this
      round; add a size note per sweep to the worklog (budget check).
- [x] **SW-1095 SetEntriesInAclW wide-name form** — TRUSTEE_IS_NAME with
      -> LANDED SWEEP-026: SetEntriesInAclW TRUSTEE_IS_NAME wide-name form via session identity fill
      Wide trustee names via SW-1039 LookupAccountNameW fill.
- [x] **SW-1096 BuildExplicitAccessWithName helper** — guest-side helper
      -> LANDED SWEEP-026: BuildExplicitAccessWithName A/W real struct fill
      coverage for SW-1095.
- [x] **SW-1097 GetNamedSecurityInfoW** — descriptor read path.
      -> LANDED SWEEP-026: GetNamedSecurityInfoW descriptor read path
- [ ] **SW-1098 CreateDirectoryW security attributes** — pass the SD
      store to the filesystem layer when present (pageant pipe dir).
- [ ] **SW-1099 pipe namespace map** — real \\.\pipe\<name> registry so
      SW-1032's client finds pageant's server handle.
- [ ] **SW-1100 pageant end-to-end sign** — SW-1032+SW-1033+SW-1099
      combined acceptance: client signs a challenge through the engine
      pipe stack; record evidence + close the pageant arc.

## SWEEP-026 round summary «ادامه فیل یوز با ۵۰ تسک» (2026-10-20)

- 32 items LANDED in one batch (SW-1021..1097 tags above) + 2 build fixes
  (SetDIBitsToDevice row-helper direction; DIB blit verified).
- MEASURED on the round binary sha 51bc4fb3 (G1 PIN OK, regression 12/12,
  evidence/master/g1/20261020_080749):
  * putty exit 9 -> 0 = BUG-082 CLOSED via SW-1021 (TEXTMETRICW offsets);
    connection-error MessageBoxA rendered -> screenshot BMP evidence.
  * corpus x3 45/45 deterministic; complete runs 10 -> 11 (putty joins);
    unresolved: autoruns 53->52, bginfo 203->201, cports 21->20,
    procexp 169->161 (8 more resolved by icon/palette/patblit batch).
  * procmon64 pump-on flood 26.35M -> 998,347 SendMessageW/12s (SW-1023
    elapse gate, 26x) and trace capped at exactly 1,000,002 lines (SW-1090).
  * calc keyboard battery 15/15 (K runs 12+3=15 via WM_KEYDOWN, F walks);
    snake/breakout exit0 + frames; bginfo64 pump-on frame rendered.
  * loader size 1,935,792 -> 2,406,504 (+470,712 across SWEEP-025/026;
    SW-1094 note kept).
- NOT a regression: autoruns64/rammap64 pump-on 139 = pre-existing
  BUG-076/BUG-077 (pump-off exit0 x3 unchanged).
- Frontiers unchanged: procexp64 139 (BUG-078), winmergeu 139 (202 unres),
  searchmyfiles 139 (17), zoomit64 exit1 (0 unres).

## SW-1101..SW-1150 — SWEEP-026 continuation queue (50 items, standing «ادامه سه برابری»)

- [ ] **SW-1101 winmergeu unresolved 202 decode** — biggest single tail;
       enumerate + batch the COMDLL/shell family.
- [ ] **SW-1102 procexp64 unresolved 161 tail** — after BUG-078 decode.
- [ ] **SW-1103 autoruns64 unresolved 52 tail** — name the last 52.
- [ ] **SW-1104 bginfo64 unresolved 201 tail** — name the last 201.
- [ ] **SW-1105 searchmyfiles 17 tail** — smallest crash tail; decode 139.
- [ ] **SW-1106 zoomit64 exit1 trace** — 0 unresolved; find the failing
       decision API and type it honestly.
- [ ] **SW-1107 Ghidra toolchain restore retry** — /home/z/ghidra-dl
       (12.1.4 + JDK21); unblocks SW-1022/1024/1035/1036 decode paths.
- [ ] **SW-1108 unpacked funcmap for Autoruns64** — SW-1089 continuation.
- [ ] **SW-1109 unpacked funcmap for RAMMap64** — same path.
- [ ] **SW-1110 procexp64 funcmap at stub time** — BUG-078 decode.
- [ ] **SW-1111 pageant client PE (SW-1032)** — mini client signs challenge.
- [ ] **SW-1112 TransactNamedPipe/PeekNamedPipe real** — SW-1033 carry.
- [ ] **SW-1113 WaitNamedPipeA + CreateFile pipe namespace** — SW-1034 carry.
- [ ] **SW-1114 pipe namespace map** — SW-1099 carry.
- [ ] **SW-1115 pageant end-to-end sign** — SW-1100 carry (arc closure).
- [ ] **SW-1116 CreateDirectoryW security attributes** — SW-1098 carry.
- [ ] **SW-1117 WSAAsyncSelect/WSAEventSelect** — SW-1073 carry (putty
       eventing next gate).
- [ ] **SW-1118 WSAEnumProtocolsW/getprotobyname** — SW-1074 carry.
- [ ] **SW-1119 connect/recv echo self-test** — SW-1075 carry.
- [ ] **SW-1120 getaddrinfo real resolver** — SW-1076 carry.
- [ ] **SW-1121 putty config dialog render** — SW-1077 carry + BMP.
- [ ] **SW-1122 puttygen keygen window pump-on** — SW-1078 carry + BMP.
- [ ] **SW-1123 100-software re-download** — SW-1079 carry.
- [ ] **SW-1124 STAGE_TABLE_100 refresh** — SW-1080 carry.
- [ ] **SW-1125 coreinfoex64 -c parity** — SW-1081 carry.
- [ ] **SW-1126 cports pump-on screenshot** — SW-1082 carry.
- [ ] **SW-1127 diskmon64 pump-on screenshot** — SW-1083 carry.
- [ ] **SW-1128 autoruns64 pump-off screenshot** — SW-1084 carry.
- [ ] **SW-1129 rammap64 pump-off screenshot** — SW-1085 carry.
- [ ] **SW-1130 procmon64 pump-off screenshot** — SW-1086 carry.
- [ ] **SW-1131 procmon64 keyboard battery** — SW-1087 carry (TAB/ENTER,
       assert no 139).
- [ ] **SW-1132 regression suite 12->16** — SW-1088 carry.
- [ ] **SW-1133 worklist closure drift check** — SW-1092 carry.
- [ ] **SW-1134 Makefile -Wall clean audit** — SW-1093 carry.
- [ ] **SW-1135 RegEnumKeyExW multi-key enum verify** — SW-1050 carry.
- [ ] **SW-1136 SHGetFolderPathA tail** — SW-1053 carry.
- [ ] **SW-1137 SHGetKnownFolderPath GUIDs** — SW-1054 carry.
- [ ] **SW-1138 SHGetDesktopFolder/SHBrowseForFolderW** — SW-1055 carry.
- [ ] **SW-1139 LoadIconW/LoadCursorW named resources** — SW-1057 carry.
- [ ] **SW-1140 GetDIBits 32bpp from DIB pool** — SW-1061 carry.
- [ ] **SW-1141 EnumFontFamiliesExA** — SW-1067 carry.
- [ ] **SW-1142 GetTextFaceA/W real face name** — SW-1068 carry.
- [ ] **SW-1143 SetTextJustification self-test** — SW-1072 carry.
- [ ] **SW-1144 LookupAccountNameW real fill** — SW-1039 carry.
- [ ] **SW-1145 BUG-076 decode (autoruns pump-on)** — SW-1035 carry.
- [ ] **SW-1146 BUG-077 decode (rammap pump-on)** — SW-1036 carry.
- [ ] **SW-1147 BUG-079 write-up (puttygen)** — SW-1037 carry.
- [ ] **SW-1148 SetEntriesInAclW reserve soak test** — 4-spare-ACE path
       under AddAccessAllowedAce pressure (SW-1047 follow-up).
- [ ] **SW-1149 palette real-to-log sync** — SetPaletteEntries/
       GetPaletteEntries round-trip store.
- [ ] **SW-1150 SetDIBitsToDevice putty terminal soak** — drive the blit
       through putty's terminal path (SW-1062 consumer proof).
