# AI-Native Windows Compatibility Runtime

## Project History and Status

**Last Updated:** 2026-07-30
**Version:** 0.1.0-pre
**Status:** Active Development — Phase 1 Complete (Real Execution Milestone)

---

## Project Goal

### Why This Project Exists

Windows compatibility on Linux has been solved incrementally over 30 years. Wine implements
thousands of Win32 APIs by hand. ReactOS reimplements Windows from scratch. Both approaches
share the same fundamental limitation: **human engineers must manually identify, implement, and
debug each compatibility gap.**

This project asks a different question: *Can an AI system observe real Windows execution,
detect compatibility failures automatically, and generate fixes without human intervention?*

The goal is not to build another compatibility layer. It is to build an **observation and
correction pipeline** that sits alongside existing execution backends (Wine, native Windows,
or future runtimes) and:

1. **Observes** every aspect of execution (API calls, memory access, DLL loading, return values)
2. **Records** complete execution traces deterministically
3. **Compares** observed behavior against expected Windows behavior
4. **Analyzes** discrepancies to identify root causes
5. **Generates** compatibility plugins or patches
6. **Verifies** fixes through automated replay and regression testing

### Difference from Wine and ReactOS

| Aspect | Wine | ReactOS | This Project |
|--------|------|---------|--------------|
| **Goal** | Run Windows apps | Reimplement Windows OS | Observe, analyze, and auto-fix compatibility |
| **Approach** | Manual API implementation | Manual OS reimplementation | AI-driven trace analysis and plugin generation |
| **Execution** | IS the compatibility layer | IS the operating system | Uses existing backends (Wine, etc.) |
| **Scope** | Full Win32 + kernel | Full Windows NT kernel | Observation layer + AI analysis loop |
| **Testing** | Manual + test suites | Manual + test suites | Deterministic replay + regression |
| **Fix Generation** | Human engineers | Human engineers | AI-generated compatibility plugins |

This project is complementary to Wine and ReactOS. It does not replace them — it
augments them with an automated intelligence layer that can identify and propose fixes
for compatibility issues.

### AI Compatibility Engineering Vision

The long-term vision is a closed-loop system:

```
Application EXE
    |
    v
Execution Backend (Wine / Native / Custom)
    |
    v
Mandatory Trace System (observes everything)
    |
    v
Normalization (structured, comparable format)
    |
    v
Deterministic Replay (crash reproduction)
    |
    v
AI Root Cause Analysis
    |
    v
Compatibility Plugin Generation
    |
    v
Regression Testing (replay + compare)
    |
    v
Plugin Deployment
    |
    v
(Loop back to execution with plugin applied)
```

The core philosophy: **No bug without replay, no fix without regression test,
no compatibility patch without evidence.**

---

## Architecture

### High-Level Pipeline

```
EXE (Windows x64 PE32+)
    |
    v
PE Parser (Rust)
  - DOS Header, PE Signature, COFF Header
  - Optional Header (PE32+/PE32)
  - Section Headers (.text, .rdata, .data, .reloc, etc.)
  - Import Directory (DLL names, function names, thunks)
  - Export Directory
  - Base Relocations
    |
    v
Execution Backend
  - Wine (native x64 execution via Wine 9.0)
  - Simulated (analysis mode, traces load + dispatch)
    |
    v
Trace Collection
  - Wine +relay: API call/return pairs with args and return values
  - Wine +module: DLL loading with addresses and section mappings
  - Structured JSON trace events (Rust trace system)
    |
    v
Normalization
  - Parse raw Wine debug output into structured JSON
  - Group by function, thread, DLL
  - Compute statistics (call counts, unique functions, thread count)
    |
    v
Replay System
  - Load trace from disk
  - Replay events in order
  - Generate regression specifications
  - Cross-iteration comparison
    |
    v
AI Analysis Engine
  - Root cause analysis for crashes
  - Compatibility gap identification
  - Plugin request generation
  - Trace statistics and pattern detection
    |
    v
Compatibility Plugin
  - Generated patches based on trace analysis
  - Applied to execution backend
  - Verified through replay
```

### Component Inventory

