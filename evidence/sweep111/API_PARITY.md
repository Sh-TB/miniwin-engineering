# SWEEP-111 — API Parity Tracker: Wine vs MiniWin (fine-grained, WinRAR 7.12 GUI)

Continuation of `evidence/sweep110/ICON_PARITY.md` from the single icon family to the
FULL Win32 surface. This is the "exactly what does Wine load that MiniWin does not"
tracker, measured, not estimated.

## Inputs (both measured, no estimates)

| Side | Source | Scale |
|---|---|---|
| Wine | `artifacts/sweep033/winrar-gui.wine-dynamic-apis.json` (WINEDEBUG=+relay, wine-10.0, winrar-gui init session) | 872,988 calls, 602 distinct `module.Api` |
| MiniWin | `evidence/111/winrarinstd/sandbox/miniwin-results/WinRAR.exe/api_trace.json` (SWEEP-111 battery, binary 41e7036b) | ~10,146 traced `[API]` calls, 122 distinct |

## Module-level census (Wine side)

| module | Wine calls | Wine funcs |
|---|---|---|
| ntdll | 604,638 | 174 |
| ucrtbase | 102,019 | 52 |
| KERNEL32 | 93,606 | 139 |
| user32 | 65,880 | 42 |
| advapi32 | 2,749 | 34 |
| kernelbase | 1,462 | 44 |
| shlwapi | 1,187 | 8 |
| ntoskrnl | 350 | 31 |
| combase/rpcrt4/setupapi/msvcrt | 2,144 | 45 |
| shell32/imm32/win32u/gdi32/ole32/gdiplus/... | 412 | 18 |

ntdll/ucrtbase/msvcrt/ntoskrnl are CRT-internal in MiniWin (hosted in-process, no
per-call trace boundary), so the honest comparison plane is the 297 distinct Win32
APIs in the remaining modules.

## Headline numbers

- Wine Win32-level distinct APIs: **297**
- MiniWin reaches: **58** (19.5%) — and **0 unresolved imports** (all 509 guest imports
  resolve; the other Wine APIs are calls the guest does not make YET because it parks
  earlier, not engine gaps)
- Wine-reached-but-MiniWin-never-called: **239** — dominated by bulk string/locale
  phases Wine's session runs after the point where MiniWin's guest parks
  (CharLowerW 65,546, CompareStringOrdinal 42,318, lstrcmpiW 21,507, PathMatchSpecW
  2,138 — the resource-dir + path-matching scans of a full interactive session)

## The parking-point diff (where the guest stops vs Wine)

MiniWin's installed WinRAR now: decodes all 21 toolbar PNG glyphs (48x36 buttons +
16x16), reads the full `Toolbar\Buttons` state (40 `f.*`/`a.*` values, SW-1546 seed),
creates toolbar children (id=12/13/15), runs the TB_ENABLEBUTTON pass (23), creates
combo + statusbar + listview children, then parks in the WM_TIMER id=100 refresh loop
of its rebar manager.

Wine's session continues into: the full path-match scan (PathMatchSpecW x2138), the
locale bulk (LCMapStringW/CompareStringW), service-control queries, and the first
PAINT cycle with toolbar imagelist add. The imagelist add is the NEXT reachable
frontier (below).

## What each side does on the toolbar (measured, this round)

| Step | Wine | MiniWin (SWEEP-111) |
|---|---|---|
| Toolbar registry state | reads `HKCU\Software\WinRAR\General\Toolbar\{Layout,Buttons}` (written by WinRAR's own first GUI run on the prefix) | SW-1546 seed supplies the same state (40 values) — guest **consumes all 40** |
| PNG glyph load | 'PNG' named resources -> IStream -> GdipCreateBitmapFromStream | 21/21 decode -> GpBitmap -> HBITMAP (SWEEP-110 round 2) |
| Imagelist | ImageList_Create(48,36) + ImageList_Add x21 | guest never issues ImageList_Create (the default-layout path is gated by `Band76_0`/`ViewSmall` decode — next frontier) |
| Button add | TB_ADDBUTTONSW (0x444) | not reached headless |

## Registry value model (decoded from the guest binary strings this round)

Button ids: `add exto test view del find prn wiz cvt info exit rep extr vir cmt prot
lock SFX rpt bnc` (19). Value templates on `General\Toolbar\Buttons`: `f.<id>`
(button on file toolbar), `a.<id>` (archive toolbar), `f.<id>.sep` (separator after).
Layout key: `Band%d_%d` rebar bands (guest probed `Band76_0`), `ViewSmall`/`ViewMain`
icon-size flags on `General\Toolbar`. Seed fixture: `scripts/sweep111_seed.py`
(SW-1546), wired into `plugins/winrarinstd/plugin.tsv` via the new
`reg_seed_extend` manifest key (plugins/provisioning, SW-1546b).

## Gates this round (binary 41e7036be939ae6e)

- G1 PIN 12/12 (evidence/master/g1/20261030_175415)
- corpus_gate sweep111: 15 rows, rc=0 in 9, crash total 0
- winrar SFX: EXIT rc=0 x3
- winrarinstd: ALIVE_PUMP rc=124 x3 deterministic, frames=1, with the SW-1546 seed
  applied (evidence/111/winrarinstd)
- npp64: PASS_INPUT_STATE_CHANGE x3 (frames=28 uniq=13 — byte-identical to SWEEP-110)
- idman: rc=9 x3 deterministic (SW-1539 frontier unchanged)
- New this round: SW-1547 SHGetFileInfoW real shell icon DB (HKCR DefaultIcon probe +
  synthesized per-class glyphs; WinRAR's drive queries now resolve real HICONs),
  SW-1548 SHGetFolderPathW CSIDL_DESKTOPDIRECTORY/CSIDL_DESKTOP mapped
