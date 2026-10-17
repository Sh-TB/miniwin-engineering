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
