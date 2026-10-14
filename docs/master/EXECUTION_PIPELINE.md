# EXECUTION PIPELINE — Source-Grounded Map

**Produced:** TASK-003, 2026-10-14, at HEAD `39181aa`. Single source: `src/loader/loader.c` (3,481 lines, sha256 `502ae588...`) + `include/pe.h` (269 lines, sha256 `928a780c...`). Line numbers refer to HEAD.
**Legend:** Status: OK = implemented AND executed successfully on UPX 4.2.4 (decompressed) per current evidence · PART = implemented, partially verified · BROKEN = implemented but fails at HEAD · STUB = compatibility stub · DEAD = present but never called.

---

## Stage 0 — Process Setup (`main`, L3386–3480)

| Aspect | Detail |
|---|---|
| Function | `main` (L3386) |
| Inputs | argv (PE path + args) |
| Behavior | Builds ANSI + wide command line (L3397-3405); populates `g_argv/g_argc` (L3410-3415); installs crash handlers (L3418); creates `miniwin-results/<exe>/api_trace.json` trace (L3421-3446, parent-dir mkdir fix present L3427); `resolve_proc_init()`; `load_pe()`; `setup_teb_peb()`; jumps to entry via C function pointer cast (L3466-3468) |
| Assumptions | Entry point called as `int(*)(void)` — SysV convention at the jump site; the PE's CRT entry is MinGW code but tolerates being entered with garbage registers (verified by execution) |
| Status | **OK** (executes on UPX: trace shows EP reached) |
| Evidence | trace `Jumping to EP=0x…`; regression tests 1-5 |
| Limitations | No environment block passed (`g_initenv_ptr=NULL`); return path assumes PE returns via CRT exit or falls off `main` |

## Stage 1 — PE File Open & Validation (`load_pe`, L3027–3063)

| Aspect | Detail |
|---|---|
| Function | `load_pe` (L3027) |
| Validation | 'MZ' (L3042), 'PE\0\0' via e_lfanew (L3048-3054), PE32+ magic 0x020B (L3059) |
| Outputs | `g_entry_point` (RVA, L3065), `image_base`, `size_of_image`, `num_sections`, `num_dd` |
| Status | **OK** (UPX 10-section PE loads) |
| Limitations | No bounds checks on `e_lfanew` or header sizes against file size (adversarial-PE risk, Phase-5 audit item); no COFF machine check (0x8664 unverified) |

## Stage 2 — Image Mapping (L3075–3093)

| Aspect | Detail |
|---|---|
| Function | `load_pe` (L3082-3084) |
| Call | `mmap((void*)image_base, size_of_image, PROT_READ|WRITE|EXEC, MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED, -1, 0)` |
| Semantics | **MAP_FIXED forces the preferred ImageBase (0x400000 for UPX); silently unmaps ANYTHING already mapped there — including the loader itself if layouts collide** (loader code segment is at 0x2000000 via `-Wl,-Ttext-segment=0x2000000`, heap at static addresses ~0x2018000+ and 0x2022170 `__iob_func`, TEB/PEB static buffers — all outside 0x400000+0x213000, so UPX works; a PE with larger SizeOfImage could clobber loader data) |
| Relocation | **NOT IMPLEMENTED.** No .reloc processing anywhere (search: no relocation loop in load_pe). Works only because UPX-decompressed loads at preferred base |
| Status | **PART** — functional for preferred-base PEs; unsafe generally (full audit in `IMAGE_MAPPING_AND_RELOCATION_AUDIT.md`, Phase-5 task) |
| Evidence | trace "Mapped at 0x400000 (forced via MAP_FIXED)" |
| Regression | none dedicated (Phase-5 must add: collision test, alternate-base test) |

## Stage 3 — Header Copy & Section Mapping (L3098–3131)

