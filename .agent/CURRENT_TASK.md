# CURRENT TASK

**Task ID:** TASK-OPENSRC-BATTERY (P1) — 7-Zip + sqlite3 + jq + curl
**Status:** 3 of 4 programs GREEN (2026-10-17, wave W3+W4); curl = frontier

## Achieved (all evidence in evidence/master/)
- **7-Zip 25.01 (7za.exe, x64, official)**: banner/i/a/x/t/l all exit 0;
  archive create -> extract -> SHA-256 round trip **3/3 deterministic**.
  46-import surface closed (BUG-052) incl. OLEAUT32 fixed ordinals
  (2/4/6/7/9/10/149); BUG-054 path normalization. 7z.exe+7z.dll banner OK
  (0 unresolved); full ops need LoadLibraryW = OPENSRC-7ZIP-3 (open).
- **sqlite3 3.50.4 (official)**: --version banner; file-DB create/insert/
  SELECT round trip **3/3 deterministic**. Closed: BUG-055 (strdup registry),
  BUG-056 (GetStdHandle registry handles), BUG-057 (WCTMB fixed-length +
  LoadLibrary api-ms-win-* pseudo-handles + GetModuleHandleExW +
  InitializeCriticalSectionEx), BUG-058 (OVERLAPPED offsets).
- **jq 1.7.1 (official)**: --version + filters **3/3 deterministic**.
- **curl 8.17.0 (official)**: IAT page-crossing fix (BUG-059) + InitOnce
  nesting deadlock fix (BUG-060); remaining = UCRT state-pointer crash,
  OPENSRC-CURL-1 (open, next micro-steps listed).

## Gates kept green after every wave
G1 PIN OK (UPX banner, regression 12/12); sweep t01..t07+t10..t12 PASS
(t08 = BUG-038 G2 frontier, t09 = API-009 frontier — unchanged);
t13_wide_abi PASS; t14_file_read PASS; dispatch 4/4.

## Next micro-tasks
1. TASK-OPENSRC-CURL: OPENSRC-CURL-1 micro-steps (UCRT state layout).
2. TASK-LOADER-DLL: PE DLL loading (unblocks 7z.exe full ops; also
   ffmpeg PNG pipeline uses avcodec DLLs? no — ffmpeg build is static).
3. BUG-053: wprintf/wide-%s output path (7za `i` Formats table mojibake).
4. TASK-TEST-014 remainder: pin the sweep binary set in-repo (gitignore
   policy decision) + add t13/t14 to run_sweep.sh TESTS list.

---

## 2026-10-17 (CONTINUE #10) — Ladder wave: BUG-054b + BUG-061 + LADDER-008 zstd VERIFIED
- **BUG-054b CLOSED (TESTED)**: CreateDirectoryW/RemoveDirectoryW/DeleteFileW/
  GetDiskFreeSpaceExW now apply w3_normalize_path (absolute backslash -o root
  works; 3/3, SHA-256 byte-perfect; scripts/bug054b_accept.sh).
- **BUG-061 CLOSED (TESTED)**: _stat64 wrote POSIX offsets + memset 128B into
  the 56-byte MS x64 struct (72-byte overflow -> guest corruption -> RIP=0x0
  in zstd). Exact MS layout now (dev@0/ino@4/mode@6/nlink@8/rdev@16/size@24/
  times @32/40/48, memset 56).
- **LADDER-008 (zstd.exe 1.5.6 x64, mingw/msvcrt, SHA 6b5c50dd...) VERIFIED**:
  --version banner exit 0 3/3; -T1 compress + decompress round trip exit 0 3/3,
  SHA-256 byte-perfect. zstd-wave APIs added: FindFirstFileA, FindNextFileA,
  SetPriorityClass, _filelengthi64, fgetpos, fsetpos (msvcrt/kernel32),
  SleepConditionVariableCS (kernel32, mirrors SRW variant).
- G1 pin OK after each change (latest evidence/master/g1/20261017_131102);
  regression 12/12.
- New planning base per user directive: .agent/PROGRAM_LADDER.md (100 rungs),
  docs/master/MSVC_RUNTIME_PLAN.md (ADR-012), docs/master/ANDROID_FEASIBILITY.md
  (ADR-013), docs/master/GITHUB_RESOURCES_MAP.md (ADR-014 borrowing policy).

---

## 2026-10-17 (CONTINUE #11) — BUG-062 real guest threads; LADDER-008 re-verified on 0x60000-threshold payload
- **New harness**: scripts/ladder_run.sh — standardized 3× evidence runner for
  every ladder rung (command/exit/stdout/stderr/api_trace + artifact hashes,
  phase-named evidence dirs). Deterministic payload: samples/real/ladder_payload.bin
  (401408 B, SHA 35e6e204…).
