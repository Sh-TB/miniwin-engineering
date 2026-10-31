# SWEEP-013 — "5x multiplication" + GUI calculator acceptance (2026-10-19)

User directives this sweep:
1. (test at least one simple graphical calculator or another program) —
   test at least one simple graphical program (a calculator).
2. (multiply all suggested programs and follow-ups into a task queue at least 5x bigger so that many problems get solved) — multiply the task queue ~5x, solve MANY problems
   per sweep, keep measuring per-program progress.

## A. Calculator GUI program (DELIVERED)

`tests/gui/calc.c` — a REAL Win32 calculator: main window, EDIT display
control, 16 BUTTON children, WM_COMMAND routing, SendMessageW text I/O,
GetDlgItem/SetDlgItemInt, BeginPaint/FillRect/Rectangle/DrawTextW painting,
GetMessage loop. Cross-compiled with the restored mingw-w64 toolchain
(Debian debs re-extracted to scripts/mingw-debs/root/usr — home reset wiped
the previous copy).

End-to-end run under the loader (MINWIN_CLICKS="109,110,116,105,106,114"
= the scripted user pressing 1 2 + 4 5 =):

    exit 0, 3x deterministic, stdout: [CALC] 12+45 = 57
    screenshot: calc_screenshot.bmp (guest's own WM_PAINT output:
    title "MiniWin Calculator", expression "12+45=57", display "57")

Second scenario (C 9 * 8 =): exit 0, `[CALC] 9*8 = 72`, final expr `9*8=72`.

Engine machinery this demanded (all real, registry-backed):
- mini window manager: child controls get their own HWND identities
  (0x0003xxxx), text store, check state, control id, parent.
- CreateWindowExW/A detect system control classes (MAKEINTATOM 0x8080..0x8085
  and "BUTTON"/"EDIT"/... strings) with a non-NULL parent.
- pump v3: MINWIN_CLICKS=id,... delivers WM_COMMAND(MAKEWPARAM(id,0), child)
  — generic scripted input for dialog/control guests.

## B. Honest batch #3 (program-count driven, ~65 new/real-ified APIs)

user32: GetDlgItem, SetWindowTextW/A, GetWindowTextW/A(+Length),
SendMessageW/A (real: WM_SETTEXT/WM_GETTEXT/WM_GETTEXTLEN/BM_GETCHECK +
wndproc routing), IsWindow, IsWindowEnabled, EnableWindow, GetParent,
GetDlgCtrlID, SetFocus, GetDlgItemTextW/A, SetDlgItemTextW/A, GetDlgItemInt,
SetDlgItemInt, CheckDlgButton, IsDlgButtonChecked, CheckRadioButton,
LoadMenuW/A, SetMenu, GetMenu, DrawMenuBar, CheckMenuItem, EnableMenuItem,
LoadImageW/A, GetClassInfoExW/A (per-registered-class-name), SetRectEmpty,
GetWindowPlacement, GetWindowRect, MoveWindow, GetForegroundWindow,
SetForegroundWindow, IsIconic, ClientToScreen, ScreenToClient, GetMessagePos,
GetMessageTime, MessageBeep, CharLowerW/A, IsBadStringPtrW/A, IsBadReadPtr,
IsBadWritePtr, RegisterWindowMessageA, CreateDialogParamW/A (modeless),
CreateDialogIndirectParamW, EnumDisplayMonitors, GetMonitorInfoW/A,
GetTextMetricsW/A, GetTextCharsetInfo, GetTextCharset, EnumFontFamiliesW/A,
DrawTextW, ExtTextOutW.

