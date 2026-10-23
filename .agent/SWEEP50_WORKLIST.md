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

- [x] ** winmergeu unresolved 202 decode** — biggest single tail; -> LANDED SWEEP-027: winmergeu unresolved 202->150 this round (-52: theme/shlwapi/CRT/user32 batch); crash decode still open
       enumerate + batch the COMDLL/shell family.
- [ ] **SW-1102 procexp64 frontier** (SWEEP-039 PROGRESS, final overrun open):
       LANDED this round: (a) NtQuerySystemInformation class 64/76
       SystemHandleInformation/SystemExtendedHandleInformation v1 — the plane
       procexp builds its handle view on; our INVALID_INFO_CLASS answer (seen
       once at run-1 trace) steered the guest into fallback paths; v1
       enumerates g_handle_table live payloads, two-pass
       0xC0000004+retlen contract honored; (b) advapi32 EnableTraceEx/Ex2/
       EnableTrace/RegisterTraceGuidsW symbols — GetProcAddress(EnableTraceEx)
       returned NULL right before the fatal window (real advapi32 always
       exports it); family-typed 1168 semantics. STATIC DECODE (evidence for
       the final fix): embedded .NET CLR counter names at .rdata
       0x14015ad10/40/70 ('Total # of contentions', '# bytes in all heaps',
       '# Gen 0 collections'); name-table search loop at 0x1400a0486
       (for i<r15->count: _wcsicmp(name, table[i]) via call 0x1401025d8);
       crash stack holds the names PACKED back-to-back — copy overrun eats the
       frame RA with 'ntio' of 'contentions' (SWEEP-033 class decode stands).
       NEXT LEVER: guest-stack write instrumentation to catch the offending
       copy loop (worklist SWEEP-036 note).
- [x] ** autoruns64 unresolved 52 tail** — name the last 52. -> LANDED SWEEP-027: autoruns64 unresolved 52->26 (-26: WinHttp family + wow64 + threadpool timers + SearchPath/QueryDosDevice/K32)
- [x] ** bginfo64 unresolved 201 tail** — name the last 201. -> LANDED SWEEP-027: bginfo64 unresolved 201->145 (-56: uxtheme classic-fallback set + GDI+ zero-codec + SNMP + ole tails)
- [x] ** searchmyfiles 17 tail** — smallest crash tail; decode 139. -> LANDED SWEEP-027: searchmyfiles CRASH->exit0 x3 + pump-on exit0 (CRT tail _wtoi/_wcslwr/_itow/_wcmdln/_msize/WNet/SHFileOperation) — NEW complete run #12
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
- [x] ** getaddrinfo real resolver** — SW-1076 carry. -> LANDED SWEEP-027: getaddrinfo already upstream (drift check); WinHttp 12-family typed no-network (12029/12180) landed
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
- [x] ** worklist closure drift check** — SW-1092 carry. -> LANDED SWEEP-027: drift check run: TranslateAcceleratorW/IsAppThemed/IsThemeActive/SetWindowTheme/getaddrinfo already upstream — batch adjusted; 45 genuinely missing landed
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

## SWEEP-027 round summary «ادامه یوز یوز مثل فیل و واگن های قطار» (2026-10-20)
- Env reset detected post-push (local 55b3982 vs origin 3cd11c0): reconciled
  from origin (no force-push), deterministic rebuild sha 51bc4fb3 EXACT.
- One wagon batch = 45 imports + _wcmdln data import + UTF-16 cmdline fill
  (149 ENTRY lines incl. dll aliases): accel store (Create/Copy/Destroy/
  TranslateAcceleratorA real), menu tails, DrawTextExW, DefFrame/MDI,
  INI A-tails + WritePrivateProfile W/A, RegEnumKeyW, SCM service set
  (OpenService/QueryServiceStatus real STOPPED/CtrlDispatcher 1063),
  wow64 no-op pair, threadpool timer cookies, WinHttp 12-family typed
  12029/12180, shlwapi real strings (StrChrW/StrStrIW/StrCmpIW/Logical/
  Trim/FormatKBSize/ByteSize), CRT real (_strcmpi/_mbscmp/_wtoi(64)/
  _wcslwr/_itow/_ui64tow/_msize/wcsncat), shell/ole typed, GDI+ zero-codec,
  SNMP real, uxtheme NULL-classic-fallback set, GlobalReAlloc real,
  GetTempFileNameA real file creation, GetTimeFormatA real.
- MEASURED on sha 0f4b4773 (G1 PIN OK 12/12, evidence/master/g1/20261020_085912):
  * searchmyfiles 139 -> exit0 x3 = complete runs 11 -> 12
  * unresolved: autoruns 52->26, bginfo 201->145, cports 20->6, diskmon 8->5,
    produkey 19->10, rammap 7->4, procexp 161->128, winmergeu 202->150
  * corpus x3 45/45 deterministic, ZERO regressions
  * calc battery 15/15; putty/bginfo64 pump-on frames
- Loader size 2,406,504 -> 2,497,432 (+90,928).
- zoomit64 exit1 (0 unresolved) unchanged — trace-decode task stays queued.


## SWEEP-028 round summary «ادامه یوز قطار فیل ۱۰۰ تسک جوا و پوش» (2026-10-20)
- SW-1101 DOUBLE DECODE: (a) CreateDIBSection top-down 32bpp underflow -> MWBmp
  view-format engine fix (SW-1101a); (b) OLEAUT32 by-ordinal map + typed
  LoadTypeLib (SW-1101b). winmergeu 139->9, imports 150->0.
- ~155 imports landed; unresolved totals 464->54 (-410); produkey/diskmon64/
  rammap64/searchmyfiles/autoruns64 at zero-unresolved.
- Gates: G1 PIN sha 2bfe6fc3 12/12 (20261020_105600); corpus x3 16/16
  deterministic, zero regressions. Commit d2d5e46 pushed (no force).
- Next: SW-1102 procexp zero-stub frontier decode; winmergeu exit-9 decode;
  bginfo64 54-tail wagon; SW-1106 zoomit exit1 typing.

## SW-1101..SW-1150 queue marks — LANDED in SWEEP-029 «قانون کلی قطار» (2026-10-20)
- [x] ** bginfo64 54-tail (SWEEP-028 next wagon)** — -> LANDED SWEEP-029: 54 unique
       unresolved -> 0 (GDI raster/region family real: InvertRect/FillRgn/FrameRgn/
       SetPixelV real pixels, CreatePolygonRgn/CreateRoundRectRgn/CreateDIBitmap/
       CreateHatchBrush/GetObjectType/AlphaBlend->StretchBlt; USER32 tails: MapVirtualKeyW/
       ExW, CharUpperBuffW real, IsCharLowerW real, GetWindowRgn NULLREGION, NotifyWinEvent,
       SetSysColors, GetProcessWindowStation, DestroyAcceleratorTable; K32 REAL /proc enum:
       K32EnumProcesses/K32EnumProcessModules/K32GetModuleBaseNameW; Net* static models:
       NetWkstaUserGetInfo/NetWkstaGetInfo/NetServerGetInfo; printer/eventlog/service cookies:
       OpenPrinterW/ClosePrinter, Open/Read/CloseEventLog, CreateServiceW typed ACCESS_DENIED +
       RegisterServiceCtrlHandlerW/SetServiceStatus; Safer identity pipeline trio; LSA typed
       NOT_IMPLEMENTED; env-block cookies; Imm/SHAppBar/OleLockRunning/CreateILockBytesOnHGlobal/
       PlaySoundW typed)
- [x] ** winmergeu 4 ordinal tail (counted nameless)** — -> LANDED SWEEP-029: SHELL32.155
       SHCreateDirectory (real mkdir), SHELL32.701 CDefFolderMenu_Create2 (typed E_FAIL),
       SHLWAPI.2 ParseURLW (real scheme parse), SHLWAPI.12 SHCreateMemStream (typed NULL,
       mirrors CreateStreamOnHGlobal honesty); names via wine-mirror spec fetch 2026-10-20
- [x] ** bginfo64 ODBC32 ordinal tail (9 -> 0 unresolved)** — -> LANDED SWEEP-029b: ODBC32
       9/24/31/75/136/141 = SQLDescribeCol/SQLAllocHandle/SQLFreeHandle/SQLSetEnvAttr/
       SQLGetDiagRecW (NO_DATA)/SQLSetConnectAttrW typed-honest; OLEAUT32 25/114/418 =
       SafeArrayPutElement/VarBstrFromDate (real BSTR)/DllUnregisterServer

## SW-1151..SW-1200 — SWEEP-029 continuation queue (standing «قانون کلی قطار»: like a train, dozens of wagons)
- [x] **SW-1151 winmergeu gs fail-fast decode** — -> LANDED SWEEP-030 (ENGINE FIX, full
       chain decoded): (a) failing fn = Frame[7] func RVA 0x2279a0, personality =
       __GSHandlerCheck_EH shim 0x319158 -> GS-check core 0x319070 (reads stored cookie
       at [EstFrame + LSDA_dword1(0x572&~7=0x570)], tail-jmps __security_check_cookie
       0x318660) -> __report_gsfailure 0x318730 (IsProcessorFeaturePresent(23) gate ->
       int 0x29 code=2) — cookie slot = frame_base + 0x570 (prologue: rbp=entry-0x4a8,
       cookie=[rbp+0x470]=entry-0x38, stored global^frame_base, frame=0x5a8);
       (b) ROOT CAUSE: our EstablisherFrame was the POST-UNDO RSP (function-entry RSP,
       +0x5a8 off) — ReactOS GetEstablisherFrame: FrameReg==0 -> Context->Rsp BEFORE the
       codes run (call-boundary RSP = frame base), FrameReg!=0 -> live FP - FrameOffset*16;
       (c) FIX: ms_est computed in seh_internal_virtual_unwind (+out param), passed as
       dc.EstablisherFrame + handler RDX; mw_RtlVirtualUnwind same rules + RETURN-ADDRESS
       pop (ctx.Rip=[new_rsp], rsp+=8; machframe exempt) — Windows contract for guest
       walkers (winmergeu walker loop-of-2 previously decoded the same pc twice with
       marching est); mw_RtlUnwindEx resume = tgt_frame DIRECTLY (old entry-alloc
       cancellation removed; GCC-bridge 0x21474343 intercept resume = est_va directly);
       BUG-031 walk stop = pre_rsp == tgt_frame; (d) fastfail honesty:
       IsProcessorFeaturePresent(23=PF_FASTFAIL_AVAILABLE)=1 + crash_handler detects
       guest `cd 29` (#GP->SIGSEGV, RIP at instr) -> [FASTFAIL] trace + exit 9.
       RESULT: winmergeu exit 9 -> 133 — GS check PASSES, guest C++ EH chain runs
       end-to-end (10 frames, guest personalities decide), terminate->int3 (guest's own
       uncaught-exception path). G1 re-pinned, corpus 16/16 deterministic ZERO regressions.
- [x] **SW-1151b winmergeu post-GS frontier** — -> REGISTERED SW-1201 (see below)
- [x] **SW-1201 winmergeu startup uncaught C++ exception** — -> LANDED SWEEP-031:
       DOUBLE root cause. (1) SetWindowsHookExW(5) returned NULL -> MFC threw
       CMemoryException (ThrowInfo 0x1404fd7b0 -> CMemoryException/CSimple/
       CException/CObject/void*) that no frame catches BY DESIGN (Frame[7] GS
       shim: GS_DATA 0x572 bit0=0 gates phase-1 FH4 off — real compiler data,
       not an engine bug). Fix: real hook machinery — live HHOOK table
       0x00B30001+i (low16=0 handle pattern), WH range validation (-1..14,
       MSGFILTER needs tid), Unhook validates handles (bad -> 1400), CallNext
       unchanged. (2) The catch in func 0x298db0 THEN matched but resumed ONE
       BYTE EARLY (0x140298df4, mid `e8 rel32` -> SIGSEGV): the dispatcher
       subtracted 1 from dc.ControlPc AND ctx.Rip for ALL personalities (GCC
       hack) — FH4 echoed ctx.Rip-1 back as RtlUnwindEx TargetIp. Fix: the -1
       is GCC-only (ER 0x20474343/0x21474343). MEASURED sha 83df098e:
       winmergeu 133 -> 124 = LIVE GUI DAEMON (986K Sleep(0) message-loop
       lines; FH4 catch -> RtlUnwindEx target_ip=0x140298df5 resume
       e/sweep031/winmergeu_sw1201_key_lines.txt); hook trace: 3 installs
       (ids -1/7/5 -> 0xb30001/2/3) + Unhook(0xb30002) TRUE (id=7).
- [x] **SW-1202 zstd explicit output-arg ignored** — -> LANDED SWEEP-031:
       premise was a MISDIAGNOSIS — the guest's own --help proves zstd v1.5.6
       has NO positional-output form (usage: `[INPUT... | -] [-o OUTPUT]`);
       `zstd -f in out` = TWO INPUTS, and "can't stat out -- ignored" for a
       nonexistent 2nd input is REAL zstd behavior (FIO_openSrcFile). The real
       defects, both fixed: (a) _stat64/_fstat64/_stat64i32 never bridged host
       errno into the guest mirror (stale EPERM); (b) `strerror` was bound
       DIRECTLY to SysV glibc — guest ms_abi calls passed errno in RCX while
       glibc read RDI (garbage string "Operation not permitted" for errno=2).
       Now: mw_strerror ms_abi wrapper + errno bridges; zstd prints the
       correct "No such file or directory"; canonical/explicit -o/decompress
       round trips re-verified on sha 83df098e.
- [ ] **SW-1152 RtlCaptureContext SSE/debug regs** — FloatSave + Xmm + Dr stay zeroed;
       fill from sigcontext if a consumer appears.
- [x] **SW-1153 ODBC mini-driver** — -> LANDED SWEEP-034: REAL sqlite3-backed
       (host libsqlite3, Makefile -lsqlite3): DBC=sqlite3* (0x00B8 table),
       STMT=sqlite3_stmt*+cursor (0x00B9); DriverConnectW opens real files
       (DATABASE=/DBQ=/Data Source= keywords, sqlite3_open16 UTF-16),
       ExecDirectW prepare16_v2+step (DML affected recorded), Fetch step,
       GetData real conversion (C_WCHAR/C_CHAR/C_SLONG/C_LONG/C_DOUBLE,
       SQL_NULL_DATA), BindCol, NumResultCols, RowCount (changes/-1),
       Disconnect, DescribeCol (name16+value-type), DiagRecW (HY000 +
       sqlite errmsg16). ENV keeps the 0x00031000 cookie pool. No pool
       guest exercises ODBC in-run yet — honest note; corpus gates green.
- [x] **SW-1154 SafeArray real element store** — -> LANDED SWEEP-031: real
       SAFEARRAY model (MwSa 0x00B40001+i): SafeArrayCreate allocates
       vt-sized store (20 VT codes, 8 dims, 16M elem cap), LBound/UBound real
       from bounds, AccessData -> pvData, Put/GetElement flat row-major with
       per-dim range checks (DISP_E_BADINDEX), Destroy frees; BSTR/UNKNOWN
       copy the guest pointer (SW-1148 pattern). +SafeArrayGetElement/Destroy
       ENTRYs. winmergeu's ordinal path did not reach PutElement in-run (0
       calls, no regression).
- [x] **SW-1155 CreatePolygonRgn true polygon raster** — -> LANDED SWEEP-031:
       even-odd scanline (half-open edge rule, per-row crossing sort, spans
       coalesced vertically into the 16-rect region list; degenerate ->
       NULLREGION; FillRgn/FrameRgn paint the spans = real raster; fill-mode
       ALTERNATE/WINDING share even-odd v1). bginfo64 exercises it (exit 0 x3).
- [x] **SW-1157 GetDIBits real for 32bpp topdown** — superseded: consumers
       ride the MWBmp view format (SWEEP-028) + SW-1158 CBM_INIT copy.
- [x] **SW-1158 CreateDIBitmap CBM_INIT pixel copy** — -> LANDED SWEEP-031:
       CBM_INIT + BI_RGB 24/32bpp (bottom-up AND top-down via biHeight sign)
       copies DIB [B,G,R] rows into the MWBmp view through mwb_set (4-align
       stride honored); unsupported formats keep the blank-v1 path with a
       trace saying so.
- [x] **SW-1159 AlphaBlend real blend** — -> LANDED SWEEP-031: AC_SRC_OVER
       enforced, per-channel out = (src*a + dst*(255-a) + 127)/255 with
       a = SrcConstantAlpha (SCA=255 fast path = SRCCOPY); AC_SRC_ALPHA
       per-pixel alpha collapses to opaque in the RGB view (documented v1).
       bginfo64 exercises it (exit 0 x3).
- [x] **SW-1160 K32EnumProcessModules real module list** — -> LANDED SWEEP-031:
       /proc/self/maps executable file-backed mappings -> coalesced module
       cookies 0x00B50001+i; guest image leads the list; GetModuleBaseNameW
       resolves image + cookies. procexp64's pre-crash path (BUG-078) does
       not reach it yet; no corpus delta.
