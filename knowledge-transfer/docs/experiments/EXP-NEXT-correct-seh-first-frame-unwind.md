# EXP-NEXT: Correct SEH First-Frame Handling and Unwind Validation

## Experiment ID
EXP-NEXT-001

## Date
2026-08-07

## Status
COMPLETE — Evidence Collected

## Goal
Determine whether we can reliably reach a PE frame containing an EH handler
via prolog-corrected stack walking from the RaiseException call site.

## Initial Hypothesis
Using prolog byte scanning to detect 'sub rsp, N' in function prologs,
we can correct RSP for frames with malformed UNWIND_INFO and walk the
stack to find a frame with EHANDLER.

## Tools Used
- Custom prolog scanner (scan_prolog_alloc function)
- EXP-NEXT diagnostic script (scripts/exp_next_diagnostic.py)
- .pdata static analysis
- Modified loader with diagnostic output

## Evidence

### FINDING 1 [CONFIRMED]: REX prefix classification bug
The scanner treated 0x48 (REX.W for `sub rsp`) as a REX prefix for push,
skipping the actual instruction.

**Before fix**: scan_prolog_alloc returned 0 (no alloc detected)
**After fix**: scan_prolog_alloc returned 0x28 (correct)

**Root cause**: 0x48 is in the REX range (0x40-0x4F), but it's actually
the REX.W prefix for `sub rsp, imm8`, not a push prefix.

### FINDING 2 [CONFIRMED]: EHANDLER reachable via prolog-corrected walk
First RaiseException call stack walk:
```
Frame[0]: RF[2015] 0x9d560, bytes=48 83 ec 28, prolog_alloc=0x28
Frame[1]: RF[2999] 0xe0190-0xe0211, MALFORMED
Frame[2]: RF[3003] 0xe0290-0xe02da, MALFORMED
Frame[3]: RF[10]   0x1570-0x15bb, EHANDLER at RVA 0xe0220
```
**RESULT**: EHANDLER at Frame[3] is reachable.

### FINDING 3 [CONFIRMED]: Systemic malformed UNWIND_INFO
Static analysis of 601 RF entries:
- 236 with slot overflows (count_codes < total slots)
- 87 with missing ALLOC (prolog has sub rsp but no ALLOC opcode)
- 20 with EHANDLER

These are NOT edge cases — the GCC/MinGW toolchain generates systematically
malformed unwind metadata.

### FINDING 4 [CONFIRMED]: Stack walk precision
First call: parent_rip=0x4e0203 (RF[2999] at 0xe0190-0xe0211)
- Off by one RF entry — boundary gap
Second call: parent_rip=0x49d680 (past RF[2015] and RF[2016])
- Walk hit .pdata gap, no EHANDLER reached

### FINDING 5 [UNKNOWN]: Second call walk diverges
The second RaiseException call (deeper stack) produces:
- Frame[0]: corrected RSP gives parent in RF[2017], which has MALFORMED unwind
- Frame[2]: leaves PE code entirely

### FINDING 6 [CONFIRMED]: Compiler prolog changes with compilation
Checkpoint binary (older gcc): frame size = 0x208 bytes
Current gcc 14.2.0: frame size varies (0x208 to 0x218)

**CONCLUSION**: Hardcoding frame offsets is FUNDAMENTALLY UNSAFE.
Fixed with `__attribute__((naked, ms_abi))` stub.

## Code Changes
- Added scan_prolog_alloc() function for x86-64 prolog scanning
- Fixed REX prefix classification (0x48 for sub rsp)
- Implemented diagnostic frame walk with validation
- Documented ABI bridge requirements (SysV vs ms_abi)

## Result
EHANDLER is reachable at Frame[3] via prolog-corrected walking.
The naked stub approach was validated as the correct solution for
precise register capture.

## Root Cause (of original problem)
The compiler-generated prolog of mw_RaiseException changes size between
compilations. Hardcoded frame offsets caused register capture to fail.

## Lessons Learned
1. NEVER hardcode compiler-generated frame offsets
2. `__attribute__((naked))` is essential for ABI boundary functions
3. GCC/MinGW produces systematically malformed UNWIND_INFO
4. .pdata tables may have gaps between entries
5. Direct SysV→ms_abi calls crash with XMM alignment issues

## Future Work
1. Implement full dispatcher using naked stub findings
2. Handle .pdata gaps in stack walk
3. Build CONTEXT from naked stub state
4. Implement DISPATCHER_CONTEXT construction
5. Parse GCC LSDA (at RVA 0x201088)
6. Call language handler via SysV ABI bridge

## Attached Evidence
- docs/architecture/prolog_analysis_mw_RaiseException.txt
- evidence/execution_logs/stderr_exp_next.txt
- scripts/exp_next_diagnostic.py
