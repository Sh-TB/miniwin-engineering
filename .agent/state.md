# Engineering State

**Date**: 2026-08-15
**Build**: CLEAN (warnings only, 0 errors)
**Regression**: 12/12 PASS
**Binary**: 268KB ELF x86-64

## Verified Working
- PE64 loading, 10 sections mapped
- 162/162 imports resolved
- TEB/PEB environment
- Heap/Virtual memory APIs
- CRT init (_initterm, __getmainargs)
- .pdata parsing (3030 entries)
- RtlLookupFunctionEntry (binary search)
- RtlVirtualUnwind (9 opcodes + GCC 9-15)
- Exception dispatch (5-frame walk, 2 handlers called)
- RaiseException naked ms_abi stub
- RtlUnwindEx (longjmp path, sets Rip/Rsp/Rax only)

## Current Failure

UPX --version crashes with SIGSEGV (exit 139) AFTER reaching the catch
landing pad. The execution flow:

1. Exception 0x20474343 (GCC C++ exception) raised at RIP=0x49d5b1
2. Frame walk: 5 frames (0-4)
3. Frame[3]: handler returns ContinueSearch
4. Frame[4]: handler calls RtlUnwindEx(target_ip=0x40331c, target_frame=0x7ffd2510e6d8)
5. RtlUnwindEx sets CTX.Rip=0x40331c, CTX.Rsp=0x7ffd2510e6d8, CTX.Rax=0x3387320
6. longjmp back to dispatcher
7. Dispatcher copies CONTEXT to g_unwind_regs[], returns CONTINUE_EXEC
8. Naked stub restores registers from g_unwind_regs and jumps to 0x40331c
9. Landing pad executes: mov rbx,rdi; lea rcx,[rsp+0x30]; mov r12,rax; call 0x401550
10. Inside 0x401550: reads [RCX+0x08] which is 0 → SIGSEGV at RIP=0x401561

## Crash Analysis

Crash: RIP=0x401561, RAX=0x0, RSP=0x7ffd2510e6d0
Landing pad code at 0x40331c:
  mov rbx, rdi        ← needs RDI (nonvolatile!)
  lea rcx, [rsp+0x30]
  mov r12, rax        ← RAX=exception object (OK)
  call 0x401550

## Hypothesis

RtlUnwindEx only sets 3 CONTEXT fields (Rip, Rsp, Rax). Nonvolatile
registers (RBX, RBP, RSI, RDI, R12-R15) are NOT restored to the target
frame's state. The frame walk in seh_dispatch_exception does unwind
each frame's CONTEXT, but RtlUnwindEx then OVERWRITES Rip/Rsp without
re-unwinding from the handler's call site back to the target frame.

## Key Addresses
- Exception site: RIP=0x49d5b1, RSP=0x7ffd2510e580
- Landing pad: RVA=0x331c (VA=0x40331c)
- Target frame: RSP=0x7ffd2510e6d8
- Exception object: 0x3387320
