# SWEEP-007 — SW-562 event-pump battery (2026-10-18)

User directive: (continue) (continue). SW-562 was the next open worklist item:
run every SWEEP-003 GUI program under the real event pump (MW_GUI_EVENTS=1)
and map which ones survive real message delivery — the honest post-SWEEP-005/006
frontier measurement.

## Re-acquisition (the dl/ tree was lost to the earlier ENOSPC cleanup)
- `scripts/sweep007_redownload_gui.py` re-downloaded the 16-program set from the
  same official endpoints as SWEEP-003 (NirSoft x64 zips, PuTTY w64, Sysinternals
  zips, WinMerge GitHub x64-exe). **15/16 acquired**; every exe pefile-verified
  (14 GUI-subsystem + 1 console — Coreinfo64.exe, correct).
- **ADExplorer is UNOBTAINABLE**: `download.sysinternals.com/files/ADExplorer.zip`
  returns 404 — Microsoft retired the download. SW-553's crash analysis now has
  no reproducible binary from the official source; recorded in the worklist.
- Manifest with per-exe SHA-256: `artifacts/sweep50/sweep007_redownload_manifest.tsv`.
- Binary-identity spot checks: cports v1 (no pump) reproduces SWEEP-003's exact
  crash site 0x14001b338, and putty still renders the identical WinSock error
  dialog — the re-downloaded set is behaviorally the SWEEP-003 set.

## Battery design (`scripts/sweep562_battery.sh`)
- 15 programs x3 runs each, `MW_GUI_EVENTS=1` (pump v2, default budget 400),
  no scripted keys — pure survival mapping, 120 s timeout per run.
- Classification markers come from the per-run `api_trace.json` (MW_TRACE lands
  there, NOT stderr — stderr only carries [WARN]/[STUB]/[CRASH]).
- Marker set: `event pump v2 ARMED` (pump entered), `-> WM_PAINT` count (real
  paint deliveries), `event budget exhausted | no event source` (quit path),
  process exit code, screenshot frame count.
- Raw data: `battery_raw.tsv` (105 rows), per-run artifacts under
  `artifacts/sweep50/runs_gui562/<name>/run{1,2,3}/`.

## Result map (15/15 programs 3x-deterministic — exit code, paints, frames)

| program | SWEEP-003 v1 (no pump) | SWEEP-007 pump v2 | reading |
|---|---|---|---|
| produkey | exit 0 (v1 contract) | **CRASH 0x22f24, armed=1** | pump delivers synchronous WM_CREATE; produkey's WM_CREATE handler creates comctl32 child controls through unresolved stubs (CreateToolbarEx/ImageList_*) and faults on a NULL return at stub call #4. Under v1 the same binary exits 0 (9 stub calls, immediate WM_QUIT). The SWEEP-003 "GUI lifecycle" label was a v1-contract artifact, not a real message loop. |
| cports | CRASH 0x14001b338 | **CRASH 0x140002119, armed=1** | same binary (v1 site reproduces exactly); the pump moves the fault EARLIER into the WM_CREATE handler — the old post-init site (SW-551) is never reached. |
| procmon64 | exit 0 | exit 0, **armed=0** | runtime trace shows RegisterClassExW x2 then a clean exit — NO runtime CreateWindowExW/GetMessageW at all (the `[175] CreateWindowExW` lines in the trace are import-resolution entries, not calls). Both exit-0 paths are pre-window. |
| diskmon64 | exit 0 | exit 0, armed=0 | same class as procmon64 (RegisterClassA only). |
| coreinfoex64 | exit 0 | **exit 1** | not a regression: today's Coreinfo.zip contains a NEWER Coreinfo64.exe (console). This is exactly SW-561's version-drift prediction (needs SetThreadGroupAffinity/GetProcessWorkingSetSize/version.dll trio). |
| putty | exit 1 + dialog image | exit 1 + **messagebox_1.bmp** | 2nd independent confirmation of the SWEEP-004 raster path ("Unable to load any WinSock library" dialog). |
| pageant | exit 1 | exit 1 + **messagebox_1.bmp (NEW)** | FIRST pageant image: "Pageant Fatal Error — Unable to access security APIs. Pageant will not run, in case it causes a security brea[ch]" — the guest's own text rendered by MiniWin. |
| procexp64 | exit 2 | exit 2 | named-stub gate unchanged (SW-555). |
| autoruns64 | exit 1 | exit 1 | named-stub gate unchanged (SW-555). |
| rammap64 | exit 1 | exit 1 | named-stub gate unchanged (SW-555). |
| puttygen | CRASH RIP=0 | CRASH RIP=0 | unchanged (SW-552). |
| searchmyfiles | CRASH 139 | CRASH 0x140028001 | same class (SW-557/558 family). |
| zoomit64 | TRAP 133 | TRAP 133 | C++ unhandled family (SW-553 class). |
| bginfo64 | TERM 9 | TERM 9 | C++ unhandled family. |
| winmergeu | TERM 9 | TERM 9 | C++ unhandled family. |
| adexplorer64 | TRAP 133 | **UNOBTAINABLE** | official download retired (404). |

Image evidence: `putty_dialog_minwin.png`, `pageant_dialog_minwin.png` (2x
nearest upscale of the guest's own dialogs), `battery_map.png` (this table as a
chart).

## Engineering conclusions

1. **The pump's WM_CREATE contract is correct Win32 semantics and it is a real
   compatibility cliff.** In real Win32, WM_CREATE is sent synchronously inside
   CreateWindowEx. Programs that build comctl32 child controls in WM_CREATE
   (produkey) fault on our honest stub returns. Fixing produkey's pump path
   requires child-control emulation or the SW-509 PE-DLL loader (comctl32.dll
   as a real module) — SW-559's open remainder is confirmed by measurement.
2. **procmon64/diskmon64 "GUI lifecycle" labels from SWEEP-003 were overly
   generous.** Neither ever calls CreateWindowEx/GetMessage at runtime; they
   register classes and exit cleanly. The honest stage for both is
   PRE-WINDOW-CLEAN-EXIT, now recorded in the map.
3. **The 4 named-stub gate programs (procexp64/autoruns64/rammap64 + puttygen's
   NULL call) are the dominant survival blocker** — 4 of 15 crash/exit on the
   same class of unresolved-import return. SW-554/555 isolation is the highest
   leverage next step for this program set.
4. **Version drift is now a tracked phenomenon** (Coreinfo64 SW-561 confirmed;
   ADExplorer retired). Every future re-acquisition must SHA-pin its exes — the
   redownload manifest does this.

## Gates kept green
- Clean rebuild; `scripts/g1_accept.sh` PIN OK before the battery:
  binary sha256 `791334c827a0292f2a8ac19f771805d2b04efbd16cb10cb28fd37ad24b0501bc`
  (byte-identical to the SWEEP-006 recorded build — reproducible), UPX banner
  exact, regression 12/12, no [CRASH] in G1 stderr.

## Files
- `scripts/sweep007_redownload_gui.py` — re-acquisition + SHA manifest
- `scripts/sweep562_battery.sh` — the battery (15 x3, classification, determinism)
- `scripts/sweep562_images.py` — PNG evidence + battery map
- `battery_raw.tsv` — 105-row raw classification
- `artifacts/sweep50/runs_gui562/` — per-run stdout/stderr/trace/exit
- `artifacts/sweep50/sweep007_redownload_manifest.tsv` — SHA-256 of every exe
