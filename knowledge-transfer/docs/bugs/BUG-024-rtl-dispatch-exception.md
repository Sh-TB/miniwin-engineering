# BUG-024: RtlDispatchException — Exception Dispatcher Implementation

**Status**: ACTIVE — Phase 1
**Priority**: HIGH (blocks UPX --version)
**Parent**: BUG-023 (SEH unwind infrastructure)

## Problem

MiniWin has working RtlLookupFunctionEntry and RtlVirtualUnwind (proven by EXP-NEXT and EXP-NEXT-2). However, there is no actual exception dispatcher. When RaiseException is called, the current naked stub only runs diagnostics and returns — it does not walk the unwind chain, discover handlers, or invoke them.

**Current blocker for UPX --version:**
- UPX calls `RaiseException(0x20474343)` (GCC C++ exception code)
- Naked stub captures state and runs diagnostic walk
- Stub returns to caller → PE code continues into UnhandledExceptionFilter → SIGSEGV
- The PE expects the exception to be caught by a GCC personality routine via the EHANDLER chain

## Proven State (EXP-NEXT-2 PASS)

```
RaiseException(IAT)
    ↓
naked stub captures real RIP/RSP
    ↓
CONTEXT construction verified
    ↓
RtlLookupFunctionEntry verified
    ↓
RtlVirtualUnwind verified
    ↓
parent RIP recovery verified
    ↓
EHANDLER discovery verified
```

Frame 0 (func_C): no handler, unwind to parent — correct
Frame 1 (func_B): EHANDLER flag detected, handler RVA found — correct

## Implementation Plan

### Phase 1: Isolated Exception Dispatcher Module

Add `mw_RtlDispatchException(EXCEPTION_RECORD* record, CONTEXT* context)`:

1. Walk frames using RtlLookupFunctionEntry + RtlVirtualUnwind
2. For each frame with EHANDLER flag:
   - Extract handler address
   - Build DISPATCHER_CONTEXT
   - Call handler with (EXCEPTION_RECORD*, ESTABLISHER_FRAME, CONTEXT*, DISPATCHER_CONTEXT*)
   - Check return: ExceptionContinueSearch / ExceptionContinueExecution
3. Continue walking if handler returns ContinueSearch

### Phase 2: Scope (UPX-only features)

**Required:**
- EHANDLER / UNW_FLAG_EHANDLER
- GCC/MinGW exception path (personality routine)
- dispatcher context with LSDA

**Ignored initially:**
- Vectored handlers (VEH chain)
- Debugger exceptions
- Async unwind
- XMM unwind complexity
- UHANDLER (termination handler)

### Phase 3: Synthetic Tests (before UPX integration)

- **Test A**: func_C throws, func_B catches → handler discovered
- **Test B**: 3 nested frames, handler at frame 3 → deep discovery
- **Test C**: handler returns ContinueSearch → walker continues
- **Test D**: handler returns ContinueExecution → execution resumes

### Phase 4: UPX Integration

After all synthetic tests pass, replace the naked diagnostic stub with real dispatch.

## Frozen Components (DO NOT MODIFY)

- PE loader (`load_pe`)
- Import resolver
- CRT stubs
- TLS stubs
- Heap allocation
- TEB/PEB setup
- Signal handler

## Engineering Rules

1. Evidence before code
2. Root cause only
3. Preserve working baseline
4. Synthetic proof before real binary
5. No stack scanning hacks
6. No blind patches
7. Checkpoint before modification
8. Update worklog after every milestone

## Key Files

- `src/loader.c` — main implementation (2349 lines)
- `include/pe.h` — PE structures, CONTEXT offsets, DISPATCHER_CONTEXT
- `tests/exp_next2/` — EXP-NEXT-2 harness (proven PASS)

## Previous Bugs (all fixed, do NOT regress)

1. rel32 VA/RVA confusion
2. .pdata RVA handling
3. .pdata sorting
4. UNWIND_CODE encoding (bits [15:12]=op, [11:8]=info, [7:0]=offset)
5. EH handler RVA alignment
6. Import directory RVA
