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

## ADR-010 (2026-10-15, this session): EH hacks isolated behind `MINWIN_EH_HACKS`, default OFF
EH_FIX (dispatcher LSDA override) and CXX-EH-FIX (load_pe byte patch) violate the engineering rules (app-specific, target-modifying, undocumented) and EH_FIX's dispatch-1 hijack was live-implicated as BUG-029's poison. Decision: flag-guard both (default OFF), delete nothing (history preservation). `MINWIN_EH_HACKS=1` restores the legacy behavior exactly (verified). The clean default path must reach the catch through the PE's own personality; any future fix must generalize via the PE's own protocol data (e.g. BUG-030's fingerprint-guarded 4-param ER), never via hardcoded sample addresses. Status: ACTIVE (EXP-038).

## ADR-011 (2026-10-15, CONTINUE #5): GCC SEH unwind chain — resume commands + raise-site register fidelity
BUG-031 live evidence (EXP-038 §5.1, EXP-039 §4-§7) established three facts: (1) the GCC bridge's unwind-phase re-raise (`0x21474343`, 4-param ER) and the continue-helper's direct RtlUnwindEx are *resume commands*, not fresh exceptions — a fresh frame walk can never serve them because the raising C stack has no PE RUNTIME_FUNCTIONs; (2) `seh_build_context`'s CPU-state volatile reads are compile-dependent garbage that flips the personality's phase-1 decisions per recompile; (3) the PE's GCC core additionally depends on rax/r10/r11 at the personality-call boundary (ms_abi-undefined; observed crash-vs-wrong-answer flip). Decision: the dispatcher honors `0x21474343` 4-param records as synthesized landing-pad resumes (design (a) of EXP-038 §5.1); the continuation path resumes the target LP directly from the OriginalContext; CONTEXT volatiles and the trampoline's rax/r10/r11 are pinned to the stub's true raise-site captures. No sample-specific constants are involved (fingerprint: code+nparams+NONCONTINUABLE+LP-inside-image). Status: **ACTIVE** — validated by G1 acceptance (EXP-039 §7-§8); generalization burden moves to TASK-TEST-004/005 (synthetic multi-frame EH suite).