- **BUG-062 CLOSED (TESTED)**: _beginthreadex/_CreateThread were failure stubs;
  zstd's pool got zero workers → ZSTDMT queued jobs → CLI livelocked in
  SleepConditionVariableCS for inputs ≥ 0x60000 bytes (393216; bisection
  0x5FFFF OK / 0x60000 hang, content- and level-independent). Live state proven
  by ptrace futex sample — NOTE: first attach hit the `timeout` wrapper via
  pgrep -f (its own legitimate sigsuspend = red herring); pgrep -x targets the
  real loader process. Fix: thread registry (64 slots, alloc_handle-issued
  handles) + ms_abi trampoline calling guest start on a host pthread; real
  WaitForSingleObject (WAIT_OBJECT_0/TIMEOUT); _endthreadex sets done+exit_code;
  ExitThread ends the calling thread only (was exit()!); GetExitCodeThread +
  CloseHandle wired. docs/bugs/BUG-062.md.
- **LADDER-008 zstd 1.5.6 RE-VERIFIED post-fix**: version 3/3 (banner), round
  trip 3/3 on the 401408-byte payload (compress + decompress + cmp,
  SHA-256 byte-perfect 35e6e204…), dedicated 393216-byte threshold case 3/3.
  Evidence: evidence/master/ladder/LADDER-008_{version,roundtrip}_20261017_*.
- G1 pin OK after change (evidence/master/g1/20261017_141251, binary
  062fa371…, regression 12/12). I/O paths exonerated with PE probes
  (scripts/probe_bug062_readfile.c/.fread.c; zig cc cross-compiler via pip
  `ziglang` now available in this env).

---

## 2026-10-17 (CONTINUE #11b) — BUG-063 + BUG-064: lz4 LADDER-009 VERIFIED
- **BUG-063 CLOSED (TESTED)**: mw_printf/mw_fprintf/mw_fwprintf were ms_abi
  variadic but walked SysV va_list — garbage arg slots (lz4 --version SIGSEGV
  inside mw_fprintf; 1-arg shapes had survived by layout luck). Fixed with
  __builtin_ms_va_list (+ va_arg dispatch); emit path unchanged.
- **BUG-064 CLOSED (TESTED)**: real kernel objects. Events (MwKEvent:
  manual/auto-reset, Set/Reset real), semaphores (MwKSem: count/max,
  ReleaseSemaphore real), IOCP queue (INFINITE branch was missing -> worker
  exited its GQCS loop -> pool lost worker; ms now uint32_t — 64-bit read
  picked dirty high bits), WaitForSingleObject dispatch (thread/event/sem),
  WaitForMultipleObjects wait-all/any, CloseHandle slot release,
  MapViewOfFile real mmap over the CreateFileMapping registry, plus
  RtlAddFunctionTable/__doserrno/_vsnprintf exports. Trace lines now carry
  [Txxxx] host thread ids (permanent facility).
- **LADDER-009 lz4 1.10.0 VERIFIED**: version 3/3; round trip 3/3 on the
  401408-byte payload, SHA-256 byte-perfect (35e6e204...). Sample provenance
  samples/real/lz4/ (official release asset, SHA c2b1ecab...).
- zstd LADDER-008 re-checked post-changes: 3/3 (no regression). G1 pin OK
  12/12 (evidence/master/g1/20261017_145001). Upstream source read:
  lz4 v1.10.0 threadpool.c (borrowing policy ADR-014 — read-only guidance).

---

## 2026-10-17 (CONTINUE #11c) — BUG-065 md5deep wave: 4 sub-fixes + 1 regression caught
- **BUG-065 sub-fixes (VERIFIED)**: 15 imports + 3 data imports (incl. __pioinfo
  full ioinfo layout, _wfindfirst/next64 552B registry, sprintf/_snwprintf with
  __builtin_ms_va_list, _wstat64); VirtualProtect EXEC bits + honest old-protect
  + page-walk over section gaps; argv[0] = basename (md5deep cuts "deep" in
  place — also unblocks busybox multi-call); FILE* issuance registry in
  map_crt_stream (glibc "invalid stdio handle" abort prevention).
- **Regression caught by re-verify**: zstd LADDER-008 → 139 because zstd opens
  output via _open+_fdopen (unregistered FILE*). Fixed by registering
  _fdopen/_fsopen/_wfopen results; zstd 3/3, lz4 3/3, G1 12/12 re-pinned.
- **LADDER-013 md5deep64 = RUNS-PARTIAL**: exit 0 3/3, dispatch+hashing work,
  final display line deterministically lost (winpthreads shutdown protocol) —
  frontier + next steps documented in docs/bugs/BUG-065.md.
- gzip (LADDER-010) = BLOCKED-OBTAIN: no official native win64 build exists
  (GnuWin32 is 32-bit only); revisit via busybox64 gzip applet or self-build.