| Aspect | Detail |
|---|---|
| Function | `load_pe` |
| Behavior | Copies `SizeOfHeaders` bytes; per-section: memcpy `SizeOfRawData` → `image+VirtualAddress`; zero-fill BSS tail (L3113-3116); `mprotect` per section characteristics R/W/X (L3124-3130) |
| Permissions | Initial whole-image RWX at mmap; refined per-section AFTER data copy (safe order) |
| Status | **OK** (UPX sections verified via trace dump) |
| Limitations | mprotect length `VirtualSize` rounded to SectionAlignment — may touch adjacent section header page (benign so far); no page-boundary validation of `PointerToRawData` (adversarial risk) |

## Stage 4 — Import Resolution & IAT Patch (L3133–3241)

| Aspect | Detail |
|---|---|
| Function | `load_pe` (import walk) + `g_import_table[]` (L2848+, ~162 KERNEL32/msvcrt entries) + `g_data_imports[]` |
| Behavior | Walks ImportDirectoryEntry array; per-DLL walks ILT (falls back to IAT when OriginalFirstThunk=0 — required for packed UPX); import-by-name via Hint/Name; ordinal imports counted UNRESOLVED (L3181-3186); patches IAT slot with stub VA; data imports patched with data addresses |
| Measured | **159 resolved** on current UPX sample (trace; regression test 2). Historical docs claim 157 and 162 — 159 is the current-source measurement |
| Unresolved policy | Slot left unchanged (holds name RVA for bind-emulated PEs) + stderr WARN — the exact mechanism that caused BUG-025's packed-UPX crash; now mitigated only because LoadLibraryA/ExitProcess stubs exist (L2839-2841) |
| Status | **OK for UPX-decompressed; PART generally** (fake module handles: `mw_LoadLibraryA` returns `0xdead0001` L468-474; `mw_GetProcAddress` special-cases `0xdead0001` and `g_image_base` L3000+) |
| Evidence | trace "Import resolution: 159 resolved, 0 unresolved" |
| Regression | regression test 2 |

## Stage 5 — TLS (L3243–3250)

UPX has TLS directory with `AddressOfCallBacks = 0`; loader logs it and does nothing. Callback invocation code not present (would be needed for other targets). Status: **PART** (parse-only). UPX does not exercise it.

## Stage 6 — .pdata Capture (L3252–3261)

`g_pdata_rva/size`, `g_num_rt_functions = size/8` (3030 for UPX). **OK** (regression test 7).

## Stage 7 — CXX EH FIX: target-binary LSDA patch (L3263–3287) — ⚠️ APP-SPECIFIC HACK

| Aspect | Detail |
|---|---|
| Behavior | `mprotect` a page of the mapped PE writable, then **patch the PE's own .xdata/LSDA byte at hardcoded RVA `0x20107c+12+64 = 0x20113C`** to 0x7f (DWARF sleb128 −1 = catch-all), then restore protections |
| Rationale in comment | make `check_basic_cxx_exception_handling`'s filter match any type |
| Assessment | **VIOLATES engineering rules**: hardcoded UPX-specific address inside general loader; silently modifies target metadata; undocumented in any report until now; would corrupt any other PE that happens to have data at that RVA |
| Status | **BROKEN-BY-DESIGN** (must be isolated behind explicit flag or removed in Phase B/C; inventory in API/backlog docs) |
| Note | Related but distinct from the dispatcher-side `EH_FIX` LSDA override (L1607-1644) |

## Stage 8 — TEB/PEB Setup (`setup_teb_peb`, L117–193)

| Aspect | Detail |
|---|---|
| Behavior | Static TEB/PEB/params buffers; real stack bounds via `pthread_getattr_np`; TEB fields (Self, StackBase/Limit, ClientId, PEB ptr at 0x60); PEB ImageBaseAddress/Ldr=0/ProcessParameters; wide cmdline; `arch_prctl(ARCH_SET_GS)` → TEB (L192) |
| ABI note | Works because Linux maps `gs` for TLS; setting GS base redirects PE's `gs:0x30` accesses |
| Status | **OK** on UPX (CRT reads PEB/TEB fine) |
| Limitations | Single-threaded (one static TEB); LastErrorValue written but `TlsGetValue/SetLastError` trace lines suggest separate TLS emulation; Ldr=NULL (anything walking PEB_LDR_DATA will crash) |

