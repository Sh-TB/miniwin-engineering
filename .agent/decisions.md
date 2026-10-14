# Architectural Decisions

## D-001: Stack-local CONTEXT buffer (2026-08-14)
CONTEXT is allocated on seh_dispatch_exception's stack (1232 bytes).
Risk: handler calls may use enough stack to overlap. Mitigated by
longjmp restoring RSP to setjmp point.

## D-002: Global g_unwind_regs for register transfer (2026-08-14)
After longjmp, CONTEXT buffer may be at risk. Registers are copied
to globals BEFORE any function calls, then naked stub reads globals.

## D-003: Skip intermediate frame cleanup in RtlUnwindEx (2026-08-14)
RtlUnwindEx does NOT walk frames or call cleanup handlers.
Acceptable for UPX --version (no critical dtors in throw path).
Must be implemented for real applications.

## D-004: GCC personality function delegation (2026-08-14)
We call the GCC personality function directly and forward its
EXCEPTION_DISPOSITION. We don't parse LSDA ourselves.

## (2026-10-14 session) Decisions D-005+ moved to docs/master/ARCHITECTURE_DECISIONS.md
Canonical ADR record now lives in docs/master/ARCHITECTURE_DECISIONS.md (ADR-000..ADR-009).
Summary of new decisions this session: ADR-005 MAP_FIXED accepted with owed collision detection; ADR-006 naked stub design retained (BUG-026 is an operand regression, not a design failure); ADR-008 OriginalContext adoption proposed pending EXP-038; ADR-009 Rust runtime/ frozen out of C critical path.
