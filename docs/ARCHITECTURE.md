# Architecture

## AI-Native Windows Compatibility Runtime

**Version:** 0.1.0-pre
**Last Updated:** 2026-07-30

---

## Overview

The system observes Windows executable execution, captures complete traces, enables
deterministic replay, and provides AI-powered analysis of compatibility issues.

```
┌─────────────────────────────────────────────────────────────┐
│                    WINDOWS EXE (PE32+)                      │
│                  Real compiler-generated                     │
└─────────────────────┬───────────────────────────────────────┘
                      │
                      ▼
┌─────────────────────────────────────────────────────────────┐
│                    PE PARSER (Rust)                          │
│  DOS Header → PE Signature → COFF Header → Optional Header  │
│  Sections → Imports → Exports → Relocations                │
│                                                              │
│  Output: Structured PeFile object with all metadata         │
└─────────────────────┬───────────────────────────────────────┘
                      │
                      ▼
┌─────────────────────────────────────────────────────────────┐
│                  EXECUTION BACKEND                            │
│                                                              │
│  ┌──────────────┐          ┌───────────────────┐           │
│  │  Wine Mode    │          │  Simulated Mode   │           │
│  │  (native x64) │          │  (analysis only)  │           │
│  │              │ │          │                   │           │
│  │ Calls wine64  │ │          │ Traces PE load    │           │
│  │ binary        │ │          │ Dispatches APIs   │           │
│  │ Captures      │ │          │ No real CPU exec  │           │
│  │ stdout/exit   │ │          │                   │           │
│  └──────┬───────┘          └────────┬──────────┘           │
│         │                           │                        │
│         └───────────┬───────────────┘                        │
└─────────────────────┼───────────────────────────────────────┘
                      │
                      ▼
┌─────────────────────────────────────────────────────────────┐
│                  TRACE COLLECTION                            │
│                                                              │
│  Rust Trace System:           Wine Debug Channels:           │
│  - JSON-structured events     - WINEDEBUG=+relay            │
│  - 7 categories              - WINEDEBUG=+module           │
│  - Crash-resilient writes     - WINEDEBUG=+warn/err/fixme    │
│  - Category filtering         - Raw text output              │
│                                                              │
│  Events: PeParse, MemoryLoad, ApiCall, ApiReturn,           │
│          Execution, Crash, System                           │
└─────────────────────┬───────────────────────────────────────┘
                      │
                      ▼
┌─────────────────────────────────────────────────────────────┐
│                  NORMALIZATION                               │
│                                                              │
│  Wine → Structured JSON:        Rust → Already JSON:        │
│  - Regex parse Call/Ret lines   - Already structured         │
│  - Pair calls with returns     - Direct replay              │
│  - Group by DLL/function       - Direct analysis            │
│  - Compute statistics          - Direct comparison           │
│  - Thread ID tracking          - Type-safe fields            │
│                                                              │
│  Output: api_trace.json, execution.json, environment.json  │
└─────────────────────┬───────────────────────────────────────┘
                      │
                      ▼
┌─────────────────────────────────────────────────────────────┐
│                  DETERMINISTIC REPLAY                         │
│                                                              │
│  - Load trace from disk                                       │
│  - Replay events in sequence                                  │
│  - Generate regression specs:                                │
│    - Expected exit code                                      │
│    - Expected API call sequence                               │
│    - Expected console output                                  │
│  - Cross-iteration comparison                                 │
│    - Detect differences between runs                           │
│    - Report PASS/FAIL per check                               │
│                                                              │
│  Output: regression_spec.json, comparison report             │
└─────────────────────┬───────────────────────────────────────┘
                      │
                      ▼
┌─────────────────────────────────────────────────────────────┐
│                  AI ANALYSIS ENGINE                           │
│                                                              │
│  - Root cause analysis from trace data                        │
│  - Compatibility gap identification                          │
│  - Plugin request generation                                  │
│  - Trace statistics and pattern detection                    │
│  - Suggestion generation (template-based, v0)                │
│                                                              │
│  Output: AnalysisReport with findings and suggestions       │
└─────────────────────┬───────────────────────────────────────┘
                      │
                      ▼
┌─────────────────────────────────────────────────────────────┐
│                  COMPATIBILITY PLUGIN                         │
│                                                              │
│  (Future) AI-generated patches:                              │
│  - Wine configuration (winetricks, DLL overrides)             │
│  - Registry patches                                          │
│  - API wrapper/shim DLLs                                     │
│  - Environment variable settings                             │
│                                                              │
│  Verified through replay + regression testing               │
└─────────────────────────────────────────────────────────────┘
```