| Component | Language | Lines | Location | Purpose |
|-----------|----------|-------|----------|---------|
| PE Parser | Rust | 1,372 | `runtime/src/pe/mod.rs` | Full PE32+/PE32 parsing |
| Execution Backend | Rust | 213 | `runtime/src/execution.rs` | Wine + Simulated modes |
| Trace System | Rust | 355 | `runtime/src/trace/mod.rs` | JSON-structured event logging |
| Win32 Dispatch | Rust | 466+673 | `runtime/src/{dispatch,win32}/mod.rs` | API handlers (analysis + execution modes) |
| Loader | Rust | 194 | `runtime/src/loader/mod.rs` | PE section mapping, imports |
| Replay System | Rust | 266 | `runtime/src/replay/mod.rs` | Deterministic replay |
| Crash Recorder | Rust | 165 | `runtime/src/crash_recorder.rs` | Crash state capture |
| AI Analysis | Rust | 392 | `runtime/src/analysis/mod.rs` | Root cause analysis |
| Memory Manager | Rust | 485 | `runtime/src/mem/mod.rs` | mmap-based guest memory |
| Error Types | Rust | 63 | `runtime/src/error.rs` | Unified error handling |
| CLI Entry Point | Rust | 332 | `runtime/src/main.rs` | 6 subcommands |
| Trace Collector | Python | 563 | `scripts/trace_collector.py` | Wine trace capture + parsing |
| PE Generator | Python | 166 | `scripts/gen_test_pe.py` | Minimal PE32+ test binary |

**Total: ~5,500+ lines across 14 source files (Rust + Python)**

---

## Completed Work

### Phase 1: Environment Setup

**Date:** 2026-07-29
**Task ID:** 1 (in worklog)

#### Linux Environment

- **Distribution:** Debian GNU/Linux 13 (trixie)
- **Kernel:** Linux 5.10.134-013.8.3.kangaroo.al8.x86_64
- **Architecture:** x86_64
- **User:** `z` (uid=1001, no sudo access)
- **Disk:** 9.9GB total, 5.0GB free
- **Rust:** 1.97.1 (via rustup)
- **Python:** 3.12.13 (with venv)
- **GCC:** 14.2.0

#### Portable Wine Installation

Wine was not available through apt (no sudo). Installed from a portable binary:

```bash
# Download Wine 9.0 Staging TkG portable
curl -L \
  "https://github.com/Kron4ek/Wine-Builds/releases/download/9.0/wine-9.0-staging-tkg-amd64.tar.xz" \
  -o tools/wine-portable.tar.xz

# Extract
cd tools && tar xf wine-portable.tar.xz
# → tools/wine-9.0-staging-tkg-amd64/
```

**Wine Details:**
- **Version:** wine-9.0.r0.gcab93f47 (TkG Staging Esync Fsync)
- **Source:** https://github.com/Kron4ek/Wine-Builds/releases/tag/9.0
- **Binary path:** `/home/z/my-project/tools/wine-9.0-staging-tkg-amd64/bin/wine64`
- **Prefix:** `/home/z/.wine-runtime`
- **Archive size:** 56MB compressed
- **Initialized with:** `wine64 wineboot --init`

**Important:** The `wine` wrapper script does not work in this environment (uses `/bin/sh`).
Always call `wine64` directly:

```bash
WINE=/home/z/my-project/tools/wine-9.0-staging-tkg-amd64
export WINEPREFIX=/home/z/.wine-runtime
$WINE/bin/wine64 <executable.exe>
```

#### LLVM MinGW Cross-Compiler Installation

No system MinGW cross-compiler was available. Installed LLVM-MinGW portable:

```bash
# Download LLVM MinGW cross-compiler (Linux x86_64 → Windows x86_64)
curl -L \
  "https://github.com/mstorsjo/llvm-mingw/releases/download/20240917/llvm-mingw-20240917-ucrt-ubuntu-20.04-x86_64.tar.xz" \
  -o tools/llvm-mingw.tar.xz

# Extract
cd tools && tar xf llvm-mingw.tar.xz
# → tools/llvm-mingw-20240917-ucrt-ubuntu-20.04-x86_64/
```

**Compiler Details:**
- **Version:** Clang 19.1.0 (LLVM-based MinGW-w64 cross-compiler)
- **Source:** https://github.com/mstorsjo/llvm-mingw/releases/tag/20240917
- **Binary path:** `tools/llvm-mingw-20240917-ucrt-ubuntu-20.04-x86_64/bin/x86_64-w64-mingw32-gcc`
- **Target:** x86_64-w64-windows-gnu (UCRT runtime)
- **Archive size:** 84MB compressed
- **Includes:** GCC, G++, AR, NM, STRIP, DLLTOOL, and full Windows headers/libs

