# MiniWin Development Roadmap

## Current State (2026-08-14)

### Working
- PE64 loading (10 sections, relocations, section permissions)
- Import resolution (162/162 resolved)
- TLS handling
- TEB/PEB environment setup
- Heap and memory APIs
- CRT initialization
- .pdata parsing (3030 entries, binary search)
- x64 unwind engine (9 opcodes + GCC extensions)
- Exception dispatcher (RtlDispatchException with frame walking)
- EH handler discovery and invocation
- LSDA parsing (GCC format)
- RtlUnwindEx (longjmp-based, partial)
- API tracing (JSON format)
- 12 regression tests (all passing)
- EXP-NEXT-2 synthetic proof (handler discovery verified)

### Blocked
- **UPX `--version` execution**: Handler at Frame[4] triggers RtlUnwindEx but context
  restoration after longjmp is incomplete. The catch clause is reached but
  execution does not resume correctly at the landing pad.

---

## Phase 1: Complete Exception Handling (Current)

### 1.1 Fix RtlUnwindEx Context Restoration [CRITICAL]

**Problem**: RtlUnwindEx uses longjmp to return to the dispatcher, but
the CONTEXT record is not properly restored to the target frame's state.

**Approach**:
1. Save jmp_buf before calling handler
2. When RtlUnwindEx is called:
   - Record target IP and target frame from parameters
   - Walk frames from current to target using RtlVirtualUnwind
   - Restore CONTEXT to target frame state
   - Longjmp to the dispatcher's dispatch loop
3. Dispatcher checks if CONTEXT.Rip == target IP
4. If yes, set RSP = CONTEXT.Rsp and jump to CONTEXT.Rip

**Evidence needed**:
- Target IP from RtlUnwindEx: 0x40331c
- Target frame: 0x7fffe21cc49c8
- Landing pad address from LSDA

**Test**: UPX `--version` should print "upx 4.2.4" and exit 0

### 1.2 Implement GCC LSDA Interpretation [HIGH]

**Problem**: The handler at Frame[3] returns ContinueSearch because
we don't parse the LSDA to find matching catch clauses. We just call
the GCC personality function and forward its response.

**Approach**:
1. Parse call site table (CS entries)
2. For each call site, check if RIP is in range [start, start+length)
3. If yes, read landing_pad and action index
4. If landing_pad != 0 and action != 0:
   - Look up action chain from action table
   - For each action, look up type index
   - Match exception type against catch clause type
   - If match: unwind to landing_pad

**This is what __C_specific_handler does for MSVC, but for GCC**.

### 1.3 Exception Object Cleanup [MEDIUM]

When exception is caught, the exception object must be cleaned up.
GCC uses `_Unwind_DeleteException` for this.

---

## Phase 2: Expand Application Support

### 2.1 Stage 2 Target: Simple C Console App

Build a minimal MSVC-compiled C program that prints "Hello":
```c
#include <stdio.h>
int main() { printf("Hello from MiniWin\n"); return 0; }
```

This validates the entire pipeline without exception handling.

### 2.2 Stage 3 Target: CLI Tools

- `dir /b` equivalent (ls-like)
- `echo` command
- `type` command (cat-like)

### 2.3 File I/O Completion

Implement real file I/O:
- CreateFileA → open()
- ReadFile/WriteFile → read()/write()
- CloseHandle → close()
- GetFileSize → fstat()
- FindFirstFileA/FindNextFileA → opendir()/readdir()

---

## Phase 3: Architecture Improvements

### 3.1 Module Split

Split `loader.c` (~2400 lines) into modules:
```
src/loader/pe_loader.c       — load_pe, section mapping
src/loader/relocation.c      — base relocation processing
src/imports/import_resolver.c — IAT resolution
src/memory/heap.c            — HeapAlloc/Free/Realloc
src/memory/virtual.c         — VirtualAlloc/Free/Protect/Query
src/win32/kernel32.c         — KERNEL32 stubs
src/win32/msvcrt.c          — msvcrt stubs
src/runtime/teb_peb.c        — TEB/PEB setup
src/runtime/crt.c            — CRT startup helpers
src/exception/raise.c        — RaiseException + naked stub
src/exception/dispatch.c     — RtlDispatchException
src/unwind/lookup.c          — RtlLookupFunctionEntry
src/unwind/virtual_unwind.c  — RtlVirtualUnwind
src/unwind/rtl_unwind_ex.c   — RtlUnwindEx
```

### 3.2 Real Heap

Replace bump allocator with a real free-list allocator:
- Track allocated blocks
- Support HeapFree properly
- Support HeapRealloc with in-place growth

### 3.3 Thread Support

- Implement CreateThread → pthread_create
- Per-thread TEB
- TLS with real per-thread storage
- Critical sections with pthread_mutex

---

## Phase 4: DLL Loading

### 4.1 LoadLibrary / GetProcAddress

- Parse DLL PE files
- Map into process address space
- Resolve DLL's own imports
- Export table lookup for GetProcAddress

### 4.2 Common DLL Stubs

Priority DLLs:
1. KERNEL32.DLL (already stubbed in-process)
2. msvcrt.dll (already stubbed in-process)
3. USER32.DLL (MessageBoxA, CreateWindowExA)
4. GDI32.DLL (BeginPaint, TextOutA)
5. ADVAPI32.DLL (RegOpenKey, etc.)
6. SHELL32.DLL (ShellExecuteA, etc.)

---

## Phase 5: GUI Support

### 5.1 Console Window

- Allocate a pseudo-terminal (pty)
- Connect PE's console I/O to pty
- Support GetConsoleScreenBufferInfo
- Support SetConsoleTextAttribute (colors)

### 5.2 Basic Window Management

- CreateWindowExA → X11 window or terminal rendering
- MessageBoxA → terminal dialog
- Basic message loop (GetMessage/TranslateMessage/DispatchMessage)

---

## Milestones

| Milestone | Target | Status |
|-----------|--------|--------|
| M1: PE loads and CRT runs | UPX (partial) | DONE |
| M2: No crash before EP | UPX | DONE |
| M3: Imports all resolve | UPX (162/162) | DONE |
| M4: .pdata parsed | UPX (3030 entries) | DONE |
| M5: Unwind engine works | EXP-NEXT-2 PASS | DONE |
| M6: Exception dispatched | UPX (4 frames walked) | DONE |
| M7: Handler invoked | UPX (2 handlers called) | DONE |
| M8: RtlUnwindEx completes | — | BLOCKED |
| M9: UPX --version output | UPX prints version | BLOCKED on M8 |
| M10: Simple C program | Hello World | PENDING |
| M11: File I/O CLI tool | dir/echo/type | PENDING |
| M12: DLL loading | LoadLibrary | PENDING |
| M13: GUI window | Basic window | PENDING |