---

## Component Details

### 1. PE Parser (`runtime/src/pe/mod.rs`)

**Lines:** 1,372
**Language:** Rust

Parses Windows PE files from raw bytes into structured Rust types.

**Structs:**
```
PeFile
├── raw: Vec<u8>                    # Raw file bytes
├── path: PathBuf                  # File path
├── dos_header: DosHeader          # 64-byte DOS header
├── pe_signature: PeSignature      # PE\0\0 at e_lfanew
├── coff_header: CoffHeader        # 20-byte COFF header
├── optional_header: OptionalHeader # 240-byte PE32+ optional header
├── sections: Vec<SectionHeader>   # Per-section metadata
├── imports: Vec<ImportFunction>    # All imported functions
├── exports: Option<ExportDirectory> # Export table (if present)
├── relocations: Vec<RelocationBlock> # Base relocation blocks
├── machine_type: MachineType      # AMD64/I386/ARM64
├── subsystem: Subsystem            # Console/GUI/Native
├── is_64bit: bool
├── entry_point_rva: u32
├── image_base: u64
└── size_of_image: u32
```

**Supports:**
- PE32+ (x86-64) — primary target
- PE32 (x86) — limited support
- Machine types: AMD64 (0x8664), I386 (0x014C), ARM64 (0xAA64)
- Import by name and by ordinal
- ILT/IAT parsing
- RVA to file offset conversion via section mapping
- 16 data directories (export, import, resource, exception, cert, reloc, debug, etc.)

**Unit Tests:** Built-in test suite generates minimal PE32+ binary and validates parsing.

### 2. Execution Backend (`runtime/src/execution.rs`)

**Lines:** 213
**Language:** Rust

Two execution modes with auto-fallback:

**Wine Mode:**
```
1. Check if wine64 binary exists via `which::which("wine64")`
2. Run: Command::new("wine64").arg(pe_path).output()
3. Capture: stdout, stderr, exit code
4. Record trace events
```

**Simulated Mode:**
```
1. Load PE via PELoader (section mapping, import resolution)
2. Collect imported functions from PE
3. For each import, dispatch with synthetic arguments:
   - GetStdHandle → arg: STD_OUTPUT_HANDLE
   - WriteConsoleA → arg: "Hello, World!"
   - ExitProcess → arg: exit code 0
4. Record all dispatch events to trace
```

**Auto-fallback:** If `--backend wine` is requested but Wine is unavailable,
falls back to simulated mode with a warning.

### 3. Trace System (`runtime/src/trace/mod.rs`)

**Lines:** 355
**Language:** Rust

JSON-structured event logging with crash-resilient writes.

**Event Categories:**
```rust
enum TraceCategory {
    PeParse,       // PE parsing events
    MemoryLoad,    // Section mapping, memory allocation
    ApiCall,       // Win32 API call dispatched
    ApiReturn,     // API return value
    Execution,     // CPU execution events
    Crash,         // Crash/exception events
    System,        // System-level events
}
```

**Event Format (JSON lines):**
```json
{
  "timestamp": "2026-07-29T21:06:17.123456Z",
  "category": "api_call",
  "event": {
    "thread_id": 0,
    "module": "KERNEL32.DLL",
    "function": "GetStdHandle",
    "arguments": {"nStdHandle": -11},
    "return_value": "0x0000000000000003"
  }
}
```

**Crash Resilience:** Each event is flushed to disk immediately.
A crash mid-trace still preserves all events written before the crash.

### 4. Win32 Dispatch (`runtime/src/dispatch/mod.rs` + `runtime/src/win32/mod.rs`)

**Lines:** 466 + 673 = 1,139 total
**Language:** Rust

Two dispatcher implementations:

**Analysis Mode** (`dispatch/mod.rs` — 466 lines):
- JSON-based argument handling
- 33 API handlers
- FakeHeap allocator for VirtualAlloc/HeapAlloc simulation
- Used by the simulated execution backend

