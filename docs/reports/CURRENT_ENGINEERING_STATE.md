# Current Engineering State

**Date**: 2026-08-14
**Active Codebase**: C loader (`src/loader/loader.c`)
**Target Binary**: UPX 4.2.4 (upx_decompressed.exe, MinGW/GCC AMD64)
**Test Command**: `./minwin_loader samples/upx_decompressed.exe --version`
**Expected Output**: `upx 4.2.4`
**Actual Output**: (none — SIGSEGV, exit code 139)

---

## What Works

### PE Loading Pipeline
- DOS/PE/COFF header parsing with magic verification ('MZ', 'PE\0\0', 0x8664, 0x020B)
- 10 sections mapped with correct permissions (RX, R, RW, etc.)
- Base relocation processing (if ImageBase differs from preferred 0x400000)
- Entry point extraction and trampoline jump

### Import Resolution
- Import Directory Table walk (OriginalFirstThunk / FirstThunk)
- 162 imports resolved: KERNEL32.DLL (68) + msvcrt.dll (94)
- 0 unresolved imports
- All IAT slots filled with stub addresses

### Runtime Environment
- TEB (Thread Environment Block) with real stack bounds from pthread_getattr_np
- PEB (Process Environment Block) with ImageBaseAddress, process parameters
- RTL_USER_PROCESS_PARAMETERS with standard handles (0/1/2) and command line

### Memory Management
- HeapAlloc: bump allocator from 16MB static mmap region (working, no free)
- HeapFree: no-op (acceptable for short-lived CLI apps)
- HeapRealloc: malloc + copy
- VirtualAlloc: mmap wrapper
- VirtualFree: munmap wrapper
- VirtualProtect: mprotect wrapper
- VirtualQuery: /proc/self/maps parser

### CRT Support
- `_initterm`: walks function pointer arrays, calls C and C++ initializers
- `__getmainargs`: parses command line, sets argc/argv/envp
- `__set_app_type`: sets console app type
- `__iob_func`: returns 3 FILE structs at static address 0x2018000
- Standard C library: malloc, free, printf, fprintf, strlen, strcmp, etc.

### Exception Handling Infrastructure
- `.pdata` parser: 3030 RUNTIME_FUNCTION entries, stored globally
- `RtlLookupFunctionEntry`: binary search through .pdata (O(log n))
- `RtlVirtualUnwind`: full opcode simulation (PUSH_NONVOL, ALLOC_SMALL, ALLOC_LARGE, SET_FPREG, SAVE_NONVOL, SAVE_NONVOL_FAR, SAVE_XMM128, SAVE_XMM128_FAR, PUSH_MACHFRAME) + GCC extensions (opcodes 9-15)
- `seh_dispatch_exception`: frame walking loop with EHANDLER discovery
- GCC personality routine invocation via ms_abi inline asm (with shadow space)
- LSDA parsing: LPStart, TType encoding, call site table
- `RaiseException` naked ms_abi stub for precise register capture
- VEH chain support (AddVectoredExceptionHandler / RemoveVectoredExceptionHandler)
- SetUnhandledExceptionFilter
- API trace logging in JSON format

### Verified on Real Binary (UPX)
- PE loads at 0x400000, EP=0x4014f0, 10 sections
- 162 imports resolved, CRT init runs, __getmainargs executes
- RaiseException(0x20474343) reached (GCC C++ exception class)
- 5 frames walked correctly
- Frame[3]: EHANDLER at RVA 0xe0220, returned ContinueSearch
- Frame[4]: EHANDLER at RVA 0xe0220, triggered RtlUnwindEx

### Testing
- 12 automated regression tests (all passing with pre-built binary)
- EXP-NEXT-2 synthetic proof (PASS — handler discovery verified)
- 4 synthetic dispatch test PEs (test_a/b/c/d.exe)
- 5 checkpoint archives at development milestones

---

## What Is Incomplete

### Critical Blocker

**RtlUnwindEx context restoration** — When the GCC personality routine at
Frame[4] requests an unwind to the catch landing pad (target_ip=0x40331c),
RtlUnwindEx longjmps back to the dispatcher. The dispatcher must then:
1. Walk frames from exception site to target frame using RtlVirtualUnwind
2. Restore all nonvolatile registers from each frame's unwind info
3. Set CONTEXT.Rsp = target frame address
4. Set CONTEXT.Rip = target_ip (0x40331c)
5. Jump to CONTEXT.Rip via inline asm

Currently, only the longjmp works. Steps 1-5 are not implemented.

### Compilation Errors

The current `loader.c` has 3 compile errors introduced in the final
development session:
1. `g_cap_er` undeclared at line 1177
2. `EH_UNWINDING` redefined at lines 3113-3115

The pre-built binary (from an earlier version) works correctly.

### __C_specific_handler Is a No-Op

Returns `DISP_ExceptionContinueSearch` (1) unconditionally. This means:
- The handler at Frame[3] returns ContinueSearch because we don't parse
  the LSDA to find matching catch clauses ourselves
- We rely on the GCC personality routine to make the decision
- Full GCC LSDA interpretation (call site table, action table, type table)
  is not implemented

### Bump Allocator Never Frees

HeapFree is a no-op. Long-running applications will exhaust the 16MB heap.

### No DLL Loading

No LoadLibrary / GetProcAddress. Only flat IAT resolution at load time.

### No File I/O

