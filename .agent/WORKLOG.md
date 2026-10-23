# WORKLOG — MiniWin (append-only)

---
Task ID: TASK-ISS1-P1
Agent: Super Z (engineering agent)
Task: Read GitHub Issue #1 line-by-line; execute its Phase-1 6-target plan (UPX + t08 + t09 + t10 + t11 + t12); fix the first general root cause found; keep evidence; report per §19.

Work Log:
- Fetched https://github.com/Sh-TB/miniwin-engineering/issues/1 (anonymous HTML → cleaned text, 1095 lines) and read it in full.
- Phase 0 audit: HEAD 17c024aab5733b92dd8b1f21e7db444e1fb30d36 = origin/main (0/0), historical commits 08d94ac + 4039b61 exist; wrote artifacts/audit/repository_state.md.
- Baseline: clean build sha256 40646f4b…; scripts/g1_accept.sh → G1 PIN OK (UPX banner exact, exit 0, regression 12/12); UPX 3× repeat deterministic (stdout sha 1bfb775b…).
- Phase 1 finding: t08–t12 fixtures MISSING from repo (0 grep hits). Installed mingw-w64 locally (dpkg-deb extraction, no sudo) and recreated all 5 as real PE32+ x64 C/C++ fixtures in tests/corpus/ with host-verified expected values (t11 probe value 1761356 computed on host; t12/t11 arithmetic bugs in my first drafts found and fixed BEFORE blaming the runtime).
- First run: t12 = SEGV (9 unresolved imports: fopen/fclose/fread/fseek/feof/fgetc/remove/_strtoi64/_strtoui64). t08 = exit 1 (159/255). t09/t10/t11 = PASS (t11 after fixture fix).
- WS-D slice in src/loader/loader.c (forbidden zones untouched): implemented fopen/fclose/fread/fseek/ftell/feof/fgetc/remove/_strtoi64/_strtoui64; upgraded fwrite/fputs/fputc/fflush/getc/fprintf/vfprintf to delegate; then TWO deeper semantic fixes driven by live traces:
  (1) FILE* must be MSVCRT `struct _iobuf` layout (guest CRT introspects _file for lock indexing and _flag bits; opaque handle caused heap corruption via garbage CRITICAL_SECTION);
  (2) real ungetc via _charbuf (stub lost pushback → every fscanf token shifted one char: d=42 word=[xt] dbl=0.75 hex=15 → after fix d=-42 word=[txt] dbl=3.75 hex=255).
- Added errno sync into g_errno_storage on fopen/remove failure paths.
- Final acceptance: 6 targets × 3 runs → upx/t09/t10/t11/t12 exit 0 ×3 (byte-identical stdout each), t08 exit 1 ×3 (159/255, deterministic). Regression 12/12. G1 green.
- x86 classification (Issue §4): built real PE32/i386 binary with i686-mingw → loader refuses explicitly `[ERROR] Not a PE32+ file (magic=0x10b)`, exit 1 — recorded as UNSUPPORTED, not a runtime defect.
- Artifacts: artifacts/audit/{repository_state,issue_reconciliation}.md, artifacts/research/tooling_and_upstream_audit.md, artifacts/results/issue1_phase1_manifest.md; .agent/{EVIDENCE_INDEX,NEXT_ACTION,CURRENT_TASK,SESSION_HANDOFF}.md updated.

Stage Summary:
- 5/6 Phase-1 targets PASS deterministically; 1 PARTIAL with a precisely classified open root (BUG-038 lineage: multi-clause trivial-type catch selector + stack clobber).
- General fix landed (WS-D stdio file I/O family), proven by t12 SEGV→PASS and unchanged 12/12 regression.
- All evidence real: commands, exit codes, SHA-256s, traces under evidence/master/ and artifacts/.

---
Task ID: TASK-ISS1-P1-merge
Agent: Super Z
Task: Merge parallel upstream work (709b9ee, 9d0af32) without losing either side; re-validate.

