# SWEEP-009 — honest-stub batch + post-batch gate map (2026-10-18)

Follow-on to SWEEP-008b (REA adopted; SW-555 gate identity). This sweep turns
the queued "cheap honest stubs" into real implementations and re-measures the
three gate programs 3x each under MW_GUI_EVENTS=1.

## Implemented (loader.c, all named per stub-call decode)
| import | contract | motivation (from SWEEP-008b decode) |
|---|---|---|
| GetVersionExW | fills OSVERSIONINFOW/EXW (Win10, NT, build 0 = same identity as GetVersionExA), returns BOOL | rammap64 lied 0 ELEVEN times (BOOL-false + untouched struct) |
| RegOpenKeyW | wraps RegOpenKeyExW -> ERROR_FILE_NOT_FOUND + SetLastError | autoruns64/rammap64 IAT; old stub returned 0 == lying SUCCESS with NULL handle |
| RegCreateKeyW | wraps RegCreateKeyExW -> ERROR_ACCESS_DENIED + SetLastError | EULA success-path writer |
| GlobalAddAtomW | REAL atom table 0xC000..0xFFFF, refcounted, dedup, hole-reuse | procexp64/rammap64 IAT; old stub returned atom 0 (invalid) |
| GlobalFindAtomW / GlobalDeleteAtom / GlobalGetAtomNameW | same table (find no-add; delete drops ref, frees at zero; get-name copies) | completeness of the Win32 string-atom surface |
| ColorRGBToHLS / ColorHLSToRGB | REAL Win32 0..240 HLS math (COLORREF 0x00BBGGRR) | procexp64's 36-caller 22%-luminance shading helper read BLACK for every color |

Honesty classes: wrappers inherit the established "no registry on host ->
real Win32 error code" contract; the atom table and HLS pair are REAL
user-mode implementations (honest by construction, not by failure).
CoInitializeEx stays an all-purpose-stub call in autoruns64 — returning 0 is
S_OK (success), which happens to be the correct answer.

## HLS verification (scripts/sweep009_color_roundtrip.c)
- Exhaustive RGB -> HLS -> RGB over all 2^24 colors: max channel deviation 4
  (inherent to the 0..240 Win32 scale — one hue unit shifts a high-delta
  channel by up to ~6.4 levels; real shlwapi has the same quantization),
  and **L error EXACTLY 0** across the whole space — luminance is the only
  component procexp64's shading helper consumes (FUN_14007baf0: L*22/100,
  clamp 240).
- The test caught a REAL bug pre-fix: integer hue division truncated instead
  of rounding (dev 2 at 0x000251); after the rounding fix, hue quantization
  is the only residual (documented, bounded, matches the Win32 scale).
- Shading sanity: 0xFF0000 -> 0x370000 (dark red) instead of 0x000000 (black).

## Post-batch stub-call map (runs in artifacts/sweep50/runs_stubrip9/, 3x each,
## ALL 3-run identical guest_rip sequences; exits unchanged 1/1/1, 1/1/1, 2/2/2)
- autoruns64: 6 -> 2 stub calls. Remaining: ole32!CoInitializeEx @0x14001be6d
  (0 == S_OK, benign), user32!DialogBoxIndirectParamW @0x140062d1c (EULA gate).
- rammap64: 16 -> 1 stub call. Remaining: DialogBoxIndirectParamW @0x14000438f
  (same EULA gate).
- procexp64: 20 -> 9 stub calls, ALL NEW SITES — the program progressed past
  the color-shading init into UI-resource init:
  GetSystemMetrics x4 @0x140001eb5..0x140001fab (stub 0 = zero-sized metrics),
  comctl32!ImageList_Create x2 @0x140001f0f/0x140001fab,
  comctl32!ImageList_Destroy x2 @0x140132d5e/0x140132d2e,
  kernel32!SetDefaultDllDirectories x1 @0x1400f29ec.
  The exit(2) decision is downstream of these — same comctl32/UI-surface
  family as SW-509/SW-556.

## Behavioral insight (REA, autoruns64 EULA function)
With RegOpenKeyW now honestly ERROR_FILE_NOT_FOUND, autoruns64 takes the
open-fail branch (0x140062976) and exits WITHOUT ever building/showing the
EULA dialog (the old stub-0 path reached the dialog and died there).
Implication: the EULA gate fix is NOT just DialogBoxIndirectParamW — the
guest requires the Sysinternals registry key to OPEN (then either an
EulaAccepted value or the dialog path). Honest options, in order:
  1. implement DialogBoxIndirectParamW AND have RegOpenKeyW succeed via a
     minimal in-memory registry hive (HKCU\Software\Sysinternals\<tool>) so
     the first-run dialog path is reachable; or
  2. a documented compat shim seeding EulaAccepted (policy decision — NOT
     taken this sweep; kept the registry honestly empty).

## G1 re-pin (loader.c changed)
- binary sha256 c06c5eb8c6c777a95174ef91f4d519eda998f360141302e4920ebf9944632bb9,
  UPX banner exact, regression 12/12 (evidence/master/g1/20261018_124314).

## Ops notes (recorded for future sweeps)
- Workspace rewind: between sessions the repo snapshot reverted to b783c44
  (losing 2ff4f40+6c5f651 from local history while GitHub retained them).
  Recovery = fetch + `git reset --hard origin/main`; pefile needed reinstall.
- Tool lesson: a MultiEdit batch that fails mid-way is NOT atomic in
  practice — verify symbol counts after multi-insert edits
  (scripts/sweep009_dedup*.py did the cleanup for this sweep's retry).
