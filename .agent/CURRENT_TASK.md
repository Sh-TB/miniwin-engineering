# CURRENT TASK

**Task ID:** TASK-BUG-026
**Title:** Fix `g_cap_rip` capture operand in `mw_RaiseException` (naked stub)
**Status:** READY (diagnosis complete; implementation deliberately not started this session)
**Priority:** P0

## One-line description
In `src/loader/loader.c` L1697-1702, the sequence `movq 0x58(%rsp), %rax; movq 0x00(%rsp), %rax; movq %rax, g_cap_rip(%rip)` stores the stack pointer into `g_cap_rip`. It must store the saved return address (slot `[rsp+0x08]`, already populated by the stub's earlier code) — i.e. `movq 0x08(%rsp), %rax` immediately before `movq %rax, g_cap_rip(%rip)`, exactly as in `src/loader/loader.c.exp_next2_backup` L842-843.

## Why this is the task (evidence)
- Probe run (fresh build): `g_cap_rip == g_cap_rsp_entry` (stack address) while true return address `[rsp_entry] == 0x49d5b1` for both dispatches — `docs/master/UPX_FAILURE_FORENSICS.md` §1 F8.
- Effect: CONTEXT.Rip outside PE → `Frame[0]: RIP outside PE — walk ends` (trace) → unhandled → `terminate` → exit 134.
- Regression tests 8 & 10 fail for exactly this reason (baseline 10/12).

## Execution notes for the next agent
- Follow `.agent/CONTINUE_PROTOCOL.md` steps C–F.
- Change ONE line's operand (delete the stray `0x58` reload + change `0x00` → `0x08`); keep the naked stub otherwise untouched.
- Predicted post-fix state: regression 12/12 (exit 139-era expectations), UPX proceeds into the documented 5-frame walk; a NEW failure at/near the landing pad is expected (see forensics §3–5) — that becomes TASK-EXP-036/037/038, NOT a reason to keep hacking inside this task.
- Record the fresh trace in `evidence/master/` and write `docs/master/experiments/EXP-036-*.md`.