Work Log:
- push rejected (remote ahead): fetched — remote had 2 new commits (appcompat sweep t01–t12, BUG-037 closed, BUG-038 registered, UCRT API waves, native TLS).
- Merged; 4 conflicts (loader.c + 3 .agent files). Adopted upstream stdio architecture (map_crt_stream + UCRT __stdio_common_v* waves) — SUPERSEDED my interim MwFile-only design.
- Discovered on merged tree: upstream's fopen returning RAW glibc FILE* crashes my t12 (guest static CRT does InitializeCriticalSection(fp+0x30) → heap corruption in libc). Restored registry-backed fopen (MSVCRT-strided handle + 0x40 slack) and routed EVERY FILE* API through map_crt_stream (registry → iob window → pass-through). This preserves upstream's iob/fmemopen behavior AND fixes the CRT-lock contract.
- Contributed upstream-style additions: fseek/ftell/feof bridges + errno mirror sync (fopen/remove).
- Found+fixed script bug in tests/appcompat/run_sweep.sh usage (relative BIN_DIR after cd — use absolute path).
- Rebuilt missing sweep binaries (tests/appcompat/*.exe) with mingw-w64.
- Final merged-tree validation: my 6 targets (5 PASS ×3, t08 159/255), sweep 10/12 (t08/t09 C++-EH family), regression 12/12, UPX G1 exit 0.

Stage Summary:
- Merge d9d2dc4+origin/main integrated with zero work lost; single stdio architecture (map_crt_stream + registry) now covers both fixture families.
- Open roots unchanged: C++ EH multi-clause trivial catch (BUG-038) + iostream-family timeout.

---
Task ID: TASK-BUG-039
Agent: Super Z (engineering agent)
Task: Read GitHub Issue #1 line-by-line (re-fetch with authenticated API, 50972 chars, 738 lines); continue from .agent/NEXT_ACTION (BUG-038 target-frame delivery); fix the next general root cause; keep evidence.

Work Log:
- Re-fetched Issue #1 via GitHub API (anonymous API was 403 rate-limited — the Mission's 403 family; authenticated PAT call succeeded, 0 comments, OPEN) and read all 20 sections line-by-line: §2 audit artifacts exist (artifacts/audit/repository_state.md + issue_reconciliation.md); §7 (UNWIND_INFO) and §19 (report format A-F) drive this slice.
- Baseline: HEAD 648b473 = origin/main (0/0), tree clean, build sha 0630d3ce, regression 12/12, UPX G1 exit 0.
- Reproduced: t08_probe (p2 len=0, ok clobbered to heap ptr), t08_big_cpp_stl score=159/255, exit 1.
- Live-trace investigation with temporary register dumps (added in allowed zones only): Frame[1] (__cxa_throw, RF 0x209f0, codes ALLOC_SMALL 0x20 + PUSH rbx/rsi/rdi) restored RDI=[E1]=RETURN ADDRESS instead of [E1-8]=saved rdi.
- Root cause A (BUG-039/A): all three UWOP_PUSH_NONVOL replay sites incremented RSP BEFORE reading — the code array is in undo (descending-offset) order so the read must come first. Reference: ReactOS sdk/lib/rtl/amd64/unwind.c PopReg (read THEN +=8). Fixed in seh_internal_virtual_unwind (main + CHAININFO) and mw_RtlVirtualUnwind.
- After fix A, probe p2 resumed at the CORRECT pad with RDX=2 but crashed: Frame[2] (main = TARGET frame) replay had replaced throw-time registers with the target's CALLER's values (RDI=0x370dd38 heap vs required &got).
- Root cause B (BUG-039/B): the search-phase walk unwinds the live CONTEXT THROUGH the handler's frame; a landing pad must resume with the target frame's throw-time registers. Fix: per-frame pre-unwind snapshot in the dispatcher search loop -> g_target_frame_ctx; mw_RtlUnwindEx copies the 16 GPRs from it before the RIP/RSP/RAX/RDX protocol overrides.
- Intermediate crash was my own temporary DBG dump dereferencing a leftover register value (RDI=0x64) at loader.c:2607 — removed before commit; never a semantic defect.
- Results: t08_probe 3/3 OK exit 0 (x3 deterministic), t08_big_cpp_stl score=255/255 exit 0 (x3), regression 12/12, UPX G1 exit 0, corpus t09/t10/t11/t12 exit 0.
- Sweep hygiene: t09's 90s timeout ballooned api_trace to 7.8GB and filled the root FS (ENOSPC corrupted the sweep's t12 result); bounded t09 to 20s in run_sweep.sh (classification unchanged) and rebuilt the stale sweep t08 binary from source with llvm-mingw. Final sweep: 11/12 PASS (was 10/12), t09 TIMEOUT = known TASK-API-009 frontier.
- Evidence: evidence/master/bug039/{probe_trace_postfix.json,t08_trace_postfix.json,probe_stdout_final.txt,t08_stdout_final.txt,SHA256SUMS.txt}; docs/bugs/BUG-039.md (CLOSED TESTED).
- BUG-038 stays OPEN re-scoped: the placeholder-TargetIp hypothesis applies to the llvm-mingw/MS-ER family, not the GNU-mingw fixtures fixed here.

Stage Summary:
- G2 C++ EH acceptance reached for the GNU-mingw family: t08 255/255 deterministic, probe 3/3, sweep 11/12.
- Two general semantic laws fixed with ReactOS-verified references; both would fail visibly if reverted (t08/probe + any multi-frame unwinding that restores nonvolatives).

---
Task ID: TASK-OPENSRC-1
Agent: Super Z (engineering agent)
Task: Download two famous open-source exes (ImageMagick portable, ffmpeg static), run them under MiniWin, and attempt real render outputs; fix every API/semantic gap encountered along the way.

Work Log:
- Downloads: ImageMagick 7.1.2-34 portable Q16 x64 (magick.exe, sha 1232979c..., 7z from download.imagemagick.org, extracted with a self-contained 7zz 25.01) + ffmpeg master-latest win64 GPL (ffmpeg.exe, sha 78f83187..., BtbN release zip).
- PE analysis (scripts/pe_imports.py): magick = PE32+ x64 console, 461 imports over 15 DLLs; ffmpeg = PE32+ x64 console, 1026 imports over 30 DLLs.
- Wave-3/3B/4/4C/4D/4E (~380 new APIs, all in clearly-marked ISS1-WAVE* blocks + table entries): heap/global/local, process/env/module/paths, files (CreateFile family on real fds), directory enumeration (FindFirstFileExW family), console, codepage/locale with REAL per-type values (LOCALE_IDEFAULTANSICODEPAGE="1252" etc.), time, SRW/condition/InitOnce/address-waits, threads (honest single-team), registry (honest ERROR_FILE_NOT_FOUND), BCrypt (real /dev/urandom for GenRandom), real .rsrc resource directory walk (FindResourceA/W + LoadResource/LockResource/SizeofResource), VCOMP single-team OpenMP (incl. _vcomp_fork fixed-arity dispatcher), CRT family (_initterm_e, _seh_filter_exe, atexit registry, time64, wide fs, findfirst64i32, math), pure-CRT bridges (~200 glibc-backed), WinSock2 (real host sockets; correct x64 WSADATA layout).
- Real semantic fixes proven by live evidence:
  (1) MultiByteToWideChar/WideCharToMultiByte size-query must return the count INCLUDING the null for srclen=-1 (old code returned the byte count → one-wchar heap overflow → ImageMagick "free(): invalid size").
  (2) GetStartupInfoW: dwFlags is at +60, not +72 (old write clobbered lpReserved2).
  (3) LCMapStringW/A sizing calls (dst=NULL) must not write.
  (4) GetLocaleInfoW/A must return real per-type values and honor LOCALE_RETURN_NUMBER.
  (5) GetACP=1252 (UTF-8 ACP breaks UCRT's 256-byte ctype-table conversion).
  (6) GetLogicalProcessorInformationEx must implement the ERROR_INSUFFICIENT_BUFFER retry contract (a stub made the CRT loop forever).
  (7) WSADATA is 408 bytes on x64 with a specific field order (a wrong 512-byte write smashed the caller's stack — ffmpeg __stack_chk_fail).
  (8) atexit must NOT hand guest code pointers to the host atexit (glibc jumped into the PE during teardown); guest registry + run at ExitProcess.
  (9) __intrinsic_setjmp must not write a 200-byte glibc jmp_buf into the guest's 128-byte mingw buffer; side-table implementation.
- Diagnostics built: MINWIN_WATCHDOG (SIGALRM dumps tagged RIP+stack), crash_handler tagged stack scan, HeapFree/alloc-registry traps, MINWIN_HEAP_GUARD guard-page heap (catches overruns/UAF at the exact write).
- Results: magick PARTIAL — 390→~150 unresolved; runs into config-path resolution; BLOCKED root: UCRT locale ctype-init path delivers a corrupted frame to GetStringTypeW (needs a dedicated session). ffmpeg PARTIAL — 678→~425 unresolved; full CRT init + library constructors (WSAStartup, vulkan probe, per-component CreateEvent/Mutex) execute; BLOCKED root: heap metadata corruption ("malloc(): invalid size (unsorted)") during late CRT init, before main.
- Regression discipline maintained after every wave: 12/12, t08 255/255, probe 3/3, UPX G1 exit 0, sweep 11/12 (final).
- Evidence: evidence/master/opensource/ (PE reports, unresolved lists, last stderr of both targets, SHA256SUMS).

Stage Summary:
- Two real famous open-source programs now execute hundreds of thousands of guest instructions into their CRT init under MiniWin; the API surface grew ~2.6x with semantics verified against the live traces of both targets.
- Next session: (a) ffmpeg late-CRT heap corruption (use the guard/UAF tooling already in place), (b) magick locale ctype path, then the render commands.

---
Task ID: TASK-OPENSRC-1-PUSH
Agent: Super Z
Task: Push TASK-OPENSRC-1.
Work Log:
- First push attempt stalled: the commit accidentally included 946MB of downloaded/extracted binaries. Amended (pre-push, no history rewrite of pushed commits): artifacts/opensource/{imagemagick,ffmpeg}/*.zip/*.7z moved to .gitignore; evidence reports kept.
- Push verified: 7d047a8..f75aa97 main -> main (0 ahead / 0 behind after fetch).
Stage Summary:
- Remote HEAD f75aa97 contains both milestones of the session (BUG-039 + OPENSRC-1).

## 2026-10-17 — CONTINUE #10: ladder + MSVC/Android/research base + BUG-054b/061 + zstd
- User directive: old small/medium software; 100-program ladder; MSVC runtime base;
  Android feasibility; deep GitHub reuse (license-clean).
- Research: wibo(MIT)/loadlibrary(Apache-2)/retrowin32/wine+reactos(read-only)/
  msvc-wine+winetricks(patterns)/Winlator+Mobox+Hangover+box64+FEX(android).
- Deliverables: PROGRAM_LADDER.md (100 rungs x 10 tiers), MSVC_RUNTIME_PLAN.md
  (ADR-012: 3 modes, no DLL redistribution), ANDROID_FEASIBILITY.md (ADR-013:
  Path A proot+qemu shortcut, Path B loader/engine split, Path C oracle),
  GITHUB_RESOURCES_MAP.md (ADR-014 borrowing policy), backlog Wave-12.
- Engineering: BUG-054b CLOSED (CreateDirectoryW/RemoveDirectoryW/DeleteFileW/
  GetDiskFreeSpaceExW normalize; 3/3 SHA match); zstd wave (7 APIs); BUG-061
  CLOSED (_stat64 layout+overflow); LADDER-008 zstd VERIFIED 3/3; G1 pinned 4x;
  regression 12/12; proof PNG v3.

---
Task ID: SWEEP-007 (SW-562)
Agent: Super Z (engineering agent)
Task: User «ادامه بده» — continue per worklist: SW-562 battery (all GUI programs
      under MW_GUI_EVENTS=1), honest post-SWEEP-005/006 frontier measurement.

Work Log:
- Re-acquired the SWEEP-003 GUI set (dl/ tree lost to ENOSPC cleanup):
  scripts/sweep007_redownload_gui.py, same official endpoints; 15/16 OK,
  pefile-verified (14 GUI + 1 console); ADExplorer.zip retired by Microsoft
  (404) — recorded as UNOBTAINABLE; SHA-256 manifest per exe
  (artifacts/sweep50/sweep007_redownload_manifest.tsv).
- Binary-identity spot checks: cports v1 reproduces SWEEP-003 crash site
  0x14001b338 exactly; putty renders the identical WinSock dialog — same set.
- Clean rebuild; G1 PIN OK pre-battery (binary sha 791334c8..., byte-identical
  to SWEEP-006 build; regression 12/12; UPX banner exact).
- Battery: scripts/sweep562_battery.sh, 15 programs x3 under MW_GUI_EVENTS=1,
  classification from per-run api_trace.json (MW_TRACE target — NOT stderr;
  first battery pass mis-grepped stderr, fixed and re-run).
- Results 15/15 3x-deterministic: produkey+cports ARM the pump and crash in the
  synchronous WM_CREATE (produkey 0x22f24 at stub call #4 = comctl32 child
  controls on NULL returns; cports fault moves earlier 0x1b338 -> 0x2119, same
  binary); procmon64/diskmon64 exit 0 with armed=0 — NO runtime
  CreateWindowEx/GetMessage (import-resolve trace lines are not runtime calls;
  SWEEP-003 GUI-lifecycle labels corrected to PRE-WINDOW-CLEAN-EXIT);
  coreinfoex64 exit1 = SW-561 version drift CONFIRMED x3; putty 2nd dialog
  image; pageant FIRST dialog image ("Unable to access security APIs");
  named-stub gates (procexp64/autoruns64/rammap64/puttygen RIP=0) unchanged;
  zoomit64 TRAP133, bginfo64/winmergeu TERM9 unchanged.
- Images: scripts/sweep562_images.py -> putty_dialog_minwin.png,
  pageant_dialog_minwin.png (2x nearest), battery_map.png (v1-vs-pump map).
- Worklist: SW-562 closed; SW-553 scoped to zoomit64-only; SW-561 confirmed.
- Gates after battery: no loader.c change this sweep (pure measurement) —
  G1 pin from pre-battery stands; regression untouched.

Stage Summary:
- SW-562 DONE with the honest map: 2/15 reach the pump and both die in
  WM_CREATE comctl32 cliff; 2/15 exit0 pre-window; 2/15 produce dialog images;
  4/15 named-stub gates (SW-554/555 = highest leverage next); 1 version drift
  (SW-561); 1 retired binary (SW-553 scope-cut); 5 unchanged crash classes.
- Evidence: evidence/sweep007_battery/ (README.md, battery_map.png, 2 dialog
  PNGs, battery_raw.tsv, determinism.txt, per-program run1 stderr/cmdline);
  artifacts/sweep50/runs_gui562/ (per-run artifacts x45).

---
Task ID: SWEEP-008 (REA evaluation)
Agent: Super Z (engineering agent)
Task: User directive — test the trending GitHub RE tool "REA" (morluto/rea)
      on a few of our programs and assess whether it helps MiniWin.

Work Log:
- Identified REA = "Reverse Engineer Anything" (morluto/rea, npm rea-agents
  6.7.0, MIT): MCP server + CLI connecting agents to RE engines.
- Toolchain brought up in user space (outside repo): Ghidra 12.1.4 PUBLIC +
  Temurin full JDK 21.0.12.1 at /home/z/ghidra-dl (system java = JRE only,
  REA ghidra-java check demands a full JDK — first doctor fail fixed);
  pwntools 4.15.0 + pyelftools + unicorn into the venv for REA_PWNTOOLS_PYTHON.
  `rea doctor --provider ghidra` -> healthy.
- Provider truth: Kuna REJECTS PE (ELF/Mach-O only); pwntools provider is
  ELF-only; Ghidra provider handles our x64 PEs end to end.
- Program tests:
  * puttygen.exe (SW-552): `rea function` decompiled WinMain (15.2K chars) +
    call graph in one command; objdump+pefile cross-check proved the NULL call
    is `call *%rdi` with RDI from a .data FUNCTION-POINTER (0x14008a8e8, NOT
    IAT) — SW-552 hypothesis rewritten; xrefs give writer candidates.
  * cports.exe (SW-551): `rea inspect-native-instruction` decoded the fault as
    `CMP dword ptr [R11+0xb8], R13D` — DATA fault on invalid struct pointer.
  * pageant.exe (SW-554): `rea trace "security APIs"` -> FUN_1400017e0; full
    decompile shows the gate (MessageBoxA "Unable to access security APIs" ->
    return 1 = exit(1)); teardown probes confirmed post-decision.
  * ProduKey.exe: `rea inspect-pe-resources` engine-free structured evidence.
- Verdict: ADOPT for isolation work (function/xrefs/trace/instruction).
  Gaps: no PE for Kuna/pwntools; ephemeral Ghidra project per CLI call;
  first-call auto-analysis cost 40-90 s per PE.
- Worklist deltas recorded (SW-551/552/554 UPDATE blocks). No loader.c change.
- Evidence: evidence/sweep008_rea/ (README + 7 REA JSON payloads).

Stage Summary:
- REA adopted as the isolation toolchain; three worklist entries advanced with
  concrete, address-level facts; toolchain env documented for future sessions
  (GHIDRA_INSTALL_DIR, JAVA_HOME=JDK21, REA_PWNTOOLS_PYTHON).

---
Task ID: SWEEP-008b
Agent: Super Z (main session)
Task: user directive — hook REA up WITHOUT any token/API and keep applying it
  to Windows programs; push with the user-supplied GITHUB_TOKEN.

Work Log:
- API/token audit: rea doctor green with zero auth env vars; the only apiKey
  strings in the package are its Electron-leak-detection FEATURE; rea setup
  (external agent-client registration) deliberately skipped — main agent
  consumes rea CLI directly, fully offline. Caveat: rea function emits YAML.
- Pushed pending b783c44+2ff4f40 to origin/main with the user token (env-only
  credential helper, no token written to any file).
- SW-555 prerequisite delivered: mw_unresolved_import_stub now logs guest_rip;
  new scripts sweep008b_stub_candidates.py (PE imports vs dispatch table ->
  IAT slot map) + sweep008b_decode_callers.py (FF 15 disp32 -> dll!import).
- Re-ran autoruns64/rammap64/procexp64: all 42 stub calls named, 0 unmapped,
  2-run identical. Gates: autoruns+rammap = Sysinternals EULA dialog gate
  (DialogBoxIndirectParamW -> 0 -> "not accepted" -> exit(1)); procexp =
  ColorRGBToHLS/ColorHLSToRGB palette corruption (36-caller shading helper),
  exit(2) from a resolved API — open.
- REA decompiles trimmed to raw_result YAMLs in evidence/sweep008_rea/;
  G1 re-pinned (sha f40d2687..., 12/12).

Stage Summary:
- REA confirmed 100% API-free and now load-bearing for SW-555; three gate
  programs have named gates + a queued honest-stub fix list (GetVersionExW,
  RegOpenKeyW, GlobalAddAtomW, Color* pair); EULA-dialog path needs
  DialogBoxIndirectParamW (SW-509/SW-559 family).

---
Task ID: SWEEP-009
Agent: Super Z (main session)
Task: «ادامه» — land the queued honest-stub batch (SW-555), re-measure the
  three gate programs, push.

Work Log:
- REWIND RECOVERY: workspace snapshot had reverted to b783c44 (2ff4f40+6c5f651
  missing locally; GitHub intact). fetch + reset --hard origin/main restored
  SWEEP-008/008b; pefile reinstalled (python3 -m pip).
- Implemented 9 imports (loader.c): GetVersionExW, RegOpenKeyW/RegCreateKeyW
  (honest-failure wrappers), GlobalAddAtomW/Find/Delete/GetName (REAL atom
  table), ColorRGBToHLS/ColorHLSToRGB (REAL 0..240 HLS). MultiEdit retry
  duplicated blocks (non-atomic failure) — sweep009_dedup*.py cleaned; all
  symbols verified exactly-once.
- scripts/sweep009_color_roundtrip.c: exhaustive 2^24 roundtrip — caught hue
  TRUNCATION bug (dev 2 @0x000251); fixed to rounding; final max channel dev
  4 (Win32-scale inherent), L error 0 (procexp only consumes L).
- Post-batch 3x runs (runs_stubrip9): all deterministic; autoruns64 6->2,
  rammap64 16->1, procexp64 20->9 ALL-NEW sites (GetSystemMetrics x4,
  ImageList_Create/Destroy x4, SetDefaultDllDirectories — past color init,
  into UI init). Honest RegOpenKeyW reroutes autoruns64 to the open-fail
  branch 0x140062976 = NO EULA dialog — EULA fix needs dialog + reachable
  key-open (in-memory hive); EulaAccepted auto-seed shim NOT taken.
- G1 re-pinned sha c06c5eb8, 12/12. Evidence: evidence/sweep009_honest_stubs/.

Stage Summary:
- All "lying stub" lies named in SWEEP-008b are gone; three gate programs
  have exactly one/few named gates left; next lever = GetSystemMetrics +
  ImageList_* (procexp) and the EULA dialog + mini-hive (autoruns/rammap),
  all in the SW-509/SW-556 child-control family.

---
Task ID: SWEEP-010
Agent: Super Z (main session)
Task: «ادامه» — procexp64 UI-init batch (GetSystemMetrics + ImageList_* +
  SetDefaultDllDirectories), re-measure gates, push.

Work Log:
- REA-decoded procexp64's 4 GetSystemMetrics call sites (FF15 + backwards
  mov ecx,imm32): the program asks EXACTLY SM_CYSMCAPTION (50) and
  SM_CYSMICON (49) — toolbar small-icon sizing feeding ImageList_Create.
- Implemented 6 imports (loader.c): GetSystemMetrics (REAL Win10 1920x1080
  table, honest 0s for no-network/no-digitizer/no-TS, undefined -> 0 like
  real Windows), ImageList_Create/Destroy (REAL handle lists, slot-reuse),
  ImageList_Add/GetImageCount (strip-bitmap add with real 3-byte BGR pixel
  copy from the CreateCompatibleBitmap pool), SetDefaultDllDirectories
  (documented policy no-op -> TRUE).
- Placement bugs caught: block first inserted before the GDI infra it
  references exists; first move spliced into mw_BitBlt's MULTILINE
  signature. Both fixed by marker-scanning scripts (kept in scripts/).
- Self-test sweep010_imglist_test.c links the REAL loader TU (-Dmain=...):
  29/29 PASS. ABI lesson: mw_* are ms_abi — the test must declare them
  ms_abi or SysV call sites garble args (observed ImageList_Create(4,37)).
- Post-batch 3x runs (runs_stubrip10, MW_GUI_EVENTS=1): all deterministic.
  procexp64 9 -> 1 stub call and exit(2) -> exit(1); autoruns64 2, rammap64
  1 (unchanged). The single remaining procexp stub DECODES to user32!
  DialogBoxIndirectParamW @0x14005cd6c — same import as the autoruns/rammap
  EULA gate. All three gates now share one named blocker.
- G1 re-pinned sha df28de0f, 12/12; battery re-verified post-pin.

Stage Summary:
- procexp64's old exit(2) frontier (SWEEP-008b: "decision from a resolved
  API") is broken through — UI init completes with real metrics/image lists.
- Next lever is now singular: DialogBoxIndirectParamW + DLGTEMPLATE +
  mini-hive (SW-509/SW-559 family) unlocks all three Sysinternals gates at
  once. Evidence: evidence/sweep010_ui_init/.

---
## SWEEP-011 (2026-10-18) — REA-powered speed: wall time + debugger speed
Task: user directive to maximize REA usage for lower program runtimes and a
faster debugger.
Work:
- Restored the toolchain wiped by the home reset (rea 6.7.0 npm rea-agents,
  Ghidra 12.1.4 PUBLIC, Temurin JDK 21.0.12.1); foreground downloads (bg
  processes get killed between tool calls).
- Baseline battery (13 programs x3, getrusage): guests are ms-fast; costs =
  per-line trace fflush + per-frame BMPs + 40-90 s per rea query.
- loader.c: MINWIN_FUNC_MAP symbolizer (merged comma-separated maps, eager
  load, whitespace-tolerant parser), MINWIN_PROFILE sampler + profile.json,
  64 KiB trace buffer + MINWIN_TRACE=0, MINWIN_SCREENSHOT=0, SIGTRAP crash
  report (exit 128+sig: SEGV 139 / ABRT 134 / TRAP 133 unchanged), backtrace
  walker hardening (return-chain + stack bounds — zoomit64 nested-SEGV fix).
- Ghidra persistent project funcmap exporter (Java postScript; state-dir
  import tracking); 10 guest maps + nm engine map; two parser/format bugs
  caught live (json.dump spaces; silent n==0 path).
- Battery compare: parity 13/13 exits identical; fast-mode wins breakout -27%
  / snake -15% / 7za -43% / sqlite3 -44% / t08 -50%; trace lines 21608->2.
- rea --snapshot warm query 2.0s vs 2m29s cold (74x).
- G1 re-pin sha 5e81f847 (clean build deterministic — earlier sha drift was
  an edit/build race, not nondeterminism), regression 12/12; live diagnostics
  re-verified on the pinned binary (procexp stub caller, puttygen stack
  names, zoomit 133+report).
Artifacts: evidence/sweep011_perf/ (README + 4 trimmed evidence files),
artifacts/sweep011_perf/ (TSVs, COMPARISON.md, funcmaps/), 4 new scripts,
.gitignore raw-runs exclusion, worklist SW-011 DONE block.

---
Task ID: SWEEP-012
Agent: Super Z (main agent)
Task: User directive 2026-10-18: "از اون لیست ۱۰۰ نرم افزاری هر چی میتونی تست بکن، ببین چقدر پیشرفت اجرا داشتن — تغییرات بدون پیشرفت ارزشی نداره" (test as many of the 100-program list as available; measure EXECUTION PROGRESS; changes without progress are worthless).

Work Log:
- Built scripts/sweep012_progsweep.sh (full-pool discovery, resumable, MINWIN_SCREENSHOT=0, api_trace ON) + scripts/sweep012_progress.py (guest_rip -> dll!import decoder via FF15/disp32/IAT + progress score 0..5 + named-gate clusterer + before/after differ).
- Pool: 51 real binaries (sweep50/dl Sysinternals+NirSoft+PuTTY, opensource sqlite/7z/jq, samples md5deep/zstd/lz4/gzip, games, UPX).
- BASELINE: 15 RAN-OUTPUT / 12 STUB-GATE / 13 CRASH-GUEST / 11 LOAD-REJECT, mean_score 2.69 (runs-BASELINE/progress.json).
- Honest batch #1 (program-count driven): dialog protocol (DialogBoxIndirectParamA/W + DialogBoxParamA/W run WM_INITDIALOG then WM_COMMAND(IDOK) then return EndDialog's code; EndDialog entry), COM init family (CoInitialize/Ex, OleInitialize, CoUninitialize, OleUninitialize -> S_OK/no-op), version.dll honest-absent trio, user32 misc (RegisterWindowMessageW/RegisterClipboardFormatW 0xC000-range, GetSysColorBrush stock-handle 0x0004xx, GetWindowDC, GetCursorPos, PostMessageA/W, LoadAcceleratorsA/W, GetDialogBaseUnits, LookupAccountNameW FALSE+1332, SystemParametersInfoW FALSE), kernel32 (GetSystemDefaultLangID, CreateMutexExW, CreateEventExW), msvcrt __wgetmainargs (wide CRT init over existing build_wargv_wenv).
- LIVE DIAGNOSIS (autoruns64 EULA path, REA decompile FUN_140062810 confirmed): crash RIP=0 at guest jmp-rax thunk. Root cause chain: (1) GetModuleHandleW returned NULL for EVERY named module -> GetProcAddress(NULL,"RtlGetVersion")=NULL -> jmp 0; (2) after pseudo-handle fix, next crash = GetProcAddress(h,(LPCSTR)0x87) ORDINAL form printed as %s -> strlen(0x87) SEGV in libc -> ordinal branch added; (3) next crash = mw_RtlGetVersion read garbage dwOSVersionInfoSize -> memset(140TB) -> clamped 276..284. ALSO: GetModuleHandleW took Linux wchar_t (4-byte) while guests pass UTF-16 (2-byte) — 'kernel32.dll' mangled to 'kre3.l' (found via temp [DBG-GMHW] trace) — fixed to uint16_t*.
- Added ntdll!RtlGetVersion (10.0.19045 / VER_PLATFORM_WIN32_NT, size-clamped) + round 2 from AFTER-sweep gates: MulDiv (64-bit intermediate, Wine rounding), LoadStringA/W (resource-absent 0), SetWindowsHookExA/W (NULL), UnhookWindowsHookEx (TRUE), RegDeleteKeyW (ERROR_FILE_NOT_FOUND).
- AFTER: mean_score 2.69->2.71; RAN-OUTPUT 15->16; STUB-GATE 12->10; wins: autoruns64 exit 1 (EULA-refused) -> EXIT 0 full run (15 stubs, 3x deterministic), zoomit64 crash 133 -> EXIT 0 (9 stubs, 3x deterministic), putty resolved to own exit, Bginfo64/WinMergeU/ProduKey/7zFM/7zG gates cleared; deeper-into-crash: procexp64 1->21 stubs (past EULA+MulDiv, now ntdll NT-query frontier NtOpenSymbolicLinkObject), Coreinfo64/SearchMyFiles past old gates into new guest-internal crashes.
- G1 re-pinned sha256 6754715c687eae72 12/12; runs dirs trimmed to summary+meta+progress (16MB->72KB).

Stage Summary:
- Progress discipline established: every future change must move the 51-program mean_score or class histogram, else it is not valuable.
- Top named gates REMAINING: user32 GetDlgItem 5x / SetWindowTextW 4x / SendMessageW 4x (mini-dialog family), procexp64 ntdll NT-query family, md5deep64-suite x6 + curl + puttygen + sqlite3_analyzer crash cluster (each = own BUG).
- Artifacts: artifacts/sweep012/runs-{BASELINE,AFTER}/{summary,meta,progress.json}; scripts/sweep012_{progsweep.sh,progress.py}.

---
Task ID: SWEEP-013
Agent: Super Z (main agent)
Task: User directives 2026-10-19 — (1) test at least one simple graphical
program (a calculator); (2) multiply the task queue 5x and solve many
problems; keep measuring per-program execution progress.

Work Log:
- Restored mingw-w64 toolchain (home reset wiped it): downloaded 5 Debian
  debs (gcc-mingw-w64-x86-64-posix 14.2.0-19+27+b1, binutils 2.47, mingw
  12.0.0-5), extracted to scripts/mingw-debs/root/usr — smoke test PE32+ OK.
- loader.c mini window manager: MwChild registry (64 slots, HWND 0x0003xxxx),
  system-class detection (atom 0x8080..0x8085 + string forms), child-aware
  CreateWindowExW/A + DestroyWindow; SendMessageW/A real (WM_SETTEXT /
  WM_GETTEXT / WM_GETTEXTLEN / BM_GETCHECK + main-wndproc routing — the old
  A stub returned 0); pump v3 MINWIN_CLICKS scripted WM_COMMAND delivery.
- Honest batch #3 (~65 APIs): dialog/child family (GetDlgItem,
  SetWindowTextW/A, GetWindowTextW/A, GetDlgItemText/Int family, checkbox
  family, menus, LoadImage, GetClassInfoEx, GetWindowPlacement/Rect,
  MoveWindow, focus/foreground, CharLower, DrawTextW, ExtTextOutW, fonts
  CreateFontIndirectW/A + GetObjectW/A), kernel32 (LocalReAlloc,
  GetSystemWindowsDirectoryW/A, FlushInstructionCache, lstrcpynW/A, lstrcpyW/A,
  InterlockedPopEntrySList, GetWindowsDirectoryA), ntdll (NtQuerySystem
  Information classes 0/2 real + honest 0xC0000003 otherwise, NT object
  opens/queries with honest NTSTATUS, RtlNtStatusToDosError), advapi32
  (real SID alloc/free/equal, GetTokenInformation classes 18/20 real,
  OpenSCManager NULL+ERROR_ACCESS_DENIED, CloseServiceHandle, registry
  A-variants), shell32 (SHGetFolderPathW 8 CSIDLs, Shell_NotifyIconW),
  gdiplus (GdiplusStartup/Shutdown), ole32/oleaut32 (CoInitializeSecurity,
  SysAllocStringByteLen), msvcrt _ismbblead.
- BUG-068 fixed in-flight: WM_GETTEXT buffer/count swapped (wParam=count,
  lParam=buffer) — caught by the calculator crash at mw_SendMessageW+0x237
  (symbolized via nm: R13=0 write). Guest calc.c expression double-append
  also fixed (guest-side only).
- Calculator acceptance: tests/gui/calc.c — 3x deterministic exit 0,
  "[CALC] 12+45 = 57" (clicks 1,2,+,4,5,=), second scenario "[CALC] 9*8 = 72";
  guest-drawn BMP archived (title/expression/display/log lines visible).
- Full-pool sweep AFTER2 (53 programs incl. gui-calc): mean_score
  2.71->2.92, RAN-OUTPUT 16->22, STUB-GATE 10->5, zero-stub clean runs for
  Autoruns64/ZoomIt64/Diskmon64/7z2604-x64/jq-windows-amd64/sqlite3/gui-calc;
  procexp64 21->0 stubs (IsBadStringPtrW frontier cleared, now guest-level
  crash); Bginfo64 16->8 stubs; RAMMap64 deeper (1 gate left: InvalidateRgn);
  zstd 37/lz4 40/jq 5/trurl 7 real exit codes.
- Harness fix: sweep feeds stdin </dev/null (stdin-blockers zstd/lz4/jq/
  sqlite3/md5deep64 were previously misclassified as hang/crash at the CRT
  stage; they now measure their true stage).
- Round 2 from AFTER2 evidence: IsBadStringPtrW/A, IsBadRead/WritePtr,
  RegisterWindowMessageA, lstrcpyW/A, CreateDialogParamW/A(modeless),
  CreateDialogIndirectParamW, GetTextMetricsW/A, GetTextCharset(Info),
  EnumFontFamiliesW/A, EnumDisplayMonitors, GetMonitorInfoW/A,
  CoInitializeSecurity, SysAllocStringByteLen.
- G1 re-pinned sha256 700d83ee 12/12 (evidence/master/g1/20261018_174244).
- Task queue 5x expanded: SW-601..SW-650 (50 named, evidence-backed items)
  appended to SWEEP50_WORKLIST.md.

Stage Summary:
- Artifacts: evidence/sweep013/ (README, comparison.txt, progress.json,
  summary.tsv, calc_screenshot.bmp); artifacts/sweep013_calc/ (2 BMPs);
  artifacts/sweep012/runs-AFTER2/ (53 trimmed runs); tests/gui/calc.c+.exe;
  scripts/sweep012_progsweep.sh (stdin fix + calc pool entry).
- Calculator = first GUI program with REAL end-to-end input processing
  (scripted user clicks -> WM_COMMAND -> text I/O -> GDI paint -> BMP).
- Next lever: SW-601 InvalidateRgn (RAMMap64), SW-602/608/609 guest-level
  crash symbolization, SW-603 Bginfo64 8-stub family.

## 2026-10-18 (SWEEP-014) — «ادامه سه برابری»: batch #4 + 3x queue + calc scenario D
- **BUG-069 CLOSED (fixed)**: sweep012_progress.py decoder handled only direct
  `FF 15` calls; ProduKey/cports/7zG/sqlite3_analyzer call imports via
  `E8 rel32 -> FF 25` jmp-thunks, so their gates printed ??NOT-FF15??.
  Decoder now walks the thunk (tgt = rip+disp, slot = tgt+6+disp2) and names
  ordinal slots (`dll!#N`). Upgraded in scripts/sweep012_progress.py.
- **BUG-070 CLOSED (wired)**: WSAStartup/WSACleanup were FULLY IMPLEMENTED
  (ffmpeg-era, WSADataT 408B) yet gated 3 programs — the ENTRY lines were
  missing from g_import_table. New audit class: defined-but-unwired.
- **BUG-071 CLOSED (fixed)**: WS2_32 imports bind by FIXED ORDINAL
  (ProduKey ILT[0]=0x8000..006f); the fixed-ordinal map lacked WS2_32
  entirely (WARN printed "Ordinal 115 (not supported)", never the name).
  Added the standard ws2_32 ordinal table (29 entries, pefile ordlookup
  cross-checked vs every pool program's raw ILT) + OLEAUT32 150 =
  SysAllocStringByteLen (Bginfo64 raw ILT [4]; 395 guess discarded) +
  COMCTL32 17 = ImageList_Create (7zG). Ordinal-8 corrected to VariantInit
  (pefile real-DLL db; old 8->SysStringByteLen never fired but was wrong;
  VariantInit implemented + wired).
- **Honest batch #4 (~40 APIs)**: ws2_32 WSAStartup/WSACleanup wired +
  gethostbyname/gethostbyaddr/getservbyname/getservbyport (honest NULL +
  WSA error) + WSAAsyncSelect (vacuous success); msvcrt operator
  new/delete/new[]/delete[] + _mbsnbcat (cports 18x gate); user32
  Get/SetWindowLongPtrW + Get/SetWindowLongW (per-window store, GWLP_WNDPROC
  subclass routes g_wndproc_fwd) + Get/SetPropW/RemovePropW (MAKEINTATOM
  aware) + MapWindowPoints (identity — the engine's honest coordinate model)
  + InvalidateRgn (superset paint via real InvalidateRect path) + CopyRect +
  Set/GetWindowPlacement (store round-trip) + SetWindowPos + GetScrollInfo
  (honest empty range); kernel32 GetProfileIntW (default — no win.ini store)
  + IsWow64Process (native x64 FALSE) + SetSearchPathMode; ole32
  CoCreateInstance (POLICY UPDATE: honest REGDB_E_CLASSNOTFOUND + *ppv=NULL —
  the old "unresolved 0" was S_OK-with-NULL-punk, crashes well-formed
  callers) + CoRegisterMessageFilter; shlwapi PathFindExtensionW/
  PathFindFileNameW/A (pure pointer math); comctl32 ImageList
  SetImageCount/SetBkColor/ReplaceIcon (index contract real, placeholder
  pixels documented); advapi32 LsaOpenPolicy (STATUS_NOT_IMPLEMENTED);
  gdiplus GdipCreateBitmapFromStream/GetImageWidth/Height/PixelFormat/
  DisposeImage (honest GpStatus failures, no codecs headless); shell32
  SetCurrentProcessExplicitAppUserModelID (S_OK); **gdi32 CreateDIBSection
  REAL** (BITMAPINFOHEADER parse + MWBmp pool + real ppvBits pointer).
- **MEASURED PROGRESS (52-program pool, AFTER2 -> AFTER3)**:
  mean_score 2.92 -> 3.08; RAN-OUTPUT 22 -> 26 (+5: RAMMap64 139crash->exit0
  0-stubs, cports 139crash->exit0, ProduKey 5->0 stubs, 7zG 1->0, 7zFM 3->0);
  STUB-GATE 5 -> 0 (EVERY named stub gate in the pool is closed);
  Bginfo64/WinMergeU ran deeper (exit 9 -> 0-stub guest crashes = new
  frontier, queued SW-795/796/797/798). Stub trace limit 20 -> 200.
- **GUI acceptance 9/9 (scripts/sweep014_calc_accept.sh)**: scenario A
  12+45=57, C 9*8=72, NEW D 84/12=7 — all 3x deterministic exit 0 via
  MW_GUI_EVENTS=1 + MINWIN_CLICKS.
- **Task queue 3x**: SW-651..SW-800 (150 items) generated from AFTER3
  evidence (scripts/sweep014_queue.py): 15 crash-frontier REA funcmap
  triages, 15 ws2_32 socket ladder, 20 GUI, 10 oleaut32, 15 GDI, 10 CRT
  tail, 10 PE-DLL slices, 25 kernel/user32 tail, 30 infra/verify.
- **G1 re-pinned sha 45fd1d18 12/12** (evidence/master/g1/20261018_182015).

---
Task ID: SWEEP-015
Agent: Super Z (main agent)
Task: «ادامه سه برابری» — honest-stub continuation, broad sweep, GUI calc, worklist x3

Work Log:
- Discovered upstream had advanced to f64290e (SWEEP-014) via `git fetch` — local b783c44 was stale; fast-forwarded main to f64290e (clean ff, no divergence).
- Re-derived SWEEP-008/009-class fixes from scratch in a divergent line (self-identifying stub, mini-registry, atoms, Color*, RtlGetVersion, GetModuleHandleW identity, GetProcAddress IS_INTRESOURCE, DialogBoxIndirectParamW, EULA render) — all superseded by upstream SWEEP-009..014; port patch preserved in .agent/patches/sweep015_resource_version_port.patch.
- LANDED on top of f64290e (zero-regression subset): SW-801f pump gate-bypass (timerless guests), SW-801g UXTHEME dark-mode ordinals 104/132/133/135 (IAT map + GetProcAddress + ENTRYs). Reverted with evidence: SW-801a-e resource/version port (Bginfo64/Coreinfo64 flow regressions documented as worklist items).
- NEW GAME-CALC: tests/games/miniwin_calc.c (pure GDI + WM_KEYDOWN calc) — 3x exit0, 4 frames, all-frame SHA 1b92b95f873029f0 x3, display "1+2=" result "3".
- Regressions green: upstream calc 9/9 (A/C/D scenarios), snake 3x exit0 (30 frames, SHA 52d2cc22 x3 — frame count is environment-sensitive, recorded SW-801h), breakout 3x exit0.
- Broad sweep 18 x64 exes x3 (artifacts/sweep50/sweep015_sweep.tsv): exit0x3 = Autoruns64, Diskmon64, ProduKey, RAMMap64, ZoomIt64, cports, miniwin_calc; exit1x3 = putty/pageant/Procmon64; deterministic 139/-11 frontier = puttygen/Coreinfo64/autorunsc64/WinMergeU/procexp64/Bginfo64/SearchMyFiles/CoreinfoEx64 (SW-801h environment-sensitivity: pure-upstream binary reproduces).
- G1 re-pinned: sha 986f349fd2bf7106... 12/12 (evidence/master/g1/20261018_205456).
- Worklist: SW-801a..j appended (10 evidence-cited items) on top of the SW-651..800 x3 queue.

Stage Summary:
- main == f64290e + SWEEP-015 deltas; G1 12/12 green; calc acceptance 3x deterministic.
- Known environment frontier recorded (SW-801h); pending port batch SW-801a-e with gates.

---

## SWEEP-017 (2026-10-19) — reconcile with upstream SWEEP-014/015/016, local G1 re-pin

- Local SWEEP-008 elephant batch (named stubs / registry / dialogs / version family / calc / mass sweep / worklist x3.32) was built on a stale baseline; push revealed origin/main already ahead via SWEEP-014/015/016 which landed the same subsystems plus more.
- Honest handling: no force-push; local batch archived on branch `sweep008-local-divergent`; main synced to 2a15489 (SWEEP-016).
- Upstream verified on this machine: build 0 errors, G1 PIN 12/12 (evidence/master/g1/20261019_133300), calc 9/9, snake 25f exit0, breakout exit0, rammap64 -accepteula exit0. Committed + pushed as 5d5e06e.
- Note: local artifacts/sweep50/dl = 2024-era binaries (SWEEP-007 manifest); upstream swept re-downloaded 2026 official builds. Re-download locally before comparing frontier results against upstream reports.
- Next: SW-802..810 (9 remaining 139s) and SW-811..850 queue per upstream worklist.

---

## SWEEP-018 (2026-10-19) — «ادامه و پوش» elephant batch: 5x139->exit0, G1 green

Work Log:
- Decoded produkey/cports/diskmon64/rammap64/searchmyfiles crash RIPs with
  objdump + scripts/sweep018_decode_rip.py (new, pefile IAT map).
- ONE loader.c batch: (1) WM_CREATE now carries a real CREATESTRUCT with
  forwarded lpParam (SW-802..805 root cause: NirSoft WndProcs read
  lParam->lpCreateParams and store hwnd at +0x110); (2) honest GetPixel /
  SetStretchBltMode / StretchBlt / CreateToolbarEx / CreateStatusWindowA(W)
  (COMCTL32 ordinal #6 named via SB_SETPARTS follow-up) + SB_SETPARTS->TRUE;
  (3) LoadImageW decodes RT_BITMAP DIBs (1/4/8/24/32bpp BI_RGB) into
  HBITMAP-space objects ImageList_Add can read; (4) tables grown: children
  64->512, memDC/bitmap 8->32, ImageList 8->16; (5) crash handler hardened:
  SA_ONSTACK + 64KB altstack + sigsetjmp-guarded raw reads (silent-139 class
  eliminated).
- Iterate-don't-rewrite discipline held: one failed MultiEdit (atomic
  rollback) silently dropped 4 ENTRY lines — found by re-decoding stub sites
  on a rebuilt binary; re-applied and re-verified.
- Full breadth sweep x3 pump-off (scripts/sweep018_broad_sweep.sh ->
  artifacts/sweep018/sweep/sweep018_sweep.tsv): autoruns64/cports/diskmon64/
  produkey/rammap64 139->exit0 x3 each, zero regressions (bginfo64/coreinfoex
  held 0; pageant/procmon64/putty/zoomit64 held own exit codes; procexp64/
  puttygen/searchmyfiles/winmergeu 139 with recorded signatures SW-807/809/
  814/815).
- Gates: G1 PIN sha 616a9ba1 12/12; gui calc 9/9; snake 3x byte-identical;
  breakout 3x (bricks 40->13); miniwin_calc rebuilt (lost untracked binary,
  SW-801h class) 3x exit0 4 frames SHA 7e8821ae x3.
- New evidence: evidence/sweep018/README.md; artifacts/sweep018/.
- Worklist: SW-812..815 filed; «ادامه سه برابری» queue SW-816..880 (65 items)
  appended to .agent/SWEEP50_WORKLIST.md.

Stage Summary:
- main = 5d5e06e + SWEEP-018 batch; G1 12/12 green; sweep 5 programs flipped
  139->0; pump-on NirSoft loop root-caused and queued (SW-812, menu family).
- Known open: procexp64/puttygen/searchmyfiles/winmergeu 139s with signatures;
  pump-on menu-family queue SW-816..820.

---
Task ID: SWEEP-019
Agent: main agent
Task: «ادامه چند ده تسک همزمان و پوش در گیت هاب» — execute the queued
  SW-816..880 continuation queue (65 items, 13 families) as ONE elephant
  batch, measured sweep, push to GitHub.

Work Log:
- Implemented all 13 queue families in one loader.c edit series: menu
  storage model (SW-816..820), clipboard (821..825), comdlg cancel canned
  returns (826..830), SysListView32/SysTreeView32/SysTabControl32/SysHeader32
  class registration + LVM_/TVM_/TCM_/HDM_ routing in SendMessageW+A
  (831..840, ROOT CAUSE: sysclass matcher only knew 8 built-ins), GDI
  region rect algebra (841..845), honest no-op printer replacing the -1
  tail (846..850), shell32 family incl. SHGetSpecialFolderPath + IMalloc
  (851..855), mpr.dll WNet* + getnameinfo + WSAEnumProtocols (856..860),
  msvcrt _mbs*/_memicmp/_strlwr/_snprintf (861..865), COM completion
  (866..870), range-checked Read/WriteProcessMemory with mprotect dance
  (871..875), registry A-tail (876..878).
- 74 new ENTRY lines across user32/comdlg32/shell32/mpr/ws2_32/msvcrt/
  ole32/KERNEL32/GDI32.
- G1 re-pin: sha 2f5535f254a2819e11dc6190ce91c74932171baabdf5517afd0d0965d8ff3774,
  regression 12/12 (evidence/master/g1/20261019_153111).
- Measured sweep (scripts/sweep019_chunk.sh, 4 foreground chunks — the 3 GB
  box kills background batches): 16 programs x3 deterministic + produkey
  pump-on + calc/snake/breakout x3. ZERO regressions vs SWEEP-018; bginfo64
  unresolved 336 -> 310 (-26); calc acceptance 9/9 PASS.
- Worklist: SW-816..880 marked LANDED; new queue SW-881..920 (40 items).

Stage Summary:
- main advanced with SWEEP-019 batch; TSV artifacts/sweep019/sweep/sweep019_sweep.tsv;
  evidence/sweep019/README.md; SW-812 pump-on still open (queued SW-881..885);
  139 group unchanged (SW-807/809/814/815 signatures carried).

---
## SWEEP-020 reconcile (2026-10-19, this sandbox)

- Sandbox carried a pre-SWEEP-009 snapshot; a local divergent SWEEP-008
  batch was built here (honest stubs, in-memory registry, named-stub
  machinery, BUG-068/070/071/072) and archived WITHOUT merge on branch
  `sweep008-local-divergent-2` (pushed for the record; no force-push).
- Upstream superseded all of it (SWEEP-014/015/016 landed the same levers
  first; SWEEP-017/018/019 went further: CREATESTRUCT root cause, exit0 x3
  for autoruns64/cports/diskmon64/produkey/rammap64, menus/clipboard/
  sysclasses/regions, 65-item SW-816..880 batch).
- Local main reset-synced to origin/main = d5af5f8 (SWEEP-019); upstream
  builds byte-exact (1,771,176 bytes).
- VERIFY on this machine: G1 re-pin sha 2f5535f2 (12/12, EXACT match to
  SWEEP-019's recorded pin); sweep019 broad sweep re-run 15 programs x3 —
  parity with recorded TSV (autoruns64/bginfo64/coreinfoex64/cports/
  diskmon64/produkey/rammap64 exit0 x3); calc acceptance 9/9 PASS;
  snake 30 frames x3 byte-identical exit0 x3.
- Evidence: evidence/master/g1/20261019_173159, fresh
  artifacts/sweep019/sweep/sweep019_sweep.tsv + acceptance outputs.

## SWEEP-021 — 2026-10-19 — «ادامه چند ده تسک همزمان» elephant batch
- LANDED (one batch): SW-881..920 remainder — 24 new functions + 35 ENTRY rows in
  src/loader/loader.c: menu deepening (InsertMenuItemA/W, RemoveMenu, DeleteMenu,
  GetMenuItemRect, TrackPopupMenuEx), ListView deepening (LVM_GETNEXTITEM/SETITEM/
  DELETEALLITEMS/GETSUBITEMRECT/GETSTRINGWIDTH), TreeView/Tab deepening (TVM_GETITEM/
  SELECTITEM/EXPAND, TCM_GETITEM/ADJUSTRECT), GDI regions (ExtCreateRegion, GetRgnBox,
  OffsetRgn, PtInRegion, EqualRgn), shell (SHGetFolderPathA, SHGetKnownFolderPath 8
  GUIDs, SHGetDesktopFolder minimal IShellFolder, SHAutoComplete, SHSetValueA/W),
  RICHEDIT class 0x8C + EM_SETTEXTEX/STREAMIN/STREAMOUT/GETTEXTEX, CRT (_mbsncat,
  _mbsnbcpy, _mbslen, _splitpath, _makepath), COM (CoGetObjectContext, CoFreeUnused-
  Libraries, StringFromGUID2; g_com_init apartment flag).
- MEASURED: build 1,771,176 -> 1,836,696; 15-prog corpus x3 exit-deterministic; ZERO
  regressions (7 exit0 / 4 exit139 / 4 exit1 unchanged vs SWEEP-019); unresolved
  1325 -> 1288 (-37, real call paths); GUI accounting: 6 real GUI-subsystem progs
  exit0 x3 (autoruns64/bginfo64/cports/diskmon64/produkey/rammap64) + miniwin_calc
  9/9 + snake 30f x3 + breakout 381f x3 (40->13 bricks) = 9 GUI programs with
  lifecycle evidence; artifacts/sweep021/{sweep/sweep021_sweep.tsv,GUI_EVIDENCE.tsv}.
- G1 re-pin OK: 12/12 regression, UPX banner exact, evidence/master/g1/20261019_181613.
- Worklist: SW-881..920 marked LANDED; new queue SW-921..960 (40 items, x3 standing
  order). Carried open: SW-800, SW-801i/j, SW-807/809/814/815 crash signatures.
- evidence/sweep021/README.md + scripts/sweep021_chunk.sh + sweep021_gui_evidence.py.

## SWEEP-022 (2026-10-19) — «ادامه» elephant batch: SW-921..960 LANDED + BUG-074/075

- Synced local to origin/main 0bc6ba3 (SWEEP-021; local had been rewound to b783c44).
- ONE loader.c batch (~45 new/updated imports):
  * dialogs: DialogBoxIndirectParamW/A real DLGTEMPLATE walk (SZ_OrOrd,
    DS_SETFONT, aligned items, class ordinals 0x80..0x85) -> children in the
    shared registry; SendDlgItemMessageA/W (GetDlgItem->SendMessage).
  * ListView (+BUG-074): 0x1008=LVM_DELETEITEM (row shift) / 0x1009=DELETEALLITEMS
    swap fixed; GETITEMA/W, SET/GETCOLUMNA/W (colw+coltxt store), SORTITEMS.
  * TreeView (+BUG-075): TVGN corrected to wine commctrl.h (ROOT=0, CHILD=4,
    CARET=9, LASTVISIBLE=10; SWEEP-021 mapped 4->ROOT); NEXT/PREVIOUS walks;
    GETITEMRECT/GETVISIBLECOUNT/HITTEST/SORTCHILDREN.
  * GDI DIB: GetDIBits/SetDIBits (24bpp bottom-up over CreateDIBSection pool),
    MaskBlt (SRCCOPY->BitBlt), BitBlt strips CAPTUREBLT/NOMIRRORBITMAP.
  * fonts: EnumFontFamiliesExW/A, GetTextFaceA/W, SetTextJustification.
  * registry: in-memory hive (empty-at-start; create/set/query/enum/delete/
    info real; full A-tail routing; RegNotifyChangeKeyValue -> 0).
  * CRT: _itoa/_ltoa real signed radix.
- MEASURED: build 1,836,696 -> 1,880,672 (+43,976); 15-prog corpus x3
  deterministic 45/45; ZERO regressions (7/4/4 parity vs SWEEP-021);
  corpus unresolved 1505 -> 1476 (-29); procexp64 ONE NEW stub @0x14006c328
  (deeper progress -> SW-964).
- GUI accounting this sweep: 6 GUI programs exit0 x3 (pump-off) + produkey/
  bginfo64/cports/diskmon64 pump-on exit0 + miniwin_calc 9/9 PASS.
  autoruns64/rammap64 pump-on 139 = PRE-EXISTING (baseline-verified identical
  signature on rebuilt origin/main binary — artifacts/sweep022/pumpon_baseline/).
- G1 re-pin OK: 12/12, UPX exact, sha aecfc024..., evidence/master/g1/20261019_193310.
- Worklist: SW-921..960 marked LANDED; new queue SW-961..1000 (40 items, x3
  standing order); file 276 -> 939 lines (x3.4).
- evidence/sweep022/README.md + scripts/sweep022_chunk.sh + sweep022_calc_accept.sh;
  artifacts/sweep022/{sweep,pumpon,pumpon_baseline} + artifacts/sweep022_calc.

## SWEEP-023 (2026-10-19) — «ادامه ۶۰ تسک رو یکی یکی اجرا بکن» 60-task batch: SW-961..1000 LANDED + SW-1001..1020 queued

- ONE loader.c batch (~25 new/updated imports + 3 data models + 1 REAL bug fix):
  * ListView: per-item LPARAM store (insert/set/get/delete-shift) + LVM_SORTITEMS
    REAL guest compare callback (ms_abi fn-ptr, insertion sort over txt+lparam)
    + 64->512 items + true mid-list insert row shift (SW-976/977).
  * TreeView: real node model (lazy per-slot parent[]/state[]/text[]) —
    TVGN_CHILD/TVGN_PARENT walks, real labels, TVIS_SELECTED round-trip (SW-978/979).
  * Tab: TCM label store (SW-980; SWEEP-021 TODO comment closed).
  * GDI: DIB 16/32bpp conversion both directions (5-5-5/5-6-5/BITFIELDS/BGRA,
    DWORD strides), StretchDIBits real, Set/GetDIBColorTable store,
    GetTextExtentPoint32W/A (8x8 face), SetTextAlign/GetTextAlign applied in
    TextOutA/W (SW-981..985).
  * Network: WSAIoctl ext-fn typed fail + ioctlsocket FIONREAD translate,
    select() WSA-error mapping, SD_SEND/SD_RECEIVE shutdown state (WSAESHUTDOWN),
    WSAStringToAddressA + WSAAddressToStringA/W real (SW-986..990).
  * Registry: RegSetValueExW OVERWRITE FIX (old append-only read back FIRST
    value — real bug, killed), MINWIN_REG_SNAPSHOT hive save/load (MWREG001),
    RegSaveKey/RegRestoreKey W/A typed no-op, SHGetValueW/A real (SW-991..995).
- MEASURED: build 1,880,672 -> 1,935,792 (+55,120); 15-prog corpus x3
  deterministic (45/45); ZERO regressions vs SWEEP-022 (7 exit0 / 4 exit139 /
  4 exit1 parity); coreinfoex64 exit0 0-unresolved = SW-561 RESOLVED verified;
  procexp64 stub call #1 deterministic x3 (BUG-078: crash RIP = UTF-16 string
  ptr, static decode impossible on UPX-packed image).
- NEW test PE: tests/reg/mw_reg_test.c -> mw_reg_test.exe (zig cc) —
  pass=19 fail=0 exit0 (REG_MULTI_SZ byte-exact, DWORD overwrite, SHGetValueW
  end-to-end, honest FILE_NOT_FOUND miss).
- GUI accounting: 6 pump-off exit0 x3 + 4 pump-on exit0 (produkey/cports/
  bginfo64/diskmon64) + miniwin_calc 12+3=15 via WM_KEYDOWN + NEW child-focus
  walk 15/15 battery (scripts/sweep023_calc_battery.sh) + snake 30f +
  breakout 80f + mw_reg_test MessageBox = 10+ GUI programs with evidence
  (artifacts/sweep023/GUI_EVIDENCE.tsv).
- Infra: zig toolchain RESTORED (pip ziglang) + 3 games rebuilt
  (artifacts/sweep023/*.exe); 100-software refresh scoped honestly (local
  corpus = 16 programs; wider binaries cleaned -> SW-1003 requeue);
  sweep023_decode_stubs.py + sweep023_stage_refresh.py committed.
- BUG docs: BUG-076 (autoruns64 pump-on RVA 0x4d72d), BUG-077 (rammap64
  pump-on host-side NULL), BUG-078 (procexp64 stub->string-jump), BUG-079
  (puttygen NULL fn-ptr), BUG-080 (searchmyfiles/winmergeu classes).
- Exit1 gates SELF-NAMED this sweep: pageant "Unable to access security APIs"
  (6 ADVAPI32 imports -> SW-1001..1005), procmon64 window-layout family
  (-> SW-1006..1010), putty GDI font/comm (-> SW-1011..1015).
- Worklist: SW-961..1000 marked LANDED (per-task evidence) + new queue
  SW-1001..1020 (20 items; 60-task mandate: 40 landed + 20 queued).

---
Task ID: SWEEP-008-reconcile
Agent: main (z.ai)

Task: Reconcile stale local clone with origin/main (SWEEP-023 era).

Work Log:
- Local clone was stale at b783c44; origin/main held SWEEP-008b..SWEEP-023
  (6c5f651 WAS pushed by the prior session — local clone just never synced;
  same divergence class as SWEEP-017/020 reconciles).
- Local redone SWEEP-008 batch (commit 9bf6dcb) archived on branch
  sweep008-local-divergent-3 and pushed (independent root-cause proofs:
  RtlGetVersion hook ordering, MAKEINTATOM prop deref, memset clamps — all
  landed upstream first via SWEEP-009/010/014).
- Local main reset to origin/main 41a3c6d (no force-push).
- Verified upstream ON THIS MACHINE: binary sha 43866c5d3594bc1317468b4dd6639b591406c3d716f146abba2088f1d31bbe7e
  EXACT match to SWEEP-023 recorded pin; autoruns64/rammap64/produkey/cports
  exit0; procexp64 exit139 = documented BUG-078 stub#1 frontier; G1 re-pin OK.

Stage Summary:
- Repo synced, verified, G1 pinned. Divergent work preserved on
  sweep008-local-divergent-3.

---
SWEEP-025 (2026-10-20) «ادامه فیل و یوز» — SW-1001..1020 + 10 root-cause fixes
- Gates: pageant 1->124 daemon, procmon64 1->0, putty 1->9, puttygen 139->0.
- Root causes: LoadLibraryA full-path + no-ext, TokenUser/TokenGroups,
  OpenProcess self, SetEntriesInAclA, GetUserNameExA SPN, BUG-081 GCP fix.
- Corpus: complete runs 7->10; unresolved: autoruns 53, bginfo 203,
  winmerge 202, procexp 169 (BUG-078), searchmyfiles 17.
- GUI: bginfo64 1280x1024 frame; procmon64 26.35M SendMessageW/12s.
- G1 PIN OK sha c490f1fc regression 12/12; worklist +280 lines (SW-1021..1100).
- BUG-082 open (putty GS-cookie; Ghidra env lost -> requeue SW-1022/1030).

---
SWEEP-026 (2026-10-20) «ادامه فیل یوز با ۵۰ تسک» — resumed interrupted batch, closed it
- Found SWEEP-026 mid-flight at session resume: 764 lines uncommitted in
  loader.c (SW-1021..1097 batch + SW-1023 timer + SW-1090 trace cap).
- Build fix: mw_SetDIBitsToDevice called the wrong-direction row helper
  (from_bgr24 vs to_bgr24) — one-line fix, then clean build.
- G1 PIN OK sha 51bc4fb3 regression 12/12 (evidence/master/g1/20261020_080749).
- Corpus x3 45/45: putty 9->0 (BUG-082 closed via SW-1021 TEXTMETRICW),
  complete runs 10->11; unresolved autoruns 52 / bginfo 201 / cports 20 /
  procexp 161; zero regressions (searchmyfiles/winmergeu 139 + zoomit 1
  unchanged, documented frontiers).
- GUI: procmon64 26.35M->998,347 SendMessageW/12s (SW-1023 elapse gate 26x),
  trace cap exact 1,000,002 lines (SW-1090); putty messagebox BMP; bginfo64
  frame; calc battery 15/15 (rebuilt miniwin_calc from tests/games via
  zig 0.17 pip); snake/breakout exit0+frames.
- Worklist: 32 items marked LANDED w/ evidence; SW-1101..1150 queued (50);
  sweep026 scripts committed (sweep.sh, gui_battery.sh, mark_landed.py).

---
SWEEP-027 (2026-10-20) «واگن های قطار» — one wagon batch on reconciled tree
- Env reset detected (local 55b3982): reset to origin 3cd11c0; deterministic
  rebuild reproduced SWEEP-026 pin sha 51bc4fb3 EXACTLY before new work.
- 45 imports + _wcmdln data import in one splice (scripts/sweep027_impl.c
  + apply.py); 4 drift conflicts caught pre-build and adjusted.
- searchmyfiles 139->exit0 (run #12); unresolved: autoruns 26, bginfo 145,
  cports 6, diskmon 5, produkey 10, rammap 4, procexp 128, winmerge 150.
- G1 PIN OK sha 0f4b4773 12/12; corpus x3 45/45; calc battery 15/15.
- Pushed as one commit; worklist marks + summary appended.

---
SWEEP-030 (2026-10-20) «قانون کلی قطار» — SW-1151 engine double-fix + fastfail honesty
- SW-1151 full decode: Frame[7] func 0x2279a0 personality = __GSHandlerCheck_EH
  shim 0x319158 -> GS-check core 0x319070 ([EstFrame+0x570] cookie slot, tail-jmp
  __security_check_cookie 0x318660) -> __report_gsfailure 0x318730 ->
  IsProcessorFeaturePresent(23) gate -> int 0x29. Cookie slot = frame_base+0x570
  (prologue rbp=entry-0x4a8, cookie=[rbp+0x470], stored global^frame_base).
- ROOT CAUSE 1: EstablisherFrame = post-undo RSP (entry RSP, +0x5a8 off) instead
  of ReactOS GetEstablisherFrame semantics (call-boundary RSP for FrameReg==0 /
  live FP - FrameOffset*16 for FrameReg!=0). Fixed via ms_est out-param flowing
  to dc.EstablisherFrame + handler RDX (dispatch + BUG-031 walk + GCC-bridge
  0x21474343 intercept) and mw_RtlVirtualUnwind est rules.
- ROOT CAUSE 2: mw_RtlVirtualUnwind never advanced ctx.Rip (guest walkers decoded
  the same pc twice with marching est). Windows contract: Rip=[rsp] RA pop,
  rsp+=8, machframe exempt (ReactOS L557-562).
- Resume convention change: mw_RtlUnwindEx resumes at tgt_frame DIRECTLY (ms_est
  = frame base); old entry-alloc cancellation removed (main + continuation path).
  UPX G1 regression caught mid-round by 3x discipline (0x21474343 intercept still
  subtracted alloc) -> fixed -> G1 green.
- Fastfail honesty: IsProcessorFeaturePresent(23)=1; crash_handler detects guest
  `cd 29` -> [FASTFAIL] trace + exit 9 (UCRT takes the real fastfail path).
- RESULT: winmergeu exit 9 -> 133: GS check PASSES; guest C++ EH chain runs
  end-to-end (10 frames, guest personalities decide); terminate->int3.
  New frontiers: SW-1201 (winmergeu catch-type decode), SW-1202 (zstd explicit
  output-arg ignored — pre-existing, verified vs old binary).
- MEASURED sha ddfa14a0: G1 PIN 12/12 (evidence/master/g1/20261020_130641);
  corpus x3 16/16 deterministic, ZERO unresolved, ZERO regressions; 7za/zstd/lz4
  round trips + jq re-verified. Loader 2,698,920 bytes. evidence/sweep030/.

---
SWEEP-038 (2026-10-21) «ادامه از جایی که ناقص مونده و پوش» — RECONCILE train 2: local interrupted session merged into remote chain
- Context: local session (base 242af84) independently re-landed SW-1106 + SW-1201
  while a parallel remote chain (SWEEP-031..037) had already landed the SAME
  wagons with deeper decodes. Push rejected (non-FF). Precedent: SWEEP-037
  reconcile protocol.
- CONVERGENCE (two independent sessions, same root causes — strong evidence):
  (1) SW-1106 = 64MB file-size cap rejecting zoomit64 68MB BEFORE parse (not a
  decision-API failure); (2) SW-1201 root cause = SetWindowsHookExW(5) NULL ->
  guest C++ throw -> EH chain failure (local decoded the unwind side: resume at
  TargetIp=RA-1 mid-instruction; remote decoded the resume fix: -1 offset is
  GCC-ONLY, MSVC gets TRUE RA + FH4 catch-and-resume at 0x140298df5);
  (3) procexp64 exit-139 wild jump RIP=0x6f00690074006e = UTF-16 bytes — local
  read it as a string-blob IAT jump; remote SWEEP-033 decoded the full class:
  guest copies CLR perf-counter names ('# bytes in all heaps', 'contentions')
  through stack frames, one overruns, RET consumes 'ntio'. Both decodes agree
  the bytes are 'ntio' from 'contentions'.
- RECONCILE actions: main reset to origin/main 92cb959 (SWEEP-037); local
  c455913 preserved as backup/sweep032-local-50qatar (contains: alternative EH
  variant — ReactOS L709-734 target-frame EH_TARGET_UNWIND final call inside
  seh_walk_intermediate_cleanups + walk-for-all-0xe06d7363 + MW_MAX_IMAGE_MB
  env-overridable cap; NOT ported: remote's True-RA fix is measured end-to-end
  (986K pump iterations), porting the second mechanism risks regression);
  UNIQUE delta re-landed: worklist SWEEP-030 summary stale throw-point
  0x14032126f corrected (EH-machinery C++-magic check, not throw site).
- MEASURED: deterministic rebuild reproduced sha 2464ad23 BYTE-EXACT (0 errors);
  G1 PIN 12/12 (evidence/master/g1/20261021_095409).

---
SWEEP-039 (2026-10-21) «ادامه ۵۰ قطار و واگن» — SW-1102 train: SystemHandleInformation + ETW symbols + static decode
- ENV-RESET RECONCILE first: workspace rolled back to 242af84 (local objects for
  SWEEP-031..038 lost) while origin/main carried 1490f96 — reset to origin/main,
  deterministic rebuild reproduced sha 2464ad23 BYTE-EXACT, G1 green; batch then
  built on 1490f96.
- SW-1102a SystemHandleInformation (class 64) + SystemExtendedHandleInformation
  (76): real enumeration of the loader's live handle table (g_handle_table with
  payload-kind verification: g_mw_events/g_mw_sems used-flag; generic bucket for
  thread/other payloads). Headers 4B/8B, entries 16B legacy (PID 1234 = guest
  GetCurrentProcessId, objtype 5 event / 6 sem / 8 generic, Object truncated) /
  32B EX (full Object ptr). Two-pass contract: short buffer -> 0xC0000004 +
  *retlen = needed — the size-retry loop procexp's NtQuerySystemInformation
  wrapper implements. Old INVALID_INFO_CLASS answer seen once in run-1 trace
  before the SCM/driver section.
- SW-1102b ETW symbols: the window right before the fatal name-copy does
  LoadLibraryA(Advapi32.dll) + GetProcAddress(EnableTraceEx) -> NULL (missing
  from dispatch) — a not-on-Windows signal. Landed EnableTraceEx/Ex2/
  EnableTrace/RegisterTraceGuidsW (family-typed 1168 + registration 0/synthetic
  handle), dual-case entries. GetProcAddress now resolves. DUPLICATE guard:
  StartTraceW/ControlTraceW/ProcessTrace/OpenTraceW/CloseTrace/FlushTraceW
  already existed (typed 1168) — kept as-is.
- SW-1102 STATIC DECODE (new evidence): embedded names at .rdata
  0x14015ad10/40/70; rip-rel xrefs cluster in 0x1400a04xx (name search via call
  0x1401025d8 over table r15={count, array}) and 0x1400a06xx-a08xx; crash-frame
  stack holds the names packed sequentially ("...ns\0# bytes in all heaps\0\0#
  Gen 0 collections\0" + 'Total # of contentions' in R12-R15) — copy overrun
  clobbers frame RA with 'ntio' of 'contentions'. procexp64 still exit 139 x3
  (deterministic) — final offending copy loop open, instrumentation next.
- MEASURED sha 256d1e92: G1 PIN 12/12 (evidence/master/g1/20261021_123620);
  census x3 15/15 DETERMINISTIC (sweep039): 11x exit0 unchanged, pageant 124 +
  winmergeu 124 daemons unchanged, procexp64 139 frontier preserved,
  autoruns64/searchmyfiles 1 K-U each (SWEEP-037 state), zoomit64 0. ZERO
  regressions. Census-script bug (missing run dirs -> bogus exit-1) caught and
  fixed before any conclusion was drawn — rerun clean.

## SWEEP-040 «قطار ۵۰ تایی» (2026-10-21) — SW-1206 + SW-1205 + SW-1102c
Start: origin/main 8c3ede7 (SWEEP-039+addendum). Three wagons landed,
two new frontiers registered (SW-1207/SW-1208), corpus 15->16.
- SW-1206: adexplorer64 corpus recovered (AdExplorer.zip re-fetched;
  e4512878...); crash chain fully decoded (MAIN dialog template,
  id=1002/1070 children, NULL GetDlgItem -> r14 cache-append crash);
  engine now parses the guest's OWN DLGTEMPLATE/DLGTEMPLATEEX resources
  (EX parser bounds-checked; STD delegates to SW-921) on all four
  Create/DialogBoxParam entries with distinct per-dialog identities;
  adexplorer64 exit 139 -> 32 full lifecycle. rammap64 regression
  (0->139) decoded and converted into a forward move: per-page dialogs
  now real, comctl32 tooltips_class32 frontier = SW-1207.
- SW-1205: per-thread win-ids + real thread-message queues +
  PostThreadMessageW/A honest contract + worker-park drain; winmergeu
  worker woke via guest's own PostThreadMessage(WM_QUIT,5679) and the
  main thread left the 0x140220068 poll loop for the first time since
  SWEEP-031 (124 -> heap-trap exit 4 on GlobalFree(0xae0001));
  w3_aligned_base_of probe-hardened (loader must never die on a guest
  pointer). 0xae0001 provenance = SW-1208.
- SW-1102c: crash_handler BUG-078 auto-decode (RIP-as-UTF16 "ntio" +
  packed-UTF-16 stack-run scan; 32-byte record stride with 8-byte gaps,
  RBP=0). Copy-loop static hunt = SW-1102d.
- MEASURED sha a0d9e9c8: G1 12/12 (evidence/master/g1/20261021_133926),
  census x3 16/16 deterministic, 7za/jq/sqlite3 round trips green on the
  final binary. Evidence: artifacts/sweep040/ (census.tsv, adexplorer64
  run1-3, roundtrip/). No force-push.

## SWEEP-041 «ادامه ۲۰ قطار» — 2026-10-21 (sha 4ecab4c2, G1 12/12, pushed)
Two open frontiers from SWEEP-040 cleared in one round, both to FULL LIFECYCLE:
- SW-1207 (rammap64 139 -> 0): tooltips_class32 was landing on MW_GUI_HWND; the
  guest's GetPropW(tooltip, atom 0xC000) NULL-fed its comctl32 subclass branch
  into a null-page absolute read. Engine now mints a distinct tooltip identity
  and auto-performs the CC32SubclassInfo prop contract (registered zeroed
  0x140 block at atom 0xC000) before CreateWindowEx returns — the same
  synchronous contract real comctl32 performs in WM_NCCREATE.
- SW-1208 (winmergeu 4 -> 0): GlobalHandle cookies were minted and forgotten;
  the guest's GlobalFree(0xae0001) hit the HEAP-TRAP. Engine now keeps a
  64-slot cookie->base registry (GlobalLock/Flags/Free/ReAlloc all resolve),
  and untracked frees degrade to the documented Win32 contract
  (return value + ERROR_INVALID_HANDLE) instead of terminating.
- Census x3 16/16 deterministic; all deltas forward; pageant/procexp64/
  adexplorer64 preserved. adexplorer64 corpus re-recovered (3rd loss) with
  sha-verified one-shot procedure now documented.
- Round trips: 7za byte-exact, jq-1.7.1 + query, sqlite3 in-memory 42.
- SW-1102d advanced (round 3): RA candidates classified; three .text functions
  identified (0x140066130 / 0x1400a1380 / 0x1400c3340 = CLR perf-counter master
  init with count*0x30/0x18/0x8 record arrays); crash-stack stride is 0x28
  (32B name + 8B) — writer still open.
- Lesson kept: the census caught nothing this time because the two fixes are
  pure forward motion; the 7za/jq/sqlite trio plus G1 regression 12/12 remain
  the regression net for the engine-wide surfaces (heap, windows, props).

## SWEEP-042 «۱۰ قطار، چند ده واگن ریزه» — 10 trains, ~42 micro-wagons (2026-10-22, HEAD 9e30c16d)
Directive: «۱۰ قطار چند ده واگن ریزه» — 10 trains carrying several dozen SMALL
wagons. Base: origin/main a06f45b (SWEEP-041), tree clean. The micro-wagon
veins came from mining the post-SWEEP-041 surfaces: adexplorer64's 23
unresolved imports (9 named + 14 ordinals — the ordinals were invisible for
rounds because the SWEEP-001 unresolved path counted but never printed them),
winmergeu's full-lifecycle trace (SWEEP-041's own open note), putty's
FormatMessage stale last error, pageant's CryptProtectMemory probe, and the
adexplorer64 ExitProcess(48925984) anomaly.

KEY DECODE — the 23/23 adexplorer64 import surface (SW-1209): wine spec
re-probe with the SWEEP-037 stale notes corrected. activeds.spec (wine DOES
ship it) carries EXPLICIT ordinals: 7/9/12/13/15/20 = ADsBuildVarArrayStr/
ADsOpenObject/ADsSetLastError/ADsGetLastError/FreeADsMem/ADsEncodeBinaryData;
wldap32.spec explicit 13/14/73/88/118/145/155/188 = ldap_unbind/
ldap_set_optionW/ldap_bind_sW/ldap_connect/ldap_err2stringW/ldap_initW/
ldap_modify_ext_sW/ldap_rename_ext_sW; shell32.spec 88 = SHDoDragDrop (the
SWEEP-037 note claiming the spec lacks 88 was simply wrong — lesson: re-probe
beats remembered absence). Spec sha256s recorded; all implementations typed-
honest per the established contract families (no HEAP-TRAP, no returning-0
lies where a real ABI shape exists).

KEY FIX — putty's real-Windows parity (SW-1211): FormatMessage miss used to
leave the STALE last error; putty's fallback string rendered "returned 2" (a
leaked ERROR_FILE_NOT_FOUND). Real Windows sets 317 ERROR_MR_MID_NOT_FOUND on
a failed format; after the fix putty renders "(unable to format: FormatMessage
returned 317)" — the same string a real Windows putty prints for its private
error -2. Plus a 12-entry canonical FROM_SYSTEM table for common Win32 errors.

KEY DECODE — adexplorer64 exit 32 (SW-1212): new ExitProcess instrumentation
logs the caller RA when code > 0xFFFF (exit-0 traces byte-identical). Two runs
gave codes 0x2EA8D20 / 0x2565D20 — varying HEAP POINTERS with stable low byte
0x20; census exit 32 = code & 0xFF. Caller RA 0x1400403f5 -> guest RVA 0x403f5
(the guest's entry path returns an uninitialized/handle variable). Engine
behavior is honest passthrough; the anomaly is guest-side and now PROVEN, not
guessed.

MEASURED sha 9e30c16d G1 PIN 12/12 (evidence/master/g1/20261021_164933):
census x3 16/16 DETERMINISTIC, deltas all forward (adexplorer64 23->0
unresolved, searchmyfiles 1->0, autoruns64's remaining 1 now NAMED in stderr
via the new ordinal diagnosability); exits preserved (adexplorer64 32, pageant
124 daemon, procexp64 139, 12x exit-0). Round trips on the final binary: 7za
x64 create+extract BYTE-EXACT (cmp), jq-1.7.1 version + query = 2, sqlite3
in-memory 42 exit 0 (paths: samples/real/7za.exe — NOTE 7z_x64/7za.exe is a
32-bit trap, samples/real/jq-windows-amd64.exe, artifacts/opensource/sqlite/
sqlite3.exe). ucrt Ex-family probes that returned NULL for rounds now RESOLVE
in winmergeu's trace (GetTempPath2W/FlsGetValue2/EnumSystemLocalesEx/
GetDateFormatEx/GetTimeFormatEx/GetUserDefaultLocaleName/IsValidLocaleName/
LocaleNameToLCID); winmergeu SPI_GETNONCLIENTMETRICS -> TRUE (2x). pageant
calls CryptProtectMemory(cb=16) 2x and keeps its (correct) daemon park.
Evidence: evidence/sweep042/{wine_specs,census.tsv,roundtrips,binary_sha256}.
Open: SW-1102d copy-loop store instruction, ole32.277/16 + COMCTL32.344
(Rule-1), SW-791 libcurl-x64.dll, procexp64 139 frontier.

## SWEEP-043 «ادامه قطار» — BUG-078 solved, procexp64 139 -> 0 (2026-10-22)
- Reconcile first (origin had advanced to SWEEP-042 339cc60 incl. the
  «۱۰ قطار چند ده واگن ریزه» batch; workspace reset mid-round): reset --hard +
  scripts/sweep043_apply.py re-lands 11 patches, assert-verified.
- SW-1102e/g: the 'ntio' writer = mw_IsBadStringPtrW's own
  mw_probe_copy(s+off, chunk*2, tmp[256]) overflow (1024B into 512B), proved
  by the MW_BUG078_DIAG single-fire stack dump + static decode of the
  0x1400a8a30/0x1400a1380/0x1400c3340 chain (RegQueryValueExW two-pass,
  tokenizer L",", formatter "%u "/"%s ", CLR name pool .rdata 0x14015ad10+).
- SW-1102h HKPD PERF_DATA_BLOCK (two-pass, empty block, NumObjectTypes 0);
  SW-1102j WinStationGetProcessSid rc!=0 failure shape; SW-1102k
  I_QueryTagInformation honest 1168 + optional-stub fallback trace;
  SW-1102l NtQuerySystemInformation class 5 (SystemProcessInformation,
  guest PID 1234 entry, two-pass).
- SW-1102f: RegQueryValueExW hit+miss MW_TRACE (probe surface now visible:
  98 Settings probes, HKPD chain).
- adexplorer64 corpus re-recovered (4th), shas recorded.
- MEASURED sha 89ce3cd9 G1 12/12; census x3 16/16 DETERMINISTIC;
  procexp64 139 -> 0 (graceful ExitProcess(0)); 13x exit-0; round trips
  7za byte-exact / jq '.a[1]'=2 / sqlite3 42. evidence/sweep043/. No
  force-push.
- Open frontiers: ole32.277/16 + COMCTL32.344 (Rule-1), SW-791 curl
  libcurl-x64.dll, pageant 124 park (correct), adexplorer64 exit-32
  (guest-side, proven).

---

## SWEEP-044 «ادامه قطار» — 15/16 exit-0, 0 unresolved, Go+Rust frontiers breached (2026-10-22)

Directive: continue the train (micro-wagon format). Start state: HEAD 7d0f8e5
(SWEEP-043), binary 89ce3cd9, corpus 14x exit-0 + adexplorer64 32 + pageant 124,
1 unresolved import (COMCTL32.344).

Work Log:
- Re-verified baseline at HEAD (single-pass census) before any change.
- SW-1215: adexplorer64 exit-32 root cause = main-thread headless WM_QUIT path
  returned 0 WITHOUT filling the guest MSG struct; guest `return (int)
  msg.wParam` read a stale heap pointer (0x3ad3d20) -> ExitProcess(garbage).
  Fixed: mw_gui_fill_msg(hwnd=NULL, WM_QUIT, g_quit_code, 0). 32 -> 0.
- SW-1216: MsgWaitForMultipleObjects traced (pageant loop was invisible);
  full-timeout honored; INFINITE = 20ms slices.
- SW-1217: ConnectNamedPipe real state machine (name registry + client attach
  via CreateFile pipe-path hook in w3_create_file + 535/995 error shapes).
  Phantom-client storm (256 calls, dozens of threads) -> ONE park.
- SW-1218: COMCTL32.344 = TaskDialog (Geoff Chappell 6.10 Named Exports, two
  independent loader-error reports) -> corpus unresolved 1 -> 0.
- SW-1220: timer-aware pump (BUG-079): gate-bypass no longer fires when a
  timer is armed; pump waits for the tick and delivers WM_TIMER first.
  snake 30f / breakout 381f 40->13 bricks / calc 9/9 — ALL golden numbers
  restored byte-identical 3x3. Falsification proof: breakout was already
  broken at clean HEAD (BUG-080, pre-existing).
- SW-1221..1227 Go chain (bombardier Go 1.20.3): LoadLibraryExA delegate;
  per-thread TEBs with real stack bounds; honest VirtualQuery (stack VMA /
  image / free); 8 KiB TEBs + TEB-routed Tls family (x64 TlsSlots 0x1480);
  TEB+0x68 lasterror (Go asmstdcall reads it directly); ConnectEx plane with
  captured WSAID_CONNECTEX bytes + IOCP associations + WSAGetOverlappedResult;
  ntdll NtWaitForSingleObject/RtlGetNtVersionNumbers/Power*.
  Result: usage path exit 2 clean; benchmark loop runs ("Bombarding ... Done!
  Statistics") exit 0; data path crash left OPEN as SW-1228.
- SW-1229: Rust fail-fast layer 1 (CreateNamedPipeW, ReadFileEx/WriteFileEx
  sync v1, ProcThreadAttributeList family). fd.exe advanced to layer 2
  (12 APIs) = SW-1230 open.
- Measurement: census x3 deterministic (sweep044_census.sh), 15x exit-0 +
  pageant 124; ZERO unresolved; G1 PIN sha 71d19afd 12/12
  (evidence/master/g1/20261021_192040); round trips 7za byte-exact / jq 2 /
  sqlite 42; snake+breakout+calc acceptances all green.

Stage Summary:
- Corpus: 15/16 exit-0 (only pageant 124 = honest daemon park remains).
- Imports: 0 unresolved corpus-wide (first time in project history).
- Next frontiers: SW-1228 (Go data plane), SW-1230 (Rust layer 2), SW-1219
  (famous game download, blocked by SW-509), SW-509 (PE-DLL loader).

## SWEEP-045 «۵ قطار بزرگ همزمان» — 5 big trains in parallel (2026-10-22, HEAD 08350c3 → b33ec3f7)
Directive: «۵ قطار بزرگ همزمان ادامه ریل ۵ تای» — five BIG trains on one rail.
Base: origin/main 08350c3 (SWEEP-044). Env-reset reconcile first (workspace
rolled back to 242af84 for the 5th time; adexplorer64 corpus re-recovered for
the 5th time, sha-verified one-shot). Baseline rebuilt byte-exact: 71d19afd.

Train 2 (SW-1230) — the round's headline: fd.exe (Rust 1.73 MSVC, fd 9.0.0,
3.4 MB) went from fail-fast layer 2 to FULL LIFECYCLE:
- 16 imports landed (12 named by SWEEP-044 + TzSpecificLocalTimeToSystemTime,
  terminate, _initialize_onexit_table, _register_onexit_function, _set_fmode).
- The boot blocker was MY OWN first-draft bug pattern: SetThreadStackGuarantee
  with an invented "<64KiB invalid" gate — Rust std requests 0x5000 (20 KiB)
  in stack_overflow.rs Handler::new and panics the process when it fails.
  Real contract: *in = request, *out = previous. (Web source quoted; the
  gate was deleted before landing.)
- Three deep engine bugs surfaced one layer at a time, each caught by the
  guest's own error output: (1) GetFileInformationByHandle hardcoded
  ARCHIVE attributes — directories never reported DIRECTORY, so Rust
  fs::metadata().is_dir() was false (fd: "Could not retrieve current
  directory"); (2) nFileSizeHigh/nFileSizeLow REVERSED in
  BY_HANDLE_FILE_INFORMATION — an 82-byte file read as 352 GB and Rust's
  read_to_end preallocation died ("memory allocation of 352187318272 bytes
  failed" = 0x5200000000, the .gitconfig byte count in the high dword —
  the diagnostic that cracked it was a HeapAlloc[GIANT] guest-RA trace);
  (3) spawned guest threads had NO implicit TLS (g_thread_tls[ti][0] NULL)
  — real Windows copies the module static TLS block per thread; the first
  draft copied the LIVE main block and inherited main's thread_info pointer
  (Rust thread_info assertion) → pristine image snapshot is correct.
- fd acceptance: --version → "fd 9.0.0" exit 0; no-args walk exit 0 with a
  real sorted listing; 'fd txt' filter exit 0; --help exit 0.
- Honest revert kept in history: the naked _CxxThrowException caller-RA
  capture (which let fd's Rust panics really unwind to the Rust personality
  at Frame[2]) was unwired the same round — the import table is name-only,
  so mingw guests were captured too and 7za's swallowed-throw contract
  broke. v2 = DLL-aware EH routing (registered as the SW-1230 note).

Train 3 (SW-509 v1) — real PE-DLL loader, first slice:
- ssh.exe + libcrypto.dll staged; the EXE import loop disk-loads unknown
  descriptors; load_pe_dll: map → relocations (DIR64) → imports (513/550
  against the engine table, honest stubs for the rest) → export directory →
  DllMain(ATTACH) → the DLL's CRT boots deep into UCRT startup
  (GetCommandLineA "ssh.exe -V" printed FROM the DLL's init).
- GetProcAddress: DLL-image handles resolve first (names + ordinals).
- Two guards learned the hard way: section table = optional-header END
  (oh+SizeOfOptionalHeader — oh+112 is the data-directory start; the first
  draft OOB-memcpy'd); *.exe/*.sys descriptors are NOT disk-loaded (procmon64
  imports its OWN Procmon64.exe — self-load re-ran its CRT and 0→139'd the
  census; the guard restored 15x exit-0 immediately).
- V2 frontier (SW-1233): DLL-CRT-in-DllMain — ucrt data imports of the DLL,
  DLL TLS, __scrt conventions (Signal 11 at libcrypto RVA 0x10312c).

Train 1 (SW-1228) — decode round: the bombardier data-plane crash is in
Go 1.20 initLongPathSupport's 10.0-build≥15063 branch; the chain
0x434460→0x434380 (mOS cluster m+0x278..0x2b8) consumes global 0xd477e0
(BSS-NULL) and the result is used as a GC-bitmap base (+3, OR 0x80).
Next: pclntab symbolization, then whatever osinit step must initialize it.

Train 5 — bookkeeping: fd.exe staged as the Rust-MSVC representative;
SW-1232 registered (7za extract 139 — falsification-proven PRE-EXISTING at
clean 08350c3, create still byte-exact); curl.se's mingw build is fully
static so SW-791 needs the DLL-linked MSVC build of curl.

Measurement: census x3 16/16 DETERMINISTIC (15x exit-0, pageant 124 park,
ZERO unresolved corpus-wide, ZERO regressions); G1 PIN 12/12 twice
(20261021_205933 sha 9910f09c pre-revert, final 20261021_210456
sha b33ec3f7); round trips jq→2 sqlite3→42 7za-create byte-exact;
games snake 30f / breakout 381f+13bricks / calc 9/9 — all green.

Lesson kept: each fd.exe layer peeled exactly one engine lie (invented
API-contract gate → hardcoded struct field → reversed field order →
missing per-thread TLS init), and the guest's own error strings were the
oracle at every step. The census caught the two real regressions I
introduced (procmon64 self-load, 7za CxxThrow capture) and the falsification
run on clean HEAD correctly exonerated the engine for 7za-extract.

## SWEEP-046 «۲۰ قطار» — 20 trains (2026-10-22, binary 848fa108, pushed)
Directive: «۲۰ قطار ادامه» — 20 trains. Base: origin/main 3ff438c (SWEEP-045);
fast-forward (local had 0 unique commits), adexplorer64 corpus re-recovered for
the 6th time (AdExplorer.zip sha 56536bff byte-exact, exe e4512878 pinned),
baseline rebuilt byte-exact b33ec3f7, census x3 green.

Train 1 — SW-1233 DLL-CRT-in-DllMain (ssh.exe + libcrypto.dll 1.9 MB):
Five engine root causes, each decoded off the guest's own behavior:
1. TEB/GS ordering: setup_teb_peb() ran AFTER load_pe(), but the EXE import
   loop INSIDE load_pe() disk-loads DLLs and runs their DllMain — the first
   guest gs:[0x10] probe (libcrypto _chkstk RVA 0x10312c) faulted with GS=0.
   Real Windows builds the TEB before any import resolves. FIX: TEB/PEB
   before load_pe + PEB.ImageBaseAddress refresh after.
2. mw_dll_export: export-directory arrays read TWO slots off
   (ed[9]/ed[10]/ed[11]; also exp_base=ed[8], nfuncs/nnames off-by-one).
   Correct IMAGE_EXPORT_DIRECTORY as uint32 words: ed[4]=Base ed[5]=nfuncs
   ed[6]=nnames ed[7]=AddressOfFunctions ed[8]=AddressOfNames
   ed[9]=AddressOfNameOrdinals (byte-verified vs libcrypto: aof=0x194f78
   aon=0x198978 aoo=0x19c378, base=1, 3712/3712). The old crash: libc
   __strcmp_evex on a wild pointer (RDI=b+0x1F001E). Never exercised before
   because DllMain crashed first (the "513/550" was the DLL's OWN imports).
3. GetModuleFileNameW/A returned Unix paths — UCRT _wpgmptr init does
   wcschr(path,'\\') + byte-terminate (ssh.exe RVA 0x411bb): NULL-store crash.
   FIX: win32_module_path() — C:\-prefixed backslash form (./ stripped).
4. Windows BASE environment absent — Win32-OpenSSH w32fd.c init_prog_paths
   (CRT static initializer, BEFORE main) fail-fasts "couldn't find ProgramData
   environment variable" (w32fd.c:217) -> ExitProcess(255) pre-main, zero
   stdio calls. Decoded via the ExitProcess exit-scan (RVA chain ->
   _invalid_parameter-style helper with file/line/expr strings). FIX:
   g_win_base_env (ProgramData/SystemRoot/windir/PATH/ComSpec/PUBLIC/
   PROCESSOR_* from host /proc/cpuinfo, NUMBER_OF_PROCESSORS from sysconf)
   merged ahead of host environ; GetEnvironmentVariableA/W + the W block
   share mw_env_lookup_a.
5. w32posix init chain: CreateWaitableTimerA/W (ssh imports the A-form),
   CancelWaitableTimer, QueueUserAPC (w32posix checks the BOOL; parked-v1,
   honest) landed. "failed to initialize w32posix wrapper" no longer blocks
   the boot path.
RESULT: ssh.exe -V exit 0 x3 DETERMINISTIC (was instant pre-main fail-fast).
Frontier (documented): w32posix fd-monitor thread select emulation + the
OpenSSH version banner still pending; LoadLibraryW(L"a") probe = handled
harmlessly by the engine exactly like real Windows (NULL + lasterror).

Train 2 — SW-509 v2 + SW-1230v2 DLL-aware EH routing:
- mw_dll_image_for_pc / mw_img_base_for_pc / seh_lookup_rf_for_pc: the
  dispatcher walks frames inside LOADED DLL images (MwDllImage now records
  .pdata rva/count from dir[3] at load). seh_internal_virtual_unwind +
  seh_total_prologue_alloc are image-base threaded (vw_img_base from the
  control PC).
- mw_cxx_throw_dll: naked _CxxThrowException twin (same slot geometry as the
  proven mw_RaiseException asm, BUG-031 volatile capture) GATED to throws
  whose caller lives in a DLL image — EXE throws keep the v1 swallow
  (mingw/7za contract intact; fd.exe full lifecycle intact). 7z.dll throw
  trace: Frame[0] RIP in the DLL, RF found in the DLL's own pdata.
- 7z.exe (full 7-Zip 25.01 + 7z.dll): usage banner exit 0 — the PE-DLL
  loader's motivating case works end-to-end through the DLL's DllMain CRT.
- Frontier: `7z i`/`7z a` — the throw chain reaches the guest's own
  (statically-linked) FH4 in the main image, which returns
  ExceptionContinueExecution at Frame[8] -> honest int3. Needs the
  FH4-funcinfo depth SW-1201 gave winmergeu. Registered as the SW-1230v2 tail.

Train 3 — SW-1232 SOLVED (see worklist; the one-line CreateSemaphoreW fix
restored byte-exact create+extract of a 1.3 MB payload).

Bookkeeping trains: adexplorer64 corpus recovery #6 (sha-pinned); census x3;
G1 pin x2; round trips; games battery; 4 trivial -Waddress/-Wcomment fixes
(SW-1093: 97 -Wformat sites left, recorded as the dedicated ticket);
temporary round diagnostics removed before landing.

MEASUREMENT (final binary 848fa108da262351429caca7d5cf7244c65e75b1a9e02a9409d89ab6aa15a045):
- census x3 16/16 DETERMINISTIC: 15x exit-0, pageant 124 park, ZERO
  unresolved corpus-wide, ZERO regressions (evidence/sweep046/census.tsv)
- G1 PIN 12/12 (evidence/master/g1/20261022_032424)
- round trips: jq '.a[1]'->2, sqlite3 6*7->42, zstd OK, lz4 OK,
  7za create+extract byte-exact x3 (evidence/sweep046/7za_roundtrip.txt)
- games: snake 30f byte-identical 3x3, breakout 381f + 13 bricks, calc 9/9
- no force-push; fast-forward base, single-round increment

## SWEEP-047 «ادامه قطار» — 20 trains (2026-10-22, binary 3dbc56d5, pushed)
Directive: «ادامه قطار» — standing 20-train mandate. Base: origin/main a418e5c
(SWEEP-046); fast-forward, baseline rebuilt byte-exact 848fa108, adexplorer64
corpus recovery #7 (sha-pinned). 16 NEW x64 guests staged (Sysinternals set +
curl 8.23.0 mingw with libcurl-x64.dll) — each guest = a train.

Train 1 — SW-791 curl --version exit 0: three root causes decoded off the
guest's own behavior (UCRT 3-import tail; SSPI InitSecurityInterfaceA NULL ->
real 28-slot wine-layout SecurityFunctionTableA with NTLM typed model; and
the BUG-035 TWIN: glibc bsearch invoked curl's ms_abi comparator SysV-style
— crash mov (%rcx),%rcx with RCX=0x61 from a qsort-comparator for a
char** table — fixed with the qsort_r bridge pattern). Full real version
banner: "curl 8.23.0 ... LibreSSL/4.3.3 ... HTTP3". Tail: HTTP thrdpool
resolver starvation (socket plane real, WSAStartup ran, getaddrinfo wired).

Train 2 — SW-1234 drive-path mapping: the (null) v(null) shared banner class
root-caused to the "X:\..." mapping landing at host-root; now maps to the
loader process cwd on THREE surfaces (normalize/CreateFile plane, CRT
fopen/_wfopen plane, w_ver_read_from_file). Zero corpus regressions.

Train 3 — SW-1235 version plane: VerQueryValueA now transcodes UTF-16 ->
ANSI (guest %s read 1 char), binary pass-through fixed (Translation),
16-slot pointer ring (all-queries-aliased-one-buffer bug caught because the
guest saves five pointers then prints). Real "Whois v1.21" banner.

Train 4 — SW-1236 gate batch: GetComputerNameExA/W, LogonUserA/W (1326),
FindFirstFileExA, FillConsoleOutputCharacterA, WNetAdd/CancelConnection2A/W.

Train 5 — SW-1237 real DNS: gethostbyname delegates to host getaddrinfo,
winsock hostent built; whois64 resolved 3 real hosts (2+1+2 addrs). Tail:
post-DNS socket stall.

Trains 6-15 — corpus: listdlls64/pipelist64/psloggedon64 = NEW full
lifecycles (exit 0 x3, zero staging); psinfo64 real EULA text; trurl real
error path; usage/EULA paths real for du/strings/sdelete/psservice;
frontiers registered SW-1238 (accesschk64 16-import ACL/LSA surface),
SW-1239 (pslist64 PDH perf-counter), SW-1240 (logonsessions64 LSA),
SW-1241 (autologon64 Winlogon), SW-1242 (handle64 139).

MEASUREMENT (final 3dbc56d53bcf...): census x3 31 programs DETERMINISTIC
(15x exit-0 + pageant 124 corpus, ZERO unresolved, ZERO regressions);
G1 PIN 12/12 (20261022_050538); round trips jq 2 / sqlite 42 / zstd / lz4 /
7za create+extract BYTE-EXACT; games snake 30f + breakout 381f/13bricks +
calc 15/15; ssh -V falsified-unchanged vs HEAD (same message + exit 0);
fd 9.0.0 intact. Lesson kept: the census caught nothing this round either
— every fix is forward motion; the regression net (7za/jq/sqlite/G1/games)
covered the engine-wide path-mapping change (SW-1234 was the riskiest).

## SWEEP-048 «چهار قطار گرافیک» (2026-10-22) — four GUI trains

Directive: test four GUI programs — ODF viewer, Windows calculator, a
Windows-only program (free choice), an XP game. Start state: origin/main =
bf0b9c9 (SWEEP-047), env had rolled back to 242af84 again (4th time);
reconciled via reset --hard, baseline binary sha 3dbc56d5 reproduced.

Payload sourcing: XP Pro x64 SP2 OEM ISO (crmpxoem-en) + XP SP3 x86 ISO
(x14-80428) from archive.org via a 6-stream ranged downloader
(scripts/sweep048_iso_dl.py, 617MB in 59s); pycdlib for i386/AMD64 listing;
.EX_ expansion done BY THE ENGINE's own 7za (9/10; CARDS.DL_ = SW-1243).
Everything x64 from voidtools; LibreOffice 26.8.1 x64 MSI from
documentfoundation (375MB), CAB carved with olefile, expanded via the engine
(19,427 files) after SW-1244.

Engine work (all in src/loader/loader.c):
1. SW-1244 handle table 64→8192 + CloseHandle w3-slot recycling — the 7za
   63-file wall broken (811 errors → 0; 874/874). MINWIN_HEAP_TRAP=warn diag.
2. SW-1245 case-insensitive disk DLL search (mw_dir_casefind) — MFC42U.DLL
   now found despite case mismatch.
3. SW-1246 crash_handler out-of-PE walker bounds — handler no longer dies on
   garbage RBP frames (mspaint's libc crash exposed it).
4. SW-1247 MFC42U x64 as real PE-DLL for mspaint + the whole msvcrt gap
   (_ltow..fputws) + gdi32/user32/kernel32 tail + __argc/__wargv data plane.
   mspaint: 633 unresolved → 0; crash 139 → abort-at-AfxInit 134.
5. SW-1250 load_pe_dll recursive dep disk-load + msvcp140/vcruntime140 de-
   pseudo'd — LibreOffice import forest 2,862 → 0 unresolved (mergedlo 50,142
   export names = largest DLL ever loaded).
6. SW-1251 UCRT tail (31 entries: stdio/locale/math/runtime/string).

Four-train scoreboard (census x3 deterministic, artifacts/sweep048/census/):
- calc.exe (XP x64 5.2.3790): exit 0; dialog cdit=30 resolved; scripted clicks
  delivered as WM_COMMAND into the guest's own wndproc.
- winmine.exe (XP x64): exit 0, ZERO stub calls, 3 MessageBox dialogs rendered
  (messagebox_1..3.bmp) + GDI frame (frame_001.bmp) — evidence/sweep048/.
- everything.exe x64: message-loop park 124 (honest GUI app behavior).
- soffice.bin x64: SW-1252 frontier (msvcp140 DllMain zeroes 8 bytes of own
  .text at RVA 0x1110; .text verified image==file pre-DllMain).
- mspaint/mshearts: SW-1247-open (MFC AfxInit "corrupted top size" with 100%
  import resolution).
- spider/freecell/notepad/pinball: exit 0 (pinball = DirectDraw-era typed
  stub plane, 200 stub calls, no crash).

Gates: G1 PIN 12/12 (sha dce5784c), corpus census x3 15/15 deterministic ZERO
regressions, batteries snake 30f byte-identical / calc 9/9 + 15/15 / breakout
381f-13bricks / BUG-054b 3/3 — ALL GREEN on the final binary. Evidence:
evidence/sweep048/ (README + provenance shas + screenshots + census). ASan
diagnostic build attempted (blocked by guest-base/shadow-gap collision +
RBP-asm — noted in SW-1249 notes).

## SWEEP-049 (2026-10-22) «ادامه با برنامه های واقعی» — five wagons, one root cause, three programs resurrected

User directive: «ادامه بده عالیه اینطوری بهتر با برنامه های واقعی» — continue
with REAL programs. Reconciled first: local was 20 behind origin/main (remote
chain SWEEP-031..048 incl. the four-GUI-train round); ff-pulled to 13969d7;
verified binary reproducibility dce5784c byte-exact vs SWEEP-048 provenance.

Payload re-staging (the gitignored stage dir was wiped between rounds):
XP Pro x64 SP2 OEM ISO re-downloaded via the 6-stream ranged downloader
(628,617,216B, sha 19b5a382... EXACT provenance match), pycdlib 2.x keyword
API adaptation, 23 payloads expanded BY THE ENGINE's own 7za (the round
doubles as a 7za-under-miniwin exercise; 7z.exe 25.01 dies with 124 stubs —
samples/real/7za.exe 26.04 is the honest corpus entry). LO 26.8.1 MSI +
Everything 1.4.1.1028 zip fetched for SWEEP-050 staging.

Wagon chain (each unlocked the next):
1. SW-1253 KUSER_SHARED_DATA page — winver 139→0 (first-block
   mov rax,[0x7ffe02c8] unmapped; RW-fill-then-seal, qpc 1e9 matches mw_QPF).
2. SW-1254 raw ordinals vs real disk-DLL exports — mspaint's 613 MFC42U
   ordinals never bound (SWEEP-048 "0 unresolved" was an early-death
   artifact: the process died in mfc42u DllMain BEFORE the MFC import
   descriptor was processed — single "Import DLL" line in the trace proves
   it). Static ordinal map stays as fallback for system DLLs.
3. SW-1255 GetDeviceCaps HORZSIZE/VERTSIZE — 0 → idiv-by-zero SIGFPE in
   mspaint RVA 0x35e8e; now real mm math (169/127 @ 640x480 96dpi).
4. SW-1256 window-tree honesty — the ancestral single-handle model
   (every top-level window = MW_GUI_HWND; GetTopWindow always non-NULL;
   GetWindow always NULL) made MFC's GetDescendantWindow self-loop
   (104,715 x GetDlgItem(0xE900) = exact 8MB/0x50 stack division). Nested
   window creation now mints distinct registered identities; GW_* relations
   are registry-backed and honest (empty = NULL).
5. SW-1257 end-placed heap guard → caught mw___dllonexit writing 1 byte
   past a FULL 256-byte onexit table = THE SW-1247 'corrupted top size'
   writer. Fixed with the real CRT contract (realloc-grow + append +
   reseat begin/end) through mw_realloc for guard-registry consistency.
   v1 guard's page slack had been ABSORBING this overflow (guard-mode
   mspaint exited 0 with the writer uncaught) — end-placement is the
   diagnostic that works.

Measurement (binary a29ca1e7): census x3 DETERMINISTIC over 29 guests —
DL corpus 14x exit-0 + pageant 124 (ZERO regressions vs SWEEP-048), new
guests mspaint 134→0, mshearts 139→0, winver 139→0, plus charmap/clipbrd/
write/sol/taskmgr/regedit/sndvol32 exit-0 (small unresolved tails remain,
honest), net rc=2 / ping rc=1 (honest usage errors), wordpad 139 (SW-1258),
drwtsn32 139 (SW-1259); G1 PIN 12/12 (evidence/master/g1/20261022_144016);
calc battery 15/15. evidence/sweep049/ (census.tsv + expand_log.tsv +
provenance_sha256.txt + mspaint trace + 2 rendered messageboxes). No
force-push. Open: SW-1258/1259 decode, tails, SW-1252 (LO staged),
SW-1243 (needs x86 SP3 ISO), Everything full lifecycle.

## SWEEP-050 (2026-10-22) «قطار با برنامه های واقعی» — five trains, two root causes, everything resurrected

User directive: «ادامه بده عالیه اینطوری بهتر با برنامه های واقعی» + «ادامه ادامه».
Reconciled first: local was 20 commits behind (origin advanced 242af84 -> 4aab7be,
SWEEP-031..049 incl. the four-GUI-train rounds); pure ff-merge, binary reproduced
byte-exact (a29ca1e7 = SWEEP-049 provenance). Payload re-staging: XP Pro x64 SP2
OEM ISO (628,617,216B, sha 19b5a382 EXACT), LibreOffice 26.8.1.1 x64 MSI
(374,870,016B, sha c6298b10 EXACT), Everything 1.4.1.1028 x64 zip (sha 4f4a9b92
EXACT) — all sha-pinned vs evidence/sweep049. 23 XP payloads extracted (pycdlib)
and expanded BY THE ENGINE's own 7za (21 rc=0).

Train 1 — Everything 1.4.1 (SWEEP-048 park-124 -> exit 0): 25-import tail honest
+ SHELL32.16 = ILFindLastID (wine shell32.spec 4.72). Live pump -> budget exit 0 x3.
Train 2 — wordpad (SW-1258 139 -> exit 0): three-layer decode — RICHED20 disk-DLL
16-name surface, SHLWAPI.231 ZoneCheckUrlExW stub-0 -> virtual-call NULL deref
(MINWIN_VTBL_DUMP diagnostic added), and the WM_CREATE=-1 contract (CreateWindowEx
W/A now return NULL on base AND nested paths; MFC InitInstance=FALSE -> exit(0)).
Train 3 — mshearts (139 -> exit 0): real hMenu contract — top-level
CreateWindowExW attaches the guest menu (GetMenu==NULL crashed EnableMenuItem at
RVA 0x9d5f). SWEEP-049's "mshearts exit-0" was measured under MW_GUI_EVENTS=0
(no WM_CREATE delivery at all); the events=1 census exposed the gap.
Train 4 — drwtsn32 (SW-1259 139 -> exit 0): ONE ROOT CAUSE = LoadResource returns
the DATA POINTER on real Windows (LockResource = documented no-op); the engine
returned the DATA_ENTRY VA so drwtsn32's RT_STRING walker read the entry header
as string lengths (SIGSEGV RVA 0x5cb3, fault 0x41f932 > image end). Fixed
LoadResource + LockResource identity (v2 heuristic "first u32 < image_size"
misfired on 'BM' bitmap data — caught and removed same round).
Train 5 — residual tails (~180 names, three waves): real implementations
(LineDDA Bresenham + guest callback, IsTextUnicode, wcstok/_wmakepath/swscanf/
sscanf, Rtl string plane, Net* workgroup-empty, GetFileTitleW, FoldStringW,
DllGetVersion 5.82, GetCalendarInfoW, GetRegionData); honest-miss families
(winmm no-hardware, DDEML no-server, NDDEAPI empty DB per wine nddeapi.spec
500-512, setupapi, ICMP, ole32 storage). Ordinals from shipped wine specs:
SHELL32.16/61/100, SHLWAPI.231/413/437, COMCTL32.2/8. KNOWN-UNKNOWN stays
(Rule 1): NDDEAPI.609-612, SHELL32.236/241/245. SW-1250 section-verify made
reloc-aware (riched20 sec1 false positive killed; the SW-1252 msvcp140 .text
zeroing signal survives).

Measurement (binary c1d45aac): census x3 ALL DETERMINISTIC over 19 SWEEP-050
guests — everything/mspaint/mshearts/wordpad/drwtsn32/calc/winmine/notepad/
write/winver/sol/freecell/charmap/clipbrd/sndvol32/taskmgr/regedt32 exit-0,
net rc=2 + ping rc=1 honest usage; ZERO stub calls; DL corpus 14x exit-0 +
pageant 124 ZERO regressions; G1 PIN 12/12 (evidence/master/g1/20261022_170026);
calc battery 15/15. evidence/sweep050/ (census.tsv + expand_log.tsv +
payloads_sha256.txt + wordpad/drwtsn32/everything traces). No force-push.
Open: SW-1260 wordpad OnCreate -1 producer, SW-1261 soffice decode (LO MSI
staged), Everything UI interaction.

## SWEEP-051 (2026-10-22) — «قطار با برنامه های واقعی» Train: LibreOffice SW-1261 stage 1

The user-mandated four real-graphical-program wagons: calc ✓ (battery 15/15),
Windows-only Everything ✓ (exit-0, SWEEP-050), XP games ✓ (mshearts/winmine/
sol/freecell exit-0), and the ODF display program = **this round's train**.

- Payload: LO 26.8.1.1 x64 MSI (c6298b10, staged SWEEP-050) -> largest OLE
  stream = MS-CAB **LZX** (typeComp 0x1503; the +4-shifted CFHEADER has
  cFolders@0x1a, cFiles@0x1c=19427, flags=4, 128-byte MSI reserve, and
  0xb0=176 at 0x10 = CFFOLDER byte offset). No cabextract/7z x64 available
  and no root: **built libmspack from source** (gcc, no autotools needed for
  the mspack core) + a 40-line extractor (scripts/sweep051_lo_extract.c) ->
  19427/19427 files, 0 failures.
- Guest staging: soffice.bin (2.7MB, 51 static imports) + sal3 + mergedlo
  (151MB) + 54 more DLLs; MSI underscore-vs-hyphen renames handled
  (orcus_parser->orcus-parser, libcrypto_3->libcrypto-3, libssl_3,
  libxmlsec_mscng); VC runtime from the MSI's own amd64 payloads.
- SIX root causes landed (instruments, not guesses): 4-arg
  _get_stream_buffer_pointers (wine spec L380; the v1 8-arg stub zeroed
  msvcp140 .text at its own return address — ptrace DR0 watchpoint made the
  writer's RIP undeniable); disk-DLL static TLS plane (sal3 0x6D654);
  MW_DLL_MAX 128 + early registration (re-entrancy dedupe); alloc registry
  512K (HEAP-TRAP exit(4) killed mergedlo's init wave); DLL .pdata cap 4M
  (mergedlo 756,578 entries — FH4 unwinding through mergedlo now works);
  TlsAlloc static-TLS collision (cppu3 0x13fc5).
- MEASURED binary **0d3b46b9**: soffice.bin x3 deterministic rc=99 / 10 stubs
  — 57 DLLs mapped once each, msvcp140/sal3/mergedlo CRT init survive,
  CreateWindowExW -> HWND 0x10001 + WM_CREATE, guest C++ throw unwinds via
  REAL vcruntime140_1 __CxxFrameHandler4 through DLL frames to the UCRT
  _seh_filter_exe -> _exit(0xE06D7363). Frontier SW-1262 = cross-DLL C++
  catch-type matching (uno::Exception catch in mergedlo).
- Gates: G1 PIN 12/12 (evidence/master/g1/20261022_184613); calc battery
  15/15; census x3 zero regressions (19 SWEEP-050 guests byte-identical; DL
  corpus = HEAD — autoruns64/zoomit64 139 A/B-verified pre-existing at
  c1d45aac, not a SWEEP-051 regression).
- Engine diagnostics added (env-gated): MINWIN_DLL_VERIFY (full reloc-aware
  section verify), MINWIN_DLL_TRACE (memcpy/[SBPTR]/[WCSDUP] logs), [DLLSLOT]
  crash-dump attribution; ptrace watchpoint tracer scripts/sweep051_watch.c.
- evidence/sweep051/ (README with the full decode chain, milestones,
  payloads_sha256, watchpoint hits, rc4/rc99 stderr); no force-push.

## SWEEP-051 Train 2 — SW-1261b winmine INTERACTION battery (9/9)

- New engine channel: MINWIN_MOUSE="x,y;x,y;..." — coordinate WM_LBUTTONDOWN/
  WM_LBUTTONUP pairs paced through the GUI pump (the id-based MINWIN_CLICKS
  cannot drive custom-drawn boards). Label-phase fix included.
- winmine (XP Minesweeper) x3: click at (24,64) delivered as a DOWN/UP pair ->
  the guest's own WndProc arms SetTimer(id=1,1000ms) (the real game-clock
  contract: the clock starts on the first left click) and repaints the board
  (88-110 16x16 BitBlts); exit 0, zero crashes. 9/9 (3 checks x 3 runs).
- G1 PIN 12/12 re-pinned on binary 0402f7de (evidence/master/g1/20261022_185323).

## SWEEP-052 Train 1 — SW-1262 cross-DLL C++ catch matching LANDED

- Inherited frontier: soffice rc=99, "mergedlo catch handlers do not claim
  the cppuhelper3msc throw". Decoded with three instruments built this train:
  (a) disk ThrowInfo4/CatchableTypeArray decoder (scripts/sweep052_eh_decode.py),
  (b) [CXEH] dispatch-time FH4 throw decoder in the engine (throw image,
  catchable type names, uno::Exception Message UTF-16 dump, object RTTI
  probe), (c) DR0 hardware-watchpoint runs (scripts/mw_watch).
- FH4 throw record disassembly (guest's REAL vcruntime140/_CxxThrowException
  RVA 0x5230): EI[3] = RtlPcToFileHeader(ThrowInfo) = throwing image base;
  EI[0]=0x1994000 only when that lookup FAILS; ThrowInfo4 = image-relative
  DWORD RVAs in both cases. __CxxFrameHandler4 prologue confirmed:
  FuncInfo4 = dc->ImageBase + *(DWORD*)dc->HandlerData.
- ROOT CAUSE #1: engine RtlPcToFileHeader v1 = EXE-only. Fixed via
  mw_module_of (guest-DLL registry). MEASURED: mergedlo's FH4 handler now
  CLAIMS the DeploymentException throw and RtlUnwindEx unwinds to the
  mergedlo catch landing pad — cross-DLL catch(uno::Exception&) matching
  works end to end.
- ROOT CAUSE #2: AccessCheck v1 = unconditional FALSE; sal3's pre-open gate
  (GetNamedSecurityInfoW -> DuplicateToken -> AccessCheck) then denied
  uno.ini/services.rdb. Fixed: AccessCheck v2 real DACL semantics (NULL-DACL
  grant-all per Windows contract, DENIED-then-ALLOWED ACE walk, session
  user+Administrators SIDs, generic->file mapping), DuplicateTokenEx v2 real
  handle duplication, DuplicateToken (3-arg) table entry. SW-1261 staging
  gap closed: uno.ini/fundamental.ini/types.rdb/services.rdb staged.
- MEASURED binary 69a015b9: soffice x3 deterministic rc=99 stub 8 (was 10;
  the -2 = DuplicateToken now resolved); regression census ZERO deltas vs
  SWEEP-051 baseline (procexp64 124 IS its baseline); G1 PIN 12/12
  (evidence/master/g1/20261022_195936); calc battery unchanged.
- NEW FRONTIER registered SW-1263: sal3's osl_openFile receives truncated
  directory prefixes of the services.rdb path; cut point moves with allocator
  layout (heap-guard A/B moves it to '...sweep051\gu') = corrupt rtl_uString
  length/header in the bootstrap string chain. GetModuleFileNameW verified
  clean; corruption is downstream (allocator interaction class).
- evidence/sweep052/ (README decode chain, cxeh_decode.log, catch_claimed.txt,
  regression.tsv); worklist SW-1262 marked + SW-1263 registered; no force-push.

## SWEEP-052 Train 2 — SW-1263 root cause #3: per-module handle contract

- The DR0 watchpoint data from Train 1 pointed at a corrupted rtl_uString
  flowing into sal3's URL->system-path conversion. Cross-checked the guest's
  own provenance chain: GetModuleHandleExW(flags=6 = FROM_ADDRESS|UNCHANGED_
  REFCOUNT) appeared in the trace immediately before the poisoned
  GetModuleFileNameW reads.
- ROOT CAUSE #3: mw_GetModuleHandleExW v1 answered FROM_ADDRESS with the EXE
  base unconditionally, and mw_GetModuleFileNameW v1 ignored the handle
  entirely (EXE path for EVERY module query). Real Windows returns the module
  containing the address and that module's path. The UNO bootstrap derives
  per-module install dirs through exactly this pair.
- Fix: FROM_ADDRESS -> mw_module_of(address) over the guest-DLL registry;
  GetModuleFileNameW -> MwDllImage.path for guest-DLL handles (C:\-form), EXE
  fallback unchanged.
- MEASURED binary 565b0803 (G1 PIN 12/12, evidence/master/g1/20261022_203645):
  FROM_ADDRESS answers cppuhelper3msc.dll / mergedlo.dll; per-module paths
  exact (mergedlo.dll u16len=49, cppuhelper3MSC.dll u16len=55); bootstrap
  file-I/O volume 63 -> 186 CreateFileW per run; soffice still rc=99 x-class
  ('Cannot obtain UNO_SERVICES') with the truncated rtl_uString now
  constructed downstream of the fixed queries (cut at 14/22/23 chars —
  allocator/length-header class, SW-1263 continues).
- Regression: calc/winmine/mspaint x1 exit-0 zero-stubs after the change.
- evidence/sweep052/train2_decode.log + train2_opens.tsv; no force-push.

## SWEEP-052 Train 3 — SW-1261b Everything 1.4.1 INTERACTION battery 9/9

- Everything (the Windows-exclusive NTFS instant-search program; no Linux
  equivalent) now driven through the coordinate-mouse channel built for
  winmine: MINWIN_MOUSE="400,20;400,100" delivers WM_LBUTTONDOWN/UP pairs
  into the search-box and listview coordinates through the live GUI pump.
- The guest's own WndProc REACTS like real Everything: WM_PAINT dispatched
  directly after the search-box click, then the WM_TIMER pump continues
  (195 dispatches per run), then a clean budget exit.
- Battery (scripts/sweep052_everything_battery.sh): 3 runs x 3 checks =
  A both click pairs delivered, B paint reaction + timer pump alive,
  C exit 0 no [CRASH] -> 9/9 PASS deterministic
  (evidence/sweep052/everything_battery_9of9.log).
- Note: the pump budget is the census baseline (MW_GUI_EVENTS_BUDGET=200);
  at 300 the app outlives a 60s timeout (it is a long-running indexer), so
  the battery pins the deterministic census profile.

## SWEEP-052 batch close — gates

- calc battery 15/15 (scripts/sweep023_calc_battery.sh) on the SWEEP-052
  binary 565b0803.
- Four-program mandate state after SWEEP-052: (1) ODF display = LibreOffice
  bootstrap advanced two root causes deep (SW-1262 LANDED: cross-DLL catch
  matching works; SW-1263 root cause #3 landed, allocator-class truncation
  frontier continues); (2) Windows calculator = calc battery 15/15; (3)
  Windows-exclusive = Everything exit-0 + NEW interaction battery 9/9
  (SW-1261b LANDED); (4) XP game = winmine interaction battery 9/9
  (SWEEP-051, unchanged this sweep).
- Census snapshot (evidence/sweep052/regression.tsv): ZERO deltas vs the
  SWEEP-051 baseline across 15 guests x3 (soffice stub 10 -> 8 is the
  DuplicateToken fix, the only intended delta).
- G1 PINs: 69a015b9 (Train 1), 565b0803 (Train 2/3) both 12/12.
- Open cars: SW-1263 (soffice rtl_uString truncation -> services.rdb ->
  VCL pump), SW-1260 (wordpad LoadStringW(61443) resource work), SW-1247-
  residual per-guest import tails (clipbrd 49 / charmap 9 / sndvol32 5 /
  sol 3 / net 4).

## SWEEP-053 — real-program SCREENSHOTS batch (SW-1264/1265/1266/1266b)

- Mandate: «ادامه اسکرین شات اجرای برنامه ها رو هم نشون بده» — make the
  four-program targets VISIBLE. Corpus re-staged from pins (XP ISO
  19b5a382, LO MSI c6298b10 -> CAB 71dfff0b -> 19427/19427, soffice.bin
  cb08cffd / mergedlo.dll 017d0c21 byte-exact); staging gaps fixed
  (mfc42u beside mspaint/mshearts/wordpad — the MFC ordinal imports crash
  the guests without it; riched20 beside wordpad; cards beside the card
  games).
- SW-1264: engine-internal resource consumers (mw_dlg_from_resource,
  mw_loadimage_bitmap_dib) read the IMAGE_RESOURCE_DATA_ENTRY header as
  the resource data (LockResource identity vs LoadResource conversion —
  SW-1259 contract honored only on the guest path). XP calc's dialog
  template 0x66 was unreadable -> zero children. Fix: entry -> data
  conversion. MEASURED: cdit=30, 30 children with template rects.
- SW-1265: real LoadStringW/A (RT_STRING blocks). calc's 86 label loads
  resolve.
- SW-1266: per-window subclass chain (engine defproc answer for
  GetWindowLongPtrW(GWLP_WNDPROC), per-window store, SendMessageW routing,
  UpdateWindow child-paint delivery + standard-control compositor,
  template-sized grow-only surface, SW-1266b MoveWindow/SetWindowPos
  grow). MEASURED: **XP calc renders (338x258, 30 controls, real face)**;
  winmine board + 3 Win95 dialogs; Everything 778x605; freecell 640x480
  card blits.
- Gates: G1 12/12 (20261022_215001); census zero deltas vs SWEEP-052
  (11 XP guests x2 + everything + soffice; soffice rc=99 stub 8 = its
  baseline). Binary 4e6e4a44. Screenshots: evidence/sweep053/shots_png/.
- New cars: SW-1267 (sol/mshearts surface sizing order), SW-1268
  (Everything content paint via fake index); SW-1263 (soffice rtl_uString
  truncation) remains the ODF frontier.

---

## SWEEP-054 (2026-10-23) — «۱۰ برنامه و ۵ بازی معرف دو بعدی قدیمی» + screenshots

Reconcile: origin/main 0f7bd36 (SWEEP-053) fast-forwarded before work (user
rule #1: GitHub first, verify pushes). Local stale loader rebuilt to the
byte-exact SWEEP-053 pin 4e6e4a44 first — the stale Oct-21 binary was
masking SW-1264/65/66 (15-guest census went 139-crash -> all-exit-0 after
rebuild: staging was never the problem).

Payload re-staging: XP x64 ISO 19b5a382 (628 MB, byte-verified) -> 26/29
payloads (NEW: SPIDER.EX_ 268 KB) -> 22/22 expanded rc=0 via engine 7za;
Everything zip 4f4a9b92 verified; LibreOffice archive URL 404 (SW-1273).

Engine wagons (all trace/disassembly-root-caused):
- SW-1269(+b) ShowWindow first-show WM_PAINT + SW_MAXIMIZE 1024x768 surface
  grow — spider white-screen root cause (pump "no event source" quit).
- SW-1270 posted-message queue (PostMessage A/W) — spider's own WM_COMMAND
  deal action was dropped headlessly; now delivered first, real priority.
- SW-1271 REAL accelerators — spider disassembly 0x10000bebf:
  `test rbx,hAccel; je loop` skips Translate/Dispatch for main-window
  messages when hAccel==NULL; RT_ACCELERATOR 16 entries now load for real
  and TranslateAcceleratorW translates + sends WM_COMMAND. Pre-landing
  regression (MAKEINTRESOURCE '%s' trace SEGV) caught via ProduKey/cports
  0->139, bisected, IS_INTRESOURCE-guarded.
- SW-1272(+b) BitBlt-into-window-surface frame capture + FRAME_EVERY
  throttle (8.6 GB first capture filled the disk).
- SW-1274 REAL timeGetTime — deal-animation clock froze at 0.

MEASURED: XP spider renders REAL CARDS (5♥ 2♦ 10♣ + spider deck), deal
completes rc=0 — first spider render ever; winmine 82 frames with
mouse-revealed cells; mspaint fixed by SW-1269; 15 screenshots in
evidence/sweep054/shots_png/; census 15 guests x3 45/45 exit-0 ZERO stubs;
DL corpus 45/45 deterministic; G1 PIN 12/12
(evidence/master/g1/20261023_030335, binary 09c33df201af8ade).
Open: SW-1275 spider brush/card-face, SW-1273 LO mirror, SW-1206 corpus.


## SWEEP-055 (2026-10-23) — «ادامه» SW-1275 spider board + SW-1273 LO mirror + SW-1206 corpus

Rule #1 (user standing law): git fetch first — origin/main = c59811a (SWEEP-054)
= local HEAD; worked on the true tip; tree had only untracked artifacts.

- **SW-1275 LANDED — XP SPIDER RENDERS THE FULL GREEN BOARD (first ever).
  Three engine root causes, all disassembly-decoded**:
  1. OS/2 BITMAPCOREHEADER DIBs: 29 of spider's 56 RT_BITMAPs (CARD2/12/13/24,
     71x96, 1/4bpp) carry the 12-byte core header — the v1 "hdr_size >= 40"
     INFOHEADER gate rejected them all -> 16x16 placeholders -> SelectObject
     of an IMAGE-space handle -> board-DC BitBlt "bad dst/src DC" -> blank
     faces. mw_loadimage_bitmap_dib decodes both formats (core: WORD w/h,
     3-byte BGR palette triples) + miss diagnostics in LoadImage/FindResource.
  2. Bitmap slot pool: MW_MEMDC_MAX 32 -> 256 (v&0xFF handle ceiling; every
     LoadImageW decode leaks its slot for the process life) + mw_DeleteObject
     now frees unselected HBITMAP slots (selected-in-memDC refuse = real GDI).
  3. mw_engine_paint_children wiped the WHOLE surface gray on UpdateWindow
     even with ZERO children (spider erased its own guest-blitted board);
     the dialog-face fill now requires >=1 live child (calc unchanged).
  MEASURED (binary c5635ee8): green felt board 645 frames; 10 columns striped
  backs; faces A♠ 3♦ 2♦ Q♥ 8♦ 4♥ 8♠ A♠ 4♥ 4♠ with real art; spider-logo deck;
  census 3x exit-0 ZERO stubs; screenshots evidence/sweep055/shots_png/
  (+ user copy /home/z/my-project/download/spider_sweep055.png).
- **SW-1273 CORE LANDED — LibreOffice mirror resolved, payload byte-exact**:
  downloadarchive .../old/26.8.1.1/ ALIVE (26.8.1.2 404s); MSI 374,870,016 B
  sha c6298b10 MATCH (sweep055_lo_restage.py); CAB carved via MSCF-magic
  stream scan (binary-UTF16 stream names) sha 71dfff0b MATCH
  (sweep055_lo_carve.py); expanded BY THE ENGINE's 7za 19,427/19,427 rc=0;
  pins cb08cffd (soffice.bin) / 017d0c21 (mergedlo.dll) verified
  (sweep055_lo_stage.sh). Closure analysis: soffice.bin's 51 static imports
  are all host DLLs — mergedlo/sal3 load dynamically. Fresh 251-file staging:
  deterministic 139/8 (bisect: HEAD binary identical -> staging-driven) —
  the libxslt/lpsolve55/libcrypto-3/libssl-3 wave dies in a CRT _initterm
  jumping to a bogus RIP -> new car SW-1276; SW-1263 rtl_uString continues.
- **SW-1206 LANDED — adexplorer64 corpus recovered (8th)**: zip 56536bff /
  exe e4512878 sha-verified via the documented one-shot procedure; census
  3x exit-0 ZERO stubs ZERO unresolved.
- **SW-1268 PARTIAL (5 contracts landed)**: FindFirstVolumeW (fixture GUID
  volume), GetVolumePathNamesForVolumeNameW + GetVolumeNameForVolumeMountPointW
  (volume<->"C:\" mapping), CreateFileW volume handles (0x000B0000 namespace),
  DeviceIoControl FSCTL contracts (GET_NTFS_VOLUME_DATA / ENUM_USN_DATA with
  MFT_ENUM_DATA_V0 cursor protocol + 11-record FRN fixture / READ_USN_JOURNAL /
  IS_VOLUME_MOUNTED), GetOverlappedResult InternalHigh. Trace-verified:
  Everything enumerates + maps the volume. Remaining gate decoded: its
  internal volume-DB walker (0x1400d24e0) + user-mode checks
  (0x1400d6df0/0x1400d6f30) fail before any API call -> new car SW-1278.
- Census (artifacts/sweep055/census/census.tsv): 16 guests x3 deterministic —
  15 x exit-0 ZERO stubs ZERO unresolved (SWEEP-054 baseline preserved),
  adexplorer64 exit-0 (SW-1206), soffice 139/8/8 (documented fresh-staging
  profile).
- G1 PIN 12/12 (evidence/master/g1/20261023_045212, binary c5635ee82bd5dd2b).
Open: SW-1278 (Everything volume-DB), SW-1276 (soffice initterm), SW-1277
(sol cards.dll layout), SW-1267/1268, SW-1273 render frontier (SW-1263).

## SWEEP-056 «چنده قطار چنده واگن حرکت» — per-module resources + sol's REAL metrics (2026-10-23)

**Rule #1 (standing law)**: fetched origin BEFORE work — remote had advanced
242af84 -> 2156eed (SWEEP-039..055 landed by the parallel chain: four-program
mandate covered, screenshots, 10+5 corpus). Fast-forward synced, no RECONCILE
needed (local had no unique commits). This batch = SWEEP-056 on top of 2156eed.

**Corpus re-staging train (wagons 1-4)**: the gitignored guest payloads were
gone again (SW-1206 pattern, 9th recovery): xp_x64.iso re-downloaded
(628,617,216 B, sha 19b5a382 MATCH, resumable 6-stream), 26/29 AMD64 payloads
extracted (pycdlib), expanded BY THE ENGINE's own 7za, staged 14 guests
(winmine/sol/freecell/mshearts/spider/calc/mspaint/notepad/wordpad/charmap/
clipbrd/write/winver/sndvol32 + cards.dll/riched20/mfc42u/mspaint.chm).
STAGING CONTRACT learned (register for the farm playbook): mshearts AND
mspaint need mfc42u.dll in their OWN dir (MFC apps; without it every MFC42U
ordinal stubs -> mspaint SIGSEGV RVA 0x5bb4d = the SW-1245 comment's crash).
Everything 1.4.1.1028 re-staged from the pinned zip (4f4a9b92, host-python
unzip; the engine's 7za mangles this zip's UTF-8 names + rc=134).

**Train: SW-1277 LANDED — two engine root causes**:
1. w3_res_find ignored hmod — walked the MAIN EXE's .rsrc for every module.
   cards.dll!cdtInit does LoadBitmapA(hinst_cards, 0x35/0x43/0x44) and takes
   the card metrics from GetObject's bmWidth/bmHeight; the v1 walk searched
   sol.exe's resource dir (8 TYPE ids, none RT_BITMAP) -> miss -> 16x16
   placeholder -> metrics 16x16 -> window 152x64 (the SWEEP-054 mystery).
   The v2 half-fix (rsrc_rva from hmod's header but base = g_image_base +
   rsrc_rva) walked sol's .text as a resource dir -> SIGSEGV (named=9332
   garbage) — caught by the [RES] walk probe, fixed by hoisting pe.
2. GetObjectW/A knew only MW_IMAGE_BASE (0x60000) placeholder handles;
   mw_loadimage_bitmap_dib returns MW_HBITMAP_BASE (0x52000) handles ->
   cdtInit's GetObjectA returned 0 -> metrics from stack garbage (31x0).
   Added the HBITMAP family decode (g_bitmap_obj, bi=(v&0xFF)-1).
   Applied mw_res_pe_base across ALL 7 hrsrc data-resolution sites.
MEASURED (binary bd7747c2): GetObjectW(HBITMAP 52001) -> BITMAP 71x96;
sol CreateWindowExW 585x384 (SWEEP-054: 152x64); LoadBitmapA(0x35/0x43/0x44)
-> real DIB decodes 0x52001/2/3 from cards.dll's 74-bitmap dir; sol status
bar renders (Time: 0 / Score: 0).

**Train: SW-1278 decode progress**: check-a = SetErrorMode+GetVolumeInformationW
wrapper (IAT 0x1401b0370), check-b = stricmp(fs,"ntfs"); both PASS with the
current fixtures (the SWEEP-055 "no API calls" half was wrong); GetDriveTypeW
gate {2,3,6} passes; enumeration verified in trace. Frontier -> post-
registration in-memory DB state. SetLastError(2) floods = guest internal
signaling, NOT failed FindFirstFileW (theory disproven).

**Train: census + evidence**: 14 guests x3 = 42/42 exit-0, ZERO stubs, ZERO
unresolved, deterministic. Regression battery green (spider 651 frames real
cards, winmine 82, freecell 10, mshearts 2). Screenshots 9 PNGs ->
evidence/sweep056/shots_png/. G1 PIN 12/12 (evidence/master/g1/20261023_060132).

**New wagons registered**: SW-1279 (sol deal chain: cdtDraw imported, never
called; WM_COMMAND 0x3e8 dispatch -> 0; client 587x32 vs surface 587x384
bookkeeping); SW-1276 open (LibreOffice re-stage for the initterm decode —
download interrupted, next train). SW-1268/SW-1278 continue.
