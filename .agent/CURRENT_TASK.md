# CURRENT TASK

**Task ID:** TASK-BUG-038
**Title:** C++ EH target-frame delivery (barrier-cache landing pad installation)
**Status:** SUPERSEDED (2026-10-15) — the GNU-mingw fixture family was fixed by BUG-039 (see docs/bugs/BUG-039.md, CLOSED TESTED): the real roots were (1) PUSH_NONVOL read-after-increment and (2) resume using the search-walk context. The target-frame delivery plan below remains the design for the llvm-mingw/MS-ER family (BUG-038, P2).
**Priority:** P0

## One-line description
After BUG-037's fix, t08's full 4-frame walk + intermediate cleanups complete
correctly and the resume uses the correct body RSP — but the loader resumes at
RtlUnwindEx's TargetIp, which llvm-mingw's libunwind fills with ControlPc as a
PLACEHOLDER. The real landing pad (0x1c9d = the catch block, proven by full
LSDA decode) lives in the exception object's barrier cache and is only
installed by the personality at TARGET-FRAME DELIVERY (ExceptionFlags
0x2|0x4, Info[1]=target_frame, returns ExceptionExecuteHandler=4).

## Fix plan (evidence-first, per protocol)
1. mw_RtlUnwindEx: after the intermediate walk reaches the target frame,
   deliver target-unwind to the target frame's personality with
   dc.ContextRecord=&resume_ctx, dc.TargetIp=tgt_ip, dc.EstablisherFrame=
   tgt_frame, ER flags UNWINDING|TARGET_UNWIND, NumberParameters=4,
   Info[1]=tgt_frame.
2. Honor the personality's context write (resume_ctx.Rip = real LP) / return 4;
   resume with that context (Rax=exception object, Rdx=selector, Rsp=body RSP).
3. Keep the GCC-style path (UPX G1) intact: GCC answers target delivery with
   ContextRecord->Rdx = Info[3] and ContinueSearch; its TargetIp IS the real LP.
4. Audit the double-decrement: our dc.ControlPc = rip-1 AND ctx.Rip -= 1 while
   libcxxabi does `ip = _Unwind_GetIP(context) - 1` itself — real Windows
   passes the FULL return address as ControlPc.

## Ground rules
- Do not touch EH_FIX / CXX-EH-FIX (flag-guarded, default OFF), the
  RaiseException stub, or call_handler_aligned's pinned-register block.
- Every change keeps regression 12/12 + G1 gate green.

## Acceptance
- t08 prints `t08: PASS`, exit 0, 3/3 deterministic runs.
- regression 12/12; UPX G1 gate; sweep 11/12 (t09 = API frontier).

## Context from CONTINUE #7 (this session)
- BUG-037 CLOSED TESTED: three spec-level fixes (opcode numbering per current
  MS/LLVM/.NET/ReactOS; CountOfCodes = slots; frame-register low-nibble) +
  FP-frame resume RSP. EXP-041. Sweep 7/9 -> 10/12 PASS (t10/t11/t12 NEW).
- UCRT wave landed: TASK-API-011/012 CLOSED TESTED (__stdio_common_v* bridges,
  __acrt_iob_func, __p_* functions, FILE-level stdio, map_crt_stream
  pass-through). Toolchain re-downloaded (TASK-INFRA-003 pending script).
- Backlog expanded to 117 tasks (+BUG-038/039/040, API-013/014/015,
  TEST-006/007, DOC-011, INFRA-003, RESEARCH-007, APP-013).