CreateFileA, ReadFile, WriteFile, CloseHandle are stubs returning success.

### No Multi-Threading

CreateThread exists as a stub. No per-thread TEB. Critical sections
are no-ops (acceptable for single-threaded apps).

### Malformed UNWIND_INFO

236 out of 3030 RUNTIME_FUNCTION entries have slot overflows or missing
ALLOC opcodes. This is systematic in GCC/MinGW output. The dispatcher
handles these gracefully (skips them) but stack walking precision is
reduced for affected frames.

---

## Next Engineering Milestone

**M8: Complete RtlUnwindEx Context Restoration**

This is the single task that unblocks UPX `--version` execution.

### Specific Approach (from ROADMAP Phase 1.1)

1. Before calling the handler, save a `jmp_buf` via `setjmp`
2. When `RtlUnwindEx` is called by the personality routine:
   - Record target_ip and target_frame from parameters
   - Longjmp back to the saved `jmp_buf` in the dispatcher
3. After longjmp, in the dispatcher:
   - Walk frames from current to target using `RtlVirtualUnwind`
   - For each intermediate frame, restore nonvolatile registers to CONTEXT
   - Set `CONTEXT.Rsp = target_frame` and `CONTEXT.Rip = target_ip`
   - Use inline asm to restore all registers from CONTEXT and jump to Rip
4. Verify: UPX `--version` should print "upx 4.2.4" and exit 0

### Evidence Needed
- Target IP: 0x40331c (from RtlUnwindEx parameters in trace)
- Target frame address (from DISPATCHER_CONTEXT.TargetIp)
- Landing pad address from Frame[4] LSDA

---

## What Should NOT Be Touched

These components are verified working and must not regress:

| Component | Function | Lines (approx) |
-----------|----------|----------------|
| PE Parser | `load_pe` | ~2800-2950 |
| Section Mapper | within `load_pe` | ~2950-3050 |
| Relocation Handler | within `load_pe` | ~3050-3100 |
| Import Resolver | `resolve_imports` | ~2500-2700 |
| TEB/PEB Setup | `setup_teb_peb` | ~2000-2200 |
| Heap APIs | `mw_HeapAlloc` etc. | ~2300-2400 |
| Virtual Memory | `mw_VirtualAlloc` etc. | ~2400-2500 |
| CRT Stubs | `_initterm`, `__getmainargs` | within stubs |
| Signal Handler | `crash_handler` | ~3100-3115 |
| .pdata Parser | within `load_pe` | ~2700-2800 |
| RtlLookupFunctionEntry | `mw_RtlLookupFunctionEntry` | ~1550-1650 |
| RtlVirtualUnwind | `mw_RtlVirtualUnwind` | ~1650-1900 |
| Naked Stub | `mw_RaiseException` | ~3150-3200 |
| API Trace | `MW_TRACE` macro | ~50-80 |

**Rule**: Before modifying any of these, run the 12-test regression suite.
After modifying, run it again. All 12 must pass.

---

## What Evidence Exists

### In Repository

| Evidence | Location | Content |
----------|----------|----------|
| Checkpoint zips | `checkpoints/` (5 files) | Full source+docs at 5 milestones |
| Test PE binaries | `tests/dispatch_tests/`, `tests/exp_next2/` | 5 synthetic PEs |
| Test harnesses | `tests/dispatch_tests/test_dispatch_harness.c`, `tests/exp_next2/exp_next2_harness.c` | 1263 lines of test code |
| Regression suite | `tests/regression/test_pe_loader_regression.sh` | 12 automated tests |
| Bug reports | `docs/bugs/` (4 files) | BUG-001, BUG-023, BUG-023-expnext, BUG-024 |
| Experiment reports | `docs/experiments/` (3 files) | EXP-NEXT, EXP-NEXT-2, EXP-NEXT-3 |
| ABI reference | `docs/windows_abi/windows_x64_complete.md` | Complete Windows x64 ABI |
| Source backups | `loader.c.baseline_2134`, `loader.c.exp_next2_backup` | Known-good versions |

### Quoted in Documentation

| Evidence | Quoted In |
----------|----------|
| Full exception dispatch trace (5 frames) | docs/reports/current_status.md lines 81-97 |
| Exception class bytes | KNOWLEDGE_BASE.md lines 223-229 |
| .pdata statistics (3030 entries, opcode distribution) | KNOWLEDGE_BASE.md lines 195-218 |
| UPX section table | KNOWLEDGE_BASE.md lines 14-31 |
| Import resolution evidence (162/162) | KNOWLEDGE_BASE.md lines 110-117 |
| EXP-NEXT-2 proof trace | KNOWLEDGE_BASE.md lines 233-241 |
| API trace excerpt (EP to RaiseException) | README.md lines 132-142 |
| Malformed UNWIND_INFO analysis | KNOWLEDGE_BASE.md lines 210-218 |

### Not in Repository (excluded by .gitignore)

| Evidence | Original Location | Content |
----------|-----------------|----------|
| 28 execution logs | evidence/execution_logs/ | stderr/stdout from dev sessions |
| API trace JSON | evidence/api_trace/ | upx_api_trace.json (329 lines) |
| Crash logs | evidence/crash_logs/ | SIGSEGV dumps |
| upx_decompressed.exe | samples/ | 2.1MB test binary |
| Pre-built minwin_loader | (binary) | 268KB working loader |