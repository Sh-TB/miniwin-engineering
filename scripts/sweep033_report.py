#!/usr/bin/env python3
"""SWEEP-033 report generator — API_BASELINE.md from campaign.json."""
import json
from collections import Counter
from pathlib import Path

REPO = Path("/home/z/my-project/workspace/miniwin-engineering")
ART = REPO / "artifacts" / "sweep033"
c = json.load(open(ART / "campaign.json"))
apps = c["apps"]
ranked = c["base_api_ranked"]

L = []
L.append("# SWEEP-033 — Level-0 Wine Relay Campaign: BASE API derived from 22 real programs\n")
L.append("Date: 2026-10-30. Method = the user's own level-0 methodology: pass every famous "
         "program through a level-0 tracker (Wine `WINEDEBUG=+relay`, wine-10.0 WineHQ noble "
         "build at `tools/wine-10.0`), capture **every call**, then compare with MiniWin's "
         "loader-resolved import sets and derive the base API that a Windows-compatible "
         "engine must implement.\n")
L.append("## Corpus (22 real Windows programs, sha256-pinned)\n")
L.append("| app | kind | wine rc | relay distinct | relay total | miniwin rc | static imports resolved | surface APIs | surface calls |")
L.append("|---|---|---|---|---|---|---|---|---|")
for name, rec in apps.items():
    w, m = rec["wine"], rec["miniwin"]
    L.append(f"| {name} | {rec['kind']} | {w['rc'] if w['rc'] is not None else 'TO'} "
             f"| {w['api_calls_distinct']} | {w['api_calls_total']:,} "
             f"| {m['rc'] if m['rc'] is not None else 'TO'} "
             f"| {m['resolved_distinct']}/{(m['import_summary'] or {}).get('total','?')} "
             f"| {rec['surface_distinct']} | {rec['surface_calls']:,} |")
L.append("")
L.append("Honest notes:\n")
L.append("- **GUI-INIT** apps ran headless (no display server in sandbox): the relay covers "
         "process init + window-creation attempt (wine `nodrv` path). Console apps are full runs.\n"
         "- **surface** = dynamic ∩ static: APIs the app both imports and actually called under "
         "wine. **infra** = dynamic − static: wine runtime internals + GetProcAddress-discovered "
         "calls (caller-PC→module attribution is future work; 2 MB relay samples per app are "
         "committed for that mining).\n"
         "- MiniWin resolved **100% of static imports for all 22 apps** this round "
         "(unresolved=0 everywhere) — the gap is no longer import resolution; it is behavioral "
         "fidelity of the resolved APIs (see rc column: putty/rammap64 SIGSEGV 139, "
         "notepad++ frontier rc=9, pageant pump-hang).\n")

L.append("## BASE API — ranked by how many programs exercise it\n")
L.append(f"Union of surfaces: **{len(ranked)} APIs**. Top of the list = the mandatory core.\n")
L.append("| # | API | apps | total calls |")
L.append("|---|---|---|---|")
for i, r in enumerate(ranked[:60], 1):
    L.append(f"| {i} | `{r['api']}` | {r['n']} | {r['calls_total']:,} |")
L.append("")
L.append("## Per-DLL surface size\n")
per_dll = c["per_dll_surface"]
L.append("| DLL | surface APIs |")
L.append("|---|---|")
for k, v in sorted(per_dll.items(), key=lambda kv: -kv[1]):
    L.append(f"| {k} | {v} |")
L.append("")
L.append("## Artifacts\n")
L.append("- `campaign.json` — full dataset (per-app surface/infra, ranked base API)\n"
         "- `<app>.wine-dynamic-apis.json` — per-app level-0 dynamic call counts\n"
         "- `<app>.miniwin-apitrace.json` — miniwin loader trace per app\n"
         "- `matrix-app-x-dll.tsv` — app × DLL matrix\n"
         "- per-app relay samples + events: `/home/z/tmp/sweep033/<app>/` (raw logs deleted "
         "after parse; disk budget)\n")
L.append("## Immediate MiniWin worklist derived from this baseline\n")
L.append("1. putty rc=139 and rammap64 rc=139 — same crash class (SW-1325/1326 registered); "
         "level-0 traces now show the last surface APIs before death.\n"
         "2. notepadpp rc=9 — MFC C++ EH frontier (SW-1201 family), 71 surface APIs exercised.\n"
         "3. coreinfo64 rc=1 — console tool that fully runs under wine; its surface (40 APIs) "
         "is a compact behavioral target.\n"
         "4. WinRAR GUI: surface 108 APIs incl. GDIPLUS.GdiplusStartup family — the GUI path "
         "is exercised at level 0 exactly as designed.\n")
(ART / "API_BASELINE.md").write_text("\n".join(L))
print("wrote", ART / "API_BASELINE.md")
print("top5:", [r["api"] for r in ranked[:5]])
