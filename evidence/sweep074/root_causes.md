# SWEEP-074 root causes — first divergences, clusters, reusable fixes

## Cluster A — UCRT `_get_narrow_winmain_command_line` unresolved (20/20 puzzles + Notepad3 x86/x64 + AHK x86)
- First divergence: mingw UCRT `__tmainCRTStartup` calls
  `_get_narrow_winmain_command_line` (api-ms-win-crt-runtime); the gate
  returned 0; the CRT argv splitter executes `mov al,[rcx]` with rcx=0
  -> SIGSEGV before main. Uniform across ALL 20 sgtatham exes
  (60/60 runs rc=139 at RVA 0x8919 class — verified on blackbox: bytes
  `8a 01 84 c0`, imports contain the name, trace shows `UNRESOLVED`).
- Contract: UCRT exe_common.inl — WinMain lpCmdLine = narrow command line
  without the program token.
- Fix SW-1323 (general): implemented with token-skipping; regression =
  entire puzzle cohort advances from pre-main SIGSEGV to a full message
  lifecycle (CreateWindowExA/WM_CREATE/WM_PAINT surface/ShowWindow/
  GetWindowLongPtrW/SetWindowLongPtrW/PostQuitMessage/WM_DESTROY).
- Remaining after fix (next shared root, SW-1324): (a) guest-constructed
  UCRT FILE* hit host vfprintf with NULL (loader crash) — fixed by routing
  unmappable streams to stderr; (b) puzzle teardown then crashes
  (rc=139 RVA 0x124e0 `mov rax,[rcx+10]` garbage RCX, or rc=134 glibc
  "free(): invalid pointer" on fifteen/slant/solo/towers) inside
  midend/state teardown after WM_DESTROY; (c) no frames captured yet
  (paint surface 32x32, class brush snapshot nil) — the puzzles' GDI
  paint path is the next render gate. Status: FAIL_CRASH (strict), with
  the lifecycle advance documented as the measured delta.

## Cluster B — PE32 late-bind GetProcAddress (7zr)
- First divergence: GetProcAddress(module=0xdead0001 kernel32,
  "SetDefaultDllDirectories") returned the raw HOST function address
  0x20602b0; the i386 interpreter executed that VA as guest code until
  EIP=0xc00 -> loader SIGSEGV.
- Fix SW-1320 (general): the gate's SW-1281 conversion now creates a NAMED
  thunk via mw32_api_thunk (real stdcall arity table; dedupes with the
  import-time thunk). The anonymous arity=32 fallback desynced the caller
  stack by +28 after 1-arg APIs (next `ret` fetched garbage EIP=0xc00).
  Arity table entry added: SetDefaultDllDirectories = 4.

## Cluster C — module header introspection on pseudo-handles (notepadpp64)
- First divergence: `movsxd rax,[rcx+0x3c]` with RCX=0xdead000c
  (comctl32 pseudo-handle) -> si_addr=0xdead0048.
- Contract: HMODULE == image base with readable MZ/e_lfanew/PE (wine
  ntdll/loader.c, reactos RtlImageNtHeaderEx).
- Fix SW-1322 (general): page-aligned synthesized module bases
  (0xDEA00000 + 64KB*i, DOS+PE headers, PROT_READ). All 9 sentinel sites
  migrated. N++ restored to its documented frontier (2 real MessageBoxes,
  then the known SW-1298-x64 0x3b94 NULL-context crash).

## Cluster D — PE32 CRT init width bugs (7zr, the 74-step signature)
- mw__initterm/_initterm_e walked 32-bit PVFV tables with an 8-byte stride
  (7zr table 0x48e000..0x48e0a4 = 164B = 41x4, not 20.5x8): neighbor dwords
  merged into bogus 64-bit "function pointers" (RIP=0x40159800000000).
- w3_run_guest_atexit called x86 guest VAs natively.
- Fixes: g_cpu32_active width dispatch + cpu32_call re-entry (SW-1316b);
  `_onexit` routed through the guest registry with the correct
  `_onexit_t` return (SW-1316); msvcrt `__p___initenv` real cell
  (7zr _setenvp stored to EA=0); i386 0F BA /4..7 and 0F A4/A5/AC/AD
  (SW-1319). Result: 7zr rc=139 (74 steps) -> full CRT init, banner +
  usage, rc=0 3/3 (arg-parse of `a` still falls back to usage — SW-1321).

## Cluster E — accelerator store (calc/spider/winmine latent corruption)
- 8-byte slot fields receiving up to 3072-byte memcpy; PE records are 8
  bytes (Wine/ReactOS cross-check; calc.exe 760B = 95x8 disproves the
  supplied report's 6-byte claim).
- Fix SW-1317/1317b: normalized store, bounded readers, DestroyAccelerator
  slot-free (ANSI export leak), full-capacity CreateAcceleratorTableW.
- Regression: n=95/12/2 parsed correctly; parity frames EXACT.

## Cluster F — async key state (games/utilities that poll)
- GetAsyncKeyState/GetKeyState returned 0; GetKeyboardState zeroed.
- Fix SW-1318: canonical table + one-shot latch + MW_GUI_KEYUP pair
  delivery; VK_LBUTTON wired from mouse events.
- Regression: BgInfo64 KEYDOWN/KEYUP pair trace; game parity EXACT.

## Utility cohort verdict notes
- CLEAN rc=0 x3: RegScanner x64, WhatInStartup x64, ShellMenuView x64,
  ShellExView x64, CurrPorts x64, FileTypesMan x64, WinObj x64, BgInfo x64
  (frames=0 — their windows do not reach the frame-capture path yet; no
  scripted-input probe passed, so PASS_LAUNCH_ONLY is NOT claimed;
  classified PARTIAL pending frame+probe evidence).
- FAIL_CRASH: Notepad3 x86/x64 (NirSoft-class gap? check raw logs),
  RegScanner x86 (x64 runs, x86 crashes — width-specific), TcpView x64,
  WinLister x64, LastActivityView, ZoomIt x86.
- PASS_EXPECTED_CLI_ERROR: AHK x64 (rc=2 usage), Sigcheck x64 (rc=255
  EULA/usage path with real banner output), Strings x64 (rc=0 no-arg
  print-filters path).
