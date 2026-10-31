# SWEEP-021 — (continue dozens of tasks at once) elephant batch (SW-881..920 remainder + GUI accounting)

Date: 2026-10-19 (UTC). Loader change: ONE batch — 24 new functions + 35 ENTRY rows.
Build: `minwin_loader` 1,771,176 -> 1,836,696 bytes (+65,520). One `make`, zero errors.

## User question answered with measured data ((did it manage to run anything completely? five graphical programs?))

**YES — 6 real GUI-subsystem programs run to completion (exit 0), x3 deterministic:**

| program | PE subsystem | exit x3 | stderr x3 | unresolved |
|---|---|---|---|---|
| autoruns64 | GUI | 0/0/0 | identical | 151->145 |
| bginfo64 | GUI | 0/0/0 | identical | 310->303 |
| cports | GUI | 0/0/0 | identical | 49->47 |
| diskmon64 | GUI | 0/0/0 | identical | 21->21 |
| produkey | GUI | 0/0/0 | identical | 39->39 |
| rammap64 | GUI | 0/0/0 | identical | 39->39 |

Plus in-house GUI battery (all PASS after the batch):
- miniwin_calc acceptance **9/9** (3 scenarios x3, arithmetic verified in stdout)
- snake **30 frames x3 byte-identical, exit 0 x3**
- breakout **381 frames x3 byte-identical, 40->13 bricks (27 destroyed), exit 0 x3**

=> **9 GUI programs** with full lifecycle evidence (6 real + 3 in-house). Evidence table:
`artifacts/sweep021/GUI_EVIDENCE.tsv` (PE-subsystem parsed from the optional header).
coreinfoex64 is the only console program in the corpus (exit 0, 0 unresolved).

## What landed (SW-881..920 remainder, 35 imports in one batch)

- **SW-881..885 menu deepening**: InsertMenuItemA/W (MENUITEMINFO x64 @0/4/8/12/16/24/56/64,
  cbSize>=72, positional insert with shift), RemoveMenu (submenu kept alive), DeleteMenu
  (submenu destroyed via mw_DestroyMenu), GetMenuItemRect (synthetic 120x18 rows),
  TrackPopupMenuEx (honest no-user dismissal: 0, SetLastError(0)).
- **SW-886..890 ListView deepening**: LVM_GETNEXTITEM (LVNI_ALL linear walk / typed -1 for
  selection flags), LVM_SETITEMA/W, LVM_DELETEALLITEMS, LVM_GETSUBITEMRECT (RECT* contract:
  subitem index in rect.left, synthetic 120x16 grid), LVM_GETSTRINGWIDTHA/W (80).
- **SW-891..895 TreeView/Tab deepening**: TVM_GETITEMA/W (TVIF_TEXT empty label, state 0),
  TVM_SELECTITEM (g_tree_sel store + TVGN_CARET readback in GETNEXTITEM), TVM_EXPAND,
  TCM_GETITEMA/W, TCM_ADJUSTRECT (display 2,26,400,300 / window 0,0,400,26).
- **SW-896..900 GDI regions**: ExtCreateRegion (RGNDATAHEADER x64, rects @+32,
  RDH_RECTANGLES==1 typed refusal), GetRgnBox (real bbox + complexity), OffsetRgn,
  PtInRegion (exact point scan), EqualRgn (order-insensitive rect-set equality).
- **SW-901..905 shell**: SHGetFolderPathA (A-twin), SHGetKnownFolderPath (8 well-known
  GUIDs incl. RoamingAppData/LocalAppData/ProgramData/Windows/ProgramFiles/Documents/
  Profile/LocalAppDataLow, CoTaskMemAlloc'd, typed E_FAIL unmapped), SHGetDesktopFolder
  (minimal IShellFolder: QI IID_IUnknown+IID_IShellFolder real, SFGAO_FOLDER|FILESYSTEM,
  empty enumerator, E_NOTIMPL elsewhere), SHAutoComplete (vacuous S_OK),
  SHSetValueA/W (routed through the real registry model: CreateKeyExW+SetValueExW+Close).
- **SW-906..910 RICHEDIT**: class family registered (0x8C; prefix "richedit" covers
  RICHEDIT/RichEdit20W/RICHEDIT50W in sysclass tables + atom range 0x80..0x8C) with
  EM_SETTEXTEX (codepage-aware), EM_STREAMIN (real guest EDITSTREAM callback invocation,
  64x1KB drain, 127-wchar text slot, dwError contract), EM_STREAMOUT (drain stored text),
  EM_GETTEXTEX (codepage-aware). Text lives in the child slot so WM_GETTEXT works.
- **SW-916..920 CRT/COM**: _mbsncat, _mbsnbcpy (MSVC zero-pad), _mbslen (CP932 lead-byte
  pairs), _splitpath (real drive/dir/fname/ext parse), _makepath (real concat +
  separator normalization), CoGetObjectContext (CO_E_NOTINITIALIZED vs E_NOTIMPL typed),
  CoFreeUnusedLibraries, StringFromGUID2 (real formatter, 39 incl. NUL).

## Measured

- 15-program corpus x3: **exit-code deterministic for all 45 runs**; 7 exit0 / 4 exit139 /
  4 exit1 — **zero regressions vs SWEEP-019**.
- Total unresolved imports 1325 -> 1288 (**-37**): the new entries are on REAL call paths
  (autoruns64 -6, bginfo64 -7, procexp64 -4, winmergeu -6, procmon64 -5, searchmyfiles -4,
  cports -2, pageant -2, putty -1).
- G1 re-pin: **OK** — regression 12/12, UPX banner exact, no crash dumps,
  `evidence/master/g1/20261019_181613/`.
- TSV: `artifacts/sweep021/sweep/sweep021_sweep.tsv` (chunked via `scripts/sweep021_chunk.sh`).

## Still open (unchanged, honest)

- exit139: puttygen (SW-807 NULL fn-ptr), searchmyfiles (SW-814 cmpw 0x22), winmergeu
  (SW-815 SSE float path), procexp64 (SW-809 UTF-16-as-code) — crash signatures recorded.
- exit1: pageant, procmon64, putty, zoomit64 (SW-553 TRAP133 family) — next batch targets.
- produkey pump-on exit1 (SW-881..885 now landed; verify with MW_GUI_EVENTS=1 next sweep).

## SW-921..960 — continuation queue (40 items, (triple-parity continuation) standing order)

See `.agent/SWEEP50_WORKLIST.md` — dialog deepening x5, ListView final x5, TreeView final
x5, GDI DIB x5, fonts x5, network x5, registry x5, CRT/COM x5.
