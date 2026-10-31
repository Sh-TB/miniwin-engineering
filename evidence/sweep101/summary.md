# SWEEP-101 — SW-1486 closed: the rc 0/124 bistability was the pump-yield
# wall-clock coupling; MINWIN_PUMP_YIELD_US=0 makes the wizard battery
# deterministic (rc=0 x3, byte-identical logs)

Date: 2026-10-28 · Base: origin/main 80ebee0 (SWEEP-100) · same binary 8158af83

## Decode

The SWEEP-100 honest note (rc 124/0/0 across identical runs) traced to
mw_pump_yield(): the GUI pump sleeps 250 us (MINWIN_PUMP_YIELD_US, default
250) whenever a poll finds nothing, so once the wizard's message loop is
ALIVE the injected-key schedule (MW_GUI_KEY_EVERY, budget) is paced by wall
clock x interpreter speed — machine-load-dependent. Pre-fix runs: rc=0,0,0,124
across 4 identical invocations; post-fix (yield 0): rc=0 x4 in 2-3 s each and
ALL FOUR stdout logs BYTE-IDENTICAL (single md5). The SetTimer monotonic gate
(SW-1023) remains the only wall-clock plane and is not on this path.

## Fix

plugins/nppsetup/plugin.tsv env column: MINWIN_OSVER=6.1
MINWIN_PUMP_YIELD_US=0 — the battery now runs step-locked.

## MEASURED (canonical battery x3, post-fix)

run1 rc=0 crash=0; run2 rc=0 crash=0; run3 rc=0 crash=0 — VERDICT: EXIT rc=0
frames=0 (the frame counter does not see the gate-rendered DialogBox; the
STATE change is captured instead: install_log_sweep100.tsv 63 ops +
registry_manifest_sweep100.tsv 20 keys, copied out of the gitignored sandbox
into evidence/ proper). Frontier registered for later polish: frame capture
for gate-rendered dialogs (the screenshot hooks bypass the DialogBoxParam
gate's paint path).

## Gates

G1 unchanged (binary untouched: 8158af83). Corpus gate unchanged (17 rows,
rc=0 in 12, crash total 0). No manifest changes.

## Constraint compliance

Linear commit on top of 80ebee0; no force-push; no Persian content.
