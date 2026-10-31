# SWEEP-104 — SW-1489 first wagon (WaitForMultipleObjects finite-wait unit
# fix) + SW-1490 runner frame-accounting fix + SW-1491 honest rc=124 taxonomy
# (the timeout quartet is ALIVE, not hung)

Date: 2026-10-28 · Base: origin/main 821cc84 (SWEEP-102) + SWEEP-103 69d8862 ·
Final binary sha256 a09aa334 (G1 PIN 12/12, evidence/master/g1/20261028_181913)

## How the round actually went

The wagon order was SW-1489 (implement the SWEEP-102 time-plane spec). The
first observation pass redirected the round — reading the SWEEP-102 spec and
then OBSERVING the rc=124 quartet's actual parking loops changed the picture
before any time-plane code was written. Three landed wagons below; the full
virtual time plane stays PARKED (SW-1489 core), now with measured input.

## Observation 1 (OBSERVED, evidence/sweep103/probes/): the rc=124 quartet
## never touches WaitForMultipleObjects

/proc sampling (utime/stime/state/wchan, 2x6s windows) + api_trace tails
under the exact battery env:

- produkey: state S, wchan=hrtimer_nanosleep, utime frozen -> PACED PARK.
  Tail signature: `[GUI] fix-F wait done spins=198 due_in=-3ms live=1
  quit=0` + `GetMessageA -> WM_TIMER id=1 (SW-1220 tick)` loop. The app is
  ALIVE on its 1s refresh timer; the injected keys never close it. Bare run
  (no injection) exits rc=0 in seconds via the batch WM_QUIT contract —
  the event source existing is what keeps it alive.
- everything: state S, paced -> ALIVE. Tail: `GetMessageW -> WM_PAINT
  child=... (SW-1398)` dispatch loop.
- tcpview64: state R, utime 568->1168 ticks/6s = ~100% core -> BUSY SPIN.
  Tail: unbounded `SendMessageW(hwnd=0x30006, msg=0x43f)` re-fire loop
  (its refresh timer drives an un-paced control-refresh storm).
- winrarinstd: state R, ~100% core -> BUSY SPIN. Tail: HeapAlloc/HeapFree +
  `RegOpenKeyExW(miss) 'HKCU\Software\WinRAR\Setup' -> ERROR_FILE_NOT_FOUND`
  re-read loop (the SW-1412 setup-phase family).

Conclusion: the quartet's rc=124 is ALIVE-AT-TIMEOUT (two honest interactive
pumps + two engine-modeling spin frontiers), NOT a wait-gate hang. Real
Windows + this battery would also time out on the two pumps.

## Wagon 1 — SW-1490: runner frame accounting for portable-zip plugins

The SW-1470 portable branch runs zip guests from $PORTABLE_CWD (the exe's
own dir), but the runner read frames from $RESROOT (repo root) — so every
portable-zip plugin silently lost ALL frame evidence (frames=0 verdicts on
real renders; the PNG evidence step also read the wrong dir). Found by
chasing produkey's frames=0 while its trace/render plainly existed
(.cache/miniwin/guests/produkey.x/miniwin-results/...). Fix: RESROOT_EFF
tracks where the run actually writes (PORTABLE_CWD/miniwin-results for
portable, sandbox/miniwin-results for sandboxed, repo root otherwise);
pre-run wipe + frame_stats + PNG step + classify trace path all follow it.

Measured: produkey frames=1 recovered; notepad2mod went rc=0 frames=0 ->
**PASS_INPUT_STATE_CHANGE (frames=4 uniq=3)** — its first full input-state
evidence ever (crash=1 at sweep081 -> rc=0 at sweep103 -> state-change PASS
now, all from engine work that predated the accounting fix).

## Wagon 2 — SW-1489 first wagon: WaitForMultipleObjects finite-wait unit fix

