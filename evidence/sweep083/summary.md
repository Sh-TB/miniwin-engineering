# SWEEP-083 — elephant train: the o32/SEH class fix set + mines first-run PASS

Base commit: 4bd053e lineage (origin/main at session start: 9bc8009).
Binary sha pre: 4f9cc889 (G1 pinned at session start, evidence/master/g1/20261026_184437)
Binary sha post: 80bca026 (G1 pinned after the fix set, evidence/master/g1/20261026_200439)

## Fix set (all generic engine work — zero app special-casing)

| SW | Class | Root cause (owl-eye chain) | Effect |
|----|-------|---------------------------|--------|
| SW-1387 | i386 o32 plane | `66 81 c7 ef 00` (add di,0xef) ran the 32-bit grp1 handler: imm32 read consumed 7 bytes, eip landed mid-instruction on the modrm byte (0xe7) -> PE32-FATAL rc=86. Whole modrm imm-group + ALU r/m<->r + mov/test/imul/shift/grp3 families were 66-blind (only B8-BF/C7/acc-ALU were o32-aware since SW-1385/86) | winnc 551 -> 12068 steps |
| SW-1388 | i386 arity table | plain `InitializeCriticalSection` MISSING from g_api32_arity -> non-CRT fallback MW32_ARITY_DEFAULT(32) -> gate over-popped 28 bytes per CS init -> whole init ran esp=true+32 -> Delphi lazy-helper ret consumed the installed SEH handler slot (0x409482) -> except-handler entered with ER*=[esp+4]=0 -> test [0+4] host SIGSEGV | steps 12068 -> 12080; 18 missing entries audited+added (zero wrong arities in the 883-entry table) |
| SW-1389 | struct layout | mw_GetSystemInfo wrote the 48-byte x64 SYSTEM_INFO through the 32-bit guest pointer — offset 36 (granularity=65536=0x10000) landed exactly on the caller's saved return address (winnc wrapper 0x40f570) -> ret to 0x00010000 -> SIGSEGV. Now bitness-aware: 36-byte x86 layout for the i386 plane (armed on t_cpu32_live \|\| g_image_base32) | steps 12080 -> 13399 |
| SW-1390 | x87 DB mod=3 | FNCLEX=DB E2 / FNINIT=DB E3 have reg field 4 — the old checks (`m.reg==7`) never fired -> every FPU re-init died. Real DB mod3 plane added (FNINIT resets CW=0x037F/SW=0/top=0, FNCLEX, FUCOMI/FCOMI) + FWAIT (0x9B) no-op | steps 13399 -> 14193 |
| SW-1391 | segment override | LEA applied the FS/GS base (modrm_apply_seg ran TWICE: NEEDM + LD_RorM* re-apply — not idempotent). Guest padding-lea `64 66 8d 80 00 00 00 00` corrupted a copy source pointer 0x3fefed9c -> 0x4fefed9c -> movups host SIGSEGV. seg_applied flag added | wild-eip fixed; memcpy path clean |
| SW-1392 | i386 SEH dispatch | RaiseException used to RETURN -> the unreachable `ret` after the call site executed heap garbage (eip=0x1112a680). Real x86 SEH now: ER+CONTEXT built in guest scratch, FS:[0] chain walk, handlers run CDECL ON THE GUEST STACK (ret-sentinel pump), ContinueSearch/ContinueExecution/unwind protocols; RtlUnwind = normal stdcall call that unwinds frames above the target (flags\|=EH_UNWINDING) and RETURNS (the handler owns the resume — proven: guest passed TargetIp=RA 0x408e78 as a formality and post-unwind code reads [esp+0x28] in the HANDLER's own frame); MW32_SEH_RESUME_CODE pump-stop rides up through nested pumps; top loop re-enters at the new eip | winnc 14193 -> 14556 steps: chain intact, dispatch->unwind->Delphi finally machinery reached for the first time |
| SW-1392e | modrm idempotence | (found during 1392) `push %fs:[0]` read 2*fsbase (heap zero) -> every SEH frame registered with prev=0 -> chain collapsed empty at the first pop -> "unhandled" | chain now real (SEH-CALL trace: 0x3fefef70 -> 0x3fefef7c) |
| SW-1394 | 0F AE group | /3 was mislabeled a fence (it is stmxcsr), /2 ldmxcsr refused — Delphi CPU-detect `ldmxcsr -0x4(%ebp)` rc=86. Real group: fxsave/fxrstor/ldmxcsr/stmxcsr/lfence/mfence/sfence | renamer rc=86 abort -> clean guest exit |

## Results x3 (plugin runs, sweep083 ledger)

| plugin | verdict | delta |
|--------|---------|-------|
| mines (NEW, Simon Tatham, official zip sha a5b00f63) | **PASS_INPUT_STATE_CHANGE** first-run (rc=0 x3, frames=6 uniq=3) | NEW program = full graphical execution on first contact |
| npp64 (user-named base app) | PASS_INPUT_STATE_CHANGE rc=0 x3 (unchanged) | trio base app holds |
| idman | PASS_LAUNCH_RENDER rc=0 x3 (unchanged) | trio base app holds |
| winrar | EXIT rc=0 frames=0 (unchanged, SW-1361 parity frontier) | trio base app holds |
| winnc | FRONTIER_SW crash=1 steps=14556 (was: abort rc=86 @551) | SW-1298-class frontier SW-1393: Delphi finally-retry router (`call *[node+4]+5` -> 0x4a899b) re-entry contract |
| renamer | EXIT rc=204 frames=0 (was: abort rc=86 @interpreter) | clean guest RunError(204) class exit; frames=0 = pre-render exit |
| notepad2mod | EXIT rc=0 frames=0 (unchanged clean exit; UCRT finalization path in trace) | x64 wagon queued |
| everything | TIMEOUT frames=0 (unchanged) | poll-loop wagon queued |
| tcpview64 | TIMEOUT frames=0 (unchanged) | toolbar-loop wagon queued |
| hxd | BLOCKED_DOWNLOAD (mh-nexus refused; 8th attempt on record) | unchanged |

## Gates
- G1 PIN pre (4f9cc889) + post (80bca026), both 12/12 (evidence/master/g1/20261026_184437, 20261026_200439)
- Corpus parity 3x: 21/21 green, zero crashes, zero fatals, deterministic frames (spider timing variance only) — ZERO regressions from 7 engine changes
- Trio re-verified pre-fix-set (no regression)

## Frontier queue
- SW-1393 winnc: Delphi finally-retry chain — `call *ecx` with ecx=[node+4]+5 lands on 0x4a8a2c (jmp 0x4a899b); the re-entry stack contract ([esp+0x28] read at 0x408e78) needs the 0x4a899b/0x408d3c router decode.
- renamer rc=204: RunError(204) class (EInvalidPointer) — pre-render guest exit; the 55x TlsGetValue(1)/SetLastError/GetLastError loop + SetUnhandledExceptionFilter(NULL) -> ExitProcess(204) tail is the decode target.
- notepad2mod x64: clean rc=0 exit with UCRT finalization in trace (GetCurrentPackageId/mscoree probes) — the "exits before WinMain renders" x64 wagon.
- everything/tcpview64: TIMEOUT frames=0 poll loops.
