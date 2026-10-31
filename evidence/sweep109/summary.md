# SWEEP-109 — «IDM فایل ستاپ واقعا اجرا بشه» + the three named programs advance

Binary sha256 `c890d54ba8e4434e72fdd4ba50e83dccab80e7dc28101a3047253361862a7836`.
G1 PIN 12/12 (evidence/master/g1/20261030_144242). corpus_gate 15 rows, rc=0 in 9,
crash total 0 — ZERO regressions (xp.* SEED rows MISSING unchanged, owed).

## The ladder (issue #25 APP-IDM): unpacker silent-exit 0 → second-stage WinMain

Every run below is against `idman642build2.exe` sha256
`276aa7e4ff7682445b011ebe6ac3920e7eca6ece7d3f6723b1f1cb515164461b` (pinned).

| Stage | Before (SWEEP-108) | After (SWEEP-109) |
| --- | --- | --- |
| Temp dir | "Could not create temp dir in unpacker, Error ID: 2" MessageBox, exit 0 | `IDM_Setup_Temp` created, extraction RUNS |
| Payload | nothing extracted | **65 files** extracted (`IDM0.tmp..IDM63.tmp`, IDM5.tmp = 6.5 MB) |
| Child spawn | `CreateProcessW("IDM1.tmp" -d ...) failed ERROR_FILE_NOT_FOUND(2)` | child `IDM1.tmp` LOADED and runs |
| Child CRT | (never reached) | full VC6 CRT init: `_initterm` x42, `_onexit` x40, `__wgetmainargs` arena argc=3 |
| Child WinMain | (never reached) | **WinMain RUNNING**: IsWow64Process2 gate passed, registry probes real (`HKCU\Software\DownloadManager`, `HKLM\...\CurrentVersion\ProgramFilesDir` = `C:\Program Files` read back SUCCESS) |
| Frontier | silent exit 0, 77k steps | SW-1539 (named, decodable) |

## Root-cause fixes landed (all measured, evidence in this dir + /tmp run logs)

1. **SW-1524** `w3_seed_windows_world()` — installer world (MINWIN_WINTEMP=1) boots
   with the FULL Windows directory tree pre-materialized (Users/MiniWin profile,
   Program Files, SysWOW64, Fonts, ProgramData). The old first-touch lazy seed
   fired INSIDE IDM's check-then-create recursion (CreateDirectory(Temp) →
   EEXIST(183) → guest abort → "Error ID: 2"). Wine parallel: wineboot builds the
   whole prefix tree before any guest runs. Plus the trailing-slash strstr fix
   ("/Windows/" → also matches ".../Windows").
2. **SW-1525** `IsWow64Process` — a PE32 guest on the x64 engine answers
   `*Wow64Process=TRUE` (the WOW64 situation). The old constant FALSE sent the
   32-bit installer down the wrong process-creation branch.
3. **SW-1526** `CreateProcessW` observability — MW_TRACE at entry (app/cmd) +
   the failing MZ-probe path logs the target. This decoded "Could not create
   process in unpacker, Error ID=2" → target `IDM_Setup_Temp\IDM1.tmp`.
4. **SW-1527** DialogProc registry — modeless dialogs carry their proc: store on
   CreateDialogParamW/A + route in SendMessageW/A AND the pump dispatch. IDM's
   extraction trigger is `SendMessageA(hDlg, 0x7E8)` — the old drop-proc answered
   0 and the extraction never ran. Wine parallel: user32/dialog.c routes every
   message through the stored dlgproc.
5. **SW-1528** `mw32_thread_trampoline` → ms_abi. The boot trampoline calls
   MwGuestStart (ms_abi, arg in RCX); the SysV callee read RDI (garbage 0x1001)
   → SIGSEGV at trampoline+6 on the FIRST m32 guest thread.
6. **SW-1529** `filemap_lookup` accepts BOTH the raw 0x6000+N object id AND the
   handle-table handle CreateFileMappingW actually returns. The old lookup
   compared the guest handle (0x6) against 0x6000+N → every MapViewOfFile failed
   ERROR_INVALID_HANDLE(6) → "Could not create map view file in GetViewFile,
   Error ID: 6". Returns the mapping index.
7. **SW-1530** Section owns a dup() of the fd (guest may CloseHandle the file
   right after CreateFileMapping — the canonical map-read dance) + m32 views are
   carved from the arena bump and MAP_FIXED'd SHARED (a NULL-hint mmap lands at
   0x7f... which a 32-bit guest truncates).
8. **SW-1531** A Windows section EXTENDS its file to the requested size
   (ftruncate at CreateFileMapping). Linux mmap does not — MAP_SHARED writes
   past EOF raised SIGBUS mid-extraction (IDM0.tmp output section).
