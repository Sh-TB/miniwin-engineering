# MiniWin Runtime — Complete Engineering Knowledge Transfer

> **A self-evolving Windows x64 PE runtime compatibility layer for Linux.**
> Not a Windows clone — the minimum Windows required by applications.

---

## What is MiniWin?

MiniWin is a research project to build the smallest possible runtime that can execute
real Windows x64 PE applications on Linux, without Wine, without a Windows VM,
and without recompilation. It is inspired by the "minimum viable emulation" principle
from the SharpEmu PS5 emulator project.

The runtime implements only the behavior actually required by target applications,
discovered through controlled experiments and evidence-based development. Every crash
is new knowledge. Every fix becomes permanent capability.

### Key Differences

| Feature | Wine | ReactOS | MiniWin |
|---------|------|---------|---------|
| Goal | Full Windows compat | Full Windows OS | Minimum viable runtime |
| Approach | Complete DLL stack | Kernel + subsystems | App-driven API discovery |
| DLL loading | Full DLL infrastructure | Full loader | Flat IAT resolution |
| Kernel | Linux syscall translation | Own NT kernel | Linux syscalls directly |
| Size | ~100MB+ | Full OS | ~120KB binary |
| Development | Community-driven | Community-driven | AI-assisted evolution |

### AI-Assisted Debugging Concept

MiniWin uses an AI-assisted development loop:
1. **Run** the target application with the runtime
2. **Capture** API traces, crash dumps, and execution evidence
3. **Analyze** failures with AI to determine root cause
4. **Implement** minimal fix for the specific behavior
5. **Test** with regression suite to prevent knowledge loss
6. **Archive** all findings as permanent engineering knowledge

---

## How to Build

### Prerequisites

- GCC 14+ (or Clang 18+)
- Linux x86_64
- GNU Make
- Python 3 (for analysis scripts)

### Build

```bash
make
```

This produces `minwin_loader` (~120KB).

### Build Flags

```makefile
CC = gcc
CFLAGS = -O2 -g -Wall -Wno-unused-function -no-pie
LDFLAGS = -no-pie -ldl -Wl,-Ttext-segment=0x2000000
```

The `-Ttext-segment=0x2000000` places the loader's own code above 0x400000 (typical
PE ImageBase) to avoid address collisions with mapped PE images.

---

## How to Run Tests

### Regression Tests (require built loader + sample binary)

```bash
# Full regression suite
./tests/regression/test_pe_loader_regression.sh .

# Expected: 12/12 tests pass
```

### EXP-NEXT-2: Synthetic Exception Dispatch Test

```bash
cd tests/exp_next2
gcc -o exp_next2_harness exp_next2_harness.c -O2 -no-pie -g
./exp_next2_harness synthetic_test.exe
# Expected: PASS — Handler discovered at Frame 1
```

### Dispatch Tests (BUG-024 synthetic PE validation)

```bash
cd tests/dispatch_tests
gcc -o test_dispatch_harness test_dispatch_harness.c -O2 -g -no-pie
./test_dispatch_harness test_a.exe
./test_dispatch_harness test_b.exe
./test_dispatch_harness test_c.exe
./test_dispatch_harness test_d.exe
```

### Run Target Application

```bash
./minwin_loader samples/upx_decompressed.exe --version
```

---

## Current Capabilities

### Verified Working (as of 2026-08-14)

- **PE64 Loading**: Full PE32+ parsing, section mapping, relocation
- **Import Resolution**: KERNEL32.DLL (68 imports) + msvcrt.dll (94 imports) = 162 total, 0 unresolved
- **TLS Execution**: Thread Local Storage callback support
- **CRT Initialization**: `_initterm`, `__getmainargs`, `__set_app_type` all working
- **TEB/PEB Setup**: Full Thread Environment Block and Process Environment Block
- **Heap APIs**: `HeapAlloc`, `HeapFree`, `HeapRealloc` working
- **Memory APIs**: `VirtualAlloc`, `VirtualFree`, `VirtualProtect`, `VirtualQuery`
- **Exception Metadata Parsing**: `.pdata` (3030 RUNTIME_FUNCTION entries), `.xdata`
- **x64 Unwind Engine**: `RtlLookupFunctionEntry` (binary search), `RtlVirtualUnwind` (all 9 opcodes)
- **Exception Dispatcher**: `RtlDispatchException` with full frame walking
- **EH Handler Discovery**: EHANDLER flag detection, handler invocation via ms_abi
- **LSDA Parsing**: GCC Language-Specific Data Area parsing
- **RtlUnwindEx**: Partial implementation (longjmp-based unwind to target frame)
- **Naked Stub**: `__attribute__((naked, ms_abi))` for precise register capture
- **API Tracing**: JSON-formatted trace of all API calls during execution

### API Trace Summary (UPX 4.2.4)

```
PE Loaded at 0x400000, EP=0x4014f0, 10 sections
162 imports resolved (KERNEL32: 68, msvcrt: 94)
CRT init complete
SetUnhandledExceptionFilter(handler=0x49c9c0) called
RaiseException(0x20474343) → exception dispatcher active
EHANDLER found at Frame[3] RVA 0xe0220 (GCC personality)
Handler called → returned ContinueSearch
RtlUnwindEx triggered → unwind to target frame
```

---

## Current Limitations

### Critical Blocker

