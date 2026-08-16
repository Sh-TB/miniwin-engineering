# RSP Evidence Checkpoint

## Date: 2026-08-07

## Key Findings

### 1. Compiler Prolog Changes Between Compilations
- Hardcoded frame offsets are UNSAFE
- Naked stub is REQUIRED for reliable register capture
- Evidence: `rsp_val + 0x208` → MISMATCH (frame was 0x218)

### 2. Naked Stub Works
- `__attribute__((naked, ms_abi))` captures registers before compiler prolog
- `[rsp_entry] = ret_addr` → MATCH confirmed

### 3. CONTEXT.Rsp for Exception Dispatch
- Must be `cs->rsp` (caller's pre-call RSP), NOT `rsp_entry`
- `rsp_entry` points to return address; `rsp_entry + 8` = caller's RSP
- `CONTEXT.Rsp = cs->rsp` (from CapturedCallState)

### 4. Caller's UNWIND_INFO is Incomplete
- Function at 0x9d560 has `sub rsp, 0x28` but NO ALLOC opcode in UNWIND_INFO
- RtlVirtualUnwind cannot correctly walk this frame
- Stack walk produces wrong parent RIP (0x201e040 = g_argv, not PE code)

### 5. ABI Issue: ms_abi from SysV
- Direct calls to ms_abi functions from SysV code crash (XMM alignment)
- Solution: internal SysV-only copies of SEH logic

## Files
- `src/loader.c` — baseline (2134 lines, hash cbc04da6)
- `src/loader.c.baseline_2134` — baseline backup
- `docs/prolog_analysis_mw_RaiseException.txt` — full analysis
- `docs/checkpoint_evidence_rsp.md` — this file

## Next Step
Implement exception dispatcher using:
1. Naked stub for register capture (already proven)
2. CONTEXT.Rsp = cs->rsp
3. Skip first frame unwind (incomplete UNWIND_INFO)
4. Walk from parent frame to find EHANDLER
5. Build DISPATCHER_CONTEXT and call language handler