**Execution Mode** (`win32/mod.rs` — 673 lines):
- Memory-backed implementation
- Integrated with MemoryManager for real memory operations
- 30+ API handlers with actual memory semantics
- Used when running code through the memory manager

**Dispatched APIs (33 in analysis mode):**

| DLL | Functions |
|-----|-----------|
| KERNEL32.DLL | GetStdHandle, WriteConsoleA, ExitProcess, GetLastError, VirtualAlloc, VirtualFree, HeapAlloc, HeapFree, GetProcAddress, LoadLibraryA, FreeLibrary, GetModuleHandleA, GetCurrentProcessId, QueryPerformanceCounter, Sleep, CreateFileA, ReadFile, WriteFile, CloseHandle, SetConsoleTitleA, GetConsoleMode, SetConsoleTextAttribute, InitializeCriticalSection, EnterCriticalSection, LeaveCriticalSection, DeleteCriticalSection, GetCurrentThreadId |
| MSVCRT.DLL | printf, malloc, free, calloc, realloc, strlen, strcmp, memset, memcpy |
| NTDLL.DLL | RtlAllocateHeap, RtlFreeHeap, RtlInitUnicodeString |

### 5. Loader (`runtime/src/loader/mod.rs`)

**Lines:** 194
**Language:** Rust

PE loader that maps sections into virtual memory and resolves imports.

**Load Process:**
```
1. For each section in PE:
   - Allocate virtual memory at section's VirtualAddress + ImageBase
   - Copy raw data from file
   - Set memory protection (R/W/X) based on section characteristics
   - Record trace event
2. Process relocations:
   - For each RelocationBlock:
     - For each entry:
       - Compute target address
       - Apply fixup (DIR64 for PE32+)
3. Resolve imports:
   - For each imported function:
     - Look up in Win32Dispatcher
     - Write function pointer to IAT slot
4. Return LoadResult with entry point and statistics
```

### 6. Replay System (`runtime/src/replay/mod.rs`)

**Lines:** 266
**Language:** Rust

Deterministic replay and regression testing.

**Regression Spec Format:**
```json
{
  "exit_code": 0,
  "api_sequence": [
    {"module": "KERNEL32.DLL", "function": "GetStdHandle"},
    {"module": "KERNEL32.DLL", "function": "WriteConsoleA"},
    {"module": "KERNEL32.DLL", "function": "ExitProcess"}
  ],
  "console_output": ["Hello, World!"],
  "crashed": false
}
```

**Cross-Iteration Comparison:**
```
1. Load current trace
2. Load previous iteration's regression spec
3. Compare:
   - Exit code match?
   - API sequence match?
   - Console output match?
   - Crash status match?
4. Report PASS/FAIL with specific mismatches
```

### 7. Crash Recorder (`runtime/src/crash_recorder.rs`)

**Lines:** 165
**Language:** Rust

Generates crash report directories with full state capture.

**Crash Report Structure:**
```
CRASH-XXXXXX/
├── report.json          # Crash metadata
├── cpu_state.dump       # CPU register state
├── memory.dump          # Process memory snapshot
├── api.trace            # API call trace at crash time
├── execution.trace      # Execution trace events
└── environment.json     # System environment at crash time
```

### 8. AI Analysis (`runtime/src/analysis/mod.rs`)

**Lines:** 392
**Language:** Rust

Template-based root cause analysis and compatibility suggestions.

**Analysis Process:**
```
1. Collect statistics from replay result:
   - Total events, API calls, unique functions
   - Thread count, execution time
2. Detect crash patterns:
   - Invalid memory access
   - Unhandled exceptions
   - Missing API implementations
3. Generate findings:
   - Root cause description
   - Affected API/module
   - Suggested fix approach
4. Generate plugin requests:
   - What compatibility plugin is needed
   - What APIs it should implement
   - Priority level
```

### 9. Memory Manager (`runtime/src/mem/mod.rs`)

**Lines:** 485
**Language:** Rust

mmap-based virtual memory management for PE loading.

**Features:**
- Guest-to-host and host-to-guest address translation
- Heap allocation via system allocator
- Stack allocation with configurable size
- Memory protection via mprotect (PROT_READ, PROT_WRITE, PROT_EXEC)
- PE image mapping at configurable base address

### 10. Python Trace Collector (`scripts/trace_collector.py`)

**Lines:** 563
**Language:** Python

Runs Windows EXEs under Wine and produces structured trace packages.

