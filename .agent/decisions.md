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
