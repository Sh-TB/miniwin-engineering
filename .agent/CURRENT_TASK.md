# CURRENT TASK

**Task ID:** TASK-BUG-027
**Title:** Fix mw_RtlUnwindEx landing-pad resume state (RSP +8; RDX selector)
**Status:** READY (evidence complete; implementation not started)
**Priority:** P0

## One-line description
`mw_RtlUnwindEx` in `src/loader/loader.c` synthesizes the landing-pad CONTEXT:
RSP = establisher − alloc_total (where ALLOC_SMALL is decoded as `OpInfo*8`,
missing `+8` → RSP 8 bytes too high) and RDX is left stale (C++ selector not
propagated). The PE resumes at the catch landing pad with a corrupted stack
position and crashes at heap RIP=0x2004167.

## Why this is the task (evidence — EXP-036, live post-BUG-026 trace)
- Walk is now correct: Frame[0..3] all resolved; Frame[3] personality called
  `RtlUnwindEx(target_frame=0x7ffe60f7def8, target_ip=0x4dcd24, ctx=0x7ffe60f7d810, exc=0x3bc8320)`.
- Target 0x4dcd24 = return address of the throw call `call 0x4a95a8` @0x4dcd1f
  inside function 0xdcbf0..0xdcdaf → catch resumes with the function's frame
  INTACT: correct RSP = carried walk RSP = `0x7ffe60f7deb0`.
- Our impl used `post_prologue_RSP=0x7ffe60f7deb8` (alloc_total=0x40). True
  prologue = PUSH R13, PUSH R12, PUSH RBX, ALLOC_SMALL 0x30 = **0x48**
  (raw UNWIND_INFO @ RVA 0x209420: `19 09 04 00 | 09 52 | 05 30 | 04 c0 | 02 d0`,
  OpCode = LOW nibble of byte1). `0x7ffe60f7def8 − 0x48 = 0x7ffe60f7deb0`.
- Landing pad `sub rdx,1; jne 0x4dcd8f` requires RDX=selector(1); we restored
  stale RDX (downstream −1 observed) → wrong branch/garbage path.
- Cross-checks: Frames 0/1/2 est−ctx.rsp = 0x28 each — walk's establisher
  math is right; only the RtlUnwindEx-side synthesis is wrong.

## Execution notes for the next agent
- Follow `.agent/CONTINUE_PROTOCOL.md` steps C–F. Baseline first: `make clean && make`
  + regression suite (must be 12/12 pre-edit) + record loader.c sha256.
- Preferred fix (ADR-008): resume with the **personality-provided ContextRecord
  (r8)** state — Rsp, Rdx, Rax, etc. as the personality prepared them —
  overriding only `Rip=TargetIp` (keep `Rax=ReturnValue` for the non-C++ path).
  Note the naked stub's unwind-return path consumes g_unwind_regs from the
  longjmp CONTEXT — keep that plumbing consistent.
- Minimal alternative (if r8 plumbing proves risky): fix ALLOC_SMALL
  `(OpInfo+1)*8` in mw_RtlUnwindEx AND adopt RDX from OriginalContext —
  but justify the remaining synthetic behavior in ADR-008.
- Boundaries: do NOT touch EH_FIX (L~1607), CXX-EH-FIX (L~3263), the
  RaiseException stub, or the frame walk.
- Distinguish P-1 vs P-2 outcomes (EXP-036 §7) if you choose the minimal path.
- Record: fresh UPX trace (landing-pad entry registers visible), regression
  (must stay 12/12), experiment note `docs/master/experiments/EXP-037-*.md`,
  closure `docs/bugs/BUG-027.md`, then update `.agent/` files per step F.

## Verified facts you may rely on (this session)
- Regression 12/12 at loader.c `e1caa5d7…` (post-BUG-026), binary `c8472d04…`.
- UPX post-BUG-026: exit 139; full trace in `evidence/master/bug026/upx_trace.json`.
- The buggy alloc_total decode lives in mw_RtlUnwindEx (trace line
  `alloc_total=0x40`); the walk-side establisher values were correct, so there
  are (at least) two decoders — fix only the RtlUnwindEx one in this task;
  TASK-API-006 owns the repo-wide ALLOC rule.