**Pipeline:**
```
1. Run EXE under Wine (WINEDEBUG=-all)
   → Capture stdout, stderr, exit code

2. Run EXE under Wine (WINEDEBUG=+relay)
   → Capture all API Call/Return lines to wine_relay.raw

3. Run EXE under Wine (WINEDEBUG=+module)
   → Capture all module loading lines to wine_modules.raw

4. Parse wine_relay.raw
   → Extract API calls with thread ID, function, args, return value
   → Pair Call lines with corresponding Ret lines

5. Parse wine_modules.raw
   → Extract loaded DLLs with base addresses, sections

6. Analyze PE file with pefile
   → Full header, section, import analysis

7. Generate package:
   → execution.json
   → api_trace.json
   → environment.json
   → replay_metadata.json
```

---

## Data Flow Diagram

### Wine Trace Capture Flow

```
hello_simple.exe
    │
    ├──[WINEDEBUG=-all]──→ stdout: "Hello from real Windows x64 executable!\n"
    │                         exit_code: 0
    │
    ├──[WINEDEBUG=+relay]──→ wine_relay.raw (90MB)
    │   │                        1,361,398 lines
    │   │
    │   └──[parse_relay_trace()]──→ api_calls: Vec<ApiCall>
    │       │                           593,358 entries
    │       │                           Each: thread_id, function, args, retval, ret_addr
    │       └──[build_api_trace()]──→ api_trace.json
    │                                  by_function: {name: count}
    │                                  calls_by_dll: {dll: [functions]}
    │                                  sample_calls: first 200
    │
    ├──[WINEDEBUG=+module]──→ wine_modules.raw (115KB)
    │   │
    │   └──[parse_module_trace()]──→ loaded_modules: Dict
    │                                  Each: path, base_addr, sections, reloc
    │
    └──[pefile analysis]──→ pe_info: Dict
                              machine, sections, imports, hashes
```

### Rust Runtime Flow (Simulated)

```
minimal_pe64.exe
    │
    └──[PeFile::from_file()]──→ PeFile object
    │                              sections, imports, relocations
    │
    └──[PELoader::load()]──→ LoadResult
    │   ├── Section mapping    │   entry_point_va: 0x1000
    │   ├── Relocation processing│   sections_mapped: 2
    │   └── Import resolution   │   imports_resolved: 1
    │
    └──[Win32Dispatcher]──→ ApiHandlerResult
        ├── KERNEL32.GetStdHandle    → return_value: 0x3
        ├── KERNEL32.WriteConsoleA   → return_value: 0x1
        └── KERNEL32.ExitProcess     → return_value: 0x0
    │
    └──[TraceRecorder]──→ execution.trace
                              JSON lines with 7 event categories
    │
    └──[ReplaySession::load()]──→ ReplayResult
        ├── events_replayed: N
        ├── api_calls: [...]
        └── regression_spec: {...}
    │
    └──[AiAnalyzer::analyze()]──→ AnalysisReport
                                  findings, suggestions, plugin_requests
```

---

## Environment Configuration

### Required Paths

```bash
# Wine
WINE=/home/z/my-project/tools/wine-9.0-staging-tkg-amd64
WINEPREFIX=/home/z/.wine-runtime

# MinGW Cross-Compiler
MINGW=/home/z/my-project/tools/llvm-mingw-20240917-ucrt-ubuntu-20.04-x86_64/bin

# Rust Runtime
RUNTIME=/home/z/my-project/runtime
RUNTIME_BIN=$RUNTIME/target/release/winrt-ai

# Traces
TRACE_DIR=/home/z/my-project/traces
REPLAY_DIR=/home/z/my-project/replays
```

### Critical Execution Notes

1. **Always use `wine64` directly**, not the `wine` wrapper:
   ```bash
   $WINE/bin/wine64 hello.exe  # CORRECT
   $WINE/bin/wine hello.exe    # FAILS (uses /bin/sh which has issues)
   ```

2. **Always set WINEPREFIX** before running Wine:
   ```bash
   export WINEPREFIX=/home/z/.wine-runtime
   ```

3. **Three separate runs** are needed for full trace capture:
   - Clean run (WINEDEBUG=-all) for stdout/exit code
   - Relay run (WINEDEBUG=+relay) for API calls
   - Module run (WINEDEBUG=+module) for DLL loading