**Other cross-compiler targets available in the same package:**
- `i686-w64-mingw32-gcc` (32-bit Windows)
- `aarch64-w64-mingw32-gcc` (ARM64 Windows)
- `armv7-w64-mingw32-gcc` (ARM32 Windows)

---

### Phase 2: Real Windows Executable Generation

**Date:** 2026-07-29
**Task ID:** 2 (in worklog)

#### Compiler

LLVM-MinGW 19.1.0 cross-compiling on Linux for Windows x86-64.

#### Source Code

Two test executables were created:

**hello_real.exe** — Uses WinMain with direct Win32 API calls:

```c
// Source: tools/test-binaries-real/hello.c
#include <windows.h>
#include <stdio.h>

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                   LPSTR lpCmdLine, int nCmdShow) {
    HANDLE hStdOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD written;
    const char msg[] = "Hello from real Windows x64 executable!\r\n";
    WriteConsoleA(hStdOut, msg, sizeof(msg) - 1, &written, NULL);
    ExitProcess(0);
    return 0;
}
```

**hello_simple.exe** — Uses printf/stdout (better Wine console output):

```c
// Source: /tmp/hello_simple.c (compiled inline)
#include <stdio.h>

int main() {
    printf("Hello from real Windows x64 executable!\n");
    return 0;
}
```

#### Build Commands

```bash
MINGW=/home/z/my-project/tools/llvm-mingw-20240917-ucrt-ubuntu-20.04-x86_64/bin

# hello_real.exe (WinMain + WriteConsoleA)
$MINGW/x86_64-w64-mingw32-gcc \
  -o /home/z/my-project/tools/test-binaries-real/hello_real.exe \
  /home/z/my-project/tools/test-binaries-real/hello.c \
  -lkernel32 -luser32 \
  -static -mconsole -O2

# hello_simple.exe (printf/stdout)
$MINGW/x86_64-w64-mingw32-gcc \
  -o /home/z/my-project/tools/test-binaries-real/hello_simple.exe \
  /tmp/hello_simple.c \
  -lucrt -mconsole -O2
```

#### Generated EXE Information (hello_simple.exe)

| Property | Value |
|----------|-------|
| **File size** | 87,552 bytes (85.5 KB) |
| **Format** | PE32+ executable for MS Windows 6.00 (console), x86-64 |
| **Machine** | AMD64 (0x8664) |
| **Subsystem** | Console (IMAGE_SUBSYSTEM_WINDOWS_CUI = 3) |
| **Entry Point** | RVA 0x1350 |
| **Image Base** | 0x140000000 |
| **Sections** | 13 (.text, .rdata, .buildid, .data, .pdata, .tls, .reloc, /4, /18, /30, /42, /53, /67) |
| **Import DLLs** | 9 (KERNEL32.dll, api-ms-win-crt-stdio-l1-1-0.dll, api-ms-win-crt-runtime-l1-1-0.dll, api-ms-win-crt-heap-l1-1-0.dll, api-ms-win-crt-private-l1-1-0.dll, api-ms-win-crt-string-l1-1-0.dll, api-ms-win-crt-math-l1-1-0.dll, api-ms-win-crt-environment-l1-1-0.dll, api-ms-win-crt-time-l1-1-0.dll) |
| **Import Functions** | 40+ (GetStdHandle, WriteConsoleA, ExitProcess, HeapAlloc, HeapFree, VirtualProtect, printf, malloc, free, strlen, etc.) |
| **MD5** | 925cbb666ccbc323688f7758080be2c5 |
| **SHA256** | ba36d9231ea683b64f70db23fc21f297d184a36d354cee0e7b8a9a46fdf26e74 |

**Key distinction:** This is a **real** compiler-generated executable, NOT a hand-crafted
synthetic PE. It has 13 sections including CRT metadata sections (/4, /18, /30, /42, /53, /67),
UCRT runtime imports, proper exception handling metadata (.pdata), and TLS support (.tls).

For comparison, the previous synthetic test PE (`tests/fixtures/minimal_pe64.exe`) was only
2,048 bytes with 2 sections and a single `RET` instruction.

---

### Phase 3: Real Execution Under Wine

**Date:** 2026-07-29
**Task ID:** 2 (in worklog)

#### Wine Version

