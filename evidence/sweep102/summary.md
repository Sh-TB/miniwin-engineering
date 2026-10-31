# SWEEP-102 — SW-1487 decode: the wall-clock coupling map (probe-divergence
# root cause + the WaitForMultipleObjects finite-wait unit bug); ENTRY probe
# gains arg1/arg2; the deterministic time plane is SPEC'd and PARKED (SW-1489)

Date: 2026-10-28 · Base: origin/main e94a0a1 (SWEEP-101) · G1 PIN 12/12
(evidence/master/g1/20261028_155649, binary 9e3e0e09)

## What this round set out to do and where it landed

Wagon order was SW-1487 (frame capture for gate-rendered dialogs). The first
observation pass hit a reproducible probe side-effect and the round pivoted
into decoding IT — the result is a precise map of every wall-clock coupling
in the hot NPP path, plus one pre-existing gate bug. The frame-capture wagon
re-parked behind it (SW-1488); the fix itself is spec'd and parked (SW-1489).

## Decode 1: the entry-trace probe divergence is a REAL wall-clock branch

MW32_ENTRY_TRACE="403dbb" makes the previously-rc=0-in-3s nppsetup run hang
(rc=124 at 30-75s). Isolation:

- The per-instruction probe cost is NOT the cause: MW32_MAX_STEPS=2M with the
  probe armed consumed its budget in <1s (eip=0x406c24, early init).
- During the hang the process is ONE thread, state S, utime FROZEN (~2.2s CPU
  consumed then zero), wchan=hrtimer_nanosleep — the guest is parked in a
  wait loop, not spinning and not deadlocked in the interpreter.
- The probe's single fprintf delays the main thread by microseconds -> a
  wall-clock comparison in the exehead's early phase flips -> the flow enters
  a wait branch that never satisfies. The SAME flip appears without any probe
  under host-load shifts (one 5-run verification window showed 124/0/0;
  a re-run minutes later: rc=0 x5, all five stdout logs byte-identical,
  md5 f2556e82...). SWEEP-101's determinism claim holds for unloaded runs;
  the coupling is load-gated, not gone.

## Decode 2: the wait-gate mixing + a pre-existing unit bug (SW-1489 input)

A MINWIN_STEP_TIME=1 experiment (virtual GetTickCount/QPC/filetime/Sleep,
base-anchored, step-locked deltas) was built and BROKE the plain flow
(rc=124 with no probe) — which DECODED the coupling map instead of shipping:

- Guest tick branches are only HALF the picture: loader-internal wait gates
  compare GUEST-derived deadlines against HOST clocks. The
  WaitForMultipleObjects gate mixes units outright: `now.tv_sec` (REALTIME
  seconds) vs `deadline/1000` (guest ms) — every FINITE wait returns
  WAIT_TIMEOUT instantly (pre-existing bug, masked because guests take
  fallback paths), and every INFINITE wait polls 10ms slices forever —
  the exact hang shape of the probe divergence.
- A virtual time plane therefore requires auditing EVERY clock consumer
  together (5 guest time APIs + mw_mono_ms/SetTimer due-math + the
  kobj-wait deadline semantics + Sleep). Half-virtual is worse than none.
  Experiment reverted (stash); spec landed here instead. See
  evidence/sweep102/probes/wait_gate_deadline_bug.txt.

## Instrument (landed)

MW32_ENTRY_TRACE lines now print arg1/arg2 ([esp+4]/[esp+8] — hwnd/msg for
stdcall procs), making message-level arrival traces readable without a
run-under-gdb (which cannot run this interpreter to completion at all).

## Frame-capture decode started (SW-1488, parked)

BeginPaint/EndPaint are called ONLY from the class wndproc region (0x401047 /
0x40116e in the exehead), never from the dialog proc 0x404169 — the NSIS
wizard paints via the dialog-manager plane. Modeless dialog gates
(mw_dlg_create_common) never register (hwnd -> dlgproc), so the SW-1398 pump
router answers "no registered wndproc" for every dialog hwnd and pump events
(paint included) never reach dialog procs. SW-1488 = dialog-proc registry +
a frame source decision (guest EndPaint vs engine compositor dump).

## Gates

- G1 PIN 12/12 (evidence/master/g1/20261028_155649, binary 9e3e0e09).
- corpus_gate sweep102: 17 rows, rc=0 in 12, crash total 0 (unchanged shape).
- nppsetup battery x5: rc=0 all, byte-identical logs (f2556e82...).
- repo_size_guard: no manifest changes this round.

## Constraint compliance

No manifest changes; no new Issues/PRs; linear commit on top of e94a0a1;
no force-push; no Persian content. The MINWIN_STEP_TIME experiment was fully
reverted from the build (verified: rebuilt binary sha 8158af83 == SWEEP-101
before re-applying only the probe extension; final binary 9e3e0e09).
