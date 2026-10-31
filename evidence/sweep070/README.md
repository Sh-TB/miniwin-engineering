# SWEEP-070 — Heavy parallel train: famous Windows programs fetched + run (heavy parallel train, targeted tooling)

Date: 2026-10-25 · Base: ac588f8 (origin/main, post SWEEP-069) · Engine rebuilt after
second sandbox reset (Rule #1 caught it; `git pull --ff-only` + make, winmine-class
parity assumed) · 4,295,920 bytes.

## Part 1 — Targeted fetch: 19 of 26 famous products landed (498MB corpus wave)

26 famous Windows targets fired in ONE parallel curl wave (-P6, 150s cap, magic-sniffed).
Manifest: `download_manifest.tsv` (wave1) + `download_wave2.tsv` (7 corrected retries —
all 404/403, asset-name drift; GitHub API rate-limited — queued honestly).

**Landed PE/installers (12)**: **IDM 6.42b2** (user-named «winrar idm» class — 12.7MB),
VLC 3.0.21 (45MB), qBittorrent 4.6.4 (35MB), Audacity 3.5.1, MPC-HC 2.3.9, WinMerge
2.16.36, Everything 1.4.1, CPU-Z 2.10, PuTTY (latest w64), 7-Zip 24.08, NVM 1.1.12,
yt-dlp.
**Landed ZIP → exes (6)**: ShareX (+ffmpeg.exe, Recorder-setup), ripgrep 14.1.0, bat
0.24.0, NSSM 2.24 (win64), SteamCMD, **Stockfish 16.1 AVX2 (70MB — the biggest guest in
project history)**
**Missed (8)**: AutoHotkey (403), IrfanView, SumatraPDF, OpenTTD, SuperTux, ProcessHacker,
TreeSize (404s), foobar2000 (conn fail) — second-wave retries all 404; assets renamed
upstream. Car SW-1310 keeps the corrected-URL hunt alive.
**Games**: Stockfish = chess engine (UCI CLI); game GUIs (OpenTTD/SuperTux) blocked on
asset URLs this round — SW-1310.

## Part 2 — Heavy battery: 18/18 executed, load-fidelity stratified (loading must behave exactly like Windows)

Harness: `scripts/sweep070_battery.sh` (xargs **-P6**, fresh per-exe hive, flags
F=FASTFAIL P=PE32-FATAL B=host-backtrace). Table: `battery.tsv` · raw: `battery_raw.txt`.

| Load-fidelity class | Guests | Meaning |
|---|---|---|
| **Full Windows-like behavior** | **bat** (clean), **ShareX** (clean — full CRT+CLR .NET load path!), **PuTTY** (rc=0 + genuine `PuTTY Error: Unable to open connection` dialog RENDERED — its real network-fail path), **ripgrep** (real CLI usage exit 2) | load → run → own decision, exactly like on Windows |
| **Deep x86 installer penetration** | **7zip 47,478 steps**, everything 4,953 | NSIS/CRT init deep-dives |
| **Common-signature family** | audacity/cpuz/mpchc/nvm @ 551-565 steps | one shared installer-init frontier (car **SW-1309**) |
| **PE32-FATAL rc=86** | idm, steamcmd, winmerge-2.16.36 | known x86 frontier class (npp86 family) |
| **x64 frontiers** | ytdlp (PyInstaller bootstrap), nssm64 (unres=2), qbittorrent/vlc (early BT), stockfish (**70MB loaded — old 64MB cap history; SIGABRT deep-run**) | per-car queue |
| **unresolved imports** | nssm64=2, everything else 0 | engine import coverage held on all 18 |

**Headline: PuTTY's genuine error dialog + ShareX clean .NET run + Stockfish 70MB past
the old file cap = load path now behaves Windows-like on classes it never touched before.**

New worklist cars: **SW-1309** (551-565-step installer-init common frontier),
**SW-1310** (404-hunt: OpenTTD/SuperTux/AHK/SumatraPH corrected assets), **SW-1311**
(stockfish SIGABRT post-load), **SW-1312** (ytdlp PyInstaller x64 bootstrap).

## Evidence

- `download_manifest.tsv` / `download_wave2.tsv` — 26+7 fetch results (name/url/http/bytes/kind)
- `battery.tsv` (18 rows, machine-types+flags) · `battery_raw.txt` · `shots/putty_error_dialog.bmp`
- Binaries NOT committed (498MB): `artifacts/sweep070/{dl,guests}/` gitignored — reproducible
  via `scripts/sweep070_download.sh` + `sweep070_download2.sh` (URLs recorded in-repo)