```
wine-9.0.r0.gcab93f47 ( TkG Staging Esync Fsync )
```

Staging branch with Esync and Fsync patches for better Linux gaming/app compatibility.

#### Execution Command

```bash
export WINE=/home/z/my-project/tools/wine-9.0-staging-tkg-amd64
export WINEPREFIX=/home/z/.wine-runtime

# Clean execution (capture stdout only)
WINEDEBUG=-all $WINE/bin/wine64 \
  /home/z/my-project/tools/test-binaries-real/hello_simple.exe
```

#### Stdout

```
Hello from real Windows x64 executable!
```

#### Exit Code

```
0
```

#### Stderr (Wine informational messages)

```
wineserver: using server-side synchronization.
```

#### Execution Time

~4.5 seconds (first run includes Wine initialization overhead).

---

### Phase 4: Trace Collection

**Date:** 2026-07-29
**Task ID:** 2 (in worklog)

#### Wine Tracing Method

Three separate Wine debug channels were used, each capturing different information:

**1. Clean Execution (WINEDEBUG=-all):**
```bash
WINEDEBUG=-all wine64 hello_simple.exe > stdout.txt 2>stderr.txt
```
Captures only the program's actual stdout/stderr output.

**2. API Call Trace (WINEDEBUG=+relay):**
```bash
WINEDEBUG=+relay wine64 hello_simple.exe > wine_relay.raw 2>&1
```
Captures every Win32 API call and return:
```
002c:Call ntdll.LdrDisableThreadCalloutsForDll(6fffffc00000) ret=6fffffc297a3
002c:Ret  ntdll.LdrDisableThreadCalloutsForDll() retval=00000000 ret=6fffffc297a3
002c:Call KERNEL32.HeapFree(001b0000,00000000,00000000) ret=7bf120dc
002c:Ret  KERNEL32.HeapFree() retval=00000001 ret=7bf120dc
```

**3. Module Loading Trace (WINEDEBUG=+module):**
```bash
WINEDEBUG=+module wine64 hello_simple.exe > wine_modules.raw 2>&1
```
Captures every DLL load with base addresses and section mappings:
```
002c:trace:module:map_image_into_view mapping PE file L"\\??\\C:\\windows\\system32\\ntdll.dll" at 0x6ffffff30000-0x6ffffffe1000
002c:trace:module:map_image_into_view mapping L"ntdll.dll" section .text at 0x6ffffff31000 off 1000 size 77000
002c:trace:module:relocating L"ntdll.dll" dynamic base 170000000 -> 6ffffff30000
```

#### Parser Design

The Python trace collector (`scripts/trace_collector.py`) uses line-level regex parsing
with a single-pass approach:

- **Call pattern:** `^([0-9a-f]+):Call\s+(.+?)\(([^)]*)\)\s+ret=([0-9a-f]+)$`
- **Return pattern:** `^([0-9a-f]+):Ret\s+(.+?)(\(\) retval=([0-9a-f]+))?\s+ret=([0-9a-f]+)$`
- **PE DLL call pattern:** Separate regex for DllMain notifications (nested parentheses)
- **PE DLL return pattern:** Matches `retval=N` after closing parenthesis

Matching uses a pending-call dictionary keyed by thread ID to pair Call/Ret lines.

**Limitation:** Multi-threaded execution with the same thread ID appearing in nested
calls can cause mis-pairing. The parser handles the common case correctly but does not
track call stack depth.

#### Captured Data (TRACE-20260729-211123 — Final/Best Trace Package)

| Metric | Value |
|--------|-------|
| **Total API call/return pairs** | 593,358 |
| **Unique API functions** | 313 |
| **DLLs called** | 14 (KERNEL32, ntdll, ucrtbase, user32, kernelbase, msvcrt, advapi32, gdi32, imm32, ntoskrnl, rpcrt4, setupapi, uxtheme, win32u) |
| **Threads observed** | 48 |
| **Loaded modules** | 20 (ntdll.dll, kernelbase.dll, kernel32.dll, msvcrt.dll, and 16 others) |
| **Raw relay trace** | 90.1 MB (1,361,398 lines) |
| **Raw module trace** | 114.9 KB (1,012 lines) |
| **Parse time** | ~2 seconds for 1.3M lines |

**Top 10 API functions by call count:**

