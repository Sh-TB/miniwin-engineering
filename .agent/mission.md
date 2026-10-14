# Mission

Build and improve a lightweight Windows x64 PE compatibility runtime capable of
loading and executing Windows PE applications on Linux without Wine.

## Target

UPX 4.2.4 (upx_decompressed.exe) — `--version` prints "upx 4.2.4" and exits 0.
NOT ACHIEVED YET (2026-10-14: exit 134, no output; root cause BUG-026 verified).

## Operating system (how this project runs)

The owner sends CONTINUE. The agent executes .agent/CONTINUE_PROTOCOL.md:
reads state, selects one bounded task from .agent/backlog.md, baselines,
implements, tests, records evidence, updates state, commits, hands off.

## Execution Pipeline

PE load (preferred base, no relocations) → import resolution (161 table)
→ TEB/PEB → CRT init → exception dispatch (GCC C++ via SEH) → catch → output.

## Current Blocker

BUG-026: naked-stub g_cap_rip capture loads the stack pointer instead of the
saved return address → CONTEXT.Rip outside PE → walk ends at Frame[0] →
terminate/abort. Fix = one operand; see .agent/CURRENT_TASK.md.

## Canonical documents

docs/master/MASTER_PLAN.md (living plan) · .agent/state.md (canonical snapshot)
· .agent/CONTINUE_PROTOCOL.md (workflow) · .agent/SESSION_HANDOFF.md (last session)