9. **SW-1532** m32 arity: `SetFileTime`=16 / `GetFileTime`=16 (the old default-32
   over-pop desynced the extraction thread's stack → wild EIP fetch fault).
10. **SW-1533** x86 opcodes 0x18/0x1A (SBB r8 forms — the Tonec decompressor's
    carry chain executes `1a d2` = SBB DL,DL at 0x4044be), 0x12 (ADC r8,r/m8),
    0x0C (OR AL,imm8 — dup with the SW-1281 block, kept there). 0x2B/0x3A/0x0A
    already existed (case-sensitive grep false alarm — verified before build).
11. **SW-1534** `mw32_dll_is_crt` case-insensitive — VC6 binaries import
    "MSVCRT.dll" (uppercase); the strcmp miss made every cdecl CRT call
    stdcall-pop 32 → 4x caller-pops violation at CRT startup → FF /7 fatal.
12. **SW-1535** `__wgetmainargs` for cpu32 — argv/env arrays built in the 32-bit
    ARENA (host .data copies sit inside the interpreter's host-image guard and
    read back as 0); argv[0] = module path, never NULL (the VC6 CRT bails to a
    silent exit-0 without calling WinMain when argv[0]==NULL — decoded at
    0x4011302).
13. **SW-1536** m32 DATA-import plane — msvcrt's `_wcmdln/_acmdln/__argc/
    __wargv/__argv/__initenv/_commode/_fmode/_adjust_fdiv` are DATA exports: the
    IAT holds the ADDRESS of arena storage, not a function thunk. The old
    data-import pass wrote the host .data address (refused by the SW-1297b
    guard → read as 0). Wine parallel: data imports resolve to the export's
    address.
14. **SW-1537** `IsWow64Process2` implemented (processMachine=I386 in the PE32
    world, nativeMachine=AMD64) + arity 12. The old unresolved stub popped 32
    for a 12-byte stdcall call — every call leaked +20 bytes; WinMain's frame
    drifted, overlapped the callee's saved-EBP slot and `leave` restored ebp=0
    (WATCH32 evidence: ebx push @3fefed24 vs arg push @3fefed2c = +20).
15. **SW-1538** engine-side guest writes are now watch-visible
    (mw32w_engine_write + mw32w_log made frame-walk-free — the
    __builtin_return_address(1) inside inlined m32_ld32 crashed the logger
    itself).

## The named frontier (SW-1539, next round)

`IDM1.tmp` WinMain → command dispatch → `0x40952b` (ProgramFilesDir path
composer) returns via `leave` with **EBP=0** → WinMain's first `[ebp-0x14]`
write faults (si_addr=0xFFFFFFEC). Watch evidence
(`/tmp/idmrY` transcript shape): the saved-EBP slot [0x3fefed10] is written
0x3fefef60 at n=554 (push ebp), read back **0** at n=564 (the leave pop), and
NO in-window write touches it (all guest stores go through m32_st32; the
engine's RegQuery writes are now instrumented and hit their correct slots
n=558: *size=0x22 @3fefed04). The zero-writer bypasses both paths — suspects:
the wsprintfW special-case's varargs host call or an uninstrumented engine
writer (RegCloseKey/GetWindowsDirectoryW/lstrcpyW). Instrument next round:
mw32w_engine_write in mw_vfmt_w16 output, mw_lstrcpyW, mw_GetWindowsDirectoryW.

## The other two named programs (no regressions, evidence rows)

- **WinRAR SFX** (winrar712.exe): INSTALL_OK — `Program Files/WinRAR/WinRAR.exe`
  (3,316,880 B) + 75 files in the sandbox (evidence/sweep109/winrar/).
- **WinRAR GUI** (winrarinstd): ALIVE_PUMP_AT_TIMEOUT **frames=1** — the menu
  band + control tree render (SWEEP-108 SW-1520/1521 intact); rc=124 x-side
  unchanged.
- **Notepad++ 8.9.8.1 x64**: PASS_INPUT_STATE_CHANGE **frames=35 uniq=16** —
  the SW-1535/1536/1537 engine changes did not disturb the input-state contract.
- Batteries: idman rc=9 x3 deterministic (frontier SW-1539, honest), winrar
  EXIT rc=0, winrarinstd ALIVE_PUMP, npp64 PASS_INPUT_STATE_CHANGE.

## Gates

- G1 PIN OK 12/12 (evidence/master/g1/20261030_144242, binary c890d54b).
- corpus_gate sweep109: 15 rows, rc=0 in 9, crash total 0.
- No force-push.