| Function | Calls |
|----------|-------|
| ntdll.RtlFreeHeap | 332,784 |
| ucrtbase.isspace | 56,358 |
| ntdll.memcmp | 55,107 |
| ntdll.memmove | 55,074 |
| ntdll.RtlCompareUnicodeStrings | 39,441 |
| KERNEL32.HeapFree | 16,513 |
| ntdll.RtlAllocateHeap | 10,856 |
| ucrtbase.wcslen | 6,462 |
| ntdll.RtlNtStatusToDosError | 2,683 |
| ntdll.RtlInitUnicodeString | 2,565 |

#### Known Limitations of Current Tracing

1. **Wine's +relay is very verbose** — a simple HelloWorld generates 90MB of trace data.
   Most calls are internal Wine/NTDLL operations unrelated to the target application.
2. **No filtering by target process** — Wine traces all processes including wineserver,
   wineboot, and other Wine infrastructure.
3. **No memory access tracing** — Wine's relay only traces API calls, not memory reads/writes.
4. **No CPU state capture** — Wine's relay does not record register state at each call.
5. **Three separate executions required** — clean output, relay trace, and module trace
   must each run the executable separately (non-deterministic timing).
6. **Thread ID reuse** — Wine recycles thread IDs across processes, complicating parsing.

---

### Rust Runtime Implementation (Phase 1 — Pre-Wine)

**Date:** 2026-07-29
**Task ID:** 1 (in worklog)

Before Wine was installed, the Rust runtime was built with a simulated execution backend.
This proved the architecture and pipeline but did NOT execute real Windows binaries.

#### What Was Built

1. **PE Parser** (`runtime/src/pe/mod.rs` — 1,372 lines)
   - Parses DOS header, PE signature, COFF header, optional header (PE32+ and PE32)
   - Section headers with characteristics (C/X/R/W flags)
   - Import directory with ILT/IAT, hint/name pairs, ordinal imports
   - Export directory with name/ordinal/RVA
   - Base relocations (all types including DIR64)
   - RVA-to-file-offset conversion
   - Full unit test suite

2. **Win32 Dispatch Layer** (`runtime/src/dispatch/mod.rs` — 466 lines)
   - 33 API handlers across KERNEL32.DLL, MSVCRT.DLL, NTDLL.DLL
   - Handlers include: GetStdHandle, WriteConsoleA, ExitProcess, GetLastError,
     VirtualAlloc, VirtualFree, HeapAlloc, HeapFree, GetProcAddress,
     LoadLibraryA, FreeLibrary, GetModuleHandleA, GetCurrentProcessId,
     QueryPerformanceCounter, Sleep, CreateFileA, ReadFile, WriteFile,
     CloseHandle, SetConsoleTitleA, GetConsoleMode, SetConsoleTextAttribute,
     GetLastError, InitializeCriticalSection, EnterCriticalSection,
     LeaveCriticalSection, DeleteCriticalSection, GetCurrentThreadId,
     RtlAllocateHeap, RtlFreeHeap, RtlInitUnicodeString,
     printf, malloc, free, calloc, realloc, strlen, strcmp, memset, memcpy

3. **Execution Backend** (`runtime/src/execution.rs` — 213 lines)
   - Wine mode: calls `wine64` binary, captures stdout/stderr/exit code
   - Simulated mode: traces PE load + API dispatch sequence without CPU execution
   - Auto-fallback: if Wine is unavailable, falls back to simulated mode

4. **Trace System** (`runtime/src/trace/mod.rs` — 355 lines)
   - JSON-structured event logging to file
   - 7 event categories: PeParse, MemoryLoad, ApiCall, ApiReturn, Execution, Crash, System
   - Crash-resilient file writes (flushes after each event)
   - Category-based filtering

5. **Replay System** (`runtime/src/replay/mod.rs` — 266 lines)
   - Load trace from directory
   - Replay events in sequence
   - Generate regression specifications (exit code, API call sequence, console output)
   - Cross-iteration comparison (detect differences between runs)

6. **AI Analysis Engine** (`runtime/src/analysis/mod.rs` — 392 lines)
   - Root cause analysis from trace data
   - Compatibility gap identification
   - Plugin request generation
   - Trace statistics computation

7. **Crash Recorder** (`runtime/src/crash_recorder.rs` — 165 lines)
   - Generates CRASH-XXXXXX directories
   - Contains: report.json, cpu_state.dump, api.trace, execution.trace, environment.json