## Stage 9 — Entry-Point Transfer & CRT Init (inside PE)

| Aspect | Detail |
|---|---|
| Mechanism | C function-pointer call to `g_image_base+0x14f0` (UPX MinGW CRT entry). PE then calls imports: `__set_app_type`, `_initterm`, `__getmainargs`, `SetUnhandledExceptionFilter`, malloc/calloc/realloc, `InitializeCriticalSection`, `TlsGetValue`, etc. — all serviced by mw_* ms_abi stubs |
| Variadic ABI | Trampolines (`write_trampoline`, L208-221) convert ms_abi→SysV for printf-family (va_list layout differs); non-variadic use `__attribute__((ms_abi))` directly |
| Status | **OK** (regression tests 3,4,5,11,12) |
| Evidence | trace shows full CRT sequence, `SetUnhandledExceptionFilter(handler=0x49c9c0)` |

## Stage 10 — Heap / Memory APIs (mw_HeapAlloc L~2400s, mw_VirtualAlloc etc.)

Bump allocator from 16MB static region; HeapFree no-op; VirtualAlloc/Free/Protect = mmap/munmap/mprotect wrappers; VirtualQuery via /proc/self/maps. Status: **PART** (functional for CLI-class targets; no free → long-run exhaustion). Evidence: regression tests 11-12, UPX trace malloc/calloc activity.

## Stage 11 — Exception Raise (`mw_RaiseException`, L1668–1755) — **BROKEN at HEAD** (root cause of current failure)

| Aspect | Detail |
|---|---|
| Mechanism | `__attribute__((naked, ms_abi))` stub: pushes 6 NV regs, `sub rsp,0x28`; saves `rsp_entry`, `[rsp_entry]`(ret) at stack slots 0x00/0x08; params to slots 0x10-0x20 |
| Capture → globals | **L1697-1702 DEFECT:** intended `movq 0x08(%rsp),%rax` (load saved ret) before `movq %rax,g_cap_rip`; actual code loads `0x58(%rsp)` then `0x00(%rsp)` → **`g_cap_rip = rsp_entry` (a STACK ADDRESS, not code)** |
| Consequence | `CONTEXT.Rip` outside PE → dispatcher Frame[0] "RIP outside PE — walk ends" → NOT_HANDLED → PE sees RaiseException return → libstdc++ terminate → `mw_abort` → `_exit(134)` |
| Correct reference | `loader.c.exp_next2_backup` L842-843: `movq 0x08(%rsp),%rax; movq %rax,g_cap_rip(%rip)` — proven by EXP-NEXT "PROOF MATCH" |
| Introduced | During working-tree "fix broken inline asm strings" edits (commit fdb939c era), synced in d36fc5e; every build from current source affected |
| Evidence | fresh trace (both dispatches `RIP=0x7ffd…` = RSP−8); exit 134; stdout terminate message; regression failures #8/#10 |
| Status | **BROKEN** — single-operand fix queued as next task (BUG-026) |

### Supporting stages (correct at HEAD, verified in source)

