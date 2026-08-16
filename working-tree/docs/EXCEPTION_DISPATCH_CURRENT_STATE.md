# Exception Dispatch Current State Audit

**Date**: 2026-08-15  
**Scope**: MiniWin `src/loader.c` exception dispatch path  
**Trigger**: Step 1 of MiniWin Exception Dispatch 5-step plan  
**Files Inspected**: `src/loader.c` (3144 lines), `include/pe.h` (269 lines)  
**Checkpoint**: `src/loader.c.pre_exception_audit`

---

## 1. RaiseException Entry Path

### 1.1 Naked Stub (`mw_RaiseException`, line 1502)

**Signature**: `__attribute__((naked, ms_abi)) void mw_RaiseException(uint32_t code, uint32_t flags, uint32_t nargs, uint64_t* args)`

**ABI**: The stub is declared `ms_abi` and `naked`, meaning no compiler-generated prolog. The PE calls RaiseException via the IAT using Windows ms_abi calling convention (RCX, RDX, R8, R9).

**State Capture Sequence** (lines 1505-1539):
1. Saves nonvolatile registers: RBX, RBP, R12-R15 (6 pushes = 48 bytes)
2. Allocates 0x28 bytes of stack space (`sub rsp, 0x28`)
3. Captures `RSP_entry` = RSP after all pushes + sub (via `leaq 0x58(%rsp), %rax`; 6*8 + 0x28 = 88 = 0x58)
4. Reads return address from `[RSP_entry]` -> `g_cap_rip`
5. Computes caller's RSP = `RSP_entry + 8` -> `g_cap_rsp`
6. Stores raw RSP_entry -> `g_cap_rsp_entry`
7. Copies parameters from ms_abi registers -> `g_cap_code`, `g_cap_flags`, `g_cap_nargs`, `g_cap_args`

**Critical Detail**: `g_cap_rip` is set to the return address to the PE caller of RaiseException (the instruction after `call [IAT_RaiseException]`). This is where execution resumes if the exception is not handled. For the GCC unwinder inside the PE, this PC must fall within a RUNTIME_FUNCTION's [BeginAddress, EndAddress) range so that RtlLookupFunctionEntry can find the caller's frame.

**Verified**: EXP-NEXT proof match confirmed `[rsp_entry] == ret_addr`.

### 1.2 Dispatcher Call

Line 1541: `call seh_dispatch_exception` — calls the C dispatcher using SysV ABI (the naked stub itself is technically ms_abi entry but the internal call uses SysV since both are within the same compilation unit).

## 2. CONTEXT Creation (`seh_build_context`, line 1059)

### 2.1 Buffer
- `uint8_t ctx[CONTEXT_SIZE]` (CONTEXT_SIZE = 0x4D0 = 1232 bytes) — stack-allocated in `seh_dispatch_exception` at line 1231.
- Zeroed with `memset(ctx, 0, CONTEXT_SIZE)`.

### 2.2 ContextFlags
- Line 1063: `*(uint64_t*)(ctx + 0x00) = 0x10007F` — sets CONTEXT_FULL (0x100007). **BUG**: Value 0x10007F is CONTEXT_FULL | CONTEXT_DEBUG_REGISTERS (0x100000 | 0x10007F). The correct value for CONTEXT_FULL alone is `0x100007`. This is cosmetic — the dispatcher never checks ContextFlags — but is technically incorrect.

### 2.3 RIP and RSP
- Line 1066-1067: Set from `g_cap_rip` and `g_cap_rsp` (captured by naked stub).
- `g_cap_rip` = return address after RaiseException call in PE code.
- `g_cap_rsp` = caller's RSP (RSP_entry + 8).

