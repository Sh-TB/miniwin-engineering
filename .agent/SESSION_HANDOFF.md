# SESSION HANDOFF — 2026-10-14 (CONTINUE #2: BUG-026 fixed + EXP-036 run)

## What this session did
- **Protocol steps A–B:** state synced at `08d94ac` (clean tree; binary hash matched the recorded baseline byte-for-byte — reality check passed). Selected the one READY P0 task: TASK-BUG-026.
- **Step C (baseline):** clean build `afa7744c…`; regression **10/12** (tests 8 & 10 fail, as recorded).
- **Step D (fix):** single-operand change in the mw_RaiseException naked stub — `g_cap_rip` now loaded from saved-rip slot `[rsp+0x08]` (stray `0x58` reload + `[rsp+0x00]` load deleted), mirroring `loader.c.exp_next2_backup` L842-843. loader.c: `502ae588…` → `e1caa5d7…`.
- **Step E (verify):** clean build `c8472d04…` 0 errors; regression **12/12 PASS**; UPX run exit 139 (was 134).
- **EXP-036 (live re-run, folded into this session):** new trace shows dispatch-1 (EH_FIX override → LP 0x401573 → catch machinery → re-throw), then dispatch-2 with a **fully correct 4-frame walk** → Frame[3] personality → **real RtlUnwindEx(TargetIp=0x4dcd24)** → landing-pad resume → **NEW failure**: SIGSEGV at heap 0x2004167.
- **New-failure root cause identified** (static UNWIND_INFO decode + live numbers): mw_RtlUnwindEx synthesizes landing-pad RSP from alloc_total (ALLOC_SMALL decode missing `+8`: 0x40 vs true 0x48 → RSP 0x7ffe60f7deb8 instead of 0x7ffe60f7deb0) and does not propagate the RDX selector from the personality's ContextRecord. Registered as **BUG-027** (READY, next task).

## Commits
- This session: `BUG-026: restore g_cap_rip capture operand` (source + docs + evidence + .agent updates + checkpoint zip `miniwin_checkpoint_bug026_20261014.zip`).
- Chain: `39181aa` → `08d94ac` (foundation) → BUG-026 commit (HEAD).
- **Pushed: NO** — still no GitHub credentials. Owner action required.

## Current blocker
BUG-027 (see `.agent/CURRENT_TASK.md`): landing-pad resume state. UPX acceptance
G1 (`--version` output) still NOT MET; the failure point has moved from
"dispatch never walks" to "landing pad resume corrupts stack".

## Next
Next task ID: **TASK-BUG-027**. Exact next command from the owner: **CONTINUE**.

## Unverified / unknown (honest list)
- Whether the preferred fix (adopt personality ContextRecord) fully clears
  0x4dcd24 — P-1/P-2 predictions in EXP-036 §7 are falsifiable, not proven.
- EH_FIX still fires live on dispatch-1 (EXP-036 §3.4) — its isolation (API-005)
  remains mandatory before any honest G-gate.
- Whether dispatch-2's re-throw originates from UPX's own logic or from our
  resume at LP=0x401573 being subtly wrong (EH_FIX dependency — related to above).
- VEH chain empty at dispatch (R-31) — unchanged this session.
- Dispatch tests still broken/vacuous (TEST-004 pending).
- GitHub push still blocked on credentials (OWNER-001).
