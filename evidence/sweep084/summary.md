# SWEEP-084 — visual-execution-proof train; NEW program solo (Simon Tatham Sudoku) PASS first contact

Base: origin/main f2f9c01 == local at session start (SWEEP-083 tip). Engine UNCHANGED this train
(no loader.c edits) — byte-exact rebuild sha256 80bca026 (matches SWEEP-083 final pin).
G1 pre-train PIN 12/12 (evidence/master/g1/20261026_201417); train-end PIN 12/12 (20261026_201812).

## Runs (3 cold runs each, scripts/plugin_run.sh, SWEEP=sweep084)

| Guest | Verdict | Evidence |
|---|---|---|
| npp64 (user-named) | PASS_INPUT_STATE_CHANGE rc=0 x3 | messagebox_1/2.png, full graphical init |
| idman (user-named) | PASS_LAUNCH_RENDER rc=0 x3 | messagebox_1.png |
| winrar (user-named) | EXIT rc=0 x3 (parity frontier SW-1361) | frames=0 |
| solo (NEW, Simon Tatham Sudoku, SWEEP-072 A-group U19) | PASS_INPUT_STATE_CHANGE rc=0 x3 | frame_001..006.png, 3 unique states, cursor moves + grid re-render |

solo manifest: official puzzles.zip sha a5b00f63 (verified), exe solo.exe, keys 37,38,39,40,13, mouse 150,150.
Second consecutive Simon Tatham first-run PASS (mines in SWEEP-083) — the collection is a reliable battery.

## Image delivery (user-requested visual proof)

Trimmed frames + labeled contact sheet exported to the session download area:
miniwin_frames_sweep084/00_contact_sheet.png (+ per-app prefixed frames).
Rows: Notepad++ x64 (dialogs), IDM x86 (error dialog), Solo x64 (sudoku sequence),
Mines x64 (SWEEP-083 battery proof: flood-fill reveal).

## Gates

- corpus parity scripts/sweep084_corpus.sh: 21/21 rc=0, crash=0, fatal=0, x3 (evidence/sweep084/corpus_parity.tsv)
- G1 12/12 pre + train-end (binary 80bca026 byte-exact both)

## GitHub check (user-requested)

- origin/main HEAD f2f9c01 == local (API verified), default_branch main, last push 2026-10-26T20:10Z
- Issue #1 state=closed, 46 comments, SWEEP-083 report is latest — this train appends comment 47.

## Next wagons

- Notepad++ interactive deepening (menu/text probes), winnc SW-1393 finally-retry router decode,
  everything/tcpview64 poll loops, renamer rc=204 pre-render path, more puzzles as new-program wagons
  (netgame, lightup, pattern, towers...), hxd download retry, notepad2mod x64 wagon.
