# MiniWin Changelog

All notable changes to the MiniWin runtime are documented in this file.
Format based on [Keep a Changelog](https://keepachangelog.com/).

---

## [Unreleased] — Current Development State

### Added (SWEEP-069 — 29 new guests + tool-impact audit «ابزار های کمکی چقدر بهت کمک کرده», 2026-10-25)
- 29 NEW guests one parallel wave (3 XP + 12 NirSoft + 13 Sysinternals x64 + GUP) — 29/29
  executed, zero regressions; winmine smoke 82-frames EXACT after env-reset rebuild
- NirSoft family STRATIFIED (was blanket 13x rc=139 in SWEEP-065): faview + recentfilesview
  CLEAN rc=0 (new), heapmemview 794,277 / iconsext 603,019 / htmlastext 460,750 steps DEEP
  past UPX stub into real OEP (new car SW-1306), 5 early-crash at 26 steps, 2 rc=86
- gup.exe hits EXACT notepadpp 2450-step signature (same codebase, 8.5x smaller A/B for
  SW-1297-x86 -> car SW-1307)
- Sysinternals x64: 5 clean exits (autologon/listdlls/pipelist/psloggedon/psservice),
  curl prints REAL usage error + exit 2, 4 real-banner 255 exits, logonsessions64 rc=133
  = winmergeu EH-chain class (first x64 data point -> car SW-1308)
- XP trio: drwtsn32/regedt32 rc=0, sndvol32 renders real 'This program will now close.'
  MessageBox (evidence/sweep069/shots/)
- Tool-impact audit (measured): Ghidra hours->minutes (x20 = 30,289 fns in ~726s), parallel
  agents 4 fixes/sweep, xargs -P4 ~5x throughput, spec cross-check 226 arity fixes, G1 gate
  caught mid-round regression, git ff-only recovered sandbox reset in ~5 min
- evidence/sweep069/ (battery.tsv 29 rows, battery_raw.txt, shots/); no force-push

### Added (SWEEP-068 — 24-guest mass-run battery «۱۰ تا ۲۰ تا برنامه و بازی رو اجرا بکنی پوش بکن گیت هاب», 2026-10-25)
- Parallel battery harness `scripts/sweep068_battery.sh`: xargs -P4, fresh per-exe
  registry hive per run (SWEEP-066x64 lesson), renamed npp64 staging to avoid result-dir
  collision with x86 notepad++.exe
- 24 guests executed in one wave — 7 games (winmine 82 EXACT, sol 38-frame full deal
  EXACT, freecell 10 EXACT, spider 491, breakout 48-frame loop NEW, snake 29+dialog NEW,
  mshearts 2 NEW), 9 XP apps (all rc=0; wordpad renders its real 'Failed to create
  empty document.' dialog), 6 modern tools (7zr 74/74 EXACT, npp-x64 2 MessageBoxes
  SWEEP-066x64 parity, npp-x86 2450 EXACT, winrar rc=1+dialog EXACT, usbdeview 27
  EXACT, driverview 794,322 EXACT), 2 Sysinternals x64 (pslist64 clean NEW, handle64
  rc=139 NEW frontier -> SW-1305)
- ZERO regressions: every comparable number EXACT origin parity; evidence/sweep068/
  (battery.tsv 24 rows, battery_raw.txt, 12 curated shots)
- New worklist car SW-1305 (handle64 x64 handle-table path; pslist64 clean isolates
  the delta to handle-specific APIs)

### Added (SWEEP-032..036 — the five-train session «۵ قطار», 2026-10-21)
- zoomit64 runs end-to-end (was loader-rejected at the 64MB file cap): 70-import
  surface (GDI+ pen state/pixel store/effects, LCIDToLocaleName,
  CompareStringOrdinal, threadpool-wait, WINMM timers, WTS, USER32 extras,
  WinRT error-info, MF platform, real file-backed IStream, COM shells,
  comctl32 subclass chain)
- RtlCaptureContext full FPU/SSE capture (all 16 XMM, real MxCsr both slots,
  fnstcw/fnstsw) — SW-1152
- ODBC mini-driver real sqlite3-backed (DriverConnect/ExecDirect/Fetch/
  GetData/BindCol/NumResultCols/RowCount/Disconnect/DiagRec) — SW-1153
- ExtFloodFill real scanline seed fill (border/surface) — SW-1156
- CBT hook event delivery (CREATEWND/DESTROYWND/ACTIVATE) — SW-1203
- AppPolicyGetThreadInitializationType + event-registry unification bug fix
  (W/A split-brain) + worker-thread honest GetMessageW park — SW-1204
- procexp64 ordinal triage (IUnknown_QueryService, TaskDialogIndirect,
  CreateSecurityPage) + BUG-078 root-cause class decode — SW-1102
- crash-handler [STACKTOP raw] dump + [KOBJ] wait-kind probe (engine
  instrumentation)


### Added
- Complete PE64 loader with section mapping and relocation
- Import resolver for KERNEL32.DLL (68) and msvcrt.dll (94)
- TEB/PEB environment setup with real stack bounds
- Heap allocator (bump allocator, 16MB static region)
- Virtual memory APIs (VirtualAlloc/Free/Protect/Query)
- CRT startup support (_initterm, __getmainargs, __set_app_type)
- Signal handler for SIGSEGV/SIGABRT
- API trace logging (JSON format)
- .pdata parser (3030 RUNTIME_FUNCTION entries for UPX)
- RtlLookupFunctionEntry (binary search through .pdata)
- RtlVirtualUnwind (all 9 standard opcodes + GCC extensions 9-15)
- RtlDispatchException (full frame walking with EHANDLER discovery)
- RaiseException with naked ms_abi stub for precise register capture
- CONTEXT construction (0x4D0 bytes)
- DISPATCHER_CONTEXT construction
- GCC personality routine invocation via ms_abi inline asm
- LSDA parsing (LPStart, TType, CS encoding)
- RtlUnwindEx (longjmp-based partial implementation)
- VEH chain support (AddVectoredExceptionHandler/RemoveVectoredExceptionHandler)
- SetUnhandledExceptionFilter support
- 12 automated regression tests (all passing)
- EXP-NEXT-2 synthetic test with controlled PE (handler discovery PASS)
- 4 dispatch test PEs (test_a/b/c/d.exe)
- Multiple analysis scripts (PE dumping, personality analysis, LSDA parsing)
- Checkpoint archives at 6 development milestones

### Fixed
- UNWIND_INFO flag parsing: `flags = (ui[0] >> 3) & 0x03` (was reading from ui[1])
- Hardcoded .pdata RVA: now reads from Exception Data Directory
- .pdata global access: stored in global variables for all functions
- REX prefix classification: 0x48 (REX.W) no longer misclassified for push
- Compiler prolog instability: naked stub replaces hardcoded frame offsets
- XMM alignment crash: internal SysV-only functions for SEH logic
- Source truncation misdiagnosis: verified current code IS the working baseline

### Known Issues
- RtlUnwindEx context restoration incomplete (blocks UPX --version)
- __C_specific_handler is a no-op (GCC LSDA interpretation not implemented)
- Bump allocator never frees memory
- No DLL loading (LoadLibrary/GetProcAddress)
- No GUI support
- No multi-threading
- 236/3030 RUNTIME_FUNCTION entries have malformed UNWIND_INFO

---

## Session History

### Session 1: Initial PE Loader
- Built initial PE loader that maps PE64 images
- Implemented basic import resolution
- Added TEB/PEB stubs
- UPX reached CRT initialization
- **Checkpoint**: miniwin_checkpoint_20260806.zip
- **Hash**: c0d143f52af939f4b41c6559b100fd17b48db00fee5768d5e4ed4ac2b76dbe7c

### Session 2: SEH Infrastructure (BUG-023)
- Implemented RtlLookupFunctionEntry (binary search)
- Implemented RtlVirtualUnwind (full opcode simulation)
- Fixed UNWIND_INFO flag parsing bug
- Stored .pdata info globally
- Added RaiseException instrumentation
- **Checkpoint**: miniwin_checkpoint_before_cpp_exception.zip
- **Hash**: e01628b78cb06bc85cbe73a442b017204491eed575f824c8ee37c5cf9a4f041d

### Session 3: Regression Recovery
- Verified no regression from session 2
- Created regression test suite (12 tests, all pass)
- **Checkpoint**: miniwin_checkpoint_bug023_recovery.zip
- **Hash**: cbc04da625befd56c443e947372e2b57f1dc3bf9ffb7ee3e5d7a917990c49c59

### Session 4: EXP-NEXT (Prolog Analysis)
- Discovered compiler prolog instability
- Implemented naked stub for precise register capture
- Analyzed UNWIND_INFO integrity issues (236/3030 malformed)
- Found EHANDLER reachable at Frame[3] via prolog-corrected walk
- **Document**: docs/bugs/BUG-023-expnext-report.md

### Session 5: EXP-NEXT-2 (Synthetic Proof)
- Built synthetic PE with 3 nested functions
- Created standalone harness (exp_next2_harness.c)
- **PROVED**: Handler discovery works correctly (PASS)
- Frame 0 (func_C): no EHANDLER, parent RIP recovered
- Frame 1 (func_B): EHANDLER found at RVA 0x1060
- **Checkpoint**: miniwin_checkpoint_before_rtl_dispatch.zip
- **Hash**: 3e9d08e0f32bc6606c5ecf04aebfdcbefc53460c9f82ea61946c9c80dbfafb34

### Session 6: EXP-NEXT-3 (Real Dispatch)
- Implemented RtlDispatchException in main loader
- Full frame walking on real UPX binary
- Handler at Frame[3] called, returned ContinueSearch
- Handler at Frame[4] called, triggered RtlUnwindEx
- RtlUnwindEx longjmps back but context restoration incomplete
- **Checkpoint**: miniwin_checkpoint_exp_next3_before_dispatch.zip
- **Checkpoint**: miniwin_checkpoint_before_master_exception_runtime.zip

### Session 7: Knowledge Transfer
- Created complete engineering archive
- All source, docs, evidence, tests migrated to GitHub repo
- This CHANGELOG created as part of transfer
