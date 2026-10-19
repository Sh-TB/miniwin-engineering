# SWEEP-008 — honest-stub elephant batch (one edit, five families, two gates passed)

## What landed

One batch of loader changes implementing five named stub families derived
from the SW-555 guest_rip gate evidence, plus four loader bugs fixed along
the way, plus a ~230-import policy-stub landing for the WARN-list union of
autoruns64/rammap64/procexp64.

| change | id | effect |
|---|---|---|
| GetVersionExW/A real fill (10.0.19045, VER_NT_WORKSTATION) + ntdll!RtlGetVersion | SW-563 | version checks pass honestly |
| minimal in-memory registry (Open/Create/Set/Query/Close, W + W-no-Ex) | SW-564 | EULA round-trip is real: RegOpenKeyW(missing)->FILE_NOT_FOUND -> dialog -> RegCreateKeyExW -> RegSetValueExW("EulaAccepted") |
| global atom table 0xC000+ | SW-565 | GlobalAddAtomW returns real atoms |
| shlwapi ColorHLSToRGB/ColorRGBToHLS (HLSMAX=240 canonical math) | SW-566 | pure-math parity |
| DialogBoxIndirectParamW/DialogBoxParamW DECLARED auto-accept -> IDOK | SW-567 | EULA gate unlocked |
| named-stub machinery (site recording with COPIES + return-address decode) | SW-555 | every future gate prints `dll!name` for free |
| BUG-068 load_pe ftell=-1 (fopen-on-directory) SIGSEGV | fix | clean error instead of crash |
| BUG-070/070b GetModuleHandleW named lookups (pseudo-handle parity + UTF-16 read) | fix | GetProcAddress(GetModuleHandleW(L"ntdll"),"RtlGetVersion") chain works |
| BUG-071 stub-site UAF (pointers into freed filedata) | fix | libc vsnprintf crash eliminated |
| BUG-072 GetProcAddress MAKEINTRESOURCE ordinals | fix | ordinal 135 -> real handler; %s-of-small-int crash gone |
| batch-2/3/4 policy stubs (~230 imports, 12 policies + ~35 real impls) | SW-5xx | autoruns64/rammap64 reach 0 unresolved |

## Gate results (exit codes before -> after)

| program | before | after | unresolved after | note |
|---|---|---|---|---|
| autoruns64 | 1 | **0** | 0 | EULA flow completed (dialog accept + registry write) |
| rammap64   | 1 | **0** | 1 (COMCTL32 ordinal) | full EULA flow, exit 0 |
| produkey   | crash/exit-non0 | **0** | 70 | GUI utility completes headless run |
| diskmon64  | 0 | 0 | 39 | held |
| procexp64  | 2 (clean) | 139 guest | 0 | DEEPER: now crashes in guest code at RIP=0x14009be71 after full UI init + 3 MessageBox renders (SW-568 lever documented) |
| putty/pageant/procmon64 | 1 | 1 | 34/34/93 | 3 dialog images each, unchanged exits |
| zoomit64   | 1 | 1 | 0 | TRAP133 area, decision now from resolved APIs (SW-578) |

## Breadth sweep (15 programs, artifacts/sweep50/runs_sweep008/summary.tsv)

4x RAN-EXIT0 (autoruns64, rammap64, produkey, diskmon64), 5x RAN-EXIT1/9,
6x CRASH-SEGV (each with a named crash site + 0-70 unresolved imports).
Full per-program rows in breadth_summary.tsv; machine-readable caller
decoding in sweep008_stub_callers.json.

## GUI evidence

- procexp64 renders THREE MessageBoxW dialog BMPs before its crash
  (miniwin-results/procexp64.exe/screenshots/messagebox_1..3.bmp in trace
  runs) — real Sysinternals UI art from a real guest.
- putty/pageant/procmon64 dialog-image path unchanged (3 dialogs each).
- produkey exits 0 headless; under MW_GUI_EVENTS=1 it hits the KNOWN
  SWEEP-007 comctl32 cliff at 0x140022f24 (SW-579/580 levers).

## Reproduce

```
make
for p in autoruns64/Autoruns64.exe rammap64/RAMMap64.exe procexp64/procexp64.exe; do
  ./minwin_loader artifacts/sweep50/dl/$p; echo "exit=$?"
done
scripts/sweep008_breadth.sh          # 15-program breadth batch
scripts/sweep008_decode.py           # guest_rip -> dll!import decoder
scripts/g1_accept.sh                 # G1 pin (12/12, sha 6c14f04c...)
```

## G1

Re-pinned after the loader changes: 12/12 regression, UPX banner exact,
sha256 6c14f04cd18a1583c5f86f70b502d35ce792ab10e9f4d32a9e62ea5afc61ad44
(evidence/master/g1/20261019_172606).
