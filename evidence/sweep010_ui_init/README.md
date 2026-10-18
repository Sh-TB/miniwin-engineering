# SWEEP-010 — procexp64 UI-init batch: GetSystemMetrics + ImageList_* + SetDefaultDllDirectories (2026-10-18)

Follow-on to SWEEP-009 (honest-stub batch). Implements the SW-556-family
"UI-resource init" sites procexp64 surfaced after SWEEP-009, and re-measures
the three gate programs 3x each under MW_GUI_EVENTS=1.

## Implemented (loader.c, 6 imports + 4 dispatch entries)

| import | contract | motivation (evidence) |
|---|---|---|
| user32!GetSystemMetrics | REAL Win10 x64 table, 1920x1080 single-monitor identity; no network stack / digitizer / terminal-server (honest 0s); undefined indexes return 0 like real Windows | procexp64 asks EXACTLY SM_CYSMCAPTION (50) and SM_CYSMICON (49) — decoded from the 4 call sites (sweep010_sm_indexes.json; `mov ecx,imm32` before each FF15) |
| comctl32!ImageList_Create / ImageList_Destroy | REAL handle-based image lists (MW_IMGL_BASE 0x00061001+i, 8 slots, cell-validated create, honest destroy) | procexp64 toolbar init created 2 lists (SWEEP-009 stub map @0x140001f0f/0x140001fab) and destroyed 2 in cleanup |
| comctl32!ImageList_Add / ImageList_GetImageCount | same family completeness: strip-bitmap add (w/cx cells, real pixel memcpy from any HBITMAP in the CreateCompatibleBitmap pool, 3-byte BGR stride), grow on demand | not yet called by the gates; proactively real so the next stub map names progress, not a lie |
| kernel32!SetDefaultDllDirectories | documented policy no-op -> TRUE (the guest loader keeps no LoadLibraryEx search-path state to configure; flags recorded in trace) | procexp64 call @0x1400f29ec (SWEEP-009 map); honesty class = CoInitializeEx stub-0 (benign, named, measured) |

## Self-test (scripts/sweep010_imglist_test.c — tests the REAL shipped code)

Links the actual loader translation unit (gcc, `-Dmain=loader_ship_main`)
and exercises the exported mw_* symbols directly: 29/29 PASS.
- GetSystemMetrics: 50->17, 49->16, screen 1920x1080, unknown -> 0.
- ImageList: create validation (cx/cy 0 and >256 -> NULL), fresh count 0,
  invalid handle/garbage HBITMAP adds -> -1, 3-image strip add -> first=0
  count=3, too-small bitmap -> -1, append -> index 3, non-multiple width
  (24 into 16) -> floor(w/cx)=1 cell, destroy lifecycle + stale-handle
  behavior, slot reuse after destroy.
- ABI lesson recorded: the mw_* symbols are __attribute__((ms_abi)); the
  test must declare them ms_abi or GCC marshals SysV-style and arguments
  garble (first run observed ImageList_Create(4,37,...) from rcx/rdx
  misalignment). Declaration-fixed, not code-fixed — the ship code was
  correct all along.

## Post-batch gate map (artifacts/sweep50/runs_stubrip10/, 3x each,
## ALL 3-run identical guest_rip sequences)

| program | stub calls | exit | remaining stub call(s) |
|---|---|---|---|
| procexp64 | 9 -> **1** | 2 -> **1** | user32!DialogBoxIndirectParamW @0x14005cd6c (IAT 0x140135340, sweep010_procexp_last_stub.json) |
| autoruns64 | 2 -> 2 | 1 | ole32!CoInitializeEx (0 == S_OK benign) + user32!DialogBoxIndirectParamW @0x140062d1c (EULA gate) |
| rammap64 | 1 -> 1 | 1 | user32!DialogBoxIndirectParamW @0x14000438f (EULA gate) |

**Headline: the exit(2) frontier in procexp64 — whose decision SWEEP-008b
could only attribute to "a RESOLVED API downstream of the color init" — is
BROKEN THROUGH.** With honest metrics (16/17 vs 0/0) and real image lists,
procexp64 completes UI-resource init, cleans up its lists via the now-real
ImageList_Destroy, and lands on the same named blocker as the other two
programs. All three Sysinternals gate programs now converge on exactly ONE
stub import: **DialogBoxIndirectParamW** (plus autoruns' benign CoInitializeEx).

## Next (single highest-leverage item)

DialogBoxIndirectParamW + in-memory DLGTEMPLATE parsing (+ the minimal
in-memory registry hive for HKCU\Software\Sysinternals\<tool> so autoruns'
first-run path is reachable, per the SWEEP-009 policy note) = SW-509/SW-559
family. One dialog implementation stands between MiniWin and all three
Sysinternals gates.

## G1 re-pin (loader.c changed)

- binary sha256 df28de0f9d0a1146c6e2d662cd73e6e960943b336aaf2887b1a10a212af23d89,
  UPX banner exact, regression 12/12 (evidence/master/g1/20261018_130104).
- Gate battery re-run AFTER the pin with the pinned binary: identical results
  (procexp 1 stub / exit 1; autoruns 2; rammap 1; 3-run identical x3).

## Ops notes

- Placement bug caught by build: the ImageList block was first inserted before
  MWBmp/MW_HBITMAP_BASE/g_bitmap_* exist in the file (build error), and the
  first move script spliced it into mw_BitBlt's MULTILINE signature. Fixed by
  forward-scanning for the standalone closing brace of
  mw_CreateCompatibleBitmap (scripts/sweep010_move_block.py,
  scripts/sweep010_fix_placement.py; both kept as recoverable artifacts).
- mw_BitBlt has a multi-line signature — never anchor edits on the first line
  of a function with a wrapped parameter list.
