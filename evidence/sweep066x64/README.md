# SWEEP-066 — SW-1297: Notepad++ 8.9.8.1 به پنجره GUI رسید (Reached the GUI window)

Date: 2026-10-25 · Base: e482146 (origin/main, post SWEEP-067) · Binary sha256 049876c0… (merged-build G1 pin)

## Result

**Wagon goal LANDED**: Notepad++ 8.9.8.1 (first download of the binary — sweep50
had only resolved v8.9.8.1, never fetched it) runs under the loader through full
CRT startup, first-run config probing, its own static-UCRT file machinery, and
**renders real GUI windows**:

- `RegisterClassW` -> ATOM 0xc001
- **`CreateWindowExW(...) -> HWND 0x10001` (main window object created)**
- **2 real modal MessageBox frames rendered** (pump-on): `Configurator — Load
  langs.xml failed!` and `Load stylers.xml failed!` — screenshots/
  messagebox_1.bmp, messagebox_2.bmp (376x122, 720x122, 24bpp).

Exit 139 after window creation = next frontier SW-1298 (see below).

## Frontier decodes this round

1. **SW-1297-a (fastfail #1 root cause)**: first run = exit 9, 67 unresolved
   imports, `__fastfail(FAST_FAIL_INVALID_ARG=5)`. Chain decoded via .pdata +
   128-slot FASTFAIL stack window: N++ `0x14f6c0` (settings/copy manager;
   imports CopyFileW/ReplaceFileW/MoveFileExW) -> copy setup `0x38070` ->
   stream-copy helper `0x37800` -> CRT `_read(fd)` -> UCRT `_get_handle(fd)`
   (0x140428c70) -> EBADF -> `_invalid_parameter_noinfo_noreturn` ->
   `__fastfail(5)`.
2. **SW-1297-b (loader bug, ENGINE FIX — GetStartupInfoW)**: our
   GetStartupInfoW advertised `STARTF_USESTDHANDLES` with std handles
   0x4/0x8/0xC, but the w3files pool's real std handles are 0x4/0x5/0x6.
   Static-UCRT `_ioinit` registers fd0/1/2 DIRECTLY from STARTUPINFO
   (bypassing GetStdHandle); handles 0x8/0xC mapped to dead pool slots ->
   fd1/fd2 never registered in the guest's own __pioinfo table -> any std
   _read/_write on fd!=0 fail-fasted. FIX: `mw_stdhandles_ensure()` now
   pre-allocates the three std handles and GetStartupInfoW advertises
   EXACTLY the identities GetStdHandle returns.
3. **SW-1297-c (guest patch, fingerprint-guarded)**: even with fd0-2
   registered, N++'s first-run copy fallback still reaches `_read(fd)` on an
   fd its table lacks. Modern UCRT turns EBADF into __fastfail; classic
   msvcrt/wine return -1. Loader now scans the mapped image pre-EP for
   `call _invalid_parameter_noinfo_noreturn(0x401794) | call
   _invalid_parameter(0x4016dc)` sites followed by an "-1 materialization +
   recover" tail (`48 83 c8 ff`, `83 c8 ff`, or `eb rel8`), and rewrites the
   5-byte call into the same -1 materialization (errno=EBADF=9 is already
   stored by the guest before the call). 4 sites patched in notepad++
   8.9.8.1: 0x4201ec, 0x423097, 0x428cd7, 0x42bbe4. Every site is logged to
   stderr ([PATCH]); the patcher is inert for any guest without the exact
   fingerprint (G1/corpus unchanged).
4. 60-import wagon batch (scripts/sweep066_impl.c + apply.py): SHLWAPI path
   family REAL (Canonicalize/Append/StripPath/RemoveExtension/IsRelative/
   GetDriveNumber/CompactPathEx per wine), InternetCrackUrlW REAL (wine
   URL_COMPONENTSW x64 layout), ImageNtHeader REAL, CopyFileExW REAL (errno
   -> Win32 mapping), GetStringTypeExA/CompareStringOrdinal/lstrcmpiA real,
   ColorAdjustLuma real, IsTextUnicode heuristic; typed-honest: CRYPT32/WINTRUST
   (no Authenticode), IMM32, UxTheme buffered-paint/animation, DWM, SENSAPI,
   SHCreateItemFromParsingName/SHOpenFolderAndSelectItems (no COM shell),
   QueueUserAPC (no APC delivery), EnumThreadWindows (no per-thread windows),
   mouse_event VOID, ToAscii 0 (no VK->char table), clipboard listeners TRUE.
   `_TrackMouseEvent` routed to the existing mw_TrackMouseEvent.

## Gates (final: rebased onto origin/main e482146 = SWEEP-065/066/067)

- Reconciliation: local SW-1297-x64 work (commit c368991 on stale 242af84)
  re-spliced onto origin/main loader.c (upstream had independently landed
  14 of the same imports — theirs kept, mine deduped; 46 new imports remain)
  plus upstream Makefile (-lsqlite3). The upstream SWEEP-066/067 "SW-1297"
  is the x86/PE32 notepad++ track — this sweep is the complementary
  x64/PE32+ track; both landed.
- G1 PIN OK sha 049876c0 12/12 (evidence/master/g1/20261025_132524) on the
  merged build (4,295,928 bytes).
- Corpus x3 (scripts/sweep066_broad_sweep.sh, CLEAN dirs — stale registry
  hive from a pre-rebase batch caused a bogus putty fail-fast artifact,
  documented): 13 programs exit0 x3 + pageant 124 x3 (daemon) + notepadpp
  139 x3 (new frontier) + rammap64 139 x3 (= origin/main parity, A/B
  verified loader_origin vs loader_merged directly). stderr diffs = host
  addresses inside crash dumps only (documented ASLR class).
- GDI/calc/snake/breakout untouched by this round.

## Next frontier (SW-1298 queue)

- notepadpp exit 139: SIGSEGV RVA 0x3b94 (`mov rax,[rcx+0x28]`, rcx=NULL)
  called from 0xeea10 with `edx=0x7e4` and ctx from [wndproc-obj+0x9b0];
  chain: NppMainWndProc 0x102330 -> 0xeea10 -> 0x3b90(callback executor).
  The +0x9b0 context is NULL because the langs.xml/stylers.xml loads FAILED
  (the dialogs prove it). Two levers: (a) make the first-run model->user
  copy SUCCEED (the empty `GetFileAttributesW()`/`CreateDirectoryW()` path
  probe right after plugins/Config suggests a path-join bug — likely a
  guest path built via our SHLWAPI family with a trailing separator), then
  langs.xml/stylers.xml parse cleanly and the NULL context is initialized;
  (b) trace why the stream-copy helper 0x37800 fails after the EBADF
  recovery (fopen dest with OPEN_ALWAYS + read src works? verify with
  MW_TRACE=1 file-op listing).
- The `Load stylers.xml failed!` MessageBox frame is the honest first-run
  behavior for a broken config dir — same as real Windows.

## Artifacts

- artifacts/sweep66_sw1297/run1..run10 (decode progression, api_trace.json per run)
- artifacts/sweep66_sw1297/run_gui1/miniwin-results/notepad++.exe/screenshots/*.bmp
- artifacts/sweep66_sw1297/sweep/sweep066_sweep.tsv (corpus x3)
- scripts/sweep066_impl.c, sweep066_apply.py, sweep066_broad_sweep.sh
- ~/download/notepadpp_gui_messagebox{1,2}_20261025.bmp (user-visible copies)
