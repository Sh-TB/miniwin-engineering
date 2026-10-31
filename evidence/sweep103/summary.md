# SWEEP-103 — Re-test round: previously-failing programs vs the SWEEP-102 engine

Round type: honest re-test train (user instruction translated: "continue; also
re-test the programs you previously could not run, and see whether progress
was made"). No engine code changed. Binary rebuilt and pin-verified.

## Baseline

- Sandbox reset recovered: local clone was 104 commits behind
  (242af84 SWEEP-030 -> 821cc84 SWEEP-102); stale untracked artifacts
  (artifacts/sweep030, sweep031) backed up out of tree, then
  `git reset --hard origin/main`.
- Clean rebuild: `minwin_loader` sha256
  9e3e0e092a7921fe494b9fc25d2412486fcca102723400282961839e2b746de8 —
  byte-identical to the SWEEP-102 G1 pin (deterministic build confirmed on
  fresh provision).
- G1 PIN OK 12/12 (evidence/master/g1/20261028_173735).

## Re-test results (all runs SERIAL — SWEEP-101/102 wall-clock lesson)

| program | last known verdict | SWEEP-103 re-test | delta |
|---|---|---|---|
| nppsetup | rc=139 crash @ 139,972,856 steps (sweep081); rc=0 x3 (upstream sweep101/102) | **rc=0 x3 crash=0**, run logs byte-identical (1 unique md5), install_log 75 ops, HKLM\\SOFTWARE\\Notepad++ registry tree captured | CONFIRMS the SW-1477/SW-1478/SW-1485 fix chain end-to-end on a fresh clone |
| notepad2mod | ADVANCED_FRONTIER_SW-1373 crash=1 rc=139 (sweep081) | **rc=0 x3 crash=0** | FIXED as side effect of the sweep082..102 engine work (no dedicated commit ever recorded this) |
| winrar (provision run) | EXIT rc=0 | rc=0 (1 install run staging winrarinstd's tree) | unchanged PASS |
| tcpview64 | TIMEOUT rc=124 (sweep096) | rc=124 x3 crash=0 | unchanged; wait-family (SW-1489 parked decode) |
| everything | TIMEOUT rc=124 (sweep096) | rc=124 x3 crash=0 | unchanged; wait-family |
| produkey | TIMEOUT rc=124 (sweep096) | rc=124 x3 crash=0 | unchanged; wait-family |
| winrarinstd | TIMEOUT rc=124 (sweep081) | rc=124 x3 crash=0 (after fresh winrar provision + 2702B reg seed) | unchanged; wait-family |
| winnc | rc=139 crash=1 (sweep081) | **BLOCKED_DOWNLOAD** — honest | historical pinned bytes (bef0fcaf..., 12,964,000 B) no longer served; live winnc.com/setup.exe is a DIFFERENT build (1,912,832 B). Hash-pin discipline held: no dishonest re-pin. |
| renamer | rc=86 (sweep081) | **BLOCKED_DOWNLOAD** — honest | den4b flow is JS-gated (HTML, no direct asset); infonautics host unreachable; web.archive.org unreachable this round. No dishonest re-pin. |

## Corpus gate

- mini.flood SEED rebuilt from tracked source after the sandbox reset
  (ziglang 0.17.0, tests/games/miniwin_flood.c unchanged): 223,744 B —
  byte-count matches the SWEEP-093 pin exactly; SHA re-measured
  b406517a00d91d4884e7d4f36462886c78c066bcdc92e304ac8c05e2f7716e04 and
  manifest row 58 re-pinned per the documented zig-timestamp
  non-reproducibility (SWEEP-095 precedent).
- corpus_gate.sh sweep103: **17 rows, rc=0 in 12, crash total 0** — canonical
  shape restored (mini.breakout/mini.flood/mini.snake/psloglist64 all rc=0 x3;
  5 xp.* SEEDs honestly MISSING — licensed-media requirement, permanent
  documented block).

## Manifest / policy hygiene

- manifest.tsv row 52 (notepad2mod.portable): UNVERIFIED_URL -> exact URL
  https://github.com/XhmikosR/notepad2-mod/releases/download/4.2.25.998/notepad2-mod.4.2.25.998_x64.zip
  — downloaded fresh, SHA-256 re-measured and matched the pinned hash
  byte-for-byte (c893ce4a..., 814,355 B). Note column updated (2026-10-28).
- repo_size_guard: GUARD FAIL carried-over violation FIXED —
  evidence/sweep099/nppsetup/probe_sandbox/probe_install_log.tsv was still
  tracked (added in SWEEP-099 023e897 despite the evidence/**/sandbox/
  policy; the SWEEP-100 "remediation carried" note did not actually land in
  git). `git rm --cached` applied, file stays on disk. Guard now
  **GUARD OK** with the 2 pre-existing documented WARNs (shexview,
  hxd.portable.setup) unchanged.

## Interpretation (OBSERVED vs INFERRED)

- OBSERVED: 2 of the 4 crash/exit-code frontiers from the sweep081 era
  (nppsetup rc=139, notepad2mod rc=139) now run rc=0 deterministically; the
  4 timeout frontiers (tcpview64/everything/produkey/winrarinstd rc=124)
  are unchanged and all sit inside the wall-clock/wait family decoded in
  SWEEP-102 (WaitForMultipleObjects REALTIME-seconds vs guest-ms unit mix:
  finite waits time out instantly, infinite waits poll forever —
  SW-1489 spec parked upstream).
- INFERRED: the rc=124 quartet is the highest-value next decode target; the
  SW-1489 time-plane implementation (all clock consumers audited TOGETHER)
  is the queued fix train. Not started this round — this round is re-test
  + evidence only.
- UNRESOLVED: winnc/renamer need exact-URL re-pins before they can run
  again (sources no longer serve the pinned bytes); xp.* needs licensed
  media.

## Artifacts

- evidence/sweep103/<plugin>/ run logs (sandbox payloads gitignored per
  policy); corpus_parity.tsv (17-row gate); G1 evidence
  evidence/master/g1/20261028_173735.
- No force-push.