8. **Memory Manager** (`runtime/src/mem/mod.rs` — 485 lines)
   - mmap-based PE memory mapping
   - Guest-to-host and host-to-guest address translation
   - Heap and stack allocation
   - Memory protection via mprotect (R/W/X)

9. **CLI** (`runtime/src/main.rs` — 332 lines)
   - 6 subcommands: analyze, run, replay, crash-analyze, dump, loop
   - Configurable log level and output directory

#### Build and Run

```bash
cd /home/z/my-project/runtime
cargo build --release
# → target/release/winrt-ai (50MB binary)

# Analyze a PE file
./target/release/winrt-ai analyze tests/fixtures/minimal_pe64.exe

# Execute with simulated backend
./target/release/winrt-ai run tests/fixtures/minimal_pe64.exe --backend simulated

# Execute with Wine backend
./target/release/winrt-ai run tools/test-binaries-real/hello_simple.exe --backend wine

# Run AI debug loop (3 iterations)
./target/release/winrt-ai loop tests/fixtures/minimal_pe64.exe --iterations 3
```

---

## Current Limitations

### What Is REAL and WORKING

| Feature | Status | Evidence |
|---------|--------|----------|
| Real Windows EXE compilation | Working | MinGW/Clang 19.1.0 produces valid PE32+ binaries |
| Real EXE execution under Wine | Working | hello_simple.exe exits with code 0, produces stdout |
| Real Wine API call tracing | Working | 593,358 real API calls captured via +relay |
| Real DLL loading observation | Working | 20 modules with base addresses and sections |
| PE file parsing | Working | Full parser handles PE32+ and PE32 |
| Structured trace packages | Working | JSON: execution.json, api_trace.json, environment.json, replay_metadata.json |
| Deterministic replay | Working | Replay system loads traces and generates regression specs |
| AI analysis (template-based) | Working | Root cause analysis and compatibility suggestions |

### What Is NOT COMPLETE

| Feature | Status | Why |
|---------|--------|-----|
| AI automatic fixing | Not started | Analysis engine generates suggestions but does not modify code |
| Windows vs Wine comparison | Not started | No Windows oracle to compare Wine traces against |
| Production compatibility layer | Not started | This project is an observation layer, not a compatibility layer |
| Memory access tracing | Not possible | Wine's +relay does not trace memory access |
| CPU register state capture | Not possible | Would need GDB integration or custom Wine patches |
| Single-execution multi-channel trace | Not possible | Wine requires separate runs for +relay and +module |
| Non-console application support | Not tested | GUI apps need X11/display which is not available |
| Multi-threaded trace accuracy | Limited | Thread ID reuse in Wine complicates parsing |
| Large application testing | Not done | Only HelloWorld has been tested |
| Trace size management | Not addressed | 90MB for HelloWorld is impractical for real apps |
| Incremental/differential tracing | Not started | No way to trace only new/unusual behavior |

### Honest Assessment

**The primary milestone is complete:** A real Windows x64 executable was compiled, executed
under Wine, and produced a real (non-synthetic) execution trace with 593K+ API calls.

**The pipeline is proven but thin:** The architecture exists and works end-to-end, but
each component is at minimum viability. The AI analysis engine produces template-based
suggestions, not learned or intelligent analysis. The replay system works for the Rust
trace format but is not yet integrated with the Wine trace format. The crash recorder
has never been tested against a real crash.

**The trace format needs standardization:** Currently there are two separate trace formats:
the Rust JSON trace format (from the simulated backend) and the parsed Wine trace format
(from trace_collector.py). These need to be unified.

---

## Reproduction Guide

### Prerequisites

- Linux x86_64 (tested on Debian 13 trixie)
- ~500MB free disk space
- Internet access (for downloading Wine and MinGW)
- No sudo required

### Step 1: Download and Install Tools

```bash
cd /home/z/my-project

# Download Wine 9.0 portable
curl -L \
  "https://github.com/Kron4ek/Wine-Builds/releases/download/9.0/wine-9.0-staging-tkg-amd64.tar.xz" \
  -o tools/wine-portable.tar.xz
cd tools && tar xf wine-portable.tar.xz && cd ..

# Download LLVM MinGW cross-compiler
curl -L \
  "https://github.com/mstorsjo/llvm-mingw/releases/download/20240917/llvm-mingw-20240917-ucrt-ubuntu-20.04-x86_64.tar.xz" \
  -o tools/llvm-mingw.tar.xz
cd tools && tar xf llvm-mingw.tar.xz && cd ..
```