- [ ] **SW-1156 ExtFloodFill seed-fill raster** — scanline seed fill on MWBmp views.

## SWEEP-031 round summary «ادامه» — cheetah speed, owl eye, elephant load, train of wagons (2026-10-20)
- SW-1201 LANDED (DOUBLE root cause, first MSVC C++ EH catch-AND-RESUME in engine
  history): (1) SetWindowsHookExW returned NULL ("no hook machinery") -> MFC
  threw CMemoryException that no frame catches BY DESIGN (decoded ThrowInfo
  0x1404fd7b0: CMemoryException/CSimpleException/CException/CObject/void*;
  Frame[7] GS shim GS_DATA 0x572 bit0=0 gates phase-1 FH4 off — real compiler
  data). FIX: real hook machinery — HHOOK table 0x00B30001+i, WH range
  validation (-1..14, MSGFILTER thread-scoped), Unhook handle validation
  (foreign handle -> ERROR_INVALID_HANDLE 1400). (2) With hooks green the
  guest's SECOND throw (a real internal C++ exception) matched a catch in
  func 0x298db0 via guest __CxxFrameHandler4 (0x14031ffc8 — FH4 packed
  FuncInfo, tables at image+0x3b6340/0x3b6350 confirmed in-binary) but the
  engine's unconditional `ControlPc-1 / ctx.Rip-1` GCC hack made FH4 echo
  TargetIp = RA-1 = 0x140298df4 (mid `e8 rel32`) -> SIGSEGV on resume. FIX:
  the -1 is GCC-only (ER 0x20474343/0x21474343); MSVC personalities get the
  TRUE return address per RtlDispatchException semantics. G1 (UPX GCC flows)
  stays green.
- RESULT: winmergeu exit 133 -> 124 = LIVE GUI DAEMON (3x deterministic;
  986K Sleep(0) message-loop lines; hooks -1/7/5 -> 0xb30001/2/3 +
  Unhook TRUE; FH4 catch -> RtlUnwindEx target_ip=0x140298df5 ->
  resume -> pump). Evidence: evidence/sweep031/winmergeu_sw1201_key_lines.txt.
