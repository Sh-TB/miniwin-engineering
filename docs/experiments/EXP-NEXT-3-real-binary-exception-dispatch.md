# EXP-NEXT-3: Real Binary Exception Dispatch (UPX)

## Experiment ID
EXP-NEXT-003

## Date
2026-08-08 to 2026-08-14

## Status
ACTIVE — Handler Called, RtlUnwindEx Incomplete

## Goal
Implement RtlDispatchException in the main loader and execute it against
the real UPX binary (upx_decompressed.exe --version).

## Initial Hypothesis
Given EXP-NEXT-2 proved handler discovery works with synthetic PEs,
the same mechanism will discover handlers in the real UPX binary.
The key challenge is correct CONTEXT construction and ms_abi handler
invocation.

## Tools Used
- Modified loader.c with seh_dispatch_exception()
- Internal SysV SEH functions (seh_internal_lookup, seh_internal_virtual_unwind)
- ms_abi inline asm for handler invocation
- API trace logging
- Naked stub for register capture

## Evidence

### Full Dispatch Trace (from exp_next3_full_trace.txt)

```
[DISPATCH] === RtlDispatchException ===
ExceptionCode=0x20474343 RIP=0x49d5b1 RSP=0x7ffe1fdc4870

EXC_STATE:
  Code: 0x20474343
  Flags: 0x0
  Address: 0x49d5b1
  NumParams: 1
  Param[0]: 0x2a8f320 (exception object)
  Exception class (u64): 0x474e5543432b2b00 ("GNUCC++\0")

Frame[0]: RVA=0x9d5b1
  RF begin=0x9d560 end=0x9d5bb ui=0x2098b4
  Unwind: handler=0x0 est=0x...898
  → no handler, parent RIP=0x4e0203

Frame[1]: RVA=0xe0203
  RF begin=0xe0190 end=0xe0211 ui=0x20931c
  Unwind: handler=0x0 est=0x...8d8
  → no handler, parent RIP=0x4e02d9

Frame[2]: RVA=0xe02d9
  RF begin=0xe0290 end=0xe02da ui=0x201270
  Unwind: handler=0x0 est=0x...908
  → no handler, parent RIP=0x401593

Frame[3]: RVA=0x1593
  RF begin=0x1570 end=0x15bb ui=0x20107c
  Unwind: handler=0xe0220 est=0x...958
  *** EHANDLER at RVA 0xe0220 ***
  LSDA at 0x601088
  Handler bytes: 48 83 ec 38 48 8d 05 85 fb fc ff 48 89 44 24 20
  LSDA: ff ff 01 08 20 03 35 00 45 06 00 00...

  Handler called → disposition=1 (ContinueSearch)
  LSDA has 6 call sites, all with action=0 (no catch)

Frame[4]: RVA=0x32f2
  RF begin=0x32c0 end=0x3368 ui=0x2011f0
  Unwind: handler=0xe0220 est=0x...9c8
  *** EHANDLER at RVA 0xe0220 ***
  LSDA at 0x601200
  Handler bytes: 48 83 ec 38 48 8d 05 85 fb fc ff 48 89 44 24 20
  LSDA: ff 9b 15 01 04 2d 05 5c 05 02 00 01 7d 00 7d 00...

  Handler called → triggered RtlUnwindEx
  RtlUnwindEx(target_frame=0x...9c8, target_ip=0x40331c)
  RtlUnwindEx: Set CTX.Rax = 0x2a8f320 (exception object)
  RtlUnwindEx: longjmping to dispatcher

  [BLOCKED: execution after longjmp does not resume at catch]
```

### Key Observations

1. **Frame[3] handler returns ContinueSearch**: The LSDA at 0x601088 has
   6 call sites all with action=0. This means no try/catch in this
   function matches the exception. The personality routine correctly
   returns ContinueSearch.

2. **Frame[4] handler triggers RtlUnwindEx**: The LSDA at 0x601200 has
   a non-trivial action table. The personality routine found a matching
   catch clause and called RtlUnwindEx to unwind to the catch landing pad.

3. **RtlUnwindEx partially works**: It correctly identifies target_ip=0x40331c
   and target_frame. It calls longjmp to return to the dispatcher. However,
   the CONTEXT is not properly restored to allow execution to continue at
   the landing pad.

## Code Changes
- Added seh_dispatch_exception() to loader.c
- Added seh_internal_lookup() (SysV binary search)
- Added seh_internal_virtual_unwind() (SysV unwind simulation)
- Added DISPATCHER_CONTEXT construction
- Added ms_abi handler invocation via inline asm
- Added LSDA logging in handler call
- Added RtlUnwindEx longjmp infrastructure
- Added g_unwind_regs for saving register state across longjmp

## Result
**PARTIAL SUCCESS**: The dispatcher correctly walks 5 frames, invokes 2
EHANDLERs, and the second handler correctly identifies a matching catch
clause and triggers RtlUnwindEx. However, execution does not resume at
the catch landing pad.

## Root Cause
RtlUnwindEx uses longjmp to return control to the dispatcher after the
handler calls it. The dispatcher needs to:
1. Walk frames from the exception site to the target frame
2. Restore CONTEXT.Rip = target_ip (0x40331c)
3. Restore CONTEXT.Rsp = target_frame
4. Actually resume execution at CONTEXT.Rip

Currently step 4 is not implemented. The dispatcher receives the longjmp
but does not know how to resume PE execution at the landing pad.

## Lessons Learned
1. RtlUnwindEx is more complex than just longjmp
2. The target IP from the handler is a landing pad, not a return address
3. The CONTEXT must be fully restored for all frames between exception
   site and target
4. The GCC personality routine uses RtlUnwindEx to perform a non-local
   goto to the catch clause
5. LSDA TType encoding 0x9b = DW_EH_PE_udata8 means 8-byte type offsets

## Future Work
1. Implement proper context restoration after RtlUnwindEx
2. Walk from current frame to target frame, applying unwind for each
3. Set RSP and RSP and actually jump to the landing pad
4. This will likely require switching to the PE's stack and jumping
   via inline asm

## Attached Evidence
- evidence/execution_logs/exp_next3_full_trace.txt
- evidence/execution_logs/exp_next3_baseline_stderr.txt
- evidence/execution_logs/exp_next3_fix_stderr.txt
- evidence/execution_logs/exp_next3_step1_stderr.txt
- evidence/execution_logs/exp_next3_cpc1_stderr.txt
- evidence/api_trace/upx_api_trace.json
- checkpoints/exp_next/checkpoint_exp_next3_before_dispatch.zip
