# ARCHITECTURE DECISIONS — Record (ADR-style, appended per decision)

## ADR-000 (historical, 2026-08): single-file loader
`src/loader/loader.c` holds everything. Rationale then: velocity. Consequence now: 3,481-line file with mixed concerns. **Decision: do NOT split speculatively** (mission §11). Split only when a refactor serves testability/safety with a mapped dependency graph (docs/master/EXECUTION_PIPELINE.md is the seed). Status: ACTIVE.

## ADR-001 (historical, D-001): stack-local CONTEXT in dispatcher
Kept; longjmp restores RSP to the setjmp frame, making the buffer durable enough; registers are copied to globals before any further calls (L1347-1367). Status: ACTIVE, re-validate after BUG-026 fix.

## ADR-002 (historical, D-002): g_unwind_regs global bridge
Naked stub reads globals; avoids stack-lifetime hazards. Status: ACTIVE.

## ADR-003 (historical, D-003): RtlUnwindEx skips intermediate cleanup
Acceptable for UPX `--version` scope only; documented gap. Status: ACTIVE-WITH-LIMIT; revisit in Phase C (cleanup semantics) — required for generalization.

## ADR-004 (historical, D-004): delegate to GCC personality; don't parse LSDA in dispatcher
**PARTIALLY BROKEN IN PRACTICE:** current source contradicts this decision twice (dispatcher EH_FIX LSDA override L1607-1644; load_pe LSDA byte patch L3263-3287) — undocumented, app-specific. Status: **CONFLICT — resolution owed (API-005): default hacks OFF, restore ADR-004 behavior, re-evaluate with evidence.**

## ADR-005 (2026-08, fdb939c): MAP_FIXED preferred-base mapping
Chosen to eliminate silent fallback-without-relocation corruption. Accepted risk: MAP_FIXED can destroy pre-existing mappings (MAP-1/2). Status: ACTIVE for current target class; D-MAP-A (collision detection) required before generalization; D-MAP-B (relocations) required for any non-preferred-base target.

## ADR-006 (2026-08): naked ms_abi stub for RaiseException capture
Proven necessary (compiler prolog instability, EXP-NEXT). Status: ACTIVE. **Defect BUG-026 (wrong operand reintroduced during later asm edits) does not invalidate the design — it validates the need for a stub unit test (proposed Tier-A: assemble-and-verify capture sequence).**

## ADR-007 (2026-10-14, this session): dual internal-SysV + ms_abi unwind implementations
Both exist (internal for dispatcher, ms_abi exports for the PE's own unwinder). Duplication is a correctness risk (bugs must be fixed ×2 — already true for ALLOC_LARGE N-1). Status: **KEEP SHORT-TERM; consolidation candidate after Phase C with Tier-A tests guarding both.**

## ADR-008 (2026-10-14, this session): evidence-gated landing-pad restoration
After BUG-026, landing-pad correctness proceeds by adopting the personality-provided OriginalContext (real RtlUnwindEx 6th-arg semantics) rather than recomputing state, with sanity checks. Rationale: it is what real Windows delivers, carries the RDX selector, and removes EXP-035's re-derivation (which embeds ALLOC_LARGE bugs). Status: PROPOSED — validated by EXP-038 before becoming default.

## ADR-009 (2026-10-14, this session): Rust `runtime/` stays out of the C critical path
~5,600 lines of parallel Rust implementation exist; not referenced by the Makefile; never verified. Status: PRESERVED, FROZEN — no effort without an explicit owner decision (scope rule: MiniWin C loader is the mission).
