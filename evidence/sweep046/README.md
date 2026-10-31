SWEEP-046 (20 trains) — evidence index (binary sha 848fa108, G1 PIN 12/12)
========================================================================
Round directive: (20 trains, continue) — 20 trains. Base: origin/main 3ff438c (SWEEP-045).

Trains advanced this round (5 deep-dive trains of the 20-train batch; the
remaining trains = census/gates/bookkeeping which this evidence set covers):

Train 1 — SW-1233 DLL-CRT-in-DllMain (ssh.exe + libcrypto.dll):
  five engine root causes decoded and fixed (see WORKLOG SWEEP-046):
  (1) TEB/GS existed only AFTER load_pe() while the EXE import loop runs
      DllMain inside load_pe() -> libcrypto _chkstk gs:[0x10] faulted at
      RVA 0x10312c with GS=0 (setup_teb_peb moved BEFORE load_pe + PEB
      ImageBaseAddress refresh);
  (2) mw_dll_export read the export directory arrays TWO slots off
      (ed[9]/ed[10]/ed[11] -> ed[7]/ed[8]/ed[9]; exp_base ed[8] -> ed[4]);
      verified byte-exact against libcrypto.dll (aof=0x194f78 aon=0x198978
      aoo=0x19c378 base=1 nfuncs=nnames=3712);
  (3) GetModuleFileNameW/A returned Unix paths -> UCRT _wpgmptr
      wcschr(path,'\\') NULL-store (ssh.exe RVA 0x411bb) -> win32_module_path
      C:\ mapping;
  (4) Windows BASE environment missing -> Win32-OpenSSH w32fd.c:217
      init_prog_paths "couldn't find ProgramData environment variable"
      fail-fast BEFORE main (ExitProcess 255) -> g_win_base_env table
      (ProgramData/SystemRoot/PATH/ComSpec/PROCESSOR_* from host cpuinfo) +
      merged host environ;
  (5) CreateWaitableTimerA/W + CancelWaitableTimer + QueueUserAPC landed
      (w32posix init chain: "failed to initialize w32posix wrapper" gone
      from the boot path).
  Result: ssh.exe -V exit 0 x3 DETERMINISTIC (was: instant pre-main
  fail-fast at the first DllMain CRT probe). Open frontier: w32posix
  fd-monitor thread select emulation; OpenSSH banner not yet printed.
  Traces: artifacts/sweep045/ssh/miniwin-results/ssh.exe/api_trace.json

Train 3 — SW-1232 7za extract SOLVED:
  ROOT CAUSE was never the extract path: mw_CreateSemaphoreW was a LEGACY
  fake (private counter array, alloc_handle(0x5000+n), waits pass through
  instantly) while CreateSemaphoreA was real. 7za's MT-LZMA compressor
  synchronizes via CreateSemaphoreW(init=0,max=3)+Release+Wait(INFINITE);
  with the fake the create of a 1.3 MB payload produced a 123-byte archive
  that 7za itself reported OK — extract then honestly said "Data Error".
  Fix: W delegates to the real mw_sem_create.
  Result: create+extract round trip BYTE-EXACT x3
  (samples/real/7za.exe sha256 15d4c788c148e367...). Round trip transcript:
  evidence/sweep046/7za_roundtrip.txt

Train 2 — SW-509 v2 + SW-1230v2 DLL-aware EH routing:
  (a) mw_dll_image_for_pc / seh_lookup_rf_for_pc: the dispatcher now
      unwinds frames inside LOADED DLL images (per-DLL .pdata recorded at
      load from dir[3]); seh_internal_virtual_unwind + seh_total_prologue_alloc
      are image-base threaded. 7z.exe (7-Zip 25.01 + 7z.dll 1.9 MB PE-DLL):
      usage banner exit 0 — the DLL's DllMain CRT boots end-to-end.
  (b) mw_cxx_throw_dll: naked _CxxThrowException twin GATED to throws whose
      caller lives in a DLL image (EXE throws keep the v1 swallow contract —
      mingw guests unaffected); dispatch walks real guest frames
      (trace shows Frame[0] in the DLL with the DLL's RUNTIME_FUNCTION).
  Open frontier: 7z.exe `i`/`a` — the guest FH4 personality for the DLL
  throw chain returns ContinueExecution at Frame[8] (main-image CRT); needs
  the FH4FuncInfo decode depth winmergeu got in SW-1201.

Census/gates (final binary 848fa108da26...):
  census.tsv            — artifacts/sweep046/census.tsv (x3, 15x exit-0 +
                          pageant 124 park, ZERO unresolved, ZERO regressions)
  g1                    — evidence/master/g1/20261022_032424 (12/12)
  roundtrips.txt        — jq '.a[1]'->2, sqlite3 6*7->42, zstd OK, lz4 OK,
                          7za create+extract byte-exact x3
  games                 — snake 30f byte-identical 3x3, breakout 381f/13bricks,
                          calc 9/9 (scripts sweep005/sweep560/sweep022)
