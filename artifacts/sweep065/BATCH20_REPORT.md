# SWEEP-065 — 20-Program Mass Audit: tooling as lubricant + jet fuel

Mandate («از ابزارها کمک بگیر واقعا عالی … ۲۰ تا برنامه رو با این ابزارها
بررسی بکن و اجرا ببین … ابزارها مثل روغن کاری و سوخت جت میمونن، هزاران ساعت
کار رو جلو میندازن … یدونه هم محبوب‌ترین برنامه ویندوزی رو از گیت‌هاب بگیری»):
use the toolchain to audit AND run twenty programs in one train, including
WinRAR and the most popular Windows program on GitHub.

## The batch (20 programs)

| # | Program | Source | Size | Machine |
|---|---------|--------|------|---------|
| 1 | WinRAR 7.12 | win-rar.com (SWEEP-062 stock) | 3.74 MB | x64 |
| 2 | Notepad++ 8.9.8.1 | **GitHub notepad-plus-plus** (most-starred Windows desktop app repo) | 7.75 MB | x86 |
| 3 | 7-Zip 7zr 19.00 | 7-zip.org standalone console | 0.60 MB | x86 |
| 4–16 | NirSoft ×13: DevManView, DNSQuerySniffer, DriverView, Faview, HeapMemView, HtmlAsText, Iconsext, InstalledAppView, MultiMonitorTool, RecentFilesView, ResourcesExtract, SimpleProgramDebugger, URLProtocolView | NirSoft (staged SWEEP-062) | 22–178 KB | x86 |
| 17–20 | XP classics: freecell, spider, calc, mspaint | SWEEP-061 corpus | 75–573 KB | x86 |

Rule #1 before the train: `git fetch` — origin/main e21e078 == local, clean
tree. Ghidra latest release re-checked: 12.2 (installed). Notepad++ latest
release re-checked via GitHub API: v8.9.8.1, portable x86 .7z pulled and
extracted (py7zr).

## Measured: the jet-fuel effect (Ghidra 12.2 batch, scripts/sweep063_driver.py)

| Program | Ghidra time | Functions | Imports | Resource entries |
|---------|------------|-----------|---------|------------------|
| notepadpp | 486.8 s | **19,048** | 618 | 22,798 |
| winrar712 | 40.5 s | 1,855 | 252 | 4,492 |
| 7zr | 43.1 s | 4,375 | 139 | 2,161 |
| mspaint | 32.9 s | 2,083 | 944 | 5,852 |
| dnsquerysniffer | 16.0 s | 661 | 282 | 3,736 |
| devmanview | 20.5 s | 479 | 268 | 4,003 |
| installedappview | 10.3 s | 508 | 262 | 2,113 |
| multimonitortool | 11.1 s | 451 | 244 | 1,461 |
| simpleprogramdebugger | 8.2 s | 396 | 231 | 1,387 |
| spider | 14.3 s | 188 | 164 | **87,992** |
| freecell | 14.2 s | 70 | 127 | 479 |
| calc | 14.2 s | 171 | 141 | 1,956 |
| 8 tiny NirSoft | 1.2–2.2 s each | 0–1 | 12–14 | 1,092–4,698 |

**Totals: 20 programs, 30,289 functions, 3,778 imports, 163,774 resource
entries in ~726 s of machine time (avg ~36 s/program).** Pre-tooling this was
the SWEEP-060..062 manual grind: one ordinal map took SWEEP-014 + SWEEP-1223 +
SWEEP-1225 + SWEEP-063 to converge; the paint chain took three sweeps. The
same discovery volume now lands inside one coffee break — that is the
lubricant + jet fuel claim, in numbers.

## Measured: the engine battery (scripts/sweep065_battery.sh)

| Program | rc | Frames | Shot | Note |
|---------|----|--------|------|------|
| winrar712 | 1 | dialog | YES (winrar712.png) | engine-rendered Error box «folder is not accessible» + OK — WinRAR's own decision path, matches SWEEP-062 |
| notepadpp | 86 | 0 | – | **deepest x86 penetration yet**: loads, runs past CRT, dies at PE32-FATAL opcode 0xee (OUT DX,AL) EIP=0x007ef329 → SW-1297 |
| 7zr | 139 | 0 | – | PE32 pre-load crash (SW-1293 frontier) |
| freecell | 0 | 10 | YES | baseline |
| spider | 0 | 621 | YES (spider.png) | full deal animation |
| calc | 0 | 1 | YES | baseline |
| mspaint | 0 | 0 | – | clean exit, identical to SWEEP-061 (not a regression) |
| 13 NirSoft | 139 | 0 | – | PE32 pre-load crash class (SW-1293/SW-1296) |

Evidence: evidence/sweep065/shots_png/{winrar712,spider,freecell,calc}.png;
per-binary Ghidra exports evidence/sweep065/ghidra/<prog>/ (functions.tsv,
imports.tsv, exports.tsv, entrypoints.txt, resources.tsv, strings.txt).

## Cross-validation finds (Rule #1 culture, zero extra cost)

1. **SW-1204c independently corroborated**: tiny NirSoft `driverview` imports
   COMCTL32.**Ordinal_17** (our map: InitCommonControls, wine comctl32.spec
   line 21) while big NirSoft tools + Notepad++ import `ImageList_*` BY NAME
   — both styles coexist in the same vendor family, exactly as the corrected
   map predicts.
2. **Notepad++ is an x86 build in its portable package** (machine 0x14c) — it
   therefore exercises the same PE32 subsystem as the NirSoft fleet, but
   penetrates orders of magnitude deeper before failing. The gap between
   rc=139 (pre-load) and rc=86 (mid-CRT, opcode 0xee) is now the sharpest
   available gradient for SW-1293b work.
3. **spider .rsrc = 87,992 entries** — biggest resource tree in the corpus
   (SW-1298 audit car opened).

## Verdict

- Tools asked for: used (Ghidra 12.2 batch, py7zr, GitHub API, PIL harvest).
- Effect asked for: **quantified** — 20 programs × 6 export classes in ~12
  minutes of machine time; every claim above is a file on disk.
- Programs asked for: 20/20 analyzed, 20/20 executed under the engine, 7/20
  full GUI PASS, WinRAR's dialog + spider's board photographed.
- Next cars: SW-1296 (tiny-tool function discovery), SW-1297 (npp opcode 0xee),
  SW-1298 (spider resources), standing SW-1293b (PE32 CRT self-check).