- SW-1202 LANDED (decode + double fix): the "positional output" premise was a
  MISDIAGNOSIS — zstd v1.5.6 usage (from the guest's own --help) has no
  positional-output form; `can't stat <2nd input> -- ignored` is REAL zstd
  behavior. Real defects fixed: (a) _stat64/_fstat64/_stat64i32 now bridge
  host errno into the guest mirror (stale EPERM killed zstd's stat diagnostics);
  (b) `strerror` was bound DIRECTLY to SysV glibc — ms_abi guests passed errno
  in RCX, glibc read RDI -> wrong strings ("Operation not permitted" for
  errno=2). mw_strerror ms_abi wrapper added. zstd round trips re-verified.
- Wagon batch (SWEEP-031 engine upgrades, 7 wagons): SW-1154 SafeArray real
  store (MwSa, 20 VT codes, Put/Get round-trip, +GetElement/Destroy ENTRYs);
  SW-1155 CreatePolygonRgn even-odd scanline raster; SW-1158 CreateDIBitmap
  CBM_INIT real pixel copy (24/32bpp, top-down + bottom-up); SW-1159
  AlphaBlend real per-channel alpha (SCA, AC_SRC_OVER enforced); SW-1160
  K32EnumProcessModules real /proc/self/maps module list + BaseNameW
  resolution; SW-1202 stat/strerror; SW-1201 hooks. Handle-base pattern
  enforced (low16=0): hooks 0x00B3, safearray 0x00B4, k32mod 0x00B5.
- MEASURED sha 83df098e (G1 PIN OK 12/12, evidence/master/g1/20261020_154229):
  corpus x3 16/16 deterministic, ZERO unresolved, ZERO regressions
  (pageant 124, procexp64 139 BUG-078 pre-existing, zoomit64 1 SW-1106
  pre-existing); 7za/zstd/lz4 round trips + jq re-verified.
- Loader 2,698,920 -> 2,722,720 bytes (+23,800).
- Next wagons queued: SW-1203 hook EVENT firing (CBT/GetMessage delivery into
  guest procs), SW-1153 ODBC mini-driver, SW-1156 ExtFloodFill seed fill,
  SW-1102 procexp jump-into-string, SW-1106 zoomit exit1, SW-1152
  RtlCaptureContext SSE regs.

## SWEEP-029 round summary «قانون کلی» — cheetah speed, owl eye, elephant load, train of wagons (2026-10-20)
- SW-1103 ENGINE ROOT-CAUSE FIX: mw_RtlCaptureContext was a NO-OP — every guest
  consumer (mingw gs_report) read ctx.Rip=0 -> RtlLookupFunctionEntry(pc=0)->NULL ->
  UnhandledExceptionFilter -> TerminateProcess(0xC0000409). Real capture now fills
  integer regs + segments + EFlags + MxCsr + Rip=caller RA. winmergeu exit-9 tail now
  walks RtlVirtualUnwind 2 frames with REAL rip (0x140318939) before the genuine
  /GS cookie failure terminates — frontier is now a real guest cookie mismatch
  (SW-1151), not a zeroed context.
- Wagon batch: 54 (bginfo64) + 4 (winmergeu SHELL32/SHLWAPI ordinals) + 6 ODBC32
  + 3 OLEAUT32 ordinals = 67 imports/ENTRY lines across two splices
  (scripts/sweep029_impl_a.c + _b.c, apply + apply2). All ordinal names
  cross-checked against wine-mirror spec files (2026-10-20).
- MEASURED on sha 1c8e23f7 (G1 PIN OK 12/12, evidence/master/g1/20261020_113847):
  * bginfo64 54 -> 0 unresolved, exit0 x3, stderr byte-identical x3
  * winmergeu 4 -> 0 unresolved (imports complete; exit 9 = gs fail-fast frontier)
  * corpus x3 15/15 deterministic exits, ZERO unresolved on ALL programs, ZERO
    regressions (pageant 124 daemon, procexp64 139 BUG-078, zoomit64 exit1 SW-1106)
  * procexp64 stderr addr-diff PRE-EXISTING (SWEEP-028 hashes also differ; ASLR
    addresses in crash dump only — exits/RIP identical)
- Loader size 2,497,432 -> 2,698,328 (+200,896).
- Next: SW-1151 winmergeu gs fail-fast decode; SW-1102 procexp frontier; SW-1106
  zoomit exit1; SW-1153 ODBC mini-driver.

## SWEEP-030 round summary «قانون کلی قطار» — wagon batch: SW-1151 ENGINE DOUBLE-FIX + fastfail honesty (2026-10-20)
- SW-1151 ENGINE ROOT-CAUSE FIX (two engine bugs, one guest frontier):
  * Bug 1 — EstablisherFrame semantics: our unwind reported the POST-UNDO RSP
    (function-entry RSP) as the establisher frame; Windows/ReactOS
    GetEstablisherFrame gives the call-boundary RSP (FrameReg==0) or
    live_FP - FrameOffset*16 (FrameReg!=0). MSVC __GSHandlerCheck_EH indexes
    its /GS cookie at [est + GS_offset]: WinMergeU func 0x2279a0 (frame 0x5a8,
    cookie slot frame_base+0x570) failed the compare -> __report_gsfailure
    0x318730 -> int 0x29 -> our old fallback chain -> exit 9.
  * Bug 2 — mw_RtlVirtualUnwind never advanced ctx.Rip: guest stack walkers
    re-read ctx.Rip per iteration (winmergeu walker loop-of-2 @0x318928) and
    decoded the SAME frame twice with a marching establisher. Windows
    contract: Rip=[unwound rsp] (return address), rsp+=8 (RA pop); machframe
    exempt. Now matches ReactOS RtlVirtualUnwind L557-562.
  * Fix set: ms_est out-param in seh_internal_virtual_unwind; dc.EstablisherFrame
    + handler RDX = ms_est (dispatch + BUG-031 intermediate walk, stop
    condition pre_rsp==tgt_frame); mw_RtlVirtualUnwind est rules + RA pop;
    mw_RtlUnwindEx resume rsp = tgt_frame DIRECTLY (entry-alloc cancellation
    removed — main path + BUG-031 continuation path); GCC-bridge 0x21474343
    intercept resume = est_va directly. UPX G1 regression caught by the 3x
    discipline during the sweep (resume landed 0x48 below the frame body,
    pre-EXP-035 P-deref crash) -> intercept fixed -> G1 green again.
  * Fastfail honesty: IsProcessorFeaturePresent(23=PF_FASTFAIL_AVAILABLE)=1;
    crash_handler detects guest `cd 29` (#GP -> SIGSEGV, RIP at the
    instruction) -> [FASTFAIL] trace (code + caller RA + stack window) +
    exit 9 (STATUS_STACK_BUFFER_OVERRUN low byte). UCRT gs_report now takes
    the real fastfail path, not the legacy walk+UEF fallback.
- MEASURED on sha ddfa14a0 (G1 PIN OK 12/12, evidence/master/g1/20261020_130641):
  * winmergeu exit 9 -> 133: /GS cookie check PASSES; guest C++ EH chain runs
    end-to-end (throw after SetWindowsHookExW(5), 10 frames, guest
    __CxxFrameHandler3 personalities decide ContinueSearch, terminate ->
    int3 0x2bd10e) — new frontier SW-1201 (guest-side catch-type decode).
    CORRECTION (SWEEP-038 reconcile): 0x14032126f was an EH-machinery
    C++-magic check, NOT the throw site — real throw decode = SetWindowsHookExW
    NULL -> CMemoryException, ThrowInfo 0x1404fd7b0 (see SW-1201 LANDED entry).
  * corpus x3 16/16 deterministic, ZERO unresolved, ZERO regressions
    (pageant 124 daemon, procexp64 139 BUG-078, zoomit64 exit1 SW-1106)
  * round trips re-verified: 7za create+extract SHA-OK 3/3; zstd canonical
    -q -f round trip OK (SW-1202 = pre-existing explicit-output-arg frontier,
    old binary behaves identically); lz4 round trip SHA-OK 3/3; jq OK.
- Evidence: evidence/sweep030/ (corpus TSV, winmergeu + UPX post-fix traces);
  artifacts/sweep030/sweep/. Recon scripts under /home/z/my-project/scripts/
  (sweep030_recon.py, sweep030_disasm.py, sweep030_gschain.py, sweep030_ui2279a0.py).
- Loader size 2,698,328 -> 2,698,920 (+592).
- Next: SW-1201 winmergeu catch-type decode; SW-1153 ODBC mini-driver; SW-1154..1159
  raster/OLE wagons; SW-1160 K32EnumProcessModules; SW-1202 zstd argv decode.

## SWEEP-032 round summary — TRAIN 1 of 5 «۵ قطار» (2026-10-21)
- SW-1106 LANDED: zoomit64 was NEVER broken — the loader's 64MB file cap
  rejected its 68,768,056-byte PE (valid PE32+, SizeOfImage 0x4264000,
  .rsrc 60.5MB embedded media, overlay 10,040B — pefile-verified) before a
  single header byte was parsed ("[ERROR] bad size" -> exit 1). Cap raised
  to 512MB. Full import surface landed: 66 named + 4 raw ordinals
  (COMCTL32.410/412/413 = SetWindowSubclass/RemoveWindowSubclass/
  DefSubclassProc, SHLWAPI.219 = QISearch — wine-mirror spec cross-check
  2026-10-21). zoomit64: 70 unresolved -> 0, exit 0 x3 deterministic.
- SW-1152 LANDED (engine): RtlCaptureContext now captures all 16 XMM
  low-128 (read BEFORE any compiler SSE work so XMM0-5 are caller-live;
  XMM6-15 real by ms_abi callee-save), real stmxcsr MxCsr at both
  CONTEXT+0x34 and FltSave+0x118, real fnstcw/fnstsw control/status words,
  honest TagWord 0xFFFF. Layout per winnt.h (Xmm0@0x1A0+16i).
- ENGINE FIX (owl-eye): MW_GDIPLUS_NOTIMPL was 4 (=Status::ObjectBusy);
  NotImplemented is 6. GdipBitmapLockBits honest value corrected.
- Wagon batch (70 ENTRY lines + gdobj state store + COM shell + real
  IStream): pen state (color/width/caps/join) real; bitmap pixel store
  (32bpp canonical, Scan0 copy) -> GdipBitmapGetPixel real read; effects
  real lifecycle + honest unrendered apply; LCIDToLocaleName real table
  (+neutral langs with LOCALE_ALLOW_NEUTRAL_NAMES); CompareStringOrdinal
  REAL (SW-508 overlap); threadpool-wait objects (0x00B6 table) +
  SetThreadpoolWaitEx/TimerEx; WINMM timeSetEvent/timeKillEvent real
  table; WTS session-notification registration; USER32 extra-info slot,
  ClipCursor/GetClipCursor real rect, MonitorFromRect real intersection,
  SendInput cbSize-validated count, VkKeyScanW real US table,
  SetWindowDisplayAffinity, ChangeDisplaySettingsExW honest
  TEST=0/real=-1; clipboard owner+sequence wired (GetOpenClipboardWindow,
  GetClipboardSequenceNumber real); RegSet/RegDeleteKeyValueW routed
  through the hive; CoGetApartmentType REAL (g_com_init + MTA);
  RoFailFastWithErrorContext real fail-fast (exit 9); Ro* error-info
  family honest HRESULTs; MFStartup/Shutdown version-stamp
  (0xC00D36C2 wrong version) + MFCopyImage REAL row-copy +
  MFEnumDeviceSources 0-devices; SHCreateItemFromParsingName real
  path-existence (0x80070002); SHGetKnownFolderItem real GUID whitelist;
  SHCreateStreamOnFileEx REAL file-backed IStream (14-slot vtable, all
  ms_abi: Read/Write/Seek/SetSize/Stat real on a dedicated fd);
  COM shell objects (QI/AddRef/Release real, E_NOTIMPL body) for
  MFCreateMediaType/MFCreateAttributes/shell items.
- MEASURED sha e36f6e40 (G1 PIN 12/12, evidence/master/g1/20261020_175719):
  corpus x3 15/15 deterministic, ZERO unresolved on ALL programs, ZERO
  regressions (pageant 124, winmergeu 124 live daemons, procexp64 139
  BUG-078 — all pre-existing frontiers).
- Round trips re-verified BYTE-EXACT (cmp): 7za create+extract 2-file OK;
  jq --version + .a query OK (jq-1.7.1); sqlite3 :memory: CREATE/INSERT/
  SELECT OK. NOTE: a transient "[ello" display artifact was investigated
  and DISPROVEN (od/cmp byte-exact) — no BUG entry, no regression.
- Loader 2,722,720 -> 2,866,856 bytes (+144,136).
- Next wagons queued: SW-1203 hook EVENT firing, SW-1153 ODBC mini-driver,
  SW-1156 ExtFloodFill, SW-1102 procexp jump-into-string, SW-1156,
  SW-508 remainder (CompareStringOrdinal landed here).

## SWEEP-035 round summary — TRAIN 4 of 5 «۵ قطار» (2026-10-21)
- SW-1203 hook EVENT firing v1: CBT delivered to live guest hooks (CREATEWND
  pre-create real CBT contract incl. cancel; DESTROYWND; ACTIVATE). No pool
  guest reaches window creation post-hook-install yet — machinery verified
  by construction + G1.
- SW-1204 LANDED: AppPolicyGetThreadInitializationType (worker-thread unblock;
  winmergeu worker completed init and now parks in a REAL blocked GetMessageW).
- ENGINE BUG-FIX: W/A event split-brain (CreateEventW/MutexW/MutexA/WSA events
  legacy pool vs mw_event_* registry waiters) — unified; winmergeu main's
  WFSO(0x4) now resolves a real event (probe trace [KOBJ] added).
- GetMessageW two-contract semantics: main-thread batch = instant WM_QUIT
  (10 corpus programs exit-0 preserved — unconditional park regressed them,
  caught 3x discipline), worker threads = real blocking park.
- MEASURED sha 040f875e: G1 12/12; corpus x3 15/15 det, 0 unresolved, 0 regressions.
- Next: SW-1205 winmergeu main-thread poll (waits for worker message-loop
  progress; needs a PostThreadMessage producer or upstream window creation),
  SW-1102 guest-write instrumentation, SW-1153 consumers.

## SWEEP-036 round summary — TRAIN 5 of 5 «۵ قطار» (2026-10-21)
- Sleep(0) -> sched_yield() (real yield-the-quantum semantics; winmergeu's
  main-thread poll now rotates instead of a pure busy no-op).
- ole32.277/16 evidence re-probe: wine ole32.spec is name-ordered, no public
  ordinal table -> stay KNOWN-UNKNOWN stubs (Rule 1 upheld).
- Round trips on the final binary (sha 48fac5a1): 7za create+extract
  BYTE-EXACT (cmp x2), jq --version + query, sqlite3 CREATE/INSERT/SELECT.
- G1 PIN 12/12 (evidence/master/g1/20261020_191444); corpus x3 15/15
  deterministic, 0 unresolved, 0 regressions.
- Session totals (SWEEP-032..036, five trains, one push each): zoomit64
  1->0 (70 imports landed), winmergeu worker unblocked + parked honestly,
  procexp crash class decoded + ordinals 7->2 KNOWN-UNKNOWN, SW-1152/1153/
- 1156/1203/1204 LANDED; open frontiers: SW-1205 winmergeu main poll,
  SW-1102 guest-write instrumentation, ole32.277/16 names.

## SWEEP-037 «ادامه ۱۰ قطار ۱۰۰ واگن» — RECONCILE train (2026-10-21)
- RECONCILE DISCIPLINE (SWEEP-027 precedent): local SWEEP-031 (c23a63c,
  preserved as branch backup/sweep031-local) duplicated wagons that the
  parallel remote chain SWEEP-031..036 (0c191d1..cd5e044) already landed
  with stronger semantics (winmergeu 124 live-daemon via FH4 RA fix,
  zoomit 70-import surface @512MB gate, ODBC w/ SQLBindCol, SafeArray
  20-VT store, BUG-078 'contentions' stack-overrun decode, CBT hook
  delivery, Sleep(0)->sched_yield). main RESET to origin/main cd5e044;
  deterministic rebuild reproduced sha 48fac5a1 byte-exact.
- UNIQUE DELTAS re-landed on top (all wine-mirror spec sourced 2026-10-21):
  * SW-1223/1225 procexp64 OLEAUT32.16=SafeArrayDestroy + 277=VarUI4FromStr
    (oleaut32.spec — DISTINCT from ole32.277/16 KNOWN-UNKNOWN) -> procexp64
    FIRST-TIME ZERO unresolved imports (exit 139 = BUG-078 frontier kept).
  * SW-1220b autoruns64 SHLWAPI.487=SHLoadIndirectString (shlwapi.spec) +
    REAL %name% environment resolution -> autoruns64 2->1 unresolved.
  * COMCTL32.344 (autoruns64) + SHELL32.88 (searchmyfiles) stay
    KNOWN-UNKNOWN per Rule 1 (no credible public source this round).
- MEASURED sha 2464ad23 G1 PIN 12/12 (evidence/master/g1/20261021_053052):
  corpus x3 15/15 deterministic, ZERO regressions; unresolved census:
  procexp64 0, autoruns64 1 (COMCTL32.344 K-U), searchmyfiles 1
  (SHELL32.88 K-U), all others 0. SWEEP-031 local-round decode records
  (0x32126f validator correction, ThrowInfo vtable notes, 66-import
  zoomit census, 7-ordinal procexp naming) filed in backup branch and
  evidence/sweep031/.

## SWEEP-040 «قطار ۵۰ تایی» — 3 wagons + 2 new frontiers (2026-10-21)
- SW-1206 LANDED (double): adexplorer64 corpus RECOVERED
  (artifacts/sweep50/dl/adexplorer64/ was EMPTY; AdExplorer.zip re-fetched
  from download.sysinternals.com; sha256 e451287843b3927c...; x64 exe kept,
  x86/ARM64 dropped). First run: exit 139, crash RVA 0x1da85
  `mov [rax+8],esi` RAX=0 — decode: guest main window IS a modeless dialog
  CreateDialogParamW(L"MAIN"); MAIN template (.rsrc RT_DIALOG name MAIN,
  EX format, size 478) carries id=1002 SysTreeView32 + id=1070
  SysListView32; guest's control-cache append at 0x14001da80 dereferenced
  the NULL GetDlgItem result via the r14 path (0x14001da10).
  ENGINE FIX: real named-dialog template resolution — mw_dlg_from_resource
  (w3_res_find RT_DIALOG=5 + LockResource + SizeofResource) with a
  bounds-checked DLGTEMPLATEEX parser (menu/class/title/font walk, DWORD
  id items, string classes via mw_sysclass_w, atom classes 0x80..0x8C)
  wired into CreateDialogParamW/A + CreateDialogIndirectParamW +
  DialogBoxParamW/A; STD templates delegate to the proven SW-921 walker.
  MW_DLG_ITEMS=0 = regression kill switch. adexplorer64 now: MAIN parsed
  (cdit=4), children 1002/1070/1090/STATIC registered,
  GetDlgItem(1002)->0x30001 — exit 139 -> exit 32 FULL LIFECYCLE
  (CRT rundown: mscoree probe -> ExitProcess).
- Regression caught by census x3: rammap64 0 -> 139. Decode: rammap
  creates one dialog PER PROPERTY PAGE, each with its own id=1003
  SysListView32; the first cut (fixed 0x10002 + dedupe) made every page
  resolve to page 1's control -> crash 0x1cfa9 (`mov rcx,[0x120]` junk
  branch after GetPropW NULL). FIXED properly: every CreateDialog* mints
  a DISTINCT identity (g_dlg_id_next atomic, starts 0x10002) via
  mw_dlg_create_common. rammap64 then went FURTHER (1413 API lines vs
  the old 1-line silent exit-0 short-circuit) and reached a NEW frontier:
  CreateWindowExW('tooltips_class32') returned the FRAME hwnd (top-level
  windows all map to MW_GUI_HWND) -> GetPropW(tooltip, atom 0xC000)
  -> NULL -> comctl32-internal-branch crash. NEW WAGON SW-1207
  (comctl32 tooltips_class32 object + subclass-prop contract).
- SW-1205 LANDED: per-thread Windows-style ids (main=5678 legacy, spawned
  5679+ assigned at _beginthreadex *tid out-param) + REAL bounded
  thread-message queues (32 queues x 64 msgs) + PostThreadMessageW/A
  honest semantics (FALSE + ERROR_INVALID_THREAD_ID=1444 for unknown tid
  — the old void-queue stub always lied TRUE) + worker-park drain
  (posted WM_QUIT wakes the parked GetMessageW) + GetCurrentThreadId
  per-thread. DECODE: winmergeu main poll = 0x140220068
  `while(!done) { PeekMessageW(NULL,PM_NOREMOVE); idle: Sleep(0) }`
  (2 callers 0x1402138e4 / 0x1402b46a6); producer = worker thread via
  posted messages. WINMERGEU RESULT: guest posted WM_QUIT(5679) and the
  worker WOKE — winmergeu passed the daemon for the first time since
  SWEEP-031 (124 -> deeper path). Post-daemon frontier: guest called
  GlobalFree(0xae0001) (handle-class value) -> engine HEAP-TRAP exit 4
  (pre-existing honest trap with caller chain; probe-hardened
  w3_aligned_base_of first — the loader itself crashed in it on
  IsBadReadPtr-less magic read). NEW WAGON SW-1208
  (0xae0001 provenance + GlobalFree degrade-to-fail decision).
- SW-1102c LANDED: BUG-078 auto-decode instrumentation in crash_handler —
  when crash RIP is outside the PE: RIP-as-UTF16 decode (procexp64:
  "ntio" of 'contentions' — matches SWEEP-032/038 manual decode) +
  packed-UTF-16 stack-run scan. MEASURED on every procexp64 run: write
  region = 32-byte name records with 8-byte gaps (rsp+008..048,
  +056..088, +096..128, +136..168, +176..208, +216..240, +248..272,
  stray +1264..1272), RBP=0 at the crash frame. NEW WAGON SW-1102d
  (static hunt for the 32-byte-stride record writer loop).
- MEASURED sha a0d9e9c8 G1 PIN 12/12 (evidence/master/g1/20261021_133926):
  census x3 16/16 DETERMINISTIC (corpus grew 15 -> 16 with adexplorer64);
  deltas all FORWARD: adexplorer64 139->32 (SW-1206), rammap64 0->139
  (SW-1207 frontier), winmergeu 124->4 (SW-1205/SW-1208 frontier);
  pageant 124 preserved, procexp64 139 preserved (now auto-decoded),
  10x exit-0 unchanged. Round trips on the final binary: 7za x64
  create+extract BYTE-EXACT (cmp), jq-1.7.1 --version exit 0,
  sqlite3 in-memory CREATE/INSERT/SELECT = 42 exit 0.
- Open: SW-1207 comctl32 tooltip object, SW-1208 GlobalFree(0xae0001)
  provenance, SW-1102d BUG-078 copy-loop static decode, ole32.277/16
  names (Rule-1 blocked), SW-1156 ExtFloodFill, SW-1153 consumers.

## SWEEP-041 round summary «ادامه ۲۰ قطار» — 20-train batch, 3 wagons LANDED (2026-10-21)
- SW-1207 LANDED: comctl32 tooltips_class32 window object + CC32SubclassInfo
  creation contract. Root cause: CreateWindowExW('tooltips_class32') (popup,
  parent NULL) fell into the main-window path and returned MW_GUI_HWND, so the
  guest's GetPropW(tooltip, atom 0xC000) returned NULL and rammap64 crashed
  inside its own comctl32 subclass branch (RVA 0x1cfa9, RAX=0 -> mov rcx,[0x120]
  absolute null-page read; RDX=R12=0x10001 = the frame hwnd it was handed).
  ENGINE: (1) sysclass tables W/A + "tooltips_class32" -> 0x8D (prefix compare,
  bounds 0x8C -> 0x8D, name "Tooltips_Class32"); (2) dedicated tooltip branch in
  CreateWindowExW/A BEFORE the child gate: mint a DISTINCT child identity via
  mw_child_register (parent ?: MW_GUI_HWND), then perform the real-comctl32
  creation contract — the tooltip window proc subclasses itself during
  WM_NCCREATE and stores SUBCLASS_INFO under global atom 0xC000
  ("CC32SubclassInfo") BEFORE CreateWindowEx returns; engine allocates a
  registered GMEM_ZEROINIT 0x140-byte block (SUBCLASS_INFO-sized, zero counts =
  "no subclass frames") and mw_SetPropW(hwnd, MAKEINTATOM(0xC000), block).
  rammap64: 139 crash -> 0 FULL LIFECYCLE (1624 API lines, ExitProcess(0));
  trace shows two tooltips minted (0x30013/0x30014, props 0x36bfff0/0x36c0140)
  and GetPropW(hwnd=0x3000b, 0xC000) -> 0x36bfd50 non-NULL.
- SW-1208 LANDED: GlobalHandle cookie registry + GlobalFree/LocalFree
  degrade-to-fail. Root cause: legacy GlobalHandle stub minted 0x00AE0000|n
  cookies and FORGOT the base pointer; winmergeu called
  GlobalHandle(0x28128c0) -> 0xae0001 and its later GlobalFree(cookie) hit the
  old HEAP-TRAP exit 4 — on real Windows GlobalFree(invalid) NEVER terminates:
  it returns the same value + ERROR_INVALID_HANDLE. ENGINE: (1) MWGHandle
  registry (64 slots, cookie 0x00AE0000|n, base ptr, g_gdesc_next moved next to
  it); GlobalHandle now registers cookie->base; (2) GlobalLock resolves a
  cookie to its base (GMEM_MOVEABLE lock contract); GlobalFlags(cookie) = 2
  (GMEM_MOVEABLE); (3) GlobalFree/LocalFree accept cookies (resolve -> free
  base -> retire -> NULL) and untracked values degrade to the documented
  contract: return same value + SetLastError(6); (4) GlobalReAlloc
  probe-hardened: cookie resolve + w3_reg_has gate -> NULL + last error 6
  instead of realloc()ing a foreign pointer (SW-1205 hardening family).
  winmergeu: exit 4 HEAP-TRAP -> 0 FULL LIFECYCLE; trace:
  GlobalHandle(0x28128c0) -> cookie 0xae0001; GlobalFree(handle=0xae0001) ->
  base 0x28128c0 freed -> NULL; full CRT rundown to ExitProcess(0) (36076 API
  lines — first complete winmergeu lifecycle since the corpus landed).
- SW-1156 LANDED (mark): ExtFloodFill real scanline seed fill already in the
  engine (src/loader/loader.c mw_ExtFloodFill, "replaces no-op v1") — worklist
  row was stale; marked closed. ole32.277/16: stays KNOWN-UNKNOWN (Rule 1 —
  wine ole32.spec is name-ordered, no public ordinal table; re-probed
  SWEEP-036, no new evidence).
- SW-1102d PROGRESS (round 3, still open): procexp64 crash-stack RA candidates
  classified by section — 0x140144bd8 (.rdata) and 0x140191bf0 (.data) are data
  pointers, NOT return addresses; the three .text RAs resolve to:
  0x140066130 (locals rsp+0x20..0x78, IAT call 0x140134ae8),
  0x1400a1380 (builds {xmm0,ptr} locals at rsp+0x28/0x40, delegates to
  0x140066130 and 0x1400a8a30 with .rdata name 0x14015aa48),
  0x1400c3340 (CLR perf-counter master init: count*0x30 / count*0x18 /
  count*0x8 record arrays via alloc 0x140101668 into 0x14019d208/200/1f8,
  NtQuery-style call magic 0x83350008, VirtualAlloc 0x1000, five wide-string
  length loops over .data 0x1401887c0/8a08/c50/e98/90e0 -> lengths to
  0x140188a00/c48/e90/90d8/189320). Crash-stack records are 0x28-stride
  (32B UTF-16 name + 8B field) — none of the heap arrays is 0x28, so the
  writer is a stack-local record array in one of these frames; exact store
  instruction still open. Scripts/sweep041_dis.py committed.
- adexplorer64 corpus LOST AGAIN (third occurrence: sweep031->032, this reset)
  — RECOVERED via the one-shot procedure: curl AdExplorer.zip from
  download.sysinternals.com, sha256 must equal
  56536bff1e9da136a53f68c6f4f1cd51f79be919a27249a193a2bc8fc472e89a, unzip,
  rename ADExplorer64.exe -> adexplorer64.exe (sha e4512878...); recorded in
  evidence/sweep041/adexplorer64_corpus_sha256.txt. Corpus shas verified
  byte-exact before use.
- MEASURED sha 4ecab4c2 G1 PIN 12/12 (evidence/master/g1/20261021_154005):
  census x3 16/16 DETERMINISTIC — deltas ALL FORWARD: rammap64 139 -> 0
  (SW-1207), winmergeu 4 -> 0 (SW-1208); adexplorer64 32 preserved, pageant 124
  preserved, procexp64 139 preserved (SW-1102d frontier), 11 other programs
  exit-0 unchanged. Round trips on the final binary: 7za x64 create+extract
  BYTE-EXACT (cmp), jq-1.7.1 --version "jq-1.7.1" + query '.a[1]' -> 2,
  sqlite3 in-memory CREATE/INSERT/SELECT = 42.
- Open: SW-1102d BUG-078 copy-loop exact store instruction, ole32.277/16
  (Rule-1 blocked), SW-1153 consumers; NEW observation: winmergeu now runs the
  full app lifecycle — its post-daemon behavior is productive work, next
  frontier will be found by diffing its 36076-line trace.

## SWEEP-042 round summary «۱۰ قطار، چند ده واگن ریزه» — 10 trains, ~42 micro-wagons LANDED (2026-10-22)
Batch directive: 10 trains carrying several dozen SMALL wagons. Source of the
wagons: adexplorer64's post-SW-1206 import surface (23 unresolved), the
SWEEP-041 open note "diff winmergeu's full-lifecycle trace", putty's
FormatMessage stale last error, pageant's CryptProtectMemory probe, and the
ExitProcess(48925984) anomaly. One commit, no force-push.

- SW-1209 LANDED (Train 1 + 2 — adexplorer64 23-import decode + land): the 23
  unresolved = 9 named functions + 6 ACTIVEDS ordinals + 8 WLDAP32 ordinals.
  Rule-1 decode via wine spec re-probe (2026-10-22, sha256 recorded in
  evidence/sweep042/wine_specs/): activeds.spec carries EXPLICIT ordinals
  7=ADsBuildVarArrayStr, 9=ADsOpenObject, 12=ADsSetLastError, 13=ADsGetLastError,
  15=FreeADsMem, 20=ADsEncodeBinaryData (signatures mingw-w64 adshlp.h);
  wldap32.spec explicit 13=ldap_unbind, 14=ldap_set_optionW, 73=ldap_bind_sW,
  88=ldap_connect, 118=ldap_err2stringW, 145=ldap_initW, 155=ldap_modify_ext_sW,
  188=ldap_rename_ext_sW; shell32.spec 88=SHDoDragDrop (the SWEEP-037 "no 88
  in spec" note was WRONG — re-probe wins). Implemented typed-honest:
  UuidFromStringW real 8-4-4-4-12 parse (RPC_S_INVALID_STRING_UUID 1707),
  IIDFromString -> CLSIDFromString parse, MenuItemFromPoint -1, SD-relative
  header family (GetSecurityDescriptorOwner/Group real x64 header parse with
  SE_*_DEFAULTED control bits; GetSecurityDescriptorLength header-derived max;
  ConvertSecurityDescriptorToStringSecurityDescriptorW emits derivable
  O:/G: SDDL parts via ConvertSidToStringSidW), NetUserGetLocalGroups/
  NetUserGetGroups NERR_Success + 0 entries (no account DB headless), ADs
  family (ADsOpenObject E_ADS_BAD_PATHNAME 0x80005000, ADsEncodeBinaryData
  real "\XX" wide escaping + S_OK, FreeADsMem local-free + TRUE, Set/Get
 LastError global pair), WLDAP32 W-family (LDAP_SERVER_DOWN 0x51 /
  LDAP_NOT_SUPPORTED 0x5C; ldap_initW delegates the existing ldap_init).
  RESULT: adexplorer64 unresolved 23 -> 0 (exit 32 full lifecycle preserved).
- SW-1210 LANDED (Train 3 — kernel32 Ex/localization family, ucrt probe
  surface): GetTempPath2W/2A -> GetTempPathW/A delegates (Win8+ CRT probe),
  FlsGetValue2 -> FlsGetValue (same slot table), EnumSystemLocalesEx with the
  REAL 3-arg LOCALE_ENUMPROCEX ABI invoking L"en-US", GetDateFormatEx/
  GetTimeFormatEx -> W delegates, GetUserDefaultLocaleName "en-US" (consistent
  with LCID 0x0409), IsValidLocaleName "en*" grammar, LocaleNameToLCID
  en/en-us 0x0409, en-gb 0x0809, unknown 0 + err 87. TRACES: every ucrt probe
  that previously returned NULL (not in dispatch table) now RESOLVES
  (winmergeu: GetTempPath2W/FlsGetValue2/EnumSystemLocalesEx/GetDateFormatEx/
  GetTimeFormatEx/GetUserDefaultLocaleName/IsValidLocaleName/LocaleNameToLCID
  all non-NULL).
- SW-1211 LANDED (Train 4 — FormatMessage honesty): mw_FormatMessageA/W now
  implement FORMAT_MESSAGE_FROM_STRING (copy src) + FORMAT_MESSAGE_FROM_SYSTEM
  with a 12-entry canonical table (MS error texts: 2,3,4,5,6,32,87,126,127,
  183,1444,1813) and on miss SetLastError(317 ERROR_MR_MID_NOT_FOUND) — the
  old body returned 0 leaving the STALE last error in place; putty rendered
  "FormatMessage returned 2" (leaked ERROR_FILE_NOT_FOUND) where REAL Windows
  reports 317 for an unknown msgid. AFTER: putty messagebox renders
  "(unable to format: FormatMessage returned 317)" = real-Windows fallback
  shape. exit 0 preserved.
- SW-1214 LANDED (Train 4 — SystemParametersInfoW SPI_GETNONCLIENTMETRICS
  0x29 + SPI_SET 0x2A): real NONCLIENTMETRICSW fill — cbSize gate {500,504},
  documented Win10 metrics (border 1, scroll 17x17, caption 17x22, smcaption
  12x15, menu 17x17, padded 4) + five LOGFONTW faces (Segoe UI, lfHeight -12,
  FW_NORMAL, DEFAULT_CHARSET). winmergeu probed 0x29 2x and fell back before;
  now "-> TRUE" twice.
- SW-1213 LANDED (Train 5 — crypt32 DPAPI surface): pageant probes
  GetProcAddress(CryptProtectMemory) -> now resolves; pageant CALLS
  CryptProtectMemory(cb=16 flags=1) 2x (session-key in-place contract) —
  identity noop documented (process-lifetime confidentiality; no at-rest
  encryption headless), CryptUnprotectMemory symmetric, CryptProtectData/
  CryptUnprotectData honest FALSE + ERROR_ENCRYPTION_FAILED 6000 /
  ERROR_DECRYPTION_FAILED 6001 (cross-process blob service unavailable).
  pageant daemon 124 preserved (its park = correct message-loop wait).
- SW-1212 LANDED (Train 6 — ExitProcess provenance): anomalous-code
  instrumentation (code > 0xFFFF logs caller RA; exit-0 traces stay
  byte-identical). DECODED: adexplorer64 exits with a HEAP POINTER
  (run-1 0x2EA8D20, run-2 0x2565D20) whose low byte is stably 0x20 —
  census exit 32 = code & 0xFF; caller RA=0x1400403f5 -> guest RVA 0x403f5
  (guest returns an uninitialized/handle var from its entry path). GUEST-side
  bug; engine exit passthrough is honest. All 23-import + trace variance
  explained.
- Train 2 diagnosability: unresolved ORDINAL imports now print their names
  ("[WARN] unresolved: COMCTL32.DLL ordinal 344") — the SWEEP-001 count-only
  gap hid autoruns64's COMCTL32.344 and searchmyfiles's SHELL32.88 for rounds.
- Train 7 reconciliation: SW-1153 stale open-reference (LANDED SWEEP-034, the
  SWEEP-041 "consumers" note was stale). SW-791 re-verified 2026-10-22:
  curl-8.17.0 mingw single-exe fails CLEAN ("error initializing curl library",
  exit 2, no loader crash) — needs libcurl-x64.dll; UCRT slice stays OPEN.
  COMCTL32.344 stays KNOWN-UNKNOWN (Rule 1: wine comctl32.spec re-fetched
  2026-10-22 has explicit ordinals only to 342; 345=TaskDialogIndirect is
  MS-documented, 344 has no credible public name source).
- MEASURED sha 9e30c16d G1 PIN 12/12 (evidence/master/g1/20261021_164933):
  census x3 16/16 DETERMINISTIC — deltas ALL FORWARD: adexplorer64 unresolved
  23 -> 0 (SW-1209), searchmyfiles unresolved 1 -> 0 (SHELL32.88=SHDoDragDrop),
  autoruns64 unresolved 1 (COMCTL32.344, Rule-1 blocked, now NAMED in stderr);
  exits preserved: adexplorer64 32, pageant 124, procexp64 139, 12x exit-0
  (putty's 317 message text is the only rendered-output delta). Round trips
  on the final binary (evidence/sweep042/roundtrips/): 7za x64 create+extract
  BYTE-EXACT (cmp), jq-1.7.1 --version + '.a[1]' -> 2, sqlite3 in-memory
  CREATE/INSERT/SELECT = 42. no force-push.
- Open: SW-1102d BUG-078 copy-loop exact store instruction; ole32.277/16 +
  COMCTL32.344 (Rule-1); SW-791 curl libcurl-x64.dll; procexp64 139 frontier.

## SWEEP-043 round summary «ادامه قطار» — BUG-078 SOLVED: procexp64 139 -> 0 (2026-10-22)
Batch directive: continue the train (10-train micro-wagon format). THE round:
the procexp64 'ntio' crash (SW-1102d, open since SWEEP-032) is fully decoded
and FIXED — three engine bugs, one missing plane.

- SW-1102e/g ROOT CAUSE (the exact store, found): mw_IsBadStringPtrW copied
  chunk*2 = up to 1024 bytes into uint16_t tmp[256] = 512 bytes — a 512-byte
  ENGINE-stack overflow on every call with a large max (procexp64 passes
  0xffffffff). The overflow wrote the probed guest .rdata text over the
  engine's own saved registers + return address -> ret jumped into a fragment
  of the probed region -> RIP=0x6f00690074006e = 'ntio' inside the copied
  ".NET CLR ..." string pool (0x14015aa48..+0x400). The SWEEP-032/038/041
  "guest packing loop" attribution was WRONG: the packed 0x28-stride UTF-16
  runs on the crash stack are the engine's own overflowing memcpy. Fixed:
  tmp[512] = exactly chunk*2. Decode chain: crash-frame live geometry (RA
  0x1400a13fd at rsp+0x268 = 3 pushes + sub 0x250 of 0x1400a8a30), .data
  0x1401887c0/8a08/c50/e98/90e0 = registry ROOT PATHS (the SWEEP-041 "CLR
  perf-counter master init" label on 0x1400c3340 was wrong — it is registry-
  path canonicalization), .rdata 0x14015ad10/40/70 = CLR counter names,
  chain 0x1400c3340 -> 0x1400a1380 -> 0x1400a8a30 (RegQueryValueExW two-pass,
  tokenizer L",", formatter "%u "/"%s "). MW_BUG078_DIAG=1 diagnosability
  added (IsBadStringPtrW stack dump + uni-name EMPTY OA/UNICODE_STRING dump).
- SW-1102h HKPD contract: RegQueryValueExW(HKEY_PERFORMANCE_DATA 0x80000004 /
  0x80000050 / 0x80000060) now serves the mandatory two-pass dance (pass 1 =
  ERROR_MORE_DATA + need) and a minimal valid PERF_DATA_BLOCK (L"PERF",
  LittleEndian 0x1000, Version/Revision 1, HeaderLength 0x54, system name
  "MINIWIN", NumObjectTypes 0). After the IsBadStringPtrW fix procexp64
  reached RegQueryValueExW(HKPD, '.NET CLR Memory ') -> old FILE_NOT_FOUND
  with *size untouched -> NULL-block parse -> crash 0x14009be71
  (mov (%rdx),%ecx, rdx=0); now skips .NET columns gracefully.
- SW-1102j winsta!WinStationGetProcessSid ABI fix: procexp64 calls it as
  (NULL, 3, &out) through CFG slot 0x140191cb8 and treats rc==0 as SUCCESS,
  parsing + LocalFree-ing the out struct. Our old "return 0 = FALSE" was
  success-shaped with an unfilled out -> NULL deref. Failure shape = rc != 0:
  now returns 1 (TS session data unavailable).
- SW-1102k advapi32!I_QueryTagInformation: procexp64 resolves it via
  LoadLibraryA("advapi32.dll") + GetProcAddress; the anonymous
  mw_winapi_optional_stub returned 0 = ERROR_SUCCESS with the query struct
  untouched -> same NULL-deref class. Typed-honest entry: ERROR_NOT_FOUND
  (1168) — no SCM service-tag database headless. Diagnosability: the
  optional-stub fallback now prints "  -> optional-stub fallback" in traces
  (it was invisible; this contract mismatch stayed unexplained for rounds).
- SW-1102l NtQuerySystemInformation(class 5 = SystemProcessInformation): the
  plane procexp64's whole process view builds on. After g/k the guest reached
  its main refresh loop (HeapAlloc(grow) + class-5 query per pass) and our
  INVALID_INFO_CLASS answer drove an endless size-retry park (exit 124).
  Honest v1: ONE entry — the guest itself (PID 1234, loader convention), zero
  threads, image name = argv[0] base, full x64 SYSTEM_PROCESS_INFORMATION
  layout (record 0x100, name bytes after), two-pass 0xC0000004 + retlen.
- SW-1102f trace honesty: RegQueryValueExW gained hit-path AND miss-path
  MW_TRACE lines (registry queries were invisible in api_trace.json — the
  entire crash-window probe surface hid behind this gap; 98 Settings probes
  + the HKPD chain now visible).
- Environment-reset recovery (round-start + mid-round): workspace rolled back
  to 242af84 twice; origin/main carried 339cc60 (SWEEP-042, incl. the
  10-train batch already landed remotely). Protocol: reset --hard to
  origin/main + re-apply via scripts/sweep043_apply.py (11 patches, each
  assert-verified) + rebuild. Census binary verified BEFORE and AFTER the
  re-apply (procexp64 0 both times).
- adexplorer64 corpus LOST AGAIN (4th): recovered via the one-shot procedure,
  zip sha 56536bff... / exe sha e4512878... recorded in
  evidence/sweep043_adexplorer64_corpus_sha256.txt; exit 32 preserved.
- MEASURED sha 89ce3cd9 G1 PIN 12/12 (evidence/master/g1/20261021_181056):
  census x3 16/16 DETERMINISTIC — deltas ALL FORWARD: procexp64 139 -> 0
  (FULL LIFECYCLE, graceful FreeLibrary + ExitProcess(0)); exits preserved:
  adexplorer64 32, pageant 124, 13x exit-0 (was 12x). Round trips on the
  final binary: 7za x64 create+extract BYTE-EXACT (cmp), jq '.a[1]' -> 2,
  sqlite3 :memory: SELECT -> 42. evidence/sweep043/. no force-push.
- Open: ole32.277/16 + COMCTL32.344 (Rule-1); SW-791 curl libcurl-x64.dll;
  pageant 124 daemon (message-loop wait = correct headless park);
  adexplorer64 exit-32 (guest-side, SW-1212 proven); next frontiers via the
  now-visible registry/stub trace surfaces.

## SWEEP-044 round summary «ادامه قطار» — 15/16 exit-0, 0 unresolved, Go/Rust frontiers breached (2026-10-22)
Batch directive: continue the train (10-train micro-wagon format). Corpus state
before: 14x exit-0, adexplorer64 32, pageant 124, 1 unresolved import (COMCTL32.344).

- **SW-1215 adexplorer64 exit-32 ROOT CAUSE (32 -> 0)**: the main-thread headless
  WM_QUIT path in mw_gui_pump_getmsg returned 0 WITHOUT filling the guest MSG
  struct (every other path calls mw_gui_fill_msg). Guest WinMain does
  `return (int)msg.wParam;` — uninitialized guest-stack garbage: the WinMain
  frame held a stale heap pointer (0x3ad3d20) at MSG.wParam -> CRT exit ->
  ExitProcess(0x3ad3d20) = census exit 32. Fix: fill MSG(hwnd=NULL,
  message=WM_QUIT, wParam=g_quit_code, lParam=0) exactly like the worker-woke
  path. Real Win32 contract restored.
- **SW-1216 MsgWaitForMultipleObjects diagnosability + honesty**: the whole wait
  surface had NO MW_TRACE (pageant's main loop = alloc listener-array 0x608 ->
  MsgWait -> free read as a mystery busy-spin for rounds). Now traced; finite
  waits honor the FULL timeout (old cap 40 ms made guests loop 12x faster than
  real Windows); timeout=0 = instant poll (real contract); INFINITE = 20 ms
  park slices.
- **SW-1217 ConnectNamedPipe state machine (pageant 124 decode)**: the old
  "client-model: immediate connect" fabricated an INFINITE phantom-client storm
  for a LISTENER: every call spawned a client thread + ~1.6 MB buffers for a
  connection that did not exist (~70/s, hundreds of pipe instances). Real
  contract landed: CreateNamedPipe registers name+state; ConnectNamedPipe BLOCKS
  until a client CreateFile("\\\\.\\pipe\\<name>") attaches (or fails
  ERROR_PIPE_CONNECTED 535 when the client attached first; quit aborts with 995);
  DisconnectNamedPipe flips real state. After the fix: ONE ConnectNamedPipe call
  (was 256), ONE CreateThread (was dozens), main thread parks on the first real
  listener. pageant 124 = honest daemon park (real pageant runs forever),
  now fully traceable.
- **SW-1218 COMCTL32.344 = TaskDialog — LAST KNOWN-UNKNOWN ordinal DECODED
  (corpus unresolved 1 -> 0)**: Geoff Chappell "COMCTL32 6.10 Named Exports"
  (TaskDialog 344 / TaskDialogIndirect 345), confirmed by SO 43426776 +
  MS Learn Q1184393 loader-error reports. Supersedes the Rule-1 stale note
  (wine spec ends 342 — spec absence is not evidence of absence in the real
  v6.10 export table). Typed-honest impl: S_OK + *pnButton=IDCANCEL (canned
  headless contract, same as TaskDialogIndirect).
- **SW-501 MICRO-GAME (user's 100% mandate) RE-VERIFIED + BUG-079 SOLVED**:
  re-ran the official snake acceptance at HEAD -> FAILED with 1 frame (was 30
  in SWEEP-005). Decode: the game arms SetTimer(50ms) but the SW-801f
  gate-bypass delivered the whole 11-key script in <50 ms — ZERO WM_TIMER
  ticks, one paint, instant ESC. SW-1220 fix: with a live timer the pump WAITS
  for the tick and delivers WM_TIMER first (bypass no longer fires when a
  timer is armed — the timer IS the event source; scripted input spreads via
  its gate). Result: snake 30 frames BYTE-IDENTICAL 3x3 (exact SWEEP-005
  golden), breakout RECOVERED 381 frames + bricks 40->13 (exact SWEEP-018
  golden; was 1 frame / 0 bricks at HEAD — pre-existing BUG-080, NOT a
  SW-1220 regression, proven by falsification run on clean HEAD), calc 9/9
  both batteries. SW-501 marked LANDED (real win64 PE game, 3x3 acceptance,
  frame-SHA determinism). SW-1219 (new): downloaded-famous-game class
  (Chocolate-Doom/Zork/Frotz all x86 or DLL-bound) — blocked by SW-509.
- **SW-507 Go optional syscalls LANDED (4/4)**: GetQueuedCompletionStatusEx
  (real batch pop over the MwIocp model), CreateWaitableTimerExW (event-object
  mapping), SetWaitableTimer (accepted+traced), SetProcessPriorityBoost (TRUE).
- **Go boot chain (bombardier 9.8 MB Go 1.20.3, SW-1221..1227)**: usage path
  FULLY GREEN — `bombardier` with no args prints the real usage
  ("required argument 'url' not provided") exit 2; with a URL it boots the
  whole Go runtime and runs the benchmark loop ("Bombarding ... 10 request(s)
  ... Done! Statistics") exit 0 (data path errors are guest-side:
  ConnectEx incomplete chain, see below). Wagons landed:
  - SW-1221: LoadLibraryExA was a hardcoded NULL stub (Go's
    loadOptionalSyscalls throws "advapi32.dll not found") — now delegates to
    mw_LoadLibraryA like the W variant.
  - SW-1222: per-guest-thread TEBs (real host stack bounds via
    pthread_getattr_np, per-thread TLS pointer array, PEB shared) — Go's
    runtime.minit reads gs:[0x08]/[0x10]; glibc-GS garbage gave "bad g0 stack".
  - SW-1223: honest VirtualQuery classification (caller-stack VMA -> real
    bounds MEM_PRIVATE; guest image -> MEM_IMAGE; else MEM_FREE). Go's minit
    takes mbi.allocationBase+16K as the stack base — the old fixed 0x1000 gave
    base=0x5000 vs 140 TB spread -> "bad g0 stack" (second half of that throw).
  - SW-1224: 8 KiB TEBs (x64 inline TlsSlots live at TEB+0x1480 — the old 4 KiB
    page put them OUTSIDE the TEB) + TlsAlloc/GetValue/SetValue route through
    the TEB slots (per-thread; Go writes g via gs:(tls_g) DIRECTLY — same slot
    space now).
  - SW-1225: SetLastError/GetLastError write/read TEB+0x68 (real contract);
    Go's asmstdcall reads lasterror from there DIRECTLY and netpoll treats
    errno=0 as "fatal error: runtime: netpoll failed".
  - SW-1226: ConnectEx plane — WSAIoctl(SIO_GET_EXTENSION_FUNCTION_POINTER)
    returns mw_ConnectEx for WSAID_CONNECTEX {25a207b9-ddf3-4660-8ee9-76e58c74063e}
    (bytes CAPTURED from the guest's own WSAIoctl request); ConnectEx v1 =
    sync connect + completion packet posted to the socket's associated IOCP
    (CreateIoCompletionPort now records (handle -> port,key) associations);
    WSAGetOverlappedResult v1 = TRUE/0 bytes.
  - SW-1227: ntdll family — NtWaitForSingleObject (Go's usleep2 SLEEP
    mechanism; NULL made Go call address 0), RtlGetNtVersionNumbers (10.0
    build 19045), PowerRegisterSuspendResumeNotification (0).
  - OPEN SW-1228: the benchmark data path crashes in Go scheduler internals
    (runtime.g0/heap-bitplane deref after our NtWaitForSingleObject v1) — next
    round: decode the 0x434380/0x4326xx chain, likely needs a real g-pointer
    plane (runtime.tls_g already works) + goroutine-aware stack bounds.
- **SW-1229 Rust std remainder (SW-508) first 5 LANDED**: CreateNamedPipeW
  (delegates to the SW-1217 pipe layer), ReadFileEx/WriteFileEx (sync v1),
  InitializeProcThreadAttributeList/DeleteProcThreadAttributeList/
  UpdateProcThreadAttribute (honest size accounting). fd.exe (Rust 10.2.0
  MSVC) fail-fast advanced: first int3 layer cleared, NEXT layer = 12 APIs
  (GetTimeZoneInformationForYear, SetThreadStackGuarantee, ImpersonateSelf,
  OpenThreadToken, AccessCheck, NtReadFile, NtWriteFile,
  GetUserProfileDirectoryW, __current_exception_context, __CxxFrameHandler3,
  __current_exception, _get_initial_narrow_environment) = new SW-1230.
- **SWEEP-044 measurement**: census x3 16/16 DETERMINISTIC
  (scripts/sweep044_census.sh) — 15x exit-0 (was 13), pageant 124 (documented
  daemon park). **ZERO unresolved imports corpus-wide (was 1)**. MEASURED sha
  71d19afd G1 PIN 12/12 (evidence/master/g1/20261021_192040). Round trips on
  the final binary: 7za x64 create+extract BYTE-EXACT (cmp), jq '.a[1]' -> 2,
  sqlite3 :memory: SELECT -> 42. Game acceptances: snake 30f/3x identical,
  breakout 381f 40->13 bricks, calc 9/9 x2. evidence/sweep044/. no force-push.
- **Open**: SW-1228 (Go data plane), SW-1230 (Rust fail-fast layer 2), SW-1219
  (downloaded famous game, blocked by SW-509), SW-791 (curl libcurl-x64.dll),
  SW-509 (PE-DLL loader — the single biggest remaining unblock).

## SW-1231..SW-1240 — SWEEP-045 «۵ قطار بزرگ همزمان» — 5 big trains in parallel (2026-10-22, HEAD 08350c3 base)
Directive: «۵ قطار بزرگ همزمان ادامه ریل ۵ تای» — five BIG trains running at once on
the same rail. Base: origin/main 08350c3 (SWEEP-044), tree clean. Env-reset RECONCILE
first: workspace rolled back to 242af84 (5th), reset --hard to 08350c3, deterministic
rebuild; adexplorer64 corpus re-recovered (5th loss, sha-verified zip 56536bff /
exe e4512878 via the documented one-shot procedure).

- [x] **SW-1230 LANDED — fd.exe (Rust 1.73 MSVC) FULL LIFECYCLE, fail-fast layer 2
      CLEARED** (Train 2, six sub-wagons):
  - (a) 16 imports landed with real contracts: GetTimeZoneInformationForYear +
        TzSpecificLocalTimeToSystemTime (UTC engine model, identity conversion),
        SetThreadStackGuarantee (REAL contract: *in = request, *out = previous;
        the first draft invented a "<64KiB invalid" gate that rejected Rust std's
        0x5000 request and panicked every Rust guest — removed), ImpersonateSelf
        (TRUE) / OpenThreadToken (FALSE + ERROR_NO_TOKEN 1008) / AccessCheck
        (FALSE + 1338/5 typed), NtReadFile/NtWriteFile (sync v1 over the proven
        Read/Write layer, IO_STATUS_BLOCK filled, STATUS_UNSUCCESSFUL on fail),
        GetUserProfileDirectoryW (C:\Users\miniwin, ERROR_INSUFFICIENT_BUFFER
        two-pass), __current_exception/__current_exception_context (stable
        writable slots), __CxxFrameHandler3 (alias) + terminate (alias),
        _initialize_onexit_table/_register_onexit_function (REAL growth protocol:
        {_first,_last,_end}, 4-then-doubling realloc), _set_fmode (0/0x8000 gate,
        returns old), _get_initial_narrow_environment (g_initenv_ptr).
  - (b) mw_SetThreadStackGuarantee was the REAL fd boot blocker (Rust
        stack_overflow.rs Handler::new panics "failed to reserve stack space for
        exception handling" when SetThreadStackGuarantee(0x5000) fails) — web
        source of Rust 1.73 quoted in the worklog.
  - (c) mw_GetFileInformationByHandle DOUBLE FIX: dwFileAttributes was hardcoded
        0x20 ARCHIVE (dirs never reported 0x10 → fd "Could not retrieve current
        directory" — Rust fs::metadata.is_dir() false); nFileSizeHigh/Low were
        REVERSED (p+32 = High, p+36 = Low — an 82-byte .gitconfig reported
        0x5200000000 = 352 GB → Rust read_to_end "memory allocation of
        352187318272 bytes failed"). Both from fstat now.
  - (d) Per-thread implicit TLS: g_thread_tls[ti][0] was NULL for every spawned
        thread (the image static TLS block only reached the MAIN array) — any
        guest thread reading gs:0x58[0] crashed (fd SIGSEGV at teardown
        0x14021e0a3). Real Windows gives EVERY thread its own copy: per-thread
        copies now memcpy from a PRISTINE image snapshot (copying the live main
        block inherited main's thread_info pointer and tripped Rust's
        thread_info.is_none() assertion — pristine-copy is the fix).
  - (e) SW-1230 experiment REVERTED same round (documented): naked
        _CxxThrowException capturing the caller's RA (Rust panics ARE 0xE06D7363
        via vcruntime; the walk reached Rust's personality at fd's Frame[2])
        ALSO captured mingw guests via the name-only import table → 7za x 139.
        fd's lifecycle does NOT need it; v2 = DLL-aware EH routing.
  - RESULT: fd 9.0.0 = --version exit 0 "fd 9.0.0"; no-args walk exit 0 (real
    listing); 'fd txt' filter exit 0; --help exit 0. First Rust MSVC guest at
    full lifecycle. Binaries staged at artifacts/sweep045/bin/ (sha in
    evidence/sweep045/new_binaries_sha256.txt), acceptance in
    evidence/sweep045/fd_acceptance.txt.
- [x] **SW-509 v1 LANDED — real PE-DLL loader, first slice** (Train 3): ssh.exe
      (OpenSSH-Win64, the P0 "openssh×13 needs libcrypto.dll" class) statically
      imports libcrypto.dll; the EXE import loop now disk-loads unknown
      descriptors (exe dir, then cwd; extension optional; refcounted
      LoadLibrary/GetProcAddress/FreeLibrary surface):
      load_pe_dll = read whole file → PE32+ gate → anonymous mmap RWX →
      headers+sections copy → BASE RELOCATIONS (DIR64 type-10, delta from
      preferred ImageBase) → imports resolved name-only against the dispatch
      table (unresolved → the honest stub, counted: libcrypto 513/550) →
      export directory registered → DllMain(DLL_PROCESS_ATTACH) invoked.
      GetProcAddress resolves DLL-image handles FIRST (named + MAKEINTRESOURCE
      ordinal forms) from the mapped export directory. Guard: *.exe/*.sys
      descriptors NOT disk-loaded (procmon64 imports its own Procmon64.exe —
      loading it re-ran its CRT inside DllMain and 0→139'd the census; the
      guard restored 15x exit-0). Section-table fix: sectab = optional-header
      END (oh+SizeOfOptionalHeader), not the data-directory start (oh+112) —
      the first draft read data-dir bytes as section headers and memcpy'd OOB.
      EVIDENCE: evidence/sweep045/ssh_libcrypto_pe_dll_v1.txt — libcrypto.dll
      1.9 MB loads at 0x7fa53ded0000, 513/550 imports, DllMain runs the DLL's
      CRT deep into UCRT startup (GetCommandLineA "ssh.exe -V" from the DLL!).
      V2 frontier (SW-1233): DLL-CRT-in-DllMain (ucrt data imports + TLS of
      the DLL + __scrt conventions) — Signal 11 at libcrypto RVA 0x10312c.
- [x] **SW-1228 progress (Train 1, decode round)**: bombardier data path
      reproduced (Signal 11 RIP=0x432768 movzbl (%rcx),%dl RCX=3 after the last
      API RtlGetNtVersionNumbers). Chain decoded: the crash sits in the
      10.0-build≥15063 branch of Go 1.20's initLongPathSupport (version fields
      compared on-stack); 0x434460 = g.m toucher storing &local at m+0x288 and
      delegating to 0x434380 (mOS cluster m+0x278..0x2b8, m+0xe0 busy gate,
      result = *(m+0x290) treated as a BITMAP base, +3 byte OR 0x80 = mark bit);
      the incoming pointer = global 0xd477e0 (BSS, never initialized). Next:
      symbolize via pclntab and land whatever osinit step initializes it.
- [x] **SW-1231 (Train 5): fd.exe added to the working set as the Rust-MSVC
      representative** (17th program, outside the census dir for now);
      bombardier/curl/ssh staged + sha-recorded for the trains.
- [x] **SW-1232 LANDED (SWEEP-046 Train 3): 7za x64 EXTRACT SOLVED** — the
      frontier was NEVER the extract path: mw_CreateSemaphoreW was a LEGACY
      fake (private counter array + alloc_handle(0x5000+n), waits passed
      through instantly) while CreateSemaphoreA was real. 7za's MT-LZMA
      compressor synchronizes via CreateSemaphoreW(init=0,max=3) +
      ReleaseSemaphore + WaitForSingleObject(INFINITE): with the fake, the
      CREATE of a 1.3 MB payload produced a 123-byte archive 7za itself
      reported OK — the extract "Data Error"/139 was the honest symptom.
      Fix: W delegates to the real mw_sem_create. Round trip create+extract
      BYTE-EXACT x3 (sha 15d4c788..., evidence/sweep046/7za_roundtrip.txt).
      (The 139-vs-Data-Error shape difference between rounds = decode-side
      manifestation of the same truncated archive.)
- [x] **SW-1219 status**: unchanged (Chocolate-Doom/Zork/Frotz x86 or DLL-bound).
- **Measurement (SWEEP-045)**: census x3 16/16 DETERMINISTIC
  (evidence/sweep045/census.tsv) — 15x exit-0, pageant 124 documented park,
  ZERO unresolved corpus-wide, ZERO regressions; G1 PIN 12/12
  (evidence/master/g1/20261021_205933 sha 9910f09c, final pin 20261021_210456
  sha b33ec3f7); round trips jq '.a[1]'→2, sqlite3 6*7→42, 7za create byte-exact
  (evidence/sweep045/roundtrips.txt); games snake 30f + breakout 381f/13bricks
  byte-identical, calc 9/9.
- **SWEEP-046 round summary «۲۰ قطار»** (2026-10-22, binary 848fa108, G1
  20261022_032424): Train 1 SW-1233 LANDED-PARTIAL (five engine root causes:
  TEB-before-load_pe, export-dir off-by-2, Win32 module paths, Windows base
  environment, CreateWaitableTimerA/W+Cancel+QueueUserAPC — ssh.exe -V exit 0
  x3 deterministic; frontier = w32posix fd-monitor thread + banner). Train 2
  SW-509v2/SW-1230v2 LANDED-PARTIAL (DLL-aware EH: per-DLL .pdata in the
  dispatcher, image-base-threaded virtual_unwind, mw_cxx_throw_dll gated to
  DLL-image throws; 7z.exe full 7-Zip 25.01 usage banner exit 0 with 7z.dll
  PE-DLL load + DllMain CRT boot; frontier = guest FH4 personality returning
  ContinueExecution on the DLL-throw chain). Train 3 SW-1232 LANDED (see
  above). Census x3 15x exit-0 + pageant 124, ZERO unresolved, ZERO
  regressions; round trips jq/sqlite/zstd/lz4/7za-extract all green; games
  snake/breakout/calc golden. SW-1093 plan: 97 -Wformat warnings left in
  MW_TRACE call sites (4 trivial -Waddress/-Wcomment fixed this round).
- **Open**: SW-1228 (Go data plane), SW-1233 tail (w32posix fd-monitor
  thread), SW-1230v2 tail (7z.exe `i`/`a` FH4 chain), SW-1219 (famous game),
  SW-791 (curl MSVC variant with libcurl-x64.dll), SW-1093 (-Wformat audit).

## SW-1234..SW-1242 — SWEEP-047 «ادامه قطار» — 20 trains (2026-10-22, base a418e5c)
Directive: «ادامه قطار» (standing 20-train mandate). Baseline 848fa108 byte-exact
after fast-forward; adexplorer64 corpus recovery #7 (zip 56536bff / exe e4512878).

- [x] **SW-791 LANDED-PARTIAL — curl.exe (mingw 8.23.0 Schannel) `--version` exit 0 x3**:
      (1) UCRT tail _byteswap_ulong/_byteswap_ushort (api-ms-win-crt-utility) +
      freopen_s (stdio, errno_t contract); (2) SSPI gate — InitSecurityInterfaceA
      returned NULL -> real SecurityFunctionTableA, 28 slots wine-sspi.h layout,
      dwVersion=1, NTLM typed model (Enumerate/QuerySecPkg/AcquireCred real,
      handshake slots SEC_E_UNSUPPORTED_FUNCTION 0x80090302); (3) BUG-035 TWIN —
      raw glibc bsearch called curl's ms_abi comparator SysV-style (crash
      0x14000cbf8 mov (%rcx),%rcx RCX=0x61 on the 284x16-byte table) -> fixed
      with the proven qsort_r bridge pattern (mw_ms_bsearch_bridge).
      FULL VERSION BANNER exit 0. TAIL (SW-791 open): real HTTP — thrdpool
      resolver job never reaches getaddrinfo (WSAStartup ran, socket plane real).
- [x] **SW-1234 LANDED — win32 drive-path mapping, 3 surfaces** (shared
      (null) v(null) Sysinternals banner root cause): win32_module_path issues
      "C:\..." but w3_normalize_path mapped "X:\a" -> host-root "/a" (unopenable).
      Drive prefix now maps to the loader PROCESS CWD in w3_normalize_path +
      mw_fopen/mw__wfopen + w_ver_read_from_file. Corpus census UNCHANGED
      (15x exit-0 + pageant 124) = zero regressions.
- [x] **SW-1235 LANDED — version-resource plane** (whois64 end-to-end decode):
      (a) VerQueryValueA returned UTF-16 pointers into the block ("%s" read 1
      char) -> ANSI transcode; (b) VarFileInfo\Translation binary pass-through
      was position-fragile -> last-component detection; (c) all A-queries
      aliased ONE static buffer (5 saved pointers -> last value) -> 16-slot
      ring. RESULT: real "Whois v1.21 - Domain information lookup" banner;
      shared gate for the 8 remaining Sysinternals tools (SW-1238..1241).
- [x] **SW-1236 LANDED — gate batch**: GetComputerNameExA/W (identity +
      typed 87 levels), LogonUserA/W (1326), FindFirstFileExA (W-walker
      delegate), FillConsoleOutputCharacterA (vacuous), WNetAddConnection2A/W
      (225) + WNetCancelConnection2A/W (2250). whois64 1->0, pslist64 5->0.
- [x] **SW-1237 LANDED-PARTIAL — real DNS A-plane**: mw_gethostbyname was a
      typed WSAHOST_NOT_FOUND stub -> real getaddrinfo(AF_INET) delegate +
      winsock hostent builder. whois64 resolved COM.whois-servers.net (2),
      whois.iana.org (1), EXAMPLE.COM (2) = REAL host DNS. TAIL: socket
      connect never issues post-DNS (no socket()/connect() traced).
- [x] **SW-1242..registered — new corpus 16 guests** (artifacts/sweep047/bin,
      shas in evidence/sweep047/new_binaries_sha256.txt): NEW exit-0 FULL
      LIFECYCLE x3 = listdlls64, pipelist64, psloggedon64; psinfo64 prints the
      REAL SYSINTERNALS SOFTWARE LICENSE TERMS (two-language EULA, multi-KB);
      trurl.exe real error path exit 7 reproduces; du64/strings64/sdelete64/
      psservice64 real usage text exit 255.
- [ ] **SW-1238** accesschk64: 16 unresolved (NetShareEnum/NetShareGetInfo/
      GetEffectiveRightsFromAclW/EnumServicesStatusW/DeleteAce/RegGetKeySecurity/
      LsaNtStatusToWinError/LsaEnumerateAccountsWithUserRight/LookupPrivilege
      DisplayNameW/GetSecurityDescriptorSacl/GetSecurityDescriptorControl ...).
- [ ] **SW-1239** pslist64: PDH plane — "Processor performance object not
      found" (procexp64 'contentions' perf-counter class, BUG-078 kin).
- [ ] **SW-1240** logonsessions64: LSA/session plane gate (exit 1).
- [ ] **SW-1241** autologon64: "Could not open Winlogon registry key" —
      HKLM Winlogon plane + MessageBox dialog rendered (BMP evidence exists).
- [ ] **SW-1242** handle64: 139 crash, undecoded (first decode round pending).
- **Measurement**: census x3 31 programs DETERMINISTIC (artifacts/sweep047/
  census.tsv) — corpus 15x exit-0 + pageant 124 ZERO unresolved ZERO
  regressions; new: 3x exit-0 + 4x exit-1 gates + 5x 255 usage/EULA + 139;
  G1 12/12 (evidence/master/g1/20261022_050538); round trips jq 2/sqlite 42/
  zstd/lz4/7za byte-exact; games snake 30f/breakout 381f/13bricks/calc 15/15;
  ssh -V unchanged (falsified vs HEAD — message + exit 0 identical), fd 9.0.0
  intact. evidence/sweep047/. no force-push.
- **Open**: SW-791 HTTP tail, SW-1228 (Go data plane), SW-1233 tail,
  SW-1230v2 tail (7z FH4), SW-1237 socket tail, SW-1238..1242, SW-1219,
  SW-1093 (97 -Wformat).

## SWEEP-048 «چهار قطار گرافیک» — the four GUI trains (2026-10-22)

User directive: «این چهار تا برنامه گرافیک دار رو باید تست بکنی» — (1) ODF
viewer, (2) Windows calculator, (3) a program that does not exist on Linux
(free choice), (4) a game from the Windows XP games list.

- [x] **SW-1244 LANDED — w3 handle table** (the 7za-64-file wall): MAX_HANDLES
  64→8192 + CloseHandle file-slot recycling (mw_kobj_release now returns int;
  CloseHandle falls through to w3_release_handle which closes the backing fd
  and frees the slot). Before: 7za create hit "Cannot open output file:
  Error #80070004" on 811/874 files (slot 63 exhaustion — nothing ever set
  in_use=0). After: 874/874 rc=0. Unlocked BOTH FreeOffice and LibreOffice
  extraction through the engine's own 7za. MINWIN_HEAP_TRAP=warn diagnostic
  mode (default hard _exit(4) unchanged).
- [x] **SW-1245 LANDED — case-insensitive disk DLL search**: mspaint imports
  "MFC42U.DLL", disk had "mfc42u.dll" (ext4 stat is case-sensitive) → every
  MFC ordinal resolved to stub-0 → guest wrote rax+0x28 with rax=0 (SIGSEGV
  RVA 0x5bb4d). mw_dir_casefind readdir fallback added to
  mw_loadlibrary_disk_dll (exe dir then cwd, .exe/.sys guard kept).
- [x] **SW-1246 LANDED — crash_handler walker bounds**: the out-of-PE branch
  followed *(bt_rsp+8) as a RAW pointer chain with no stack bounds — a libc
  crash frame with garbage RBP killed the handler itself (Signal 7 at the
  walker, truncated diagnostics). Every deref now bounded to [rsp, rsp+0x8000]
  and chain values must be stack-plausible.
- [x] **SW-1247 LANDED — MFC42U.dll as a real PE-DLL (mspaint64)**: XP x64
  SP2 media's MFC42U.DLL (x64, 1,460,992B) loads beside mspaint; 613
  by-ordinal imports resolve from its 6,888-export table. mspaint's msvcrt
  gap closed: _ltow/_ultow/labs/__CppXcptFilter + _wsplitpath/_wtol/wcstod/
  time/localtime/gmtime/mktime/iswspace/_wcsdup/_wcsrev/_wcsupr/wcscspn/
  wcspbrk/swprintf(16-bit formatter)/vswprintf/fgetws/fputws/_expand; gdi32
  metafile family cookies (CreateMetaFileW/CloseMetaFile/PlayMetaFile/
  EnumMetaFile...), PolyBezierTo/PolylineTo/PolyDraw/ExtCreatePen/GetDCOrgEx;
  user32 wvsprintfW/GetTabbedTextExtentA/InSendMessage/WindowFromDC/
  SetWindowContextHelpId/CountClipboardFormats; kernel32 GetProcessVersion
  (0x00050002)/lstrcatW/lstrcmpiA; DATA plane __argc/__wargv (real UTF-16
  argv built in main). mspaint went 139 (crash at first MFC import) →
  **exit 134 at MFC AfxInit with 100% import resolution** (633 unresolved → 0).
- [x] **SW-1251 LANDED — UCRT stdio/locale/math/runtime tail** (msvcp140 +
  sal3/clucene boots): __stdio_common_vsprintf_s (mw_vfmt_a-backed
  vsprintf_s), _execute_onexit_table (REAL reverse-run + empty, on the
  SWEEP-045 growth table), strcpy_s/__strncnt/iswalnum/iswxdigit/btowc/
  ldexp/copysign/_dclass/_ldclass (FP class bits)/_callnewh/_invoke_watson
  (typed _exit(3))/ _set_new_handler/_get_stream_buffer_pointers/_wfsopen,
  _Getdays/_Getmonths/_Gettnames/_W_* /_Strftime/_Wcsftime (C-locale plane),
  ___lc_collate_cp_func/___lc_locale_name_func (L"C")/__pctype_func (real
  256-entry _pctype)/_lock_locales/_unlock_locales, DisableThreadLibraryCalls/
  FlushProcessWriteBuffers.
- [x] **SW-1250 LANDED — load_pe_dll recursive dependency disk-load**: v1
  matched only ALREADY-loaded deps; mergedlo.dll's libxml2/libxslt/sal3/
  clucene descriptors all fell to the dispatch table (2,862 unresolved;
  soffice died on the first libxslt call). Now disk-loads its own unknown
  descriptors (idempotent via refcount registry); msvcp140/vcruntime140
  dropped from the known-dll pseudo-handle list so the REAL DLLs load
  (vcruntime140_1 x64 too). LibreOffice 26.8.1 x64: 19,427 files extracted
  through the engine's 7za; import forest 2,862 → **0 unresolved** across
  mergedlo (152MB, 50,142 export names) + sal3 + clucene + msvcp140 (1,515
  names) + libxml2/libxslt/vcruntime140*.
- [ ] **SW-1247-open** mspaint/mshearts: "malloc(): corrupted top size" abort
  during MFC AfxInit AFTER 100% import resolution. Corruptor = an MFC init
  initializer (MFC42U RVA 0xa3d60 → tail-jmp 0x4b400). MINWIN_HEAP_GUARD=1
  regresses the DLL load (SW-1248) so the guard cannot localize it yet.
  Next: fix SW-1248 then guard-run mspaint for the exact OOB writer.
- [ ] **SW-1243** 7za-under-miniwin: `l -tISO` on a 617MB ISO = SIGSEGV
  (RIP in 7za, RVA 0x5c125); CARDS.DL_ LZX expand = SIGSEGV (10/11 XP cabs
  expand fine). Decode pending; workarounds used this round: pycdlib for
  ISO listing/extract.
- [ ] **SW-1248** MINWIN_HEAP_GUARD=1 regresses the SW-509 DLL load path
  (MFC42U ordinals unresolved under guard; guard mode is diagnostic-only).
- [ ] **SW-1252** soffice.bin: msvcp140.dll DllMain zeroes exactly 8 bytes of
  its own .text (RVA 0x1110) during early CRT boot; section verify proves
  image==file pre-DllMain; last traces = __acrt_iob_func(2) + malloc(2)/free
  churn. Next: mprotect .text post-reloc to catch the writer, or decode
  __acrt_iob_func's write path.
- [x] **new corpus 10 guests staged** (artifacts/sweep048/stage, shas in
  evidence/sweep048/provenance_sha256.txt): XP x64 SP2 OEM ISO
  (archive.org crmpxoem-en, 628,617,216B) → AMD64 accessories expanded BY THE
  ENGINE's own 7za (calc/winmine/mspaint/sol/freecell/mshearts/spider/notepad/
  pinball + MFC42U.DLL); Everything 1.4.1.1028 x64 (voidtools zip); LibreOffice
  26.8.1 x64 MSI (documentfoundation) → olefile CAB carve → engine 7za
  extraction (19,427 files).
- **Measurement** (final binary dce5784c...): G1 PIN 12/12 (evidence/master/g1/);
  corpus census x3 DETERMINISTIC 14× exit-0 + pageant 124, ZERO regressions
  (artifacts/sweep047/census.tsv); new census x3 DETERMINISTIC
  (artifacts/sweep048/census/census.tsv): calc/winmine/spider/freecell/notepad/
  pinball exit-0 (winmine ZERO stubs), everything 124 park, mspaint+mshearts
  134 (SW-1247), soffice 139 (SW-1252); batteries: snake 30f byte-identical
  3x3, calc 9/9 + 15/15, breakout 381f/13 bricks, BUG-054b 3/3; winmine
  3 MessageBox dialogs RENDERED + GDI frame (evidence/sweep048/screenshots/);
  real XP calc dialog clicks delivered (WM_COMMAND id=131/132 into the
  guest's own wndproc). evidence/sweep048/. no force-push.

## SWEEP-049 «ادامه با برنامه های واقعی» — real-program deep-dive (2026-10-22)

- [x] **SW-1253 LANDED — KUSER_SHARED_DATA page (0x7FFE0000)**: winver.exe
  5.2.3790 x64 SIGSEGV'd in its first basic block: `48 8b 04 25 c8 02 fe 7f`
  = `mov rax,[0x7ffe02c8]` (QpcFrequency) with NO page mapped (crash RIP =
  guest RVA 0x11d8, Return addr 0x0). Engine now maps the 1-page KUSD
  RW→fill→mprotect(PROT_READ): InterruptTime/SystemTime KSYSTEM_TIME (real
  FILETIME), NtSystemRoot L"C:\WINDOWS", NtProductType=1, version 5.2,
  SuiteMask 0x110, QpcData=1, ActiveProcessorCount=host, QpcFrequency=1e9
  (MUST match mw_QueryPerformanceFrequency ns contract). winver 139→exit-0,
  0 stubs. Guest-image-overlap guard included.
- [x] **SW-1254 LANDED — raw ordinals bind against REAL disk-DLL exports**:
  the SW-509 IAT block only tried mw_dll_export(xd, func_name, 0) — ordinals
  missing from the static known-ordinal map fell to "not supported"→stub-0
  WITHOUT consulting the loaded DLL (mspaint: 613 MFC42U ordinals, ILT
  byord=613/byname=0, EAT 6,888 exports). New pre-static pass:
  mw_dll_export(xd, NULL, ordinal) for disk-DLL descriptors. Guard-mode
  mspaint unresolved 625→0. NOTE: SWEEP-048's "mspaint 633→0 unresolved"
  was an early-death artifact — the process died inside mfc42u DllMain
  BEFORE the import loop reached the MFC descriptor (trace: 1 Import DLL
  line, no [N] Ordinal lines); the ordinals never bound.
- [x] **SW-1255 LANDED — GetDeviceCaps HORZSIZE(4)/VERTSIZE(6) real math**:
  both returned 0 → mspaint's pixels→percent math `idiv r10d` with r10d=0
  → SIGFPE at RVA 0x35e8e (RAX=0x9c400=640k; witness bytes 41 f7 fa).
  Now HORZSIZE = round(HORZRES*25.4/LOGPIXELSX) = 169mm @640x96dpi (VERTSIZE
  127). Matches the real GDI contract.
- [x] **SW-1256 LANDED — honest window-tree relations (MFC frame/view)**:
  mspaint under guard-mode recursed 104,715 x GetDlgItem(0xE900
  = AFX_IDW_PANE_FIRST) → 8MB stack overflow (8388608/0x50 frame ≈ 104857,
  exact census match; RSP page-aligned, RIP in libc). Root chain:
  (1) every top-level CreateWindowExW returned the single MW_GUI_HWND so
  MFC's view created inside the frame's WM_CREATE shared its parent's
  handle; (2) GetTopWindow(h) ALWAYS returned MW_GUI_HWND (non-NULL for
  any input) and GetWindow(h,*) always NULL → MFC's GetDescendantWindow
  walk (mfc42u 0x6533c: GetTopWindow → recurse → GetWindow(GW_HWNDNEXT))
  self-looped forever. Fixes: nested top-level creations (g_wm_create_depth
  gate) mint DISTINCT identities 0x11000+n*0x10 registered {hwnd,parent,
  id=hmenu} + receive their own synchronous WM_CREATE with hwndParent;
  GetTopWindow/GetWindow(GW_CHILD/HWNDNEXT/HWNDPREV/HWNDFIRST/HWNDLAST)/
  IsChild now registry-backed over BOTH child registries (empty families
  return NULL per the real contract); GetDlgItem/GetParent consult the
  toplevel registry; SendMessage W/A forward to registered mints.
  mspaint GetDlgItem count 104,715→3.
- [x] **SW-1257 LANDED — end-placed heap guard catches the SW-1247 writer;
  SW-1247 ROOT CAUSE FIXED = mw___dllonexit table overflow**: v1 guard put
  the body at region START so page slack ABSORBED overflows (guard-mode
  mspaint exited 0 with the corruptor uncaught!). End-placed bodies (body
  ENDS exactly at the PROT_NONE page, 16-byte aligned) fault on ANY
  overflow — first catch: mw___dllonexit+0x22 `mov [rcx],rax` writing 1
  byte past a FULL 256-byte onexit table (RCX=guard page). Old contract
  "trust the guest CRT table, write at *end" is WRONG: real CRT (wine
  msvcrt __dllonexit) GROWS the table with realloc to count+1, appends,
  bumps *pend, reseats *pbegin. Implemented via mw_realloc (guard-registry
  consistent; raw realloc desyncs the registry and traps the later free).
  RESULT: mspaint exit 134→**0** (NG), mshearts 139→**0** (after mfc42u.dll
  staged next to it — it also imports MFC by ordinal), wordpad 134→139
  (deeper frontier SW-1258). 'corrupted top size' GONE.
- [x] **SWEEP-049 new real-guest corpus staged + censused x3** (payload
  provenance sha-pinned in evidence/sweep049/provenance_sha256.txt): XP
  Pro x64 SP2 OEM ISO re-fetched from archive.org (sha 19b5a382... = exact
  SWEEP-048 provenance match) → 23 payloads (.EX_/.DL_) expanded BY THE
  ENGINE's own 7za (samples/real/7za.exe) — x64 CARDS.DL_ expands fine
  (the SW-1243 trap is the x86 SP3 CAB); guests: regedit(reget32 stub)/
  taskmgr/charmap/clipbrd/write/winver/net/ping/sndvol32/drwtsn32/wordpad/
  mspaint/mshearts/sol. census x3 DETERMINISTIC (artifacts/sweep049/census/
  census.tsv): mspaint/mshearts/winver/charmap/clipbrd/write/sol/taskmgr/
  regedit/sndvol32 exit-0; net rc=2 + ping rc=1 (honest usage errors);
  wordpad 139 (SW-1258), drwtsn32 139 (SW-1259). DL corpus x3 14× exit-0 +
  pageant 124 ZERO regressions. G1 PIN 12/12 (evidence/master/g1/
  20261022_144016, binary a29ca1e7). calc battery 15/15.
- [x] **SWEEP-050 staging done** (payloads on disk, gitignored): LibreOffice
  26.8.1 x64 MSI (375MB, sha c6298b10...) for SW-1252 soffice decode;
  Everything 1.4.1.1028 x64 zip expanded (sha 4f4a9b92...).
- [x] **SW-1258 LANDED — wordpad 139→exit-0 (clean declined startup) + the
  menu/hMenu contract**: decoded to a THREE-layer chain: (1) RICHED20.DLL
  (disk-DLL) imported 16 missing names (FindAtomW/A, GetProfileSectionW/A,
  GetProfileIntA, GetStringTypeExA, CreateMetaFileA, Set/GetMetaFileBitsEx,
  CreateICA, UnregisterClassA, IsWindowUnicode, LoadBitmapA, CharUpperA/LowerA,
  CharLowerBuffW) — implemented honest; (2) SHLWAPI.231 = ZoneCheckUrlExW
  (wine shlwapi.spec) whose stub-0 return drove `mov rcx,[rax+0x40]` at RVA
  0x2fae0 (RAX = virtual-call return) — named + implemented + the vtable
  walker (vtbl 0xafd8 slot 83 -> RVA 0x2d4c0, "CRichEdit2View" IsKindOf
  scan via MFC42u.4375) decoded through the MINWIN_VTBL_DUMP diagnostic
  (new, env-gated); (3) the guest's frame WM_CREATE returns -1 (MFC
  guest-internal decision after the toolbar — the RICHEDIT child + toolbar
  BOTH mint successfully first). The engine violated the real contract by
  minting the hwnd anyway; now: WM_CREATE=-1 -> CreateWindowEx(W/A)
  returns NULL on BOTH the base and nested (SW-1256) paths, the mint is
  unregistered, and MFC's InitInstance=FALSE path exits cleanly (exit(0)
  in the trace). Also: real hMenu contract — CreateWindowExW attaches a
  non-NULL top-level hMenu as the window menu (g_main_menu); mshearts'
  menu-update path (GetMenu->EnableMenuItem 124,1) SIGSEGV'd RVA 0x9d5f
  on GetMenu==NULL before this. Continuation SW-1260: which guest check
  inside wordpad's OnCreate still fails (the -1 producer after the
  toolbar's imagelist StretchDIBits + free).
- [x] **SW-1259 LANDED — drwtsn32 139→exit-0, ONE ROOT CAUSE = LoadResource
  contract**: drwtsn32 reads LoadResource's result DIRECTLY
  (`edi = *(u16*)LoadResource(...)` then a 16-entry length-prefixed
  RT_STRING walk) — real Windows LoadResource (32-bit era onward) returns
  the DATA POINTER with LockResource as a documented no-op, but the engine
  returned the IMAGE_RESOURCE_DATA_ENTRY VA, so the walker parsed the
  16-byte ENTRY HEADER as string lengths and stepped past the image
  (SIGSEGV RVA 0x5cb3, fault 0x41f932 > image end 0x417000). Fixed:
  LoadResource returns g_image_base + entry->OffsetToData; LockResource
  is the identity no-op. v2 heuristic LockResource ("first u32 <
  image_size looks like OffsetToData") misfired on real data (mshearts
  BITMAP 'BM' = 0x4D42 < image_size -> double deref -> SIGSEGV RVA
  0x9d5f) and was removed — identity is the honest contract. drwtsn32's
  7-name tail (ClearEventLogW, dbgeng!DebugCreate, DelayLoadFailureHook,
  IsTextUnicode real heuristics, _wmakepath, swscanf real parser,
  wprintf) implemented honest.
- [x] **SW-1252 pre-work landed — section verify made RELOC-AWARE**: the
  SW-1250 diagnostic compared each section's first qword to the RAW file
  post-relocs, so a RELOCATED pointer false-positived (riched20 sec1 RVA
  0xfc000). Now collects the section's reloc targets and skips covered
  qwords; a NON-reloc mismatch is the honest SW-1252 "image corrupted /
  DllMain self-modified" signal. LibreOffice 26.8.1.1 x64 MSI re-fetched
  sha-EXACT (c6298b10...) for the SW-1261 soffice decode.
- [x] **SW-1258b everything.exe (SWEEP-048 park-124) RESURRECTED exit-0**:
  the 25-import tail implemented honest (kernel32 console/calendar/
  FindFirst-Next-CloseChangeNotification/GetEnvironmentStrings A-side/
  SetHandleCount/GetStringTypeA, user32 keyboard-layout/AdjustWindowRect
  (=Ex with ex=0)/OpenIcon/AttachThreadInput, gdi32 GetRegionData
  RDH_RECTANGLES + GetRandomRgn, advapi32 event-log trio, shlwapi
  PathIsRootW/SHRegGetUSValueW, shell32 DragAcceptFiles/DragQueryPoint/
  ILFindLastID via SHELL32.16 = ILFindLastID (wine shell32.spec 4.72),
  COMCTL32._TrackMouseEvent). Live message pump (WM_TIMER dispatch,
  GetWindowLongPtrW GWLP_USERDATA reads) then budget-WM_QUIT exit 0 x3.
- [x] **SW-1247-residual CLOSED to KNOWN-UNKNOWN stubs**: ~180 names
  across charmap/clipbrd/sndvol32/net/taskmgr/regedt32/wordpad/everything
  implemented over three waves (real: LineDDA Bresenham + guest callback,
  IsTextUnicode stats, wcstok/_wmakepath/swscanf/sscanf parsers, Rtl*
  string plane, Net* workgroup-empty model, GetFileTitleW, FoldStringW
  case maps, comctl DllGetVersion 5.82; honest-miss: winmm no-hardware
  MMSYSERR_BADDEVICEID, DDEML no-server DMLERR codes, NDDEAPI empty share
  DB (wine nddeapi.spec 500-512), setupapi ERROR_FILE_NOT_FOUND, ICMP
  IP_REQ_TIMED_OUT, ole32 storage STG_E_FILENOTFOUND). Ordinals named
  from shipped wine specs: COMCTL32.2/8, SHLWAPI.413/437, SHELL32.61/100.
  REMAINING KNOWN-UNKNOWN (Rule 1, never called): NDDEAPI.609-612,
  SHELL32.236/241/245.
- [ ] **SW-1247-residual**: the remaining per-guest unresolved tails
  (sndvol32 5, taskmgr 1, regedit 1, sol 3, charmap 9, clipbrd 49, net 4)
  are the standard import-surface work; SW-1247 itself (AfxInit heap
  corruption) is CLOSED by the __dllonexit fix.
- [ ] **SW-1260** wordpad OnCreate -1 deep decode: the guest-internal
  check that still fails after the toolbar (RICHEDIT child + toolbar mint
  OK; LoadStringW(61443)->0 resource-absent is the last API before the
  MFC string-conversion cycles). Target: full wordpad UI startup.
- [x] **SW-1261 LANDED (stage 1) — LibreOffice (the ODF display program)
  bootstraps end-to-end: 139 SIGSEGV -> deterministic rc=99 with the full
  57-DLL graph mapped, CreateWindowExW + WM_CREATE delivered, real FH4 C++
  EH unwinding through DLL frames**: SIX root causes, each proven with an
  instrument (see evidence/sweep051/README.md): (1) engine's
  _get_stream_buffer_pointers was 8-arg, real UCRT is 4-arg (wine
  ucrtbase.spec L380) — the v1 stub zeroed 8 bytes of msvcp140 .text AT THE
  CALL'S RETURN ADDRESS (guest's stdio-init thunk RVA 0x65e8 forwards its
  own frame; caught with a ptrace DR0 hardware watchpoint, the decisive
  instrument, scripts/sweep051_watch.c); (2) disk-DLL static TLS was skipped
  (SW-509 v1 documented limit) — sal3 SIGSEGV RVA 0x6D654; full TLS plane
  implemented: slot alloc + __tls_index write + g_tls_ptr_array publish +
  ATTACH callbacks + module registry for per-thread pristine copies;
  (3) MW_DLL_MAX 12->128 + EARLY slot registration (re-entrant dep chains
  duplicated images and silently dropped mergedlo's tail deps);
  (4) alloc registry 8192->524288 (mergedlo's init wave overflowed it ->
  HEAP-TRAP exit(4)); (5) DLL .pdata cap 65536->4M (mergedlo has 756,578
  entries — the truncation killed C++ EH through mergedlo frames);
  (6) TlsAlloc no longer writes the static-TLS array (cppu3 slot-4 collision,
  SIGSEGV RVA 0x13fc5). Payload chain: LO 26.8.1.1 MSI c6298b10 -> payload
  CAB (LZX) extracted 19427/19427 via source-built libmspack (no root) ->
  guest staging with hyphen-renames (orcus-parser, libcrypto-3, libssl-3,
  libxmlsec-mscng). Census: soffice x3 deterministic rc=99 / 10 stubs; 19
  SWEEP-050 guests byte-identical; DL corpus = HEAD (autoruns64/zoomit64 139
  A/B-verified pre-existing, NOT a regression); G1 12/12; calc 15/15.
- [x] **SW-1262 LANDED (SWEEP-052 Train 1) — cross-DLL C++ catch-type matching
  WORKS: the mergedlo catch now CLAIMS the cppuhelper3MSC throw end to end.**
  Root cause #1: engine mw_RtlPcToFileHeader v1 knew only the EXE image, so
  the guest's REAL vcruntime140!_CxxThrowException (disassembled: EI[3] =
  RtlPcToFileHeader(ThrowInfo) = the THROWING image base; EI[0]=0x1994000 is
  the fallback magic when that lookup FAILS) passed EI[3]=0 and the real
  vcruntime140_1 __CxxFrameHandler4 resolved the FH4 image-relative
  ThrowInfo4/CatchableTypeArray RVAs against base 0 — no catch could EVER
  match. Fixed (mw_module_of). MEASURED: DeploymentException (".?AV
  DeploymentException@uno@star@sun@com@@", base chain -> RuntimeException ->
  Exception, disk-decoded from cppuhelper3MSC RVA 0x145bd8) now claimed —
  RtlUnwindEx unwinds to the mergedlo catch landing pad. Root cause #2:
  AccessCheck v1 answered FALSE unconditionally — sal3's pre-open check
  (GetNamedSecurityInfoW -> DuplicateToken -> AccessCheck 0x120116)
  concluded uno.ini/services.rdb inaccessible ('Cannot obtain UNO_SERVICES
  from uno ini' — decoded via the new [CXEH] uno::Exception Message dump;
  the non-polymorphic uno::Exception keeps its Message rtl_uString* at
  object+0). Fixed: AccessCheck v2 REAL DACL semantics (NULL-DACL grant-all,
  ACE walk, session SIDs, generic->file mapping) + DuplicateTokenEx v2 real
  duplication + DuplicateToken (3-arg) added. uno.ini/fundamental.ini/
  types.rdb/services.rdb now staged beside soffice.bin (SW-1261 had staged
  only soffice.ini). MEASURED: soffice x3 deterministic rc=99 stub 8 (was
  10); bootstrap reaches the services.rdb stage. G1 12/12
  (evidence/master/g1/20261022_195936, binary 69a015b9); regression census
  zero deltas vs SWEEP-051 baseline (evidence/sweep052/regression.tsv;
  procexp64 124 IS its SWEEP-051 baseline value). Instruments: [CXEH] FH4
  throw decoder (types + messages), [STUB] import-name registry, CreateFileW
  caller attribution, GetModuleFileNameW output log; DR0 watch runs
  (scripts/mw_watch build of scripts/sweep051_watch.c).
- [~] **SW-1263 (SWEEP-052 Train 2, IN PROGRESS) — root cause #3 LANDED**:
  GetModuleHandleExW(FROM_ADDRESS) v1 always returned the EXE base and
  GetModuleFileNameW v1 ignored the handle entirely (EXE path for every
  module). Both fixed: FROM_ADDRESS resolves the address's OWNING module via
  mw_module_of; GetModuleFileNameW resolves guest-DLL handles through
  MwDllImage.path. MEASURED (binary 565b0803, G1 12/12 at
  evidence/master/g1/20261022_203645): FROM_ADDRESS now answers
  cppuhelper3msc.dll/mergedlo.dll correctly, per-module paths are exact
  (mergedlo.dll 49 chars, cppuhelper3MSC.dll 55 chars), bootstrap file I/O
  volume 63 -> 186 CreateFileW. REMAINING frontier: the rtl_uString holding
  the uno.ini URL is still constructed truncated inside the sal3/cppuhelper
  string chain (cut at 14/22/23 chars: 'C:\artifacts\s',
  'C:\artifacts\sweep051\g') — an allocator/length-header class bug
  downstream of the (now-correct) module queries; calc/winmine/mspaint
  regression x1 green after the change. Target: clean rtl_uString -> services
  .rdb open -> UNO service manager -> past bootstrap into the VCL pump.
- [ ] **SW-1261b** Everything 1.4.1 full lifecycle (UI interaction) queued
  behind the calc-battery-style click harness (unchanged).
- [x] **SW-1264 LANDED (SWEEP-053 Train 1) — engine-internal resource
  consumers now honor the LoadResource DATA_ENTRY->data contract**: v1
  mw_dlg_from_resource handed the IMAGE_RESOURCE_DATA_ENTRY (from
  w3_res_find) to mw_LockResource — an identity no-op since SW-1259 — so
  the DLGTEMPLATE parser read the 16-byte ENTRY HEADER as the template
  (dlgver = low half of the data RVA, sig = high half — never 1/0xFFFF),
  the EX branch skipped and the STD walker consumed a header -> ZERO
  dialog children -> every GetDlgItem NULL. Decoded from XP calc.exe
  (dialog-based app: CreateDialogParamW(hInst, MAKEINTRESOURCE(0x66), 0,
  NULL, 0)). Fix: convert entry -> g_image_base + entry.OffsetToData in
  mw_dlg_from_resource AND mw_loadimage_bitmap_dib (RT_BITMAP DIBs were
  equally unreadable). MEASURED (binary 4e6e4a44): calc dialog 0x66
  resolves (size 1196, DLGTEMPLATEEX cdit=30) — EDIT id=403 + 29 BUTTONs
  materialize with template rects.
- [x] **SW-1265 LANDED (SWEEP-053 Train 2) — real LoadStringW/A**:
  RT_STRING blocks (resource id = (id>>4)+1, 16 length-prefixed UTF-16
  strings per block, index = id&15, real truncation contract). v1 stub
  always answered 0 — XP calc's 86 control-label loads all came back
  empty. MEASURED: id=0 -> 3 wchars, id=1 -> 1, id=3 -> 9 ... all 86
  resolved.
- [x] **SW-1266 LANDED (SWEEP-053 Train 3) — per-window subclass chain +
  standard-control compositor; XP calc RENDERS**: calc WinMain
  (0x100002364..0x1000023a7, disassembled) subclasses GetDlgItem(403) via
  GetWindowLongPtrW(child, GWLP_WNDPROC) -> test -> SetWindowLongPtrW;
  v1 answered 0 so the `test rax,rax; je` skipped the subclass and calc's
  real WndProc 0x100008bc0 never ran. calc's proc is a pass-through
  (HideCaret + CallWindowProc(saved old)) — painting belongs to the
  ORIGINAL control proc. Fixes: (1) mw_engine_defproc — real
  guest-callable DefWindowProc-subset; GetWindowLongPtrW(GWLP_WNDPROC)
  answers it non-zero for engine windows (guest `if (old)` checks pass;
  CallWindowProc chains honestly); (2) GWLP_WNDPROC is PER-WINDOW store
  state, global g_wndproc_fwd stays the main-window channel; (3)
  SendMessageW routes guest-subclassed children through their stored
  proc; (4) mw_engine_paint_children — dialog-template children rendered
  at template rects (DLU x2) with Win95 chrome into the paint surface,
  one frame per UpdateWindow; template cx,cy sizes the surface
  (grow-only mw_gui_store_window resize); (5) SW-1266b: SetWindowPos/
  MoveWindow grow the surface (winmine sizes its board via MoveWindow
  after a 2x0 create). MEASURED: **XP calc renders its full face
  (338x258: display '0.', Backspace/CE/C, MC..M+, full keypad — 30
  controls)**; winmine board chrome 170x230 area (LED counters, sunken
  field, raised border) + 3 Win95 messageboxes; Everything main window at
  its real 778x605; freecell card-back blits 640x480. G1 12/12
  (evidence/master/g1/20261022_215001, binary f26bf49a pre-1266b; the
  1266b change is census-covered); census zero deltas vs SWEEP-052
  (artifacts/sweep053/census/regression.tsv; soffice rc=99 stub 8 = its
  baseline). Screenshots: evidence/sweep053/shots_png/ (user copy
  /home/z/my-project/download/screenshots/).
- [ ] **SW-1267** sol/mshearts surface sizing: deck/board draws land
  before MoveWindow grows the surface (sol frame 154x64 = status bar
  only; mshearts create decodes 79712x0 -> clamped 1280x32 strip). Needs
  the guests' sizing order decoded (MoveWindow fires pre-paint).
- [ ] **SW-1268** Everything content paint: the guest paints white
  because its list content is driven by its NTFS index (absent headless);
  a fake index volume (or a stubbed index reader returning fixture rows)
  would give the list real rows to paint.

---

## SWEEP-054 «۱۰ برنامه و ۵ بازی معرف دو بعدی قدیمی» — marks (2026-10-23)

- [x] **SW-1269 LANDED (SWEEP-054) — ShowWindow first-show paint + SW-1269b
  SW_MAXIMIZE surface grow.** Real Windows generates the first WM_PAINT when
  a window is shown; v1 only fired the CBT hook, so guests waiting for the
  system's first paint (XP spider: ShowWindow(SW_MAXIMIZE) + plain GetMessage
  loop, no InvalidateRect of its own) never painted — the pump saw "no event
  source" and quit with a white screen. Fix: mw_ShowWindow requests paint
  when events are live; SW_MAXIMIZE additionally grows the paint surface to
  the 1024x768 work area (CW_USEDEFAULT had left it 32x32; spider's board is
  704x540). MEASURED: spider's pump now delivers WM_PAINT; mspaint renders
  too (same root cause).
- [x] **SW-1270 LANDED (SWEEP-054) — posted-message queue.** PostMessage A/W
  dropped every message headlessly; real Win32 queues posted messages and
  delivers them from GetMessage BEFORE generated (lower-priority) messages.
  Spider posts its own WM_COMMAND (menu deal) before its pump starts and
  waits for it. Fix: 64-slot ring, delivered first in mw_gui_next_event;
  headless batch keeps the v1 drop contract (regression-pinned exit-0).
- [x] **SW-1271 LANDED (SWEEP-054) — REAL accelerators.** Root cause decoded
  from spider disassembly (RVA 0x10000bebf): its classic pump
  `cmp msg.hwnd,main; jne dispatch; test rbx,hAccel; je loop` — with
  hAccel==NULL the MAIN window's messages skip TranslateMessage/
  DispatchMessageW entirely; WM_PAINT was retrieved but never reached the
  WndProc. Real XP spider.exe ships RT_ACCELERATOR id=1: 96 B = 16 entries
  (F1..F5, D/M/N/O/S/Z + Ctrl/FALT variants — 6-byte ACCEL records).
  LoadAcceleratorsW/A now parse type-9 resources via w3_res_find and mint
  SW-1128 store handles; TranslateAcceleratorW/A translate WM_KEYDOWN/
  WM_CHAR against the table (FVIRTKEY VK; FSHIFT/FCONTROL/FALT vs the
  scripted-key latch) and SEND WM_COMMAND MAKEWPARAM(id,1) on match.
  REGRESSION CAUGHT PRE-LANDING: the A-path trace formatted
  MAKEINTRESOURCE names (small integers) as '%s' -> SEGV; ProduKey/cports
  rc=0->139 in events mode, bisected to the trace, fixed with an
  IS_INTRESOURCE guard (binary 09c33df2; G1 12/12 re-pinned).
- [x] **SW-1272 LANDED (SWEEP-054) — BitBlt frame capture + SW-1272b
  MINWIN_FRAME_EVERY throttle.** A SRCCOPY into the window surface is a
  presented frame (SWEEP-006 breakout contract); XP spider never calls
  BeginPaint/EndPaint — its whole board is direct BitBlts, so the EndPaint
  dump path never fired. MINWIN_FRAME_EVERY=N thins DISK WRITES only
  (numbering unconditional): spider's deal animation presented ~3,700
  frames x 2.3 MB = 8.6 GB in one run and filled the disk during capture.
- [x] **SW-1274 LANDED (SWEEP-054) — REAL timeGetTime.** Spider's deal
  animation is paced by timeGetTime deltas; the v1 stub-fallback returned 0
  forever, freezing the clock on the first card (identical blit pairs
  looping). Now the engine's monotonic ms clock (Win32 DWORD-since-boot
  contract). MEASURED: deal animation COMPLETES, rc=0.
- [x] **SW-1275 LANDED (SWEEP-055) — XP spider FULL GREEN BOARD. Three
  engine root causes, all disassembly-decoded from spider.exe**:
  (1) 29 of spider's 56 RT_BITMAPs are OS/2-format BITMAPCOREHEADER DIBs
  (12-byte header, WORD w/h, RGBTRIPLE palette — CARD2/12/13/24...); the
  v1 decode gate "hdr_size >= 40" rejected every one -> 16x16 placeholder
  handles -> SelectObject(memdc, 0x0006xxxx IMAGE-space) -> first board-DC
  BitBlt died "bad dst/src DC" -> blank faces. mw_loadimage_bitmap_dib now
  decodes BOTH header formats (core: WORD fields + 3-byte BGR triples).
  (2) The bitmap/DC slot pool (MW_MEMDC_MAX=32, shared by
  CreateCompatibleBitmap/CreateDIBSection/LoadImage decodes) exhausted
  mid-deal — each LoadImageW decode leaks its slot for the process life
  (mw_DeleteObject only freed brush/pen). Pool 32 -> 256 (v&0xFF handle
  ceiling) + DeleteObject now frees unselected HBITMAP slots (real GDI
  refuses to delete selected objects — honored). (3) mw_engine_paint_children
  wiped the WHOLE surface gray (dialog face) on every UpdateWindow even
  for childless windows — spider's guest-blitted board was erased by its
  own UpdateWindow. Fill only when the window has >=1 live child (calc's
  dialog face unchanged). Also added DIB-miss/FRES diagnostics to the
  LoadImage/FindResource paths. MEASURED (binary c5635ee8): spider renders
  the classic green felt board (645 frames), 10 columns of striped backs,
  face-up cards A♠ 3♦ 2♦ Q♥ 8♦ 4♥ 8♠ A♠ 4♥ 4♠ with real art, spider-logo
  deck bottom-right; census 3x exit-0 ZERO stubs; regressions zero (calc
  338x258 + winmine/sol/freecell/mshearts unchanged). Screenshots:
  evidence/sweep055/shots_png/.
- [x] **SW-1273 CORE LANDED (SWEEP-055) — LibreOffice mirror resolved,
  payload re-staged byte-exact**: downloadarchive .../old/26.8.1.2/ 404s
  but .../old/26.8.1.1/ is ALIVE (HTTP 200) and byte-identical to the
  SWEEP-050 pin: MSI 374,870,016 B sha256 c6298b10... MATCH
  (scripts/sweep055_lo_restage.py, 6-stream ranged). CAB carved from the
  MSI via olefile (the payload stream is binary-UTF16-named; MSCF-magic
  scan) sha 71dfff0b... MATCH (sweep055_lo_carve.py); expanded BY THE
  ENGINE's own 7za: 19,427/19,427 files rc=0; soffice.bin cb08cffd /
  mergedlo.dll 017d0c21 pins verified post-staging (sweep055_lo_stage.sh).
  Static import closure computed (sweep055_lo_closure.py): soffice.bin's
  51 static imports are all host DLLs — mergedlo/sal3 load DYNAMICALLY,
  so the staging content itself drives the run profile. Fresh 251-file
  staging runs deterministic rc=139/8 stubs (was 99/8 with the lost
  SWEEP-051 57-file dir): the extra late-loaded DLLs (libxslt/lpsolve55/
  libcrypto-3/libssl-3 wave) reach a CRT _initterm that jumps to a bogus
  RIP — registered as SW-1276. The SW-1263 rtl_uString frontier continues.
- [x] **SW-1206 LANDED (SWEEP-055) — adexplorer64 corpus recovered (8th
  time, documented one-shot procedure)**: AdExplorer.zip sha 56536bff /
  ADExplorer64.exe sha e4512878 byte-verified; census 3x exit-0 ZERO
  stubs ZERO unresolved (artifacts/sweep055/census/).
- [ ] **SW-1268 (PARTIAL — 5 contracts landed, paint gate remains)**:
  Everything's index chain starts at FindFirstVolumeW (v1 honest-fail
  error 18 -> NO volumes -> white list). LANDED: FindFirstVolumeW returns
  the fixture \\?\Volume{GUID}\; GetVolumePathNamesForVolumeNameW +
  GetVolumeNameForVolumeMountPointW map volume<->"C:\"; CreateFileW mints
  volume handles (0x000B0000 namespace) for \\?\Volume{ and \\.\X:;
  DeviceIoControl answers FSCTL_GET_NTFS_VOLUME_DATA (88-byte fixture) /
  FSCTL_ENUM_USN_DATA (MFT_ENUM_DATA_V0 cursor protocol, 11-record FRN
  fixture tree, USN_RECORD_V2 packing) / FSCTL_READ_USN_JOURNAL (empty) /
  FSCTL_IS_VOLUME_MOUNTED; GetOverlappedResult reports InternalHigh for
  volume ops. VERIFIED IN TRACE: Everything enumerates + maps the volume.
  REMAINING GATE (decoded): Everything's internal volume-DB walker
  (0x1400d24e0, linked-list of {next,name1,name2,name3}) matches the
  volume against its OWN database (populated by its Everything.db /
  internal chain — 0x1400d6df0 + 0x1400d6f30 user-mode checks fail
  without any API calls) -> the CreateFileW(GUID)+FSCTL path is never
  reached. -> SW-1278.
- [ ] **SW-1276** soffice late-DLL CRT initterm: after stub #8
  (msvcp140 ??0_Lockit) the libxslt/lpsolve55/libcrypto-3/libssl-3 load
  wave reaches `_initterm: calling 0x...` and jumps to a bogus RIP
  (RIP outside every mapped module). Bisect: same rc with HEAD binary ->
  staging-driven, not a SWEEP-055 engine regression. Decode the crashing
  module's initterm target.
- [x] **SW-1277 LANDED (SWEEP-056) — per-module resource resolution + REAL
  GetObject on HBITMAP handles. TWO engine root causes, both disassembly-
  decoded (sol.exe + cards.dll)**: (1) w3_res_find walked the MAIN EXE's
  .rsrc for EVERY module — the hmod parameter was accepted then ignored, and
  the half-v2 trap (rsrc_rva read from hmod's PE header but
  `base = g_image_base + rsrc_rva`) put cards.dll's RVA 0x5000 onto the EXE
  base and walked sol's .text as a resource directory (named=9332/ided=47168
  garbage -> SIGSEGV w3_res_find:12234). Fix: mw_res_pe_base(hmod) accepts an
  hmod only when page-aligned + MZ + PE-sig; pe stays in scope;
  base = pe + rsrc_rva. Applied across ALL 7 hrsrc data-resolution sites
  (w3_res_find, LoadResource, LoadAccelerators A/W, LoadStringW/A,
  mw_dlg_from_resource dialog template, mw_loadimage_bitmap_dib). Evidence:
  cards.dll!cdtInit does LoadBitmapA(hinst_cards, 0x35/0x43/0x44) and reads
  the card metrics from GetObject's bmWidth/bmHeight — the classic 71x96.
  sol.exe calls cdtInit(&0x1000C9E8, &0x1000C9EC) at 0x1000022b1 (through the
  FF15 thunk 0x100009572) and computes its main window from those globals.
  (2) GetObjectW/A only knew MW_IMAGE_BASE (0x60000, the 16x16 placeholder
  family) — mw_loadimage_bitmap_dib returns MW_HBITMAP_BASE (0x52000)
  handles, so cdtInit's GetObjectA(hbitmap, 32, ...) returned 0 and sol
  computed its window from stack garbage (31x0). GetObjectW now decodes the
  HBITMAP family from g_bitmap_obj[] (bi = (v & 0xFF) - 1, g_bitmap_live).
  MEASURED (binary bd7747c2): GetObjectW(HBITMAP 52001) -> BITMAP 71x96;
  sol's CreateWindowExW 585x384 (was 152x64; SWEEP-054 baseline);
  LoadBitmapA(0x35/0x43/0x44) -> REAL DIB decodes 0x52001/2/3 from
  cards.dll's 74-entry RT_BITMAP dir (the v1 walk saw only the main exe's 8
  TYPE ids {3,4,5,6,9,14,16,24} — they were never bitmaps at all, they are
  sol.exe's RT_ICON/MENU/DIALOG/STRING/ACCELERATOR/GROUP_ICON/VERSION/RCDATA
  type dirs). Census 14x3 exit-0 ZERO stubs ZERO unresolved deterministic;
  regression battery green (spider 651 frames unchanged, winmine 82,
  freecell 10, mshearts 2, calc 338x258, mspaint/wordpad/sndvol32 dialogs);
  G1 PIN 12/12 (evidence/master/g1/20261023_060132, bd7747c2a5ddbdb3).
- [ ] **SW-1279 (NEW, SWEEP-056)** sol deal/paint chain: the window is now
  585x384 with a REAL status bar (Time: 0 / Score: 0 DrawTextW + PatBlt on
  the 587x32 client) but the board area renders WHITE — cdtDraw/cdtDrawExt
  are imported yet NEVER CALLED. Pump ends deterministically:
  WM_COMMAND wp=0x3e8 (menu deal, SW-1270 pre-pump queue) dispatches -> 0,
  WM_PAINT paints the status bar only, GetMessageW -> 0 (no timer/paint/
  keys left), exit 0. Decode sol's WndProc 0x3e8 handler: the deal engine
  should call cdtDrawExt for 52 cards + green felt via cards.dll; find where
  it short-circuits (card metrics? deal RNG seeding? menu-state global?).
  Also: client rect 587x32 while the surface is 587x384 — the engine's
  GetClientRect height bookkeeping looks stale after the 152x64 -> 585x384
  fix (status-bar-only client?).
- [~] **SW-1278 decode progress (SWEEP-056)**: the SWEEP-055 gate decode was
  WRONG in one half — the checks DO make API calls. check-a 0x1400d6df0 =
  SetErrorMode(1) + **GetVolumeInformationW** wrapper (IAT 0x1401b0370;
  save/restore error-mode around it; outputs vol name + fs name strings);
  check-b 0x1400d6f30 = stricmp(fs_name, "ntfs") (the 'ntfs' constant at
  0x1401d0438 through a tolower-table compare 0x14016efe0). Walker
  0x1400d24e0 registers the volume (0x1400b3fe0) only when check-a returns
  nonzero AND check-b matches. With the CURRENT fixtures both PASS
  (GetVolumeInformationW -> "NTFS", GetDriveTypeW wrapper 0x1400d7f50 -> 3
  ∈ {2,3,6} gate at the caller 0x1400d2c0c/0x1400d2c22). The volume
  enumeration itself works (FindFirstVolumeW fixture -> 'C:\' via
  GetVolumePathNamesForVolumeNameW, verified in trace). Frontier moves to
  the POST-registration DB state: why the in-memory index is still empty
  (Everything.db read? internal MFT-enumeration chain? UI list source?).
  The direct FindFirstFileW-fails theory was DISPROVEN — the SetLastError(2)
  floods in the trace are the guest's own internal state signaling, not
  failed finds.
- [x] **SWEEP-054 MANDATE MEASURED** — 10 famous programs + 5 classic 2D
  games run with SCREENSHOTS (evidence/sweep054/shots_png/, 15 PNGs):
  games winmine (82 frames, mouse-revealed cells), sol, freecell, mshearts,
  **spider — REAL CARDS 5♥ 2♦ 10♣ + spider-logo deck, deal completes**
  (first render ever); XP applets calc/mspaint/wordpad/notepad/charmap/
  winver/sndvol32/clipbrd/write (census 45/45 exit-0 ZERO stubs); new
  programs Everything 778x605 + putty dialog + RAMMap + Bginfo + DL corpus
  census (12 exit-0, pageant live-pump 124, procexp 139 / winmergeu 133 /
  zoomit rc=1 = their baselines). G1 PIN 12/12
  (evidence/master/g1/20261023_030335, binary 09c33df201af8ade).