### Step 2: Initialize Wine Prefix

```bash
export WINE=/home/z/my-project/tools/wine-9.0-staging-tkg-amd64
export WINEPREFIX=/home/z/.wine-runtime

$WINE/bin/wine64 wineboot --init
# Expected: some warnings about services, but exit code 0
```

### Step 3: Compile a Windows EXE

```bash
MINGW=/home/z/my-project/tools/llvm-mingw-20240917-ucrt-ubuntu-20.04-x86_64/bin

cat > /tmp/hello.c << 'EOF'
#include <stdio.h>
int main() {
    printf("Hello from real Windows x64 executable!\n");
    return 0;
}
EOF

$MINGW/x86_64-w64-mingw32-gcc \
  -o /home/z/my-project/tools/test-binaries-real/hello_simple.exe \
  /tmp/hello.c -lucrt -mconsole -O2

# Verify
file /home/z/my-project/tools/test-binaries-real/hello_simple.exe
# Expected: PE32+ executable for MS Windows 6.00 (console), x86-64
```

### Step 4: Run the EXE Under Wine

```bash
export WINE=/home/z/my-project/tools/wine-9.0-staging-tkg-amd64
export WINEPREFIX=/home/z/.wine-runtime

WINEDEBUG=-all $WINE/bin/wine64 \
  /home/z/my-project/tools/test-binaries-real/hello_simple.exe

# Expected stdout:
# Hello from real Windows x64 executable!
# Expected exit code: 0
```

### Step 5: Generate Trace

```bash
python3 /home/z/my-project/scripts/trace_collector.py \
  /home/z/my-project/tools/test-binaries-real/hello_simple.exe

# Expected output:
# TRACE PACKAGE COMPLETE
# Exit Code: 0
# API Calls: 593358 total
# Loaded Modules: 20 total
# Package files: execution.json, api_trace.json, environment.json, replay_metadata.json, wine_relay.raw, wine_modules.raw
```

### Step 6: Analyze Trace

```bash
TRACE_DIR=$(ls -d /home/z/my-project/traces/TRACE-* | tail -1)

# View execution summary
python3 -c "
import json
with open('$TRACE_DIR/execution.json') as f:
    d = json.load(f)
print(f'Exit code: {d[\"exit_code\"]}')
print(f'Stdout: {repr(d[\"stdout\"])}')
print(f'API calls: {d[\"statistics\"][\"total_api_calls\"]}')
print(f'Modules: {d[\"statistics\"][\"total_loaded_modules\"]}')
"

# View top API functions
python3 -c "
import json
with open('$TRACE_DIR/api_trace.json') as f:
    d = json.load(f)
for func, info in sorted(d['by_function'].items(), key=lambda x: -x[1]['count'])[:10]:
    print(f'{func:60s} {info[\"count\"]:>8d} calls')
"
```

### Step 7: Build Rust Runtime (Optional)

```bash
cd /home/z/my-project/runtime
cargo build --release

# Run analysis on a PE file
./target/release/winrt-ai analyze ../tests/fixtures/minimal_pe64.exe

# Run simulated execution
./target/release/winrt-ai run ../tests/fixtures/minimal_pe64.exe --backend simulated

# Run Wine execution
./target/release/winrt-ai run ../tools/test-binaries-real/hello_simple.exe --backend wine
```

---

## Future Roadmap

### Milestone 1 (Next): Windows vs Wine Trace Comparison

**Goal:** Establish a baseline by running the same EXE on real Windows and Wine, then
comparing the API call sequences to identify Wine-specific divergences.

**Requirements:**
- Access to a real Windows machine or Windows CI
- Standardized trace format for both platforms
- Diff algorithm for API call sequences

### Milestone 2: Better Structured Trace Format

**Goal:** Replace the current Wine debug output parsing with a unified trace format
that captures:
- API calls with typed arguments (not raw hex strings)
- Memory allocations and accesses
- Thread lifecycle events
- Module load/unload with version info
- Structured error information

**Approach:** Either patch Wine's relay code or use a DLL injection approach to hook
API calls directly.

### Milestone 3: Real Application Testing

**Goal:** Move beyond HelloWorld to test real Windows applications:
- 7-Zip console
- putty (SSH client)
- curl for Windows
- Small installers
- Applications that crash under Wine

**Challenges:** GUI apps need display; large apps produce enormous traces.

