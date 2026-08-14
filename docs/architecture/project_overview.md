# MiniWin Project Overview

## Why MiniWin Exists

Windows applications are locked to the Windows operating system. Running them on
Linux requires either:

1. **Full virtualization** (VMware, VirtualBox) — heavy, slow, needs Windows license
2. **Wine** — complete Win32/NT implementation, ~100MB+ codebase, 30+ years of development
3. **Recompilation** — requires source code, not always available

MiniWin takes a fourth approach: **minimum viable compatibility**.

Instead of implementing all of Windows, MiniWin discovers the minimum Windows
behavior required by each target application and implements only that. This
is possible because:

- Most applications use a small subset of the Win32 API
- API behavior can be observed through traces and experiments
- AI can analyze failures and determine what's missing
- Each fixed application expands the compatibility surface

## Long Term Vision

Build "the smallest Windows that runs the maximum software."

The vision is not to replace Wine or Windows. It is to demonstrate that an
AI-assisted runtime can achieve targeted application compatibility with a
tiny codebase by being evidence-driven rather than specification-driven.

### Ultimate Goal

A runtime that can:
1. Take any Windows x64 PE executable
2. Automatically discover its API requirements
3. Implement the missing behavior
4. Run the application correctly

This is the "self-evolving" aspect — the runtime grows its own compatibility.

## Compatibility Goals

### Tier 1: Console Applications (Current Focus)
- Command-line tools (UPX, 7zip, curl, etc.)
- File utilities (dir, type, copy)
- Build tools (make, cmake wrappers)

### Tier 2: Simple GUI Applications
- Dialog boxes (MessageBox)
- Basic window management
- Text rendering

### Tier 3: Complex Applications
- Multi-window applications
- Network-aware applications
- Database connectors

### Out of Scope
- Games (DirectX, Vulkan)
- Anti-cheat protected applications
- Kernel-mode drivers
- .NET Framework applications (different runtime)

## Difference from Wine

| Aspect | Wine | MiniWin |
|--------|------|---------|
| Architecture | Complete DLL stack | Flat stub table |
| PE Loading | Full loader with DLL deps | Single-image loader |
| API Implementation | Full Win32/NT/Shell/GDI/DX | App-driven discovery |
| DLL Loading | LoadLibrary, delay-load, etc. | Not yet implemented |
| Process Model | Preloader + ntdll + wineserver | Single Linux process |
| Size | ~100MB+ source | ~120KB binary |
| Development | 30+ years, large community | AI-assisted, evidence-driven |
| Testing | Test suite + app database | Per-app regression tests |

Wine is an oracle for MiniWin — we compare our API behavior against Wine's
implementation to find differences.

## Difference from ReactOS

| Aspect | ReactOS | MiniWin |
|--------|---------|---------|
| Goal | Full Windows OS clone | Minimum viable runtime |
| Kernel | Own NT kernel | Uses Linux kernel directly |
| Drivers | Windows-compatible drivers | Uses Linux drivers |
| Filesystem | NTFS, FAT | Uses Linux filesystem |
| User mode | Complete Win32 subsystem | Flat stub table |
| Boot | Boots from disk | Runs as Linux process |

ReactOS reimplements Windows from the kernel up. MiniWin sits on top of
Linux and only provides the API surface that applications see.

## AI-Assisted Debugging Concept

Traditional debugging:
``nCrash → Developer investigates → Reads docs → Implements fix → Tests
```

MiniWin AI-assisted debugging:
``nCrash → Capture evidence → AI analyzes → AI identifies root cause →
AI implements minimal fix → Regression test → Knowledge archived
```

The AI doesn't just fix bugs — it:

1. **Captures evidence** before any code change (Rule 1: Evidence First)
2. **Creates BUG-XXX.md** documenting the complete failure state
3. **Proposes hypotheses** ranked by evidence support
4. **Tests each hypothesis** with controlled experiments
5. **Archives all findings** as permanent engineering knowledge
6. **Creates regression tests** to prevent knowledge loss

### Example: BUG-023 Discovery Process

1. **Observation**: UPX calls `abort()` after `RaiseException(0x20474343)`
2. **Evidence**: API trace shows full execution path to the crash
3. **Hypothesis 1**: HeapAlloc returns NULL → REJECTED (trace shows non-NULL)
4. **Hypothesis 2**: InitializeCriticalSection crash → REJECTED (trace shows success)
5. **Hypothesis 3**: Missing DLL imports → REJECTED (162/162 resolved)
6. **Hypothesis 4**: TLS callback failure → REJECTED (0 callbacks)
7. **Root Cause Found**: RtlLookupFunctionEntry returns NULL (stub)
8. **Fix**: Implemented binary search through .pdata (3030 entries)
9. **Result**: RaiseException now works, handler found at Frame[3]
10. **Archived**: BUG-023.md, EXP-NEXT report, checkpoint zip

This process is repeatable and systematic. Every crash becomes a new
experiment that expands the runtime's capabilities.

## Current Test Target

**UPX 4.2.4** (Ultimate Packer for eXecutables)

- Binary: `upx_decompressed.exe` (2.1MB, MinGW/GCC compiled)
- Command: `--version`
- Expected: Prints "upx 4.2.4" and exits 0
- Current: Exception handling partially works, exits with SIGSEGV

UPX was chosen because it's:
- A real-world application (not a toy)
- Console-based (no GUI needed)
- Compiled with MinGW/GCC (uses GCC exception handling)
- Relatively small (single EXE, no extra DLLs)
- Well-known (easily verifiable behavior)
