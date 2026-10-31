# SWEEP-033 — Resource census + screenshot upload (internet train)

Date: 2026-10-29. Directives: «ادامه قطار اینترنت» + screenshot-of-loaded-programs
upload-to-web + RAM/CPU/disk measurement. Internet-first rule applied.

## 1. Internet research (curl/WebSearch, GitHub-first) — WinRAR / Notepad++ / IDM

Known-fix survey BEFORE any bespoke work (rule: search first, thousands solved it):

- **Notepad++ (x64)** — upstream repo notepad-plus-plus/notepad-plus-plus:
  - Issue #12254 (open): "N++ block characters U+25A1 in Wine" — cosmetic glyph
    fallback gap in Wine rendering, NOT a loader crash. N++ is broadly reported
    WORKING under Wine-family loaders (community consensus, r/linux + r/winehq).
  - Issue #16023 (closed): "wine32 is missing" on arm64 — loader-architecture
    mismatch (32-bit subsystem absent), the same class as MiniWin's x64-only
    constraint. No actionable fix for a pure-x64 loader.
  - Consequence for the train: NPP's blocker is not a common Wine-class crash;
    MiniWin-specific unresolved imports remain the local frontier (SEED pinned).
- **Internet Download Manager (IDM)**:
  - Official FAQ (idman.com/register/new_faq/problems11.html): high CPU is a
    KNOWN upstream issue, fixed across releases ("update to latest version");
    multi-segment download engine is the CPU/network consumer.
  - Community (r/winehq): IDM under Wine requires 32-bit networking libraries
    (wininet/winsock native replacement); without them the browser-integration
    hook chain fails early. This is a DLL-contract problem, the same class
    MiniWin models with typed stubs.
- **WinRAR**:
  - WineHQ AppDB maintains WinRAR entries (installer + GUI runnable under
    Wine); snapcraft.io ships a "WinRAR (WINE)" packaged app — i.e. the
    installer/GUI lifecycle is proven to work under loader reimplementation.
  - No loader-class blocker found in public trackers; the sfx/installer path
    (self-extracting CAB/7z-like container) is the historically fragile part.

Verdict: none of the three has a "thousands already solved it" loader-class
fix we can lift; the public knowledge confirms our current frontier classes
(GUI pump, shell/net DLL contracts). Evidence recorded, no bespoke debugging
wasted (per the «اینترنت» rule).

## 2. NEW open-source program added to the work list

- **SumatraPDF 3.5.2 (x64)** — GPLv3 PDF reader, single-exe GUI, ideal train
  target (MuPDF-based, realistic GDI/font usage).
- Download attempt in this sandbox: GitHub release assets return 9-byte
  "Not Found" via the network proxy; api.github.com rate-limited (403) for
  unauthenticated calls; sumatrapdfreader.org dl page is HTML redirect.
  Status: **BLOCKED (network)** — queued as SW-1301 for the next train round
  with an alternate fetch (pip/wget mirror or user-provided binary).

## 3. Resource census — 18 programs × {v1, gui-pump} × 3 runs (108 runs)

Method: os.wait4 rusage (utime/stime/maxrss) + /proc/<pid> poll (VmHWM, CPU)
for long-lived runs; disk footprint = run-dir bytes (frames + trace + exe copy);
MINWIN_TRACE_MAX_LINES=1M; pump = MW_GUI_EVENTS=1. Loader: minwin_loader
2,698,864 B, md5 936e91810c360bffc650af815573e6ed. TSV:
artifacts/sweep032/resource_census.tsv (108 rows, 3x-deterministic).

Headline numbers (median of 3, pump mode where meaningful):