src/loader/loader.c mw_WaitForMultipleObjects: the deadline gate compared
`now.tv_sec` (CLOCK_REALTIME epoch seconds) against `deadline/1000` (guest
ms) — always-true, so every FINITE wait returned WAIT_TIMEOUT after one
10ms slice, and INFINITE waits polled 10ms slices forever (the SWEEP-102
decode, evidence/sweep102/probes/wait_gate_deadline_bug.txt). Now anchored:
elapsed-ms on CLOCK_MONOTONIC (jump-immune per the wall-clock lesson),
WAIT_TIMEOUT only when the FULL requested budget is consumed. Latent for
the current quartet (they never reach this gate) but real, spec'd, and now
correct. WSAWaitForMultipleEvents / WaitForMultipleObjectsEx inherit the
fix through the shared implementation. No pump-path caller exists (verified:
only guest ENTRY rows + the two guest-facing wrappers).

## Wagon 3 — SW-1491: honest rc=124 taxonomy in classify()

An rc=124 guest is not necessarily hung. classify() now decodes the parking
shape from the api_trace tail (raw text survives SIGKILL): ALIVE_PUMP
(GetMessage -> WM_TIMER/WM_PAINT loop) vs SPIN (SendMessage re-fire) vs
plain TIMEOUT when neither signature is present. Branch order fixed so
frames>0 + rc=124 can never read as PASS. Battery results under the new
taxonomy: produkey ALIVE_PUMP_AT_TIMEOUT frames=1; everything
ALIVE_PUMP_AT_TIMEOUT frames=0; tcpview64 SPIN_AT_TIMEOUT; winrarinstd
SPIN_AT_TIMEOUT.

## Regression watch (honest)

- nppsetup: first post-fix battery recorded rc=0/0/124 — the 124 flip is
  the SWEEP-102 documented load-gated bistability (the battery ran right
  after the heavy G1 build window); idle re-run rc=0 x3. One NEW rare shape
  appeared in that loaded window (run1, 1/12 of sweep104 runs):
  `[MessageBoxW] Notepad++ v8.9.8.1 Setup: Error decompressing data!
  Corrupted installer?` at exehead init, followed by an otherwise
  byte-identical clean flow. Never seen in any prior evidence; unreplicated
  in 11 subsequent runs (two idle x3 batteries byte-identical, md5 unique=1
  each). Registered as SW-1492 (rare load-adjacent first-pass decompression
  error dialog; decode queued — suspect uninitialized exehead state read at
  first LZMA pass, memory-pressure-sensitive).
- install_log.tsv note: the file APPENDS across batteries within one
  sandbox dir (sweep104 shows 198 = cumulative lines over 3 batteries);
  per-run op counts unchanged (75 ops per clean battery).

## Gates

- G1 PIN 12/12 (evidence/master/g1/20261028_181913, binary a09aa334).
- corpus_gate sweep104: 17 rows, rc=0 in 12, crash total 0 (canonical).
- repo_size_guard: GUARD OK (2 pre-existing documented WARNs unchanged).
- Re-test battery (serial, idle): nppsetup rc=0 x3 byte-identical;
  notepad2mod PASS_INPUT_STATE_CHANGE frames=4 uniq=3; produkey
  ALIVE_PUMP_AT_TIMEOUT frames=1; everything ALIVE_PUMP_AT_TIMEOUT;
  tcpview64 + winrarinstd SPIN_AT_TIMEOUT. No manifest changes.

## Queued (not started)

- SW-1489 core: full virtual time plane (all clock consumers audited
  TOGETHER) — stays parked; the quartet evidence above is its measured
  input.
- SW-1492 decode: the rare decompression-error dialog.
- Spin-frontier decodes: tcpview64 msg=0x43f refresh storm (pacing) +
  winrarinstd WinRAR\Setup registry-miss re-read loop (SW-1412 family).
- winnc/renamer exact-URL re-pin hunt (sources no longer serve pinned
  bytes); "Program Filesry" join decode (untouched since SWEEP-095).

No force-push.
