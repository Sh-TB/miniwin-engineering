# SWEEP-008 — honest-stub batch, named-stub gates, registry/dialog subsystems, mass sweep, MiniWinCalc 100%

**Wave:** SWEEP-008 (one elephant batch per the user's «مثل فیل و یوز» directive)
**Loader change:** src/loader/loader.c — 6 subsystem batches, ~700 lines added
**Build:** 0 errors; G1 re-pinned (see evidence/master/g1/<ts>/ for the wave timestamp)

## What landed (per-fix report)

1. **SW-555 re-derivation — named stubs (guest_rip decode).**
   `mw_unresolved_import_stub` now decodes the guest call site:
   `__builtin_return_address(0)` - 6 = `FF 15 disp32` instruction; slot =
   guest_rip + disp32; slot->name pairs recorded at load time in static
   storage (filedata is freed, so names are copied). Output format:
   `[STUB] unresolved import called -> returning 0 (call #N) <dll!import> guest_rip=0x...`
   Ordinals print as `DLL!#NNN`. All 0 errors, regression 12/12 (G1 pin).

2. **In-memory registry (honest RAM-backed hive).**
   Real create/open/query/set/delete/enumerate semantics; keys never created
   really return ERROR_FILE_NOT_FOUND (that honesty is exactly what routes
   Sysinternals EULA gates into their first-run flow). Predefined roots
   implicit; handles 0xC1000000+idx; values <=512B/512 count; strict
   buffer-size semantics (ERROR_MORE_DATA).

3. **Dialog subsystem: DialogBoxParamW / DialogBoxIndirectParamW /
   CreateDialogParamW(+Indirect) / EndDialog.**
   PE .rsrc RT_DIALOG resource walker (level-tracked root->type->name->lang;
   the level bug that walked template-as-directory was found and fixed in
   this wave). WM_INITDIALOG delivered to the guest DLGPROC. MW_EULA_ACCEPT=1
   = documented Wine-CI-style EULA automation: the template's Accept-class
   button (gre/ccep/ccpt/yes/ok fragments, BUTTON class ordinals) gets
   WM_COMMAND, the guest's EndDialog closes the modal and returns its result.
   Default (env unset) = dialog opens, nobody clicks, 0 returned (honest).

4. **Global atoms (real semantics), ColorRGBToHLS/ColorHLSToRGB (pure
   GDI 0..240 math), GetVersionExW/ExA + ntdll!RtlGetVersion (Win10 22H2
   build 19045, Windows-faithful size validation; RtlGetVersion does NOT read
   the caller size — fixed a libc memset crash from this very stub).**

5. **Module-handle class fix (the RIP=0 crash class).**
   GetModuleHandleW/A on known system DLLs now returns the same pseudo-handle
   identities LoadLibraryA mints; GetProcAddress(NULL, name) resolves by name.
   autoruns64's `GetModuleHandleW("ntdll.dll") + GetProcAddress("RtlGetVersion")`
   used to produce a NULL function pointer the guest called -> RIP=0.
   Cookie guards: name pointers < 0x1000 (MAKEINTRESOURCE-style, e.g. 0x87)
   never dereferenced (was a libc strlen crash at the procexp/autoruns
   LoadLibrary frontier).

6. **Dialog-control + GUI-init layers.**
   GetDlgItem (stable 0xC3000000+id control handles), SetWindowTextW/A,
   SendMessageW (DefWindowProc-default), IsDlgButtonChecked (BST_CHECKED only
   under MW_EULA_ACCEPT), CheckDlgButton, SendDlgItemMessageW,
   GetSystemMetrics (honest 1920x1080 per-index table), ImageList_* family
   (synthetic objects), CreateFontIndirectW/A (real GDI-object handles —
   stub-0 caused a guest NULL-deref), RegisterWindowMessageW/A (>=0xC000),
   SHGetFolderPathW (honest E_FAIL), OpenSCManagerW (real SCM handle) +
   OpenServiceW (1060) + EnumServicesStatusExW (zero services) +
   CloseServiceHandle, NtOpenSymbolicLinkObject/NtQuerySymbolicLinkObject
   (real \Device\HarddiskVolumeN answers — killed the procexp RVA 0x9be71
   NULL-buffer deref class), SetRectEmpty (real write), IsWindow/
   IsWindowVisible (synthetic-window true), lstrcpyA/lstrcatA (safe-copy),
   SetDefaultDllDirectories, QueryDosDeviceW, version trio (honest size 0).

## Gate progress (named-stub evidence, vs SWEEP-003/007 baselines)

| program    | baseline            | now (MW_EULA_ACCEPT=1 -accepteula)                  |
|------------|---------------------|------------------------------------------------------|
| autoruns64 | exit 1 @ EULA gate  | exit 1 PAST EULA — frontier COMCTL32!#344 (SW-600)   |
| rammap64   | exit 1 @ EULA gate  | exit 1 PAST EULA + CreateDialogParamW — frontier COMCTL32!#17 (SW-600) |
| procexp64  | exit 2 @ color gate | exit 139 with 37 named stub calls deep in init (fonts/paths/SCM/NT symlinks) — frontier SW-601 |
| puttygen   | RIP=0 class         | exit 1 with 4 named stubs (SW-611)                   |
| cports     | crash               | 139 with 41 named stubs — frontier _strlwr (SW-603)  |

Default (no automation) runs keep the honest first-run behavior: the EULA
message prints and the program exits 1 — unchanged contract.

## MiniWinCalc — new GUI genre at 100% (user mandate «ماشین حساب»)

- `tests/games/miniwin_calc.c` — real Win32 GDI calculator: window lifecycle,
  WM_TIMER repaint cadence, WM_KEYDOWN digits/operators, GDI keypad + display,
  deterministic result (12+34=46, 56*7=392), clean PostQuitMessage exit.
- Built with zig cc (`python3 -m ziglang cc -target x86_64-windows-gnu`).
- `scripts/sweep008_calc_accept.sh`: 3x run — **exit 0 x3, 11/11 frames,
  ALL frames byte-identical SHA-256 across runs, final frame carries the
  green DONE marker**. Evidence: artifacts/sweep008/calc_acceptance/.
- Lesson re-confirmed: MW_GUI_KEYS is DECIMAL VK codes; a GUI app without a
  timer starves the v2 pump (keys are paced by MW_GUI_KEY_EVERY against the
  delivered-event counter).

## Mass breadth sweep (23 executables — all that exist on disk)

`scripts/sweep008_mass.sh` (parallel x8, 12s budget, -accepteula retry for
exit-1/low-stub programs) -> `artifacts/sweep008/mass/sweep008_mass.tsv`.

Exit-0: 7z, 7za, jq, sqlite3, snake, breakout, **calc** (+ diskmon64, produkey
with dialog-image paths armed). All other programs now show NAMED frontiers
(first stub + first unresolved per program) instead of anonymous stub-0 —
the progress metric the user demanded («تغییرات بدون پیشرفت ارزشی نداره»).

NOTE (SW-620): the original 100-program corpus is no longer on disk — only
the 15 SWEEP-007-reacquired GUI programs + opensource + games remain (23
exes). The worklist item tracks re-downloading from download_log.tsv and
re-running the sweep for the full 100-row delta table.

## Worklist ×3 (user mandate «ادامه سه برابری»)

`.agent/SWEEP50_WORKLIST.md`: 65 -> **216 items (x3.32)** — P0 frontiers
(SW-600..619), corpus breadth (SW-620..624), generated per-program items
(SW-625..647), API-surface families (SW-648..699), infrastructure
(SW-700..715), game/GUI portfolio (SW-716..725), hardening backlog
(SW-726..750).

## Scripts

- scripts/sweep008_gates.sh — gate matrix (3 programs x default/eula)
- scripts/sweep008_mass.sh — parallel mass sweep -> TSV
- scripts/sweep008_calc_accept.sh — MiniWinCalc 3x SHA acceptance
- scripts/sweep008_dump_imports.py — packed-PE import dumper (proves
  Sysinternals import obfuscation; loader traces are the ground truth)
- /home/z/my-project/scripts/corebt.py — core-dump backtrace symbolizer
