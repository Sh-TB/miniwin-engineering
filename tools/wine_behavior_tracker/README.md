# wine_behavior_tracker

Evidence-grade capture of real application behavior under Wine, and
Wine-vs-MiniWin differential comparison. Diagnostic tooling only — lives
outside the MiniWin production runtime by design.

## Quick start (fresh environment)

```bash
# prerequisites: python3 >= 3.9, PyYAML; a wine at tools/wine-10.0 (see below);
# a built MiniWin loader (make at repo root) for miniwin-engine specs.
pip install pyyaml          # only hard dependency
pip install pillow          # optional: screenshot support

# run everything from the repo root
export PYTHONPATH=tools/wine_behavior_tracker

python3 -m wine_behavior_tracker selftest          # no Wine needed
python3 -m wine_behavior_tracker doctor            # environment audit
python3 -m wine_behavior_tracker run --spec tools/wine_behavior_tracker/specs/winrar-console-workflow-wine.yaml
python3 -m wine_behavior_tracker run --spec tools/wine_behavior_tracker/specs/winrar-console-workflow-miniwin.yaml
python3 -m wine_behavior_tracker run --spec tools/wine_behavior_tracker/specs/winrar-create-relay-wine.yaml
python3 -m wine_behavior_tracker parse --run <run-id>          # relay -> JSONL events
python3 -m wine_behavior_tracker compare --wine <run> --miniwin <run>
python3 -m wine_behavior_tracker report --run <run-id>
python3 -m wine_behavior_tracker verify --run <run-id>         # manifest re-hash
```

Run artifacts land in `artifacts/wine-tracker/runs/<run-id>/`:
`run.json`, `raw/`, `parsed/events.jsonl`, `snapshots/` (fs + registry .reg
exports), `checkpoints/`, `reports/`, `manifest.sha256`. Runs are never
overwritten; each disposable wine prefix is reclaimed at run end after its
registry files are exported (a full prefix is ~700 MB — keeping them filled
a 10 GB sandbox disk once, measured 2026-10-30).

## Wine install note (non-root sandbox)

**Current build (2026-10-30, SWEEP-033): WineHQ noble `wine-stable-amd64 +
wine-stable 10.0.0.0~noble-1` debs extracted to `tools/wine-10.0`
(layout `bin/wine64`, `share/wine/nls`, `lib64/wine/*`).** The Debian
trixie `wine 10.0~repack-6` and `repack-12+b1` builds both abort with
glibc `free(): invalid pointer` inside the loader right after prefix
creation in this sandbox (strace: abort immediately after the
`<prefix>/wineserver` marker write, before any guest code; setarch -R,
glibc malloc tunables and preloader invocation do not change it) — the
WineHQ build runs the identical prefix fine. Remaining measured repairs:
1. seed `zlib1.dll` (msvcrt flavor, from Debian `libz-mingw-w64`
   1.3.2 `usr/x86_64-w64-mingw32/lib/zlib1.dll`, stashed at
   `tools/wine-10.0/seeds/`) into each fresh prefix `system32` —
   USER32.dll imports it and guests die 0xc0000135 without it;
2. `TMPDIR` must point at a user-owned dir (`/tmp` is root-owned here);
3. `x86_64-windows`/`x86_64-unix` symlinks next to the loader (WineHQ
   layout already has its own under `lib64/wine/`). Channel syntax
   measured on this build: `+relay,+module` works; bare `+loaddll` is
   INVALID and poisons the entire WINEDEBUG spec.

SWEEP-033 note: Debian-repack wineserver64 also embeds
`/usr/share/wine/wine/nls`, unwritable on this host — the old binary
patch (`patch_wine_nls.py`) is only needed for the Debian layout, the
WineHQ layout loads nls from its own `share/wine/nls`.

## JSONL schema (v1)

Events carry `run_id, timestamp, sequence, event_type, capture_source,
confidence` plus optional `process/thread/module/api/arguments/return_value/
last_error/call_site/stack/related_artifact`. Absent stays absent — the
parser never fabricates return values or GetLastError codes. Malformed lines
become `parser_error` records. `confidence` ∈ {raw, derived, inferred}.
Event types: `process_start, process_exit, module_load, module_load_fail,
api_call, api_return, fs_effect, reg_effect, window_event, message_event,
gdi_op, screenshot, workflow_checkpoint, parser_error, run_meta`.

## Workflow specs

YAML under `specs/`. Each declares app (name, version, executable, sha256),
engine (`wine`|`miniwin`), env/timeout, deterministic fixtures, checkpoints
(`argv`, optional `host: true` for engine-independent verification steps),
and verify expressions (python evaluated with `rec/workdir/run_dir/sha256_file/
Path/os/json/re` in scope). A screenshot or process-spawn is never treated as
workflow success — acceptance is checkpoint assertions only.

## Known limitations

- MiniWin trace capture: the loader overwrites
  `miniwin-results/<exe>/api_trace.json` per invocation, so a multi-checkpoint
  run exposes only the last checkpoint's trace (recorded as divergence
  candidate #2 in `artifacts/wine-tracker/prioritized-divergences.md`).
- No ImageMagick/xwd in the sandbox: GUI screenshots degrade honestly.
- Relay logs are bounded per workflow step; full unfiltered relay is only
  kept for single-step captures (the S2 sample is 83 MB raw and stays local;
  its parse statistics and a trimmed sample are committed).

## Tests

- `python3 -m wine_behavior_tracker selftest` — synthetic end-to-end
  (schema, parser, snapshot diff, manifest); all fixtures labeled
  SYNTHETIC-TEST-DATA.
- `python3 tests/analysis/test_trace_collector_parser.py` — 18 parser
  regression cases (same labeling discipline).