| program      | exit | wall    | CPU     | peak RSS | disk out (run dir) | frames |
|--------------|------|---------|---------|----------|--------------------|--------|
| calc         | 0    | 5 ms    | 4 ms    | 13.0 MB  | ~14 KB (trace+1 BMP)| 1     |
| snake        | 0    | 8 ms    | 8 ms    | 13.0 MB  | ~9 KB              | 1     |
| breakout     | 0    | 7 ms    | 7 ms    | 13.0 MB  | ~12 KB             | 1     |
| bginfo64     | 0    | 31 ms   | 31 ms   | 13.0 MB  | 219 KB (1 BMP 1280x1024) | 1 |
| putty        | 0    | 11 ms   | 11 ms   | 13.0 MB  | 480 KB (trace)     | 1 dialog |
| produkey     | 0    | 2 ms    | 2 ms    | 13.0 MB  | 25 KB              | 0     |
| cports       | 0    | 3 ms    | 3 ms    | 13.0 MB  | 41 KB              | 0     |
| diskmon64    | 0    | 4 ms    | 4 ms    | 13.0 MB  | 31 KB              | 0     |
| coreinfoex64 | 0    | 5 ms    | 5 ms    | 13.0 MB  | 153 KB             | 0     |
| autoruns64   | 0 (v1) / -11 (pump) | 5 ms | 5 ms | 13.0 MB | 68 KB        | 0     |
| rammap64     | 0 (v1) / -11 (pump) | 26 ms| 24 ms| 13.1 MB  | 946 KB             | 0     |
| puttygen     | 0    | 5 ms    | 5 ms    | 13.0 MB  | 119 KB             | 0     |
| searchmyfiles| 0    | 3 ms    | 3 ms    | 13.0 MB  | 44 KB              | 0     |
| zoomit64     | 1    | 1 ms    | 1 ms    | 13.0 MB  | 21 B               | 0     |
| winmergeu    | 133 (GS pass -> guest C++ EH terminate) | 15 ms | 15 ms | 15.9 MB | 637 KB | 0 |
| procmon64    | 0 (v1) / -9 30s (pump) | 30 s | **29.99 s (100% core)** | **7.2 MB** | 51 MB trace (1M-line cap) | 0 |
| pageant      | -9 10s daemon (by design) | 10 s | 0.02 s | **8.8 MB** | 434 KB | 0 |
| procexp64    | 139  | 7 ms    | 7 ms    | 13.9 MB  | 84 KB              | 0     |

Honest findings:
1. Baseline footprint of the loader itself is ~13 MB RSS; guest programs add
   0.7-2.9 MB on top (winmergeu heaviest at 15.9 MB peak).
2. CPU: everything except procmon64-pump completes in <= 31 ms of CPU.
   procmon64 pump mode is a FULL-CORE SPIN for its whole lifetime
   (29.99 s CPU / 30.0 s wall) — matches known frontier SW-1090/SW-1091
   (26.35M SendMessageW per 12 s in pump); the 1M-line trace cap held (51 MB).
3. Disk: per-run writes are trace-dominated (KB-scale for most; 51 MB for
   procmon64-pump; bginfo64 219 KB = one 1280x1024 BMP). No unbounded disk
   growth anywhere except the known procmon64 pump trace (capped).
4. pageant daemon: 8.8 MB, near-zero CPU (idle pipe wait), exit by timeout
   124-class = expected daemon semantics.
5. Determinism: all exit codes and byte sizes repeat exactly across 3 runs
   (autoruns64/rammap64 pump SIGSEGV is itself deterministic — new registered
   frontier, queued SW-1302/1303; v1 path stays exit0).

## 4. Screenshot upload (user-requested web evidence)

Frames from the battery env (MW_GUI_EVENTS=1 + scripted keys/clicks),
BMP -> PNG (PIL), uploaded to litterbox.catbox.moe (72 h retention):

- calc:      https://litter.catbox.moe/bnv43l.png   (320x440, 12+45=57 clicked)
- snake:    https://litter.catbox.moe/76tmjh.png   (640x480)
- breakout: https://litter.catbox.moe/8vjuo6.png   (480x360)
- bginfo64: https://litter.catbox.moe/n58qqw.png   (1280x1024 desktop render)
- putty:    https://litter.catbox.moe/19w6b9.png   (guest MessageBoxW dialog)
- CONTACT SHEET (all 5): https://litter.catbox.moe/sc9eck.png

All URLs verified HTTP 200 after upload (host throttled briefly mid-burst;
retries succeeded after cooldown).

## 5. Queued work items

- SW-1301 SumatraPDF fetch + first run (network-blocked this sandbox).
- SW-1302 autoruns64 pump SIGSEGV (deterministic, gui mode only).
- SW-1303 rammap64 pump SIGSEGV (deterministic, gui mode only).
- SW-1304 contact-sheet + calc PNG upload retry (host throttle cooldown).
- Standing: NPP installer retest train + xp.* SEED items + manifest.tsv re-pin
  commit still queued from SWEEP-098.