1. **GCC C++ Exception Handling**: The handler at Frame[3] returns `ContinueSearch`
   because the LSDA does not match the exception type for `--version` mode.
   The handler at Frame[4] triggers `RtlUnwindEx` but the longjmp-based
   implementation does not fully restore the execution context.
   **Result**: UPX prints no output, exits with SIGSEGV (139).

2. **Malformed UNWIND_INFO**: 236 out of 3030 RUNTIME_FUNCTION entries in UPX
   have slot overflows or missing ALLOC opcodes. The dispatcher handles these
   gracefully but stack walking precision is reduced.

### Known Issues

3. **No C++ Exception Support**: `__C_specific_handler` is a no-op stub. Full
   GCC DWARF-style LSDA interpretation needed for try/catch to work.
4. **No DLL Loading**: Only flat IAT resolution. No LoadLibrary/GetProcAddress
   for runtime DLL loading.
5. **Limited Win32 APIs**: ~162 stubs implemented. Many return success without
   real behavior (per engineering rules — implement behavior, not names).
6. **No GUI**: Console applications only.
7. **Single-threaded**: Thread creation APIs exist but PE runs on main thread.
8. **No Resource Loading**: `.rsrc` section not parsed.

---

## Development Rules

See [ENGINEERING_RULES.md](ENGINEERING_RULES.md) for the complete rule set.

### Summary

1. **Evidence First** — Never implement without evidence
2. **Never Hide Failures** — Every crash becomes BUG-XXX.md
3. **Build From Smallest Target** — Hello PE → C exe → CLI → GUI → Complex
4. **Separate Layers** — Loader → API → Win32 → CRT → GUI
5. **Every Feature Needs a Test** — No untested features
6. **Checkpoint Before Major Changes** — Never destroy working version
7. **Oracle Comparison** — Compare against Wine/Windows behavior
8. **Implement Behavior, Not Names** — A function name is not compatibility
9. **AI Assisted Evolution** — Every app run produces knowledge
10. **Prefer Minimal Implementation** — Smallest Windows for maximum software
11. **No Fake Success** — Correct output, not just "doesn't crash"
12. **Knowledge Must Survive** — All discoveries documented permanently

---

## Repository Structure

```
miniwin-runtime-knowledge-transfer/
├── README.md                    # This file
├── ARCHITECTURE.md              # Technical architecture
├── ENGINEERING_RULES.md         # Development rules
├── KNOWLEDGE_BASE.md            # Accumulated knowledge
├── ROADMAP.md                   # Future development plan
├── CHANGELOG.md                 # Development history
├── Makefile                     # Build system
├── src/
│   ├── loader/                  # Main loader (loader.c ~2400 lines)
│   │   ├── loader.c             # PE loader + all subsystems
│   │   ├── loader.c.baseline_2134
│   │   └── loader.c.exp_next2_backup
│   └── pe/                      # PE format definitions (copy)
│       └── pe.h
├── include/
│   ├── pe.h                     # PE structures, CONTEXT offsets, SEH types
│   └── pe.h.exp_next2_backup
├── tests/
│   ├── regression/              # Automated regression tests
│   ├── synthetic_pe/            # Synthetic PE binaries for testing
│   ├── exp_next/                # Dispatch test PEs (test_a/b/c/d.exe)
│   ├── exp_next2/               # EXP-NEXT-2 harness + results
│   ├── dispatch_tests/          # BUG-024 synthetic tests
│   └── integration/             # Future integration tests
├── docs/
│   ├── experiments/             # Experiment archive
│   ├── bugs/                    # BUG-001, BUG-023, BUG-024
│   ├── architecture/            # Architecture decisions & analysis
│   ├── windows_abi/             # Windows x64 ABI reference
│   └── reports/                 # Status reports
├── evidence/
│   ├── api_trace/               # JSON API traces
│   ├── crash_logs/              # Crash dumps
│   ├── execution_logs/          # 28 stderr/stdout logs
│   ├── binary_analysis/         # PE analysis output
│   └── hashes/                  # Checkpoint hashes
├── scripts/
│   ├── build/                   # Build scripts & patches
│   ├── analysis/                # PE analysis & diagnostic scripts
│   └── testing/                 # Test utilities
├── checkpoints/                 # Checkpoint ZIP archives
├── samples/                     # Test binaries (upx_decompressed.exe)
├── tools/                       # Utility tools
└── worklog_archive.md           # Historical work log
```

---

## Where Experiments Are Stored

- **Experiment reports**: `docs/experiments/` (EXP-NEXT, EXP-NEXT-2, EXP-NEXT-3)
- **Bug reports**: `docs/bugs/` (BUG-001, BUG-023, BUG-023-expnext, BUG-024)
- **Execution evidence**: `evidence/execution_logs/` (28 log files)
- **API traces**: `evidence/api_trace/`
- **Checkpoint archives**: `checkpoints/`
- **Historical worklog**: `worklog_archive.md`

---

## Quick Start for New Developers

1. Read this README
2. Read `ARCHITECTURE.md` for system design
3. Read `ENGINEERING_RULES.md` for development methodology
4. Read `KNOWLEDGE_BASE.md` for accumulated knowledge
5. Read `docs/bugs/BUG-024-rtl-dispatch-exception.md` for current blocker
6. Build: `make`
7. Run tests: `./tests/regression/test_pe_loader_regression.sh .`
8. Check `ROADMAP.md` for what to work on next

---

## License

Research project. All code and documentation is archived for engineering knowledge transfer.
