# Hypotheses

## H-001: Nonvolatile registers wrong at landing pad (ACTIVE)
**Hypothesis**: RtlUnwindEx sets Rip/Rsp/Rax but not RBX/RBP/RSI/RDI/R12-R15.
The landing pad at 0x40331c does `mov rbx, rdi` which needs correct RDI.
**Status**: Under investigation. Need diagnostic dump of CONTEXT NV regs.
**Experiment**: Add MW_TRACE of all NV regs after each frame unwind.

## H-002: Stack contents at [RSP+0x38] uninitialized
**Hypothesis**: The value at [RSP+0x38] (read as [RCX+0x08] by 0x401550)
should be non-zero but is 0 because it was never set in this code path.
**Status**: Needs evidence. Dump stack at landing pad.
**Experiment**: In naked stub unwind path, dump 64 bytes at RSP+0x20.

## H-003 (REJECTED): CONTEXT buffer corrupted by longjmp
longjmp restores RSP to setjmp point which is in seh_dispatch_exception's
frame. The CONTEXT buffer (also on that frame) should still be valid.
**Evidence**: g_unwind_regs[UR_RAX] = 0x3387320 matches RtlUnwindEx's set value.