### 2.4 Nonvolatile Registers
- Lines 1070-1102: All 16 GP registers read from current CPU state via inline asm.
- **ISSUE**: These registers reflect the CPU state AT THE TIME of `seh_build_context`, which is AFTER the naked stub has pushed RBX, RBP, R12-R15 and modified RSP. The values of RBX, RBP, R12-R15 in the CONTEXT will be the values SAVED BY THE NAKED STUB (i.e., the caller's values, correctly pushed). However, RCX, RDX, R8, R9 will have been clobbered by the dispatcher's own code path between the naked stub entry and this point.

**Assessment**: For the UPX case (GCC C++ exceptions), the personality function uses the CONTEXT primarily for RIP and RSP to determine the call site. RCX/RDX/R8/R9 are not critical for the dispatch decision. This is acceptable but fragile.

### 2.5 EFlags, SegCs, SegSs
- **NOT SET**. These fields remain zero in the CONTEXT. On real Windows, EFlags contains the interrupt flag and other status bits. The GCC personality function does not read EFlags for exception dispatch decisions, so this is currently benign.

## 3. The Dispatcher (`seh_dispatch_exception`, line 1167)

### 3.1 Overview

The dispatcher implements the standard Windows x64 SEH frame walk algorithm:

```
1. Build EXCEPTION_RECORD from globals
2. Build CONTEXT via seh_build_context()
3. Register CONTEXT for RtlUnwindEx (setjmp/longjmp)
4. Try VEH chain (if any registered)
5. For each frame (up to DISPATCH_MAX_FRAMES=64):
   a. Check RIP is inside PE image
   b. RtlLookupFunctionEntry (internal SysV version)
   c. RtlVirtualUnwind (internal SysV version)
   d. If handler found: build DISPATCHER_CONTEXT, call handler
   e. Check disposition (ContinueExecution / ContinueSearch)
6. If no handler handles it: return DISP_RESULT_NOT_HANDLED
```

### 3.2 EXCEPTION_RECORD Construction (lines 1175-1190)

- `er.ExceptionCode = g_cap_code` (0x20474343 for GCC C++ exceptions)
- `er.ExceptionFlags = g_cap_flags` (0x0)
- `er.ExceptionRecord = 0` (no nested exception)
- `er.ExceptionAddress = g_cap_rip` (return address in PE code)
- `er.NumberParameters = g_cap_nargs` (1 for GCC exceptions)
- `er.ExceptionInformation[0]` = pointer to `_Unwind_Exception` object
- **Line 1178**: `memcpy(&g_cap_er, &er, sizeof(EXCEPTION_RECORD))` — **BUG**: This copies a zeroed ER into `g_cap_er` BEFORE filling in the fields. The actual copy with real data is never done. `g_cap_er` remains all zeros. This means if `RtlUnwindEx` reads `g_cap_er` for the exception record, it gets garbage (zeros).

**BUG SEVERITY**: MEDIUM. `mw_RtlUnwindEx` receives `exc_record` as an explicit parameter (line 1638), so it doesn't read `g_cap_er` directly. However, if any personality function reads `g_cap_er` (e.g., for nested exception info), it would see zeros.

### 3.3 RtlUnwindEx Integration (lines 1234-1275)

The dispatcher sets up a `setjmp`/`longjmp` mechanism for the RtlUnwindEx unwind path:

- `g_unwind_ctx = ctx` — registers the CONTEXT buffer
- `g_is_unwinding = 0` — reset flag
- `g_unwind_target_ip = 0`, `g_unwind_target_frame = 0` — reset targets
- If `setjmp(g_unwind_jmpbuf) != 0`: this is the RtlUnwindEx return path
  - Copies all 16 register values from CONTEXT to `g_unwind_regs[]` globals
  - Sets `g_unwind_target_ip` and `g_unwind_target_frame`
  - Returns `DISP_RESULT_CONTINUE_EXEC`

**Purpose**: When the GCC personality function calls `RtlUnwindEx`, RtlUnwindEx modifies the CONTEXT to target the catch landing pad, sets `g_is_unwinding = 1`, and `longjmp`s back here. The naked stub then checks `g_is_unwinding` and uses the `g_unwind_regs[]` to restore all registers before jumping to the landing pad.

**Assessment**: This mechanism is correct in concept. The use of globals to bridge between the setjmp handler and the naked stub is necessary because the stack-local CONTEXT might be overwritten by intervening function calls.

### 3.4 VEH Chain (lines 1277-1309)

- Iterates `g_veh_handlers[]` in reverse order (last registered = first called)
- Builds `EXCEPTION_POINTERS` on stack: `{ER*, CONTEXT*, ExceptionAddress}`
- Calls each VEH handler via `call_veh_aligned` (proper ms_abi trampoline)
- If VEH returns `EXCEPTION_CONTINUE_EXECUTION` (-1): immediately returns `DISP_RESULT_CONTINUE_EXEC`

**Current State**: `g_veh_count = 0` (UPX calls `AddVectoredExceptionHandler` at line 17 of crash log, but the trace shows it stores a handler at 0x2009240; however, the exp_next3 trace shows `VEH chain: 0 handlers`). This suggests the VEH registration may not be persisting correctly, or the second RaiseException call's trace starts after VEH was already removed.

**Assessment**: VEH chain implementation appears correct. The 0-handler state in exp_next3 is consistent with the UPX execution flow.

### 3.5 Frame Walk Loop (lines 1311-1493)

For each frame:

**a. Bounds check** (line 1320): `if (current_rip < img_base || current_rip >= img_end) break;`

**b. RF Lookup** (line 1326): `RUNTIME_FUNCTION* rf = seh_internal_lookup(current_rva);`
- Internal SysV binary search through `.pdata` (3030 entries for UPX)
- If NULL: walk ends (`break`)

**c. Virtual Unwind** (line 1339): `void* handler = seh_internal_virtual_unwind(current_rip, rf, ctx, &est_frame, &handler_rva, &lsda);`

**d. No Handler Path** (lines 1346-1361):
- Reads parent return address: `parent_rip = *(uint64_t*)(est_frame)`
- Updates context: `ctx.Rip = parent_rip`, `ctx.Rsp = est_frame + 8`
- Continues to next frame

**e. Handler Found Path** (lines 1363-1492):
- Builds DISPATCHER_CONTEXT (lines 1369-1389)
- Adjusts `dc.ControlPc = current_rip - 1` (line 1379) — ensures PC falls within the call instruction's range in LSDA
- Also adjusts `ctx.Rip` to `saved_ctx_rip - 1` (line 1393)
- Calls handler via `call_handler_aligned` (line 1460)
- Checks disposition:
  - `DISP_ExceptionContinueExecution` (0) -> return `DISP_RESULT_CONTINUE_EXEC`
  - `DISP_ExceptionContinueSearch` (1) -> read parent RIP, continue walk
  - `DISP_ExceptionNestedException` (2) or `DISP_ExceptionCollidedUnwind` (3) -> break

### 3.6 DISPATCHER_CONTEXT Construction (lines 1369-1389)

| Field | Value | Notes |
|-------|-------|-------|
| ControlPc | `current_rip - 1` | Adjusted for GCC LSDA call site matching |
| ImageBase | `img_base` | Base of mapped PE image |
| FunctionEntry | `rf` | Pointer to RUNTIME_FUNCTION |
| EstablisherFrame | `est_frame` | From RtlVirtualUnwind |
| TargetIp | 0 | No specific unwind target yet |
| ContextRecord | `ctx` | Pointer to CONTEXT buffer |
| LanguageHandler | `handler` | Handler VA (PE code) |
| HandlerData | `lsda` | Pointer to language-specific data |
| HistoryTable | 0 | Not used |
| ScopeIndex | 0 | Not used |

**Assessment**: DISPATCHER_CONTEXT construction matches the Windows x64 spec. The `ControlPc - 1` adjustment is a critical fix for GCC personality functions.

## 4. RtlLookupFunctionEntry

### 4.1 Internal SysV Version (`seh_internal_lookup`, line 618)

- Binary search through `g_pdata_rva` / `g_num_rt_functions`
- Input: RVA (not VA)
- Returns: pointer to RUNTIME_FUNCTION in mapped image, or NULL
- **No logging** (unlike the ms_abi export version)

### 4.2 ms_abi Export Version (`mw_RtlLookupFunctionEntry`, line 1609)

- Same algorithm but with ms_abi calling convention
- Also sets `*out_image_base` output parameter
- **Has logging** via MW_TRACE
- Called by the PE's internal GCC unwinder (e.g., `_Unwind_RaiseException`)

### 4.3 .pdata Source

- `g_pdata_rva` and `g_num_rt_functions` set in `load_pe()` at lines 2972-2975
- Read from Exception Data Directory (DD_EXCEPTION = index 3)
- UPX: RVA=0x1f8000, Size=0x8e08, 3030 entries

**Known Issue**: 3030 entries sorted by BeginAddress but NOT fully contiguous — gaps exist between entries (BUG-023-expnext-report Finding 4). This can cause the walk to leave PE code prematurely.

## 5. RtlVirtualUnwind

### 5.1 Internal SysV Version (`seh_internal_virtual_unwind`, line 883)

**Used by**: The dispatcher (`seh_dispatch_exception`)

**Inputs**:
- `control_pc`: absolute PC
- `rf`: RUNTIME_FUNCTION pointer
- `ctx`: raw CONTEXT buffer (modified in place)
- Outputs: `*establisher_frame`, `*out_handler_rva`, `*out_lsda`

**Returns**: Handler VA (or NULL)

**Unwind Code Processing** (lines 940-1020):
- Parses UNWIND_INFO header: version, flags, prolog_size, count_codes, frame_reg, frame_off
- Handles CHAININFO (lines 902-927): delegates to chained RUNTIME_FUNCTION
- Simulates unwind codes in reverse to "undo" the prolog:
  - UWOP_PUSH_NONVOL (0): pop register from stack
  - UWOP_ALLOC_LARGE (1): add allocation to RSP
  - UWOP_ALLOC_SMALL (2): add (op_info+1)*8 to RSP
  - UWOP_SET_FPREG (3): compute frame pointer
  - UWOP_SAVE_NONVOL (4): restore register from stack
  - UWOP_SAVE_NONVOL_FAR (5): restore register from stack (32-bit offset)
  - UWOP_SAVE_XMM128 (6): skip (XMM not restored)
  - UWOP_SAVE_XMM128_FAR (7): skip
  - UWOP_PUSH_MACHFRAME (8): handle machine frame (interrupt/trap)
  - Unknown opcodes (9+): skip

**Handler Discovery** (lines 1036-1052):
- If `flags & UNW_FLAG_EHANDLER`: compute handler offset, read handler RVA
- LSDA = bytes immediately after handler RVA (4 bytes)
- Returns `g_image_base + handler_rva` as handler VA

**Known Issues**:
1. CHAININFO handler sets `*establisher_frame = chained_est = 0` (line 913) — **BUG**: CHAININFO establisher frame is not calculated. Should run unwind codes for the chained function to compute it.
2. XMM registers are not restored (opcodes 6, 7 just skip). Benign for UPX since the personality function doesn't depend on XMM state for dispatch decisions.
3. Safety bound check `new_rsp <= rsp + 4096` (line 960) may prevent valid register restores if the function allocates more than 4096 bytes of stack. This is a guard against wild pointer dereferences but could be too restrictive.

### 5.2 ms_abi Export Version (`mw_RtlVirtualUnwind`, line 1724)

**Used by**: The PE's internal GCC unwinder

**Signature**: Matches Windows `RtlVirtualUnwind`:
```
void* RtlVirtualUnwind(
    uint32_t handler_type,   // UNW_FLAG_EHANDLER or UNW_FLAG_UHANDLER
    uint64_t image_base,
    uint64_t control_pc,
    void* function_entry,    // RUNTIME_FUNCTION*
    void* context_ptr,       // CONTEXT*
    void* handler_data_ptr,  // output: LSDA
    uint64_t* establisher_frame_ptr,  // output
    void* context_pointers   // not used
);
```

**Differences from internal version**:
1. Takes `handler_type` parameter (allows caller to request EHANDLER or UHANDLER)
2. Operates on `CONTEXT*` (Windows struct) rather than raw `uint8_t*`
3. Has `context_pointers` parameter (ignored)
4. Has more detailed MW_TRACE logging
5. Uses a `warned_unknown[]` array to suppress repeated unknown-opcode warnings (line 1852)

**Same unwind code processing logic** as the internal version.

**Same CHAININFO bug**: Establisher frame set to 0 (line 1750 region).

## 6. RtlUnwindEx (`mw_RtlUnwindEx`, line 1638)

### 6.1 Purpose
Called by the GCC personality function when it finds a catch handler. RtlUnwindEx must:
1. Run cleanup handlers (dtors) for intermediate frames
2. Set CONTEXT to resume at the catch landing pad
3. **Never return** to the caller

### 6.2 Current Implementation (lines 1638-1722)

**Simplified approach**:
- **Skips cleanup of intermediate frames** (lines 1658-1665 comment explains this is intentional for UPX)
- Modifies CONTEXT: `ctx.Rip = target_ip`, `ctx.Rsp = target_frame`
- Sets `ctx.Rax = exception_object` (line 1683) — the exception object pointer is passed as the 4th argument
- Sets `g_is_unwinding = 1`
- `longjmp(g_unwind_jmpbuf, 1)` — never returns

### 6.3 Known Gaps
1. **No frame cleanup**: Intermediate frame destructors are NOT called. For UPX `--version`, this is likely acceptable since the throw path is in error handling code that may not have significant cleanup. For a general solution, this would cause resource leaks.
2. **No UHANDLER invocation**: Termination handlers (UNW_FLAG_UHANDLER) are never called during unwind.
3. **Diagnostic dumps** (lines 1694-1711): The function includes extensive memory dumps for debugging. These are harmless but add noise to the trace.

## 7. ABI Trampolines

### 7.1 `call_handler_aligned` (line 1139)

Calls ms_abi PE exception handlers from SysV code:
- Creates aligned stack frame (push rbp, and rsp -16, sub 0x20 for shadow space)
- Sets RCX=ER, RDX=est_frame, R8=ctx, R9=dc
- Calls handler
- Clobbers RCX, RDX, R8, R9, R10, R11 (standard caller-saved)

**Assessment**: Correct. The 32-byte shadow space and 16-byte alignment satisfy ms_abi requirements.

### 7.2 `call_veh_aligned` (line 1118)

Calls ms_abi VEH handlers from SysV code:
- Same alignment pattern
- VEH handler takes PEXCEPTION_POINTERS* in RCX only

**Assessment**: Correct.

## 8. EXP-NEXT-3 Runtime Evidence Analysis

### 8.1 First RaiseException (exp_next3_full_trace.txt)

**Exception State**:
- Code: 0x20474343 (GCC C++ exception)
- Flags: 0x0
- Address: 0x49d5b1 (return address after call RaiseException in PE)
- NumParams: 1
- Param[0]: 0x2a8f320 (pointer to _Unwind_Exception object)
- Exception class: 0x474e5543432b2b00 ("\0++CUNG" — GCC exception class)

### 8.2 Frame Walk Results

| Frame | RIP | RVA | RF Begin | RF End | Handler | Action |
|-------|-----|-----|----------|--------|---------|--------|
| 0 | 0x49d5b1 | 0x9d5b1 | 0x9d560 | 0x9d5bb | None | Walk to parent |
| 1 | 0x4e0203 | 0xe0203 | 0xe0190 | 0xe0211 | None | Walk to parent |
| 2 | 0x4e02d9 | 0xe02d9 | 0xe0290 | 0xe02da | None | Walk to parent |
| 3 | 0x401593 | 0x1593 | 0x1570 | 0x15bb | 0xe0220 | Call handler |

**Frame 0-2**: No EHANDLER flag — correctly skipped, parent RIP recovered.

**Frame 3**: EHANDLER at RVA 0xe0220 (the GCC `__gcc_personality_v0` trampoline). Handler called.

### 8.3 Handler Call (Frame 3)

- Handler bytes: `48 83 ec 38 48 8d 05 85 fb fc ff 48 89 44 24 20`
  - `sub rsp, 0x38` (56 bytes shadow)
  - `lea rax, [rip-0x3fb]` (loads address of a data structure)
  - `mov [rsp+0x20], rax` (stores to shadow space)
- LSDA at 0x601088: `ff ff 01 08 20 03 35 00 45 06 00 00 01 00 00 00`
  - LPStart: 0xff (omitted, relative to function start)
  - TType_enc: 0xff (omitted)
  - CS_enc: 0x01 (uleb128)
  - Call site table follows
- **Disposition**: 1 = `DISP_ExceptionContinueSearch`

### 8.4 Handler Call (Frame 4)

- Handler returned ContinueSearch on Frame 3, so walk continued
- Frame 4: RIP=0x4032f2, RVA=0x32f2, RF begin=0x32c0, handler=0xe0220
- Different LSDA at 0x601200: `ff 9b 15 01 04 2d 05 5c 05 02 00 01 7d 00 7d 00`
- **Trace ends here** — no disposition logged after "calling..."

### 8.5 Crash Analysis

The trace for Frame 4 ends abruptly after "calling...". This means either:
1. The handler at Frame 4 CRASHED (SIGSEGV/SIGABRT inside the PE personality function)
2. The handler called RtlUnwindEx which triggered a different code path
3. The handler entered an infinite loop or called abort()

Given the crash log shows `abort()` being called after the second `RaiseException`, the most likely scenario is:
- Frame 3 handler returns ContinueSearch (this try/catch doesn't match this exception type)
- Frame 4 handler is called but either crashes or also returns ContinueSearch
- Walk continues, eventually leaves PE code
- Dispatcher returns DISP_RESULT_NOT_HANDLED
- Naked stub returns to caller (PE code continues after RaiseException)
- PE's internal unwinder sees RaiseException returned (exception not handled), calls abort()

**Alternative hypothesis**: The Frame 4 handler might have called RtlUnwindEx (to perform a catch), but RtlUnwindEx's simplified implementation didn't correctly set up the landing pad state, causing a crash when the naked stub jumped to the wrong address.

## 9. Bug Summary

### Critical

| # | Location | Description |
|---|----------|-------------|
| C1 | Line 1178 | `memcpy(&g_cap_er, &er, ...)` copies zeroed ER (before field assignment). Should be AFTER line 1190. |
| C2 | Frame 4 handler | Handler call at Frame 4 ends without logged disposition — likely crash inside PE personality function |

### Medium

| # | Location | Description |
|---|----------|-------------|
| M1 | Line 913 | CHAININFO establisher frame hardcoded to 0 instead of computed |
| M2 | Line 1063 | CONTEXT.ContextFlags = 0x10007F (includes DEBUG_REGISTERS bit). Should be 0x100007. |
| M3 | Lines 1658-1666 | RtlUnwindEx skips all intermediate frame cleanup (dtors not called) |

### Low

| # | Location | Description |
|---|----------|-------------|
| L1 | Lines 1403-1427 | Extensive diagnostic dumps in handler call path (performance noise, but harmless) |
| L2 | Lines 1694-1711 | Diagnostic dumps in RtlUnwindEx (same) |
| L3 | Line 3143 | Duplicate `const uint32_t EH_UNWINDING = 0x02` (already defined in pe.h) |
| L4 | Lines 960, 1008 | Safety bound `new_rsp <= rsp + 4096` may be too restrictive for large stack frames |

### Structural

| # | Description |
|---|-------------|
| S1 | .pdata has gaps (3030 entries not fully contiguous) — can cause premature walk termination |
| S2 | SEH unwind code simulation doesn't handle GCC-specific opcodes 9-15 (silently skipped) |
| S3 | No UnhandledExceptionFilter invocation when all frames return ContinueSearch |

## 10. What Works

1. **Naked stub state capture**: RIP, RSP, and parameters correctly captured (proven by EXP-NEXT proof match)
2. **.pdata binary search**: RtlLookupFunctionEntry finds correct RUNTIME_FUNCTION for valid RVAs
3. **UNWIND_INFO parsing**: Version, flags, prolog size, count codes, frame register all parsed correctly
4. **Unwind code simulation**: PUSH_NONVOL, ALLOC_LARGE, ALLOC_SMALL, SET_FPREG, SAVE_NONVOL, SAVE_NONVOL_FAR all implemented
5. **EHANDLER discovery**: Handler RVA correctly extracted from UNWIND_INFO after alignment
6. **Frame walking**: Successfully walks 4 frames from RaiseException to a handler
7. **DISPATCHER_CONTEXT construction**: Matches Windows x64 spec with ControlPc-1 adjustment
8. **ABI trampolines**: ms_abi handler calls correctly aligned and register-assigned
9. **RtlUnwindEx longjmp mechanism**: Conceptually correct setjmp/longjmp bridge
10. **VEH chain**: Implementation correct (0 handlers for UPX case)

## 11. What Doesn't Work (Current Crash Point)

The dispatch chain reaches Frame 4, calls the handler, and the trace ends. The PE then calls abort(). This indicates one of:

1. **The Frame 4 personality function crashed**: Most likely. The GCC personality function calls back into RtlVirtualUnwind and RtlLookupFunctionEntry (the ms_abi export versions). If these have subtle ABI or logic issues, the personality function could crash.

2. **All handlers returned ContinueSearch, walk ended, exception unhandled**: The dispatcher returns DISP_RESULT_NOT_HANDLED, the naked stub returns normally, the PE sees RaiseException returned, and calls abort().

3. **RtlUnwindEx was called but failed**: If the personality found a catch and called RtlUnwindEx, but the longjmp target or CONTEXT modifications were wrong, execution could jump to an invalid address.

**Most likely root cause**: The trace ends at Frame 4 "calling..." with no disposition. If the handler completed successfully, we'd see the disposition log. The absence suggests the handler itself crashed (SIGSEGV inside the PE personality function), likely because the personality called back into our ms_abi stubs (RtlVirtualUnwind, RtlLookupFunctionEntry) with incorrect context, or because the CONTEXT we passed had incorrect register values.

## 12. Line-by-Line Component Map

| Lines | Component | Status |
|-------|-----------|--------|
| 240-249 | VEH globals, .pdata globals | OK |
| 251-277 | AddVectoredExceptionHandler, RemoveVectoredExceptionHandler | OK |
| 577-615 | Capture globals, unwind globals, g_unwind_regs[] | OK |
| 618-629 | `seh_internal_lookup` (SysV RF lookup) | OK |
| 631-660 | `seh_unwind_alloc` (UNWIND_INFO alloc parser) | OK (helper) |
| 662-700 | `scan_prolog_alloc` (prolog byte scanner) | OK (helper) |
| 851-877 | BUG-024 header, DISPATCH_MAX_FRAMES, result codes | OK |
| 879-1053 | `seh_internal_virtual_unwind` | OK (M1: CHAININFO est=0) |
| 1055-1103 | `seh_build_context` | OK (M2: ContextFlags) |
| 1105-1108 | `exception_handler_fn` typedef | OK |
| 1110-1133 | `call_veh_aligned` | OK |
| 1135-1165 | `call_handler_aligned` | OK |
| 1167-1499 | `seh_dispatch_exception` | C1 (g_cap_er copy order), S3 (no UEF) |
| 1501-1583 | `mw_RaiseException` naked stub | OK |
| 1586-1590 | `mw_RaiseException_baseline` (dead code) | OK |
| 1608-1636 | `mw_RtlLookupFunctionEntry` (ms_abi export) | OK |
| 1638-1722 | `mw_RtlUnwindEx` | M3 (no cleanup) |
| 1724-1896 | `mw_RtlVirtualUnwind` (ms_abi export) | OK |
| 2971-2980 | .pdata loading in load_pe() | OK |
| 3143 | Duplicate EH_UNWINDING | L3 |
