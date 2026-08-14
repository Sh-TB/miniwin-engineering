# Experiment & Bug Index

Complete index of all experiments and bug reports in the repository.

---

## Experiments

### EXP-NEXT: Correct SEH First-Frame Handling and Unwind Validation

| Field | Value |
|-------|-------|
| **ID** | EXP-NEXT (also referred to as EXP-NEXT-1) |
| **Document** | `docs/experiments/EXP-NEXT-correct-seh-first-frame-unwind.md` |
| **Bug Report** | `docs/bugs/BUG-023-expnext-report.md` |
| **Goal** | Validate that the SEH unwind engine correctly handles the first frame after RaiseException, and identify why EHANDLER was not being discovered |
| **Method** | Instrumented the dispatcher to log every frame walk step, compared unwind results against expected prolog analysis |
| **Discovery** | 1. Compiler prolog size changes between compilations — hardcoded frame offsets are fundamentally unsafe. 2. Solution: `__attribute__((naked, ms_abi))` stub. 3. 236 out of 3030 RUNTIME_FUNCTION entries have malformed UNWIND_INFO (slot overflows, missing ALLOC opcodes) — this is systematic in GCC/MinGW, not edge cases |
| **Status** | COMPLETE |
| **Result** | Naked stub implemented; EHANDLER reachable at Frame[3] via prolog-corrected walk |
| **Related Files** | src/loader/loader.c (naked stub), include/pe.h (CONTEXT offsets) |
| **Checkpoint** | miniwin_checkpoint_bug023_recovery.zip |

---

### EXP-NEXT-2: Synthetic Exception Dispatch Validation

| Field | Value |
|-------|-------|
| **ID** | EXP-NEXT-2 |
| **Document** | `docs/experiments/EXP-NEXT-2-synthetic-exception-dispatch-validation.md` |
| **Goal** | Prove that the exception dispatcher can discover and invoke an EH handler on a controlled synthetic PE, eliminating variables from the real UPX binary |
| **Method** | Built a synthetic PE with 3 nested functions (func_A -> func_B -> func_C -> RaiseException). func_B has an EHANDLER. Created standalone harness (`tests/exp_next2/exp_next2_harness.c`, 796 lines) |
| **Discovery** | Handler discovery works correctly. Frame 0 (func_C): no EHANDLER, parent RIP recovered. Frame 1 (func_B): EHANDLER found at RVA 0x1060. The dispatcher correctly walks frames and finds handlers |
| **Status** | **PASS** |
| **Result** | Handler discovered at Frame 1 — proof that the core dispatch mechanism works |
| **Related Files** | tests/exp_next2/exp_next2_harness.c, tests/exp_next2/synthetic_test.exe, scripts/build/build_synthetic_pe.py |
| **Checkpoint** | miniwin_checkpoint_before_rtl_dispatch.zip |

---

### EXP-NEXT-3: Real Binary Exception Dispatch (UPX)

| Field | Value |
|-------|-------|
| **ID** | EXP-NEXT-3 |
| **Document** | `docs/experiments/EXP-NEXT-3-real-binary-exception-dispatch.md` |
| **Goal** | Test the full exception dispatcher on the real UPX binary, validate frame walking and handler invocation with real GCC-compiled code |
| **Method** | Implemented `seh_dispatch_exception` in the main loader. Ran UPX with `--version` flag. Traced the complete dispatch path through 5 frames |
| **Discovery** | 1. Frame[0] (RaiseException wrapper): no EHANDLER, parent RIP=0x4e0203. 2. Frame[1] (GCC internal): no EHANDLER, parent RIP=0x4e02d9. 3. Frame[2] (GCC internal): no EHANDLER, parent RIP=0x401593. 4. Frame[3] (CRT/UPX): EHANDLER at RVA 0xe0220, returned ContinueSearch (LSDA has no matching catch). 5. Frame[4] (UPX): EHANDLER at RVA 0xe0220, triggered RtlUnwindEx to target_ip=0x40331c |
| **Status** | **PARTIAL** |
| **Result** | Handlers discovered and invoked correctly. RtlUnwindEx triggered but context restoration incomplete — execution does not resume at landing pad |
| **Related Files** | src/loader/loader.c (dispatcher, RtlUnwindEx), docs/bugs/BUG-024-rtl-dispatch-exception.md |
| **Checkpoint** | miniwin_checkpoint_exp_next3_before_dispatch.zip, miniwin_checkpoint_before_master_exception_runtime.zip |

