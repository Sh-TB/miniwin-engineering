# MiniWin app-plugins

Each tested application is ONE self-contained directory: `plugins/<name>/`.
A plugin makes failures comparable and diagnosis repeatable — the manifest
carries the artifact identity (official URL + SHA-256), the input probes,
the known root-cause numbers, and the runner produces the verdict + frames.

## Contract
- `plugins/<name>/plugin.tsv` — TAB-separated manifest (see scripts/plugin_run.sh)
- Runner: `scripts/plugin_run.sh <name>` (or `--all`)
- Status ledger: `plugins/status.tsv` (one row per plugin per sweep)

## Manifest keys
key | meaning
--- | ---
name | plugin id (directory name)
arch | x64 / x86 (guest bitness)
desc | one-line description
source_url | OFFICIAL download URL (no mirrors, no web substitutes)
source_sha256 | expected SHA-256 of the artifact
artifact | local path of the downloaded artifact
unpack | none / zip
exe | exe path (relative to unpack dir, or artifact path)
args | extra loader args
runs | cold runs (default 3)
timeout | per-run seconds
keys | MW_GUI_KEYS probe (virtual-key codes, comma-separated)
mouse | MINWIN_MOUSE probe (x,y list)
known | known root-cause numbers (SW-xxxx) or NEW
note | free text

## Verdict vocabulary (honest states)
- PASS_INPUT_STATE_CHANGE — rc==0 + frames + post-input pixel change
- PASS_LAUNCH_RENDER — frames rendered, rc != 0 (or no state change)
- FRONTIER_SW crash=N SW-xxx — crashed, frontier decoded/known
- TIMEOUT / EXIT rc=N frames=N
- BLOCKED_DOWNLOAD / BLOCKED_SHA / FAIL_UNPACK / FAIL_NOEXE / NO MANIFEST

The three user-named base apps (winrar, idman, npp64) run on EVERY train;
each (continue/train) additionally runs one NEW unknown plugin.