| Stage | Function | Status |
|---|---|---|
| CONTEXT build | `seh_build_context` (L1159-1210): ContextFlags=0x100007 (L1163 — M2 fix VERIFIED); RIP/RSP from g_cap_*; all 16 GPRs read via inline asm (values partially stale for volatile regs — acceptable per audit doc, fragile) | OK (would be correct once g_cap_rip fixed) |
| EXCEPTION_RECORD | `seh_dispatch_exception` L1275-1291: fields filled, then memcpy to `g_cap_er` (C1 fix VERIFIED — copy AFTER fill) | OK |
| VEH chain | L1378-1410: reverse-order iteration, ms_abi call via `call_veh_aligned` | OK (0 handlers on UPX path) |
| RF lookup | `seh_internal_lookup` (L618+): binary search over g_pdata_rva; gaps in .pdata end walk (structural S1) | OK (on coverage-present frames) |
| Virtual unwind | `seh_internal_virtual_unwind` (L904+): flags `(ui[0]>>3)&0x07` (critical fix VERIFIED L915); CHAININFO recompute (M1 fix VERIFIED L929-1014); unwind-code simulation incl. PUSH_NONVOL/ALLOC_*/SET_FPREG/SAVE_*/MACHFRAME; opcodes 9-15 skipped-with-log; establisher = fp or new_rsp (L1132) | OK (35/35 unit assertions) |
| Handler invocation | `call_handler_aligned` (L1207+): ms_abi, 32B shadow space, RCX=ER, RDX=est_frame, R8=ctx, R9=dc; ControlPc−1 and ctx.Rip−1 adjustments (L1494, L1508) for GCC LSDA call-site matching | OK (reached Frame[3] on historical binary; currently unreachable due to Stage-11 break) |
| EH_FIX override | L1607-1644: on ContinueSearch, parses DWARF LSDA (uleb128 CS) and if action>0 & lp>0 & pc in range → jump to LP with RSP=est_frame. **App-specific bypass of personality type-matching; undocumented; does not distinguish CATCH vs CLEANUP actions** | CONTROVERSIAL — isolate behind flag in Phase B |
| RtlUnwindEx | `mw_RtlUnwindEx` (L1809-1968): uses global `g_unwind_ctx` (ignores ctx_ptr arg — NOTE: real ABI passes ExceptionRecord in R8, ReturnValue in R9; param mapping in this 5-arg signature is nonstandard but self-consistent); EXP-035: `post_prologue_rsp = target_frame − alloc_total` computed by re-scanning target function's UNWIND_INFO (L1871-1924) — alloc_total loop has its own slot-bound quirk `>= count*2+16`; sets ctx.Rip/Rsp/Rax; stores targets; longjmp | IMPLEMENTED_UNVERIFIED (never reached at HEAD; correctness of EXP-035 arithmetic unproven) |
| Unwind restore | Dispatcher setjmp handler copies 16 regs ctx→g_unwind_regs (L1350-1367) BEFORE any calls; naked stub loads 16 regs, RCX←target_ip last, RSP last, `jmp *%rcx` (L1716-1741) | OK mechanically; correctness depends on Stage-11 fix + EXP-035 |
| Not-handled path | Returns to PE normally; PE's libstdc++ terminate → `mw_abort` → `_exit(134)` (L2722-2727) — **crash_handler (which would print [CRASH] and exit 139) never fires on this path** | OK-by-design; explains regression expectation mismatch |
| SetUnhandledExceptionFilter | stores filter pointer; invocation on walk-exhaustion: **not wired** (structural S3 — no UEF call from dispatcher) | PART |
| UnhandledExceptionFilter export | exists as stub (UPX registers one at 0x49c9c0) | PART |

## Cross-Cutting Notes

1. **Implemented vs. executing:** Large parts of the exception subsystem (dispatch walk, EH handlers, RtlUnwindEx, EH_FIX, landing-pad restore) are implemented and unit-verified in isolation but currently **never execute on the real target** because Stage 11 corrupts `g_cap_rip` before dispatch begins. The last verified full-path execution was on a pre-breakage binary (historical traces only).
2. **ABI inventory:** ms_abi used consistently for all PE-facing stubs (`__attribute__((ms_abi))`); SysV internally; naked stub for capture; trampolines only for variadic printf-family. Shadow space respected in `call_handler_aligned`/`call_veh_aligned`.
3. **Two undocumented target-modifying hacks exist at HEAD** (Stage 7 LSDA byte patch; dispatcher EH_FIX override). Both must be feature-flagged or removed before generalization (Phase C/D). Neither is referenced by any prior report — discovered in this audit.
4. **Relocation absent** (Stage 2) — acceptable only while every target loads at preferred base; blocked otherwise.
5. **Exit-path inventory:** normal return (L3479), `mw_exit`/`ExitProcess` (L476-478, L2715), `mw_abort`→134, crash_handler→139 (only for real signals), `_amsg_exit` (L2339).
