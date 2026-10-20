# SWEEP-028 «ادامه یوز قطار فیل ۱۰۰ تسک جوا و پوش» (2026-10-20)

Wagon batch: SW-1101 winmergeu crash decode + cross-program unresolved tails.

## Frontier decodes (evidence-driven)
- **SW-1101a ROOT CAUSE (winmergeu 139 @ RVA 0x12d0c2)**: WinMerge requests a
  32bpp TOP-DOWN DIB (biHeight<0) via CreateDIBSection; old code computed
  (size_t)w*(size_t)h -> size_t underflow -> calloc failed -> silent NULL ->
  SelectObject(dc, NULL) seen in trace -> RGBA loop wrote at NULL+0x154.
  ALSO: pool allocated 3B/px regardless of requested bpp (32bpp heap overflow).
  FIX: MWBmp view format (vbpp/vtop/vstride), format-aware mwb_get/mwb_set over
  all raster consumers (BitBlt/StretchBlt/GetPixel/PatBlt/ImageList/Get+SetDIBits/
  StretchDIBits), zeroed calloc by honest stride. Verified: 16x-17 32bpp topdown
  DIBs allocate (stride 64); crash site retired.
- **SW-1101b (winmergeu 139 @ RVA 0x2b8de0)**: OLEAUT32!LoadTypeLib — winmergeu
  binds OLEAUT32 BY ORDINAL (ILT raw dump 0x8000..28). Old fixed map lacked
  161 -> stub returned 0 (fake S_OK) -> guest deref *pptlib NULL. Ordinal map
  completed from pefile ord db cross-check (5/11/12/15/19/20/23/24/40/94/161/
  184/185/200/201/202) + typed-honest LoadTypeLib (TYPE_E_CANTLOADLIBRARY).
  RESULT: winmergeu 139 -> 9 (controlled stop, deep advance).

## Unresolved census (pump-off, this binary)
| program | SWEEP-027 | SWEEP-028 |
|---|---|---|
| winmergeu | 150 (139) | **0** (exit 9) |
| procexp64 | 128 (139) | **0** (139, BUG-078 class frontier) |
| bginfo64 | 145 (0) | **54** (0) |
| autoruns64 | 26 (0) | **0** (0) |
| produkey | 10 (0) | **0** (0) |
| cports | 6 (0) | **0** (0) |
| diskmon64 | 5 (0) | **0** (0) |
| rammap64 | 4 (0) | **0** (0) |
| searchmyfiles | 2 (0) | **0** (0) |

Totals: 464 -> 54 unique unresolved (-410). Four programs reach zero-unresolved
(produkey/diskmon64/rammap64/searchmyfiles), autoruns64 joins.

## Batch content
~155 imports landed (352 ENTRY lines): OLEAUT32 typed family + SafeArray/error-info/
Var* conversion set; shlwapi real path surgery (PathStripToRoot/RemoveFileSpec/
Combine/MatchSpec*/FileExists real stat/IsDirectory/UNC/CreateFromUrl); ActCtx
typed quintet; Ole clipboard/drag/menu-descriptor tails; GDI map-mode/extents/
region/state family; USER32 window tails (GetTopWindow/GetLastActivePopup/
GetNextDlgTabItem/LockWindowUpdate/scroll store/...); WTS single-console session;
Toolhelp32 Process/Module32FirstW real; Nt* typed NTSTATUS; LSA/token tails;
service config tails; GDI+ cookie objects (35 funcs); SetupDi/CredUI/cert typed;
real CopyFileW/GetCompressedFileSizeW/ExpandEnvironmentStringsA/GetStringTypeExW/
CompareStringA; RegDeleteTreeW real recursion; LoadBitmapW routed to LoadImageW.

## Gates (3x discipline)
- G1 PIN OK sha 2bfe6fc3 regression 12/12 (evidence/master/g1/20261020_105600)
- corpus x3 16/16 deterministic, ZERO regressions (sweep028_sweep.tsv)
- exit0 programs: 11 (+ winmergeu 139->9 advance)

## Next frontier
- SW-1102: procexp64 past 0-imports frontier (jump-into-string class, BUG-078 kin).
- winmergeu exit-9 stop decode (controlled, post-type-lib path).
- bginfo64 54-tail (exit0 non-fatal) — next wagon.