---

## Bug Reports

### BUG-001: UPX C++ Exception Handling Crash (SIGABRT)

| Field | Value |
|-------|-------|
| **ID** | BUG-001 |
| **Document** | `docs/bugs/BUG-001.md` |
| **Goal** | Diagnose why UPX crashes with SIGABRT when running under MiniWin |
| **Discovery** | The crash was caused by `abort()` being called during C++ exception handling. Root cause traced to missing SEH infrastructure — the runtime had no exception dispatcher |
| **Status** | **FIXED** — superseded by BUG-023 |
| **Resolution** | Full SEH infrastructure built (RtlLookupFunctionEntry, RtlVirtualUnwind, seh_dispatch_exception) |
| **Related Files** | Entire exception subsystem in loader.c |

---

### BUG-023: Missing GCC C++ Exception Unwinding Support

| Field | Value |
|-------|-------|
| **ID** | BUG-023 |
| **Document** | `docs/bugs/BUG-023.md` (228 lines) |
| **Goal** | Implement complete GCC C++ exception unwinding so UPX can handle exceptions |
| **Discovery** | 1. GCC personality routine at RVA 0xe0220 appears in ~20 .pdata entries. 2. Exception class is 0x474E5543432B2B00 ("GNUCC++\0"). 3. LSDA format is GCC DWARF-style. 4. 236/3030 RUNTIME_FUNCTION entries have malformed UNWIND_INFO. 5. The unwind engine correctly simulates all 9 standard opcodes plus GCC extensions (opcodes 9-15) |
| **Status** | **PARTIALLY FIXED** — dispatcher works, RtlUnwindEx incomplete |
| **Resolution** | Dispatcher and handler invocation working. Remaining: (a) RtlUnwindEx context restoration, (b) GCC LSDA interpretation in __C_specific_handler |
| **Related Files** | loader.c (entire exception subsystem), pe.h (CONTEXT/UNWIND structs), EXP-NEXT and EXP-NEXT-2 reports |
| **Parent** | BUG-001 |
| **Children** | BUG-024 |
| **Checkpoint** | miniwin_checkpoint_before_cpp_exception.zip |

---

### BUG-024: RtlDispatchException — Exception Dispatcher Implementation

| Field | Value |
|-------|-------|
| **ID** | BUG-024 |
| **Document** | `docs/bugs/BUG-024-rtl-dispatch-exception.md` |
| **Goal** | Implement `seh_dispatch_exception` (the Windows RtlDispatchException equivalent) with full frame walking, EHANDLER discovery, and handler invocation |
| **Discovery** | 1. Frame walking works on real binary (5 frames walked for UPX). 2. EHANDLER flag correctly detected. 3. GCC personality routine successfully invoked via ms_abi inline asm. 4. Handler returns correct dispositions. 5. RtlUnwindEx is triggered by Frame[4] handler but longjmp-based context restoration is insufficient |
| **Status** | **PARTIALLY FIXED** — dispatch works, unwind incomplete |
| **Parent** | BUG-023 |
| **Related Files** | loader.c lines ~1100-1550 (dispatcher), ~1000-1100 (RtlUnwindEx) |
| **Test Evidence** | tests/dispatch_tests/test_a/b/c/d.exe (4 synthetic PEs), regression test #6-#9 |

---

## Dispatch of Experiments on Bugs

```
BUG-001 (SIGABRT crash)
    |
    v  [fixed by implementing SEH infrastructure]
BUG-023 (Missing GCC unwinding)
    |
    +-- EXP-NEXT (first-frame unwind validation)
    |       -> Discovery: naked stub needed, 236/3030 malformed entries
    |
    +-- EXP-NEXT-2 (synthetic dispatch proof)
    |       -> Result: PASS — handler discovery works
    |
    +-- BUG-024 (dispatcher implementation)
            |
            +-- EXP-NEXT-3 (real binary dispatch)
                    -> Result: PARTIAL — dispatch works, RtlUnwindEx blocked
```
