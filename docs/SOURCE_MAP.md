# Source Code Map

**Date**: 2026-08-14

Maps every component in the MiniWin C loader to its location, purpose, dependencies, and status. The entire active runtime lives in a single monolithic file (`src/loader/loader.c`, 3115 lines). The planned module split is documented in ROADMAP.md Phase 3.1.

---

## Core Components (all in src/loader/loader.c)

| Component | Lines (approx) | Purpose | Dependencies | Status |
|-----------|---------------|---------|--------------|--------|
| PE Parser | 2800-2950 | Parse DOS/COFF/Optional headers, section table, data directories | pe.h, stdio.h | STABLE |
| Section Mapper | 2950-3050 | mmap PE image, copy headers, map sections with correct permissions | PE Parser, sys/mman.h | STABLE |
| Base Relocation | 3050-3100 | Process .reloc directory, fix absolute addresses if ImageBase differs | PE Parser | STABLE |
| Import Resolver | 2500-2700 | Walk ILT/IAT, match import names to stub table, write addresses | PE Parser, all stub functions | STABLE (162/162) |
| TEB/PEB Setup | 2000-2200 | Allocate and fill TEB, PEB, RTL_USER_PROCESS_PARAMETERS | pthread.h, memory APIs | STABLE |
| Heap Allocator | 2300-2400 | Bump allocator from static 16MB mmap region | sys/mman.h | STABLE (no free) |
| Virtual Memory | 2400-2500 | VirtualAlloc/Free/Protect/Query stubs | sys/mman.h, /proc/self/maps | STABLE |
| Win32 API Stubs | 100-2000 | ~162 stubs for KERNEL32 + msvcrt | Heap, Virtual Memory, TEB | STABLE |
| CRT Support | Within stubs | _initterm, __getmainargs, __set_app_type, __iob_func | Import Resolver | STABLE |
| Signal Handler | 3100-3115 | SIGSEGV/SIGABRT handler, logs RIP/fault, exits | signal.h | STABLE |
| Naked RaiseException Stub | ~3150-3200 | __attribute__((naked, ms_abi)) captures exact register state | Inline asm | STABLE |
| RaiseException Impl | 800-850 | Build EXCEPTION_RECORD + CONTEXT, call dispatcher | pe.h structs | STABLE |
| RtlLookupFunctionEntry | 1550-1650 | Binary search through .pdata RUNTIME_FUNCTION array | Global .pdata vars | STABLE |
| RtlVirtualUnwind | 1650-1900 | Parse UNWIND_INFO, simulate unwind opcodes, find handler | pe.h, .xdata | STABLE (9 opcodes + GCC ext) |
| seh_dispatch_exception | 1100-1550 | Frame walking loop, EHANDLER discovery, handler invocation | Lookup, Unwind, inline asm | WORKING (tested on real binary) |
| __C_specific_handler | 900-1000 | GCC personality routine forwarder (no-op, returns ContinueSearch) | pe.h | STUB (blocks C++ EH) |
| RtlUnwindEx | 1000-1100 | Longjmp-based unwind to target frame | setjmp.h, dispatcher | PARTIAL (context restore incomplete) |
| API Trace Logging | 50-80 | JSON-formatted trace of all API calls during execution | stdio.h | STABLE |
| Main / Entry Trampoline | 3200-3115 | Parse args, load_pe, resolve_imports, setup_teb_peb, jump to EP | All components | STABLE |

---

## Header File

| File | Lines | Purpose | Status |
|------|-------|---------|--------|
| include/pe.h | 269 | PE structures (DOS, COFF, Optional, Section, Import, TLS, Exception), RUNTIME_FUNCTION, UNWIND_INFO, UNWIND_CODE, CONTEXT register offsets (0x4D0), DISPATCHER_CONTEXT, EXCEPTION_RECORD, GCC/MSVC exception constants | STABLE |

---

## Backup Files

| File | Lines | Purpose | Status |
|------|-------|---------|--------|
| src/loader/loader.c.baseline_2134 | 2,134 | Checkpoint before SEH implementation. Compiles and runs. Use as fallback. | STABLE |
| src/loader/loader.c.exp_next2_backup | 2,348 | Checkpoint before dispatcher was added to main loader. | STABLE |
| include/pe.h.exp_next2_backup | 254 | Header backup from EXP-NEXT-2 era. | STABLE |

---

## Planned Module Split (ROADMAP Phase 3.1)

These directories exist with `.gitkeep` placeholders. The code to fill them is
still inside `src/loader/loader.c`.

| Planned File | Current Location (lines) | Component |
|-------------|------------------------|-----------|
| src/loader/pe_loader.c | loader.c ~2800-3050 | PE parsing + section mapping |
| src/loader/relocation.c | loader.c ~3050-3100 | Base relocation processing |
| src/imports/import_resolver.c | loader.c ~2500-2700 | IAT resolution |
| src/memory/heap.c | loader.c ~2300-2400 | HeapAlloc/Free/Realloc |
| src/memory/virtual.c | loader.c ~2400-2500 | VirtualAlloc/Free/Protect/Query |
| src/win32/kernel32.c | loader.c ~100-1200 | KERNEL32 stubs |
| src/win32/msvcrt.c | loader.c ~1200-2000 | msvcrt stubs |
| src/runtime/teb_peb.c | loader.c ~2000-2200 | TEB/PEB setup |
| src/runtime/crt.c | loader.c (within stubs) | CRT startup helpers |
| src/exception/raise.c | loader.c ~800-850 + ~3150 | RaiseException + naked stub |
| src/exception/dispatch.c | loader.c ~1100-1550 | RtlDispatchException |
| src/unwind/lookup.c | loader.c ~1550-1650 | RtlLookupFunctionEntry |
| src/unwind/virtual_unwind.c | loader.c ~1650-1900 | RtlVirtualUnwind |
| src/unwind/rtl_unwind_ex.c | loader.c ~1000-1100 | RtlUnwindEx |

---

## Rust Runtime (Separate Project)

The `runtime/` directory contains a separate Rust reimplementation. It is NOT the
active development target but is preserved for reference.

| File | Lines | Purpose |
|------|-------|---------|
| runtime/src/main.rs | 331 | CLI entry point (analyze, run, replay, crash-analyze, dump, loop) |
| runtime/src/lib.rs | 41 | Crate root, module declarations |
| runtime/src/loader.rs | 1,309 | PE loading and execution orchestration |
| runtime/src/pe_mod.rs | 1,371 | PE format parsing (custom parser, not using goblin) |
| runtime/src/mem_mod.rs | 484 | Memory management abstraction |
| runtime/src/win32_mod.rs | 673 | Win32 API dispatch layer |
| runtime/src/dispatch_mod.rs | 465 | Import dispatch and stub generation |
| runtime/src/analysis_mod.rs | 391 | AI analysis engine (template-based, not real AI) |
| runtime/src/execution.rs | 212 | Execution backend (simulated + Wine) |
| runtime/src/replay_mod.rs | 265 | Deterministic replay system |
| runtime/src/trace_mod.rs | 355 | Trace collection and formatting |
| runtime/src/trace.rs | 232 | Trace data structures |
| runtime/src/crash_recorder.rs | 165 | Crash recording and reporting |
| runtime/src/loader_mod.rs | 193 | Loader module interface |
| runtime/src/error.rs | 63 | Error types (uses thiserror) |

**Note**: The Rust runtime was designed for a Wine-based oracle approach. It collects
traces from real Wine execution and replays them. The C loader takes a different
approach: direct execution without Wine. The two projects share knowledge but not
code.
