# EXP-NEXT: Correct SEH First-Frame Handling and Unwind Validation

## Status
ACTIVE — Evidence Complete


## Date
2026-08-07

## Objective
Given the captured RaiseException state, determine whether we can reliably reach
a PE frame containing an EH handler via prolog-corrected
stack walking.

## Method
1. Naked stub captures RIP/RSP at RaiseException entry (confirmed: PROOF MATCH)
2. Internal SysV functions parse .pdata and scan function prolog bytes
3. Diagnostic walk logs every frame with full validation
4. Compares Experiment A (trust UNWIND_INFO) vs B (prolog-corrected)
5. Compiles on PE binary directly (static analysis)
6. Runtime test via modified loader

## Hypothesis
Using prolog byte scanning to detect 'sub rsp, N' in function
prologs, we can correct RSP for frames with malformed
UNWIND_INFO and walk the stack to find a frame with EHANDLER.

## Verified Findings

### FINDING 1 [CONFIRMED]: REX prefix classification bug
The scanner treated 0x48 (REX.W for `sub rsp`) as a REX
prefix for push, skipping the actual instruction.
Evidence: bytes at 0x9d560 are `48 83 ec 28`.
Before fix: scan_prolog_alloc returned 0 (no alloc detected).
After fix: scan_prolog_alloc returned 0x28 (correct).
Root cause: 0x48 is in the REX range (0x40-0x4F), but it's
actually the REX.W prefix for `sub rsp, imm8`, not a push prefix.

### FINDING 2 [CONFIRMED]: EHANDLER reachable via prolog-corrected walk
First RaiseException call stack walk:
```
Frame[0]: RF[2015] 0x9d560, bytes=48 83 ec 28, prolog_alloc=0x28
Frame[1]: RF[2999] 0xe0190-0xe0211, MALFORMED
Frame[2]: RF[3003] 0xe0290-0xe02da, MALFORMED
Frame[3]: RF[10]   0x1570-0x15bb, EHANDLER at RVA 0xe0220
```
RESULT: EHANDLER at Frame[3] is reachable via
prolog-corrected stack walking.
### FINDING 3 [CONFIRMED]: Systemic malformed UNWIND_INFO in UPX
Static analysis of 601 RF entries:
- 236 with slot overflows (count_codes < total slots)
- 87 with missing ALLOC (prolog has sub rsp but UNWIND_INFO has no ALLOC opcode)
- 20 with EHANDLER
These are not edge cases — the GCC/UPX toolchain generates
systematically malformed unwind metadata.
### FINDING 4 [CONFIRMED]: Stack walk precision
First call: parent_rip=0x4e0203 (RF[2999] at 0xe0190-0xe0211)
- Off by one RF entry — the caller's return address is at the boundary
 0xe0211/0xe0203 gap
Second call: parent_rip=0x49d680 (past RF[2015] and RF[2016])
- Walk hit .pdata gap, no EHANDLER reached
This imprecision is caused by FRAME_ALLOC and FRAME_ALLOC opcodes
in the UNWIND_INFO not matching the actual
prolog byte pattern.
### FINDING 5 [UNKNOWN]: Second call walk diverges
The second RaiseException call (deeper stack) produces:
- Frame[0]: corrected RSP gives parent in RF[2017], which has MALFORMED unwind
- Frame[2]: leaves PE code entirely
This happens because the PE function at 0xe0190-0xe0211 is 0x21a bytes with multiple
call sites, and the return address from one
site falls in a .pdata gap between RF[2016] and RF[2017].
### Compiler Prolog Bug (HISTORICAL, CONFIRMED)
GCC x64 prolog modifies RSP before inline assembly executes.
Hardcoded frame offsets are UNSAFE because the compiler changes frame size
between compilations. Fixed with `__attribute__((naked, ms_abi))` stub.
## Next Steps (BLOCKED until proof complete)
1. Fix `seh_unwind_alloc` to handle slot overflow correctly
2. Handle .pdata gaps in stack walk (RFC entries may not cover all code)
3. Implement proper CONTEXT builder from naked stub state
4. Implement DISPATCHER_CONTEXT construction
5. Implement __C_specific_handler to parse GCC LSDA (at RVA 0x201088)
6. Call language handler via SysV ABI bridge (avoid ms_abi XMM crash)
7. Only after ALL of the above: remove diagnostic code, wire into dispatcher
## Technical Details
### Naked Stub
- `__attribute__((naked, ms_abi))` — no compiler prolog
- Captures: RIP=[RSP_entry], RSP=entry+8, params
- Confirmed: `[rsp_entry] == ret_addr` (PROOF MATCH)
- ABI issue: ms_abi calls from SysV code crash with XMM alignment
- Solution: internal SysV-only functions for SEH logic
### Prolog Scanner Bug
- 0x48 (REX.W for sub rsp) was misclassified as REX prefix
- Fix: only treat 0x40-0x4F as REX prefix when
  followed by 0x50-0x57 (push)
- Before fix: returned 0, after fix: returns 0x28
### RUNTIME_FUNCTION table (.pdata)
- 3030 entries for UPX
- Sorted by BeginAddress, but NOT fully contiguous
- Gaps between entries can cause walk to leave PE code
- 20 frames with EHANDLER (out of 601 sampled)
- Handler RVA 0xe0220 appears in ~20 entries
### Prolog Alloc Detection
- Pattern: `48 83 ec NN` = sub rsp, imm8
- Pattern: `48 81 ec NN NN NN NN` = sub rsp, imm32
- After REX.W prefix and any REX+push prefix
### Call Chain to EHANDLER
Frame[0]: 0x9d560 — RaiseException wrapper, NO EHANDLER
Frame[1]: 0xe0190-0xe0211 — large function, MALFORMED unwind
Frame[2]: 0xe0290-0xe02da — medium function, MALFORMED unwind
Frame[3]: 0x1570-0x15bb — EHANDLER at 0xe0220
  (Handler = GCC __C_specific_handler personality function)
  LSDA at RVA 0x201088
## Files
- src/loader.c: EXP-NEXT diagnostic code (lines 566-830)
- include/pe.h: structures and constants
- scripts/exp_next_diagnostic.py: static PE analysis script
- miniwin-results/upx_decompressed.exe/api_trace.json: runtime trace evidence
- docs/bugs/BUG-023-expnext-report.md: this report