### Milestone 4: AI Analysis Engine

**Goal:** Replace template-based analysis with real AI-powered root cause detection.

**Approach:**
- Feed trace data to an LLM with structured prompts
- Identify patterns that indicate compatibility issues
- Generate specific fix suggestions with evidence

### Milestone 5: Plugin Generation

**Goal:** Automatically generate Wine compatibility patches (winetricks, DLL overrides,
registry settings) based on trace analysis.

### Milestone 6: Regression Replay System

**Goal:** After a fix is applied, automatically re-run the application and compare the
new trace against the old one to verify the fix worked and nothing else broke.

---

## File Inventory

### Project Structure

```
/home/z/my-project/
├── docs/                           # Documentation (this file and others)
├── scripts/
│   ├── gen_test_pe.py              # Python PE binary generator (166 lines)
│   └── trace_collector.py           # Wine trace collector (563 lines)
├── runtime/                         # Rust runtime project
│   ├── Cargo.toml                   # Project manifest
│   ├── Cargo.lock                   # Dependency lock
│   ├── src/
│   │   ├── main.rs                  # CLI entry point (332 lines)
│   │   ├── lib.rs                   # Library root (42 lines)
│   │   ├── error.rs                 # Error types (63 lines)
│   │   ├── execution.rs             # Execution backend (213 lines)
│   │   ├── crash_recorder.rs        # Crash recording (165 lines)
│   │   ├── pe/
│   │   │   └── mod.rs               # PE parser (1,372 lines)
│   │   ├── loader/
│   │   │   └── mod.rs               # PE loader (194 lines)
│   │   ├── trace/
│   │   │   └── mod.rs               # Trace system (355 lines)
│   │   ├── dispatch/
│   │   │   └── mod.rs               # Win32 dispatch - analysis mode (466 lines)
│   │   ├── win32/
│   │   │   └── mod.rs               # Win32 dispatch - execution mode (673 lines)
│   │   ├── replay/
│   │   │   └── mod.rs               # Replay system (266 lines)
│   │   ├── analysis/
│   │   │   └── mod.rs               # AI analysis (392 lines)
│   │   └── mem/
│   │       └── mod.rs               # Memory manager (485 lines)
│   ├── logs/execution.trace         # Sample trace output
│   ├── replays/CRASH-000001/       # Sample crash report
│   └── test-binaries/hello.exe      # Test PE binary
├── tests/
│   └── fixtures/minimal_pe64.exe    # Minimal synthetic PE (2,048 bytes)
├── tools/
│   ├── wine-9.0-staging-tkg-amd64/ # Wine 9.0 portable binary
│   ├── llvm-mingw-20240917-*/       # LLVM MinGW cross-compiler
│   ├── test-binaries-real/
│   │   ├── hello.c                  # WinMain source
│   │   ├── hello_real.exe           # Real EXE (WriteConsoleA version)
│   │   └── hello_simple.exe         # Real EXE (printf version)
│   ├── wine-portable.tar.xz         # Wine archive (56MB)
│   └── llvm-mingw.tar.xz            # MinGW archive (84MB)
├── traces/
│   └── TRACE-20260729-211123/       # Best trace package
│       ├── execution.json            # Execution summary + PE analysis
│       ├── api_trace.json            # Parsed API calls
│       ├── environment.json          # Host + Wine environment
│       ├── replay_metadata.json     # Replay instructions
│       ├── wine_relay.raw           # Raw Wine +relay (90MB)
│       └── wine_modules.raw         # Raw Wine +module (115KB)
├── replays/                          # Rust runtime replays
│   └── loop/                        # AI debug loop iterations
└── worklog.md                        # Development history
```

### Dependencies

**Rust (runtime/Cargo.toml):**
- log 0.4, env_logger 0.11 — structured logging
- serde 1, serde_json 1 — JSON serialization
- chrono 0.4 — timestamps
- thiserror 2 — error derivation
- clap 4 — CLI argument parsing
- goblin 0.9 — alternative PE parsing (available but primary parser is custom)
- hex 0.4 — hex encoding
- uuid 1 — unique IDs
- which 7 — executable detection
- tempfile 3 — temporary files

**Python (scripts/):**
- pefile 2024.8.26 — PE file analysis
- py7zr — 7z archive extraction
- Standard library: json, re, subprocess, hashlib, struct, pathlib, datetime, collections