gdi32: CreateFontIndirectW/A + GetObjectW/A (LOGFONT round-trip), image
handles for GetObject(BITMAP).
kernel32: LocalReAlloc (real realloc via guarded mw_realloc),
GetSystemWindowsDirectoryW/A, FlushInstructionCache, lstrcpynW/A, lstrcpyW/A,
InterlockedPopEntrySList (pointer-consistent with our push),
GetWindowsDirectoryA.
ntdll: NtQuerySystemInformation (classes 0/2 real, others
STATUS_INVALID_INFO_CLASS), NtOpenSymbolicLinkObject /
NtQuerySymbolicLinkObject / NtOpenDirectoryObject / NtQueryDirectoryObject /
NtQueryObject (honest NTSTATUS), RtlNtStatusToDosError.
advapi32: AllocateAndInitializeSid (real SID bytes) / FreeSid / EqualSid,
GetTokenInformation (TokenElevationType=18 / TokenElevation=20 real,
others ERROR_INVALID_PARAMETER), OpenSCManagerW/A (NULL+ERROR_ACCESS_DENIED),
CloseServiceHandle, RegCreateKeyA / RegOpenKeyA / RegSetValueExA.
shell32: SHGetFolderPathW (8 mapped CSIDLs, E_FAIL otherwise),
Shell_NotifyIconW.
ole32/oleaut32: CoInitializeSecurity (S_OK), SysAllocStringByteLen.
msvcrt: _ismbblead (honest ASCII answer, kills the 18x startup stub noise).

## C. Measured progress (51+2-program pool, scripts/sweep012_progsweep.sh)

Baseline = SWEEP-012 runs-AFTER. Final = runs-AFTER2 (this sweep):

    mean_score   2.71 -> 2.92
    RAN-OUTPUT   16   -> 22   (+6)
    STUB-GATE    10   -> 5    (named gates left: 1x InvalidateRgn @RAMMap64)
    CRASH-GUEST  12   -> 15*  (* = md5deep64 suite + stdin EOF path now
                                 REACHED: crash moved from CRT init into the
                                 program's own file-walk code — deeper)

Zero-stub clean runs (called NO unresolved import at all): Autoruns64,
ZoomIt64, Diskmon64, 7z2604-x64, jq-windows-amd64, sqlite3, gui-calc,
7z.exe, games, upx — plus real guest-chosen exit codes for zstd (37), lz4
(40), jq (5), trurl (7), sqlite3_rsync (1), sqldiff (1).

Wins per program (before -> after):
- procexp64: 21 stubs  -> 0 stubs (IsBadStringPtrW frontier cleared; now
  crashes in its own code — next: guest-level debug)
- Bginfo64: 16 stubs exit 9 -> 8 stubs exit 9 (text/enum stack cleared)
- Diskmon64: 17 stubs -> 0 stubs, exit 0 (RegisterWindowMessageA was last)
- RAMMap64: 2 stubs exit 1 -> 20 stubs deeper crash (CreateDialogParamW
  cleared; single gate left: user32!InvalidateRgn)
- 7z2604-x64: 1 stub -> 0 stubs exit 1 (own logic exit, clean)
- jq-windows-amd64: CRASH-GUEST -> exit 0 RAN-OUTPUT (stdin EOF path)
- md5deep64 suite (6): CRASH->deeper own-code crash; stdin harness fixed
  (`< /dev/null` — stdin-blockers were misread as hangs/crashes before).

Harness fix: sweep now feeds stdin from /dev/null — stdin-blockers
(zstd/lz4/jq/sqlite3/md5deep64) were previously measured at the wrong stage.

## D. G1

Re-pinned sha256 700d83ee3b5435f32a7a9adea94f6c21be37f366aedb70b88d944d06c263f14e
(regression 12/12; evidence/master/g1/20261018_174244).

## Files

- tests/gui/calc.c + tests/gui/calc.exe (calc.exe gitignored? no — kept
  under 64KB? it is ~50KB static; kept in repo for reproducibility of runs)
- artifacts/sweep013_calc/frame_001.bmp + frame_006.bmp
- artifacts/sweep012/runs-AFTER2/ (53 program runs, trimmed)
- evidence/sweep013/: this README, comparison.txt, progress.json, summary.tsv,
  calc_screenshot.bmp
- scripts/sweep012_progsweep.sh: stdin fix + gui-calc pool entry
