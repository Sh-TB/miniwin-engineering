# MiniWin Technical Architecture

## System Overview

MiniWin is a monolithic PE loader and Windows compatibility runtime implemented in a
single C source file (`src/loader/loader.c`, ~2400 lines). It maps Windows x64 PE
binaries into Linux process memory, resolves imports to stub functions, sets up
the Windows execution environment (TEB/PEB), and jumps to the PE entry point.

```
┌─────────────────────────────────────────────────────┐
│                  Linux Process                      │
│                                                      │
│  ┌──────────────────────────────────────────────┐   │
│  │          MiniWin Loader (ELF)                │   │
│  │          at 0x2000000 (text-segment)          │   │
│  │                                              │   │
│  │  ┌─────────┐ ┌──────────┐ ┌──────────────┐  │   │
│  │  │PE Parser│→│Section   │→│Import        │  │   │
│  │  │         │ │Mapper    │  │Resolver      │  │   │
│  │  └─────────┘ └──────────┘ └──────────────┘  │   │
│  │                                              │   │
│  │  ┌──────────┐ ┌──────────┐ ┌────────────┐  │   │
│  │  │Relocation│→│TLS       │→│Win32 API   │  │   │
│  │  │Handler   │ │Handler   │ │Stubs       │  │   │
│  │  └──────────┘ └──────────┘ └────────────┘  │   │
│  │                                              │   │
│  │  ┌──────────┐ ┌──────────┐ ┌────────────┐  │   │
│  │  │TEB/PEB   │→│Exception │→│Signal      │  │   │
│  │  │Setup     │ │Runtime   │ │Handler     │  │   │
│  │  └──────────┘ └──────────┘ └────────────┘  │   │
│  └──────────────────────────────────────────────┘   │
│                                                      │
│  ┌──────────────────────────────────────────────┐   │
│  │       PE Image (mapped at 0x400000)          │   │
│  │                                              │   │
│  │  .text  .rdata  .data  .pdata  .xdata       │   │
│  │  .bss   .idata  .CRT   .tls   .rsrc        │   │
│  └──────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────┘
```

---

## Execution Pipeline

### Phase 1: PE Loading

```
load_pe(path)
  ├── Read DOS header → verify 'MZ' magic
  ├── Read PE signature → verify 'PE\0\0'
  ├── Parse COFF header → verify AMD64 (0x8664)
  ├── Parse Optional Header → PE32+ (0x020B)
  ├── Extract ImageBase, SizeOfImage, EntryPoint
  ├── Parse Data Directories (Import, Exception, TLS, etc.)
  ├── mmap() anonymous region at preferred ImageBase (0x400000)
  ├── Copy headers to mapped region
  ├── Map each section with correct permissions
  │   ├── .text  → PROT_READ|PROT_EXEC
  │   ├── .rdata → PROT_READ
  │   ├── .data  → PROT_READ|PROT_WRITE
  │   └── .bss   → zeroed
  ├── Process base relocations (if needed)
  └── Store .pdata info (RVA, size, entry count) globally
```

### Phase 2: Memory Management

```
Memory Subsystem
  ├── g_heap (static 16MB region via mmap)
  ├── HeapAlloc(size) → bump allocator from g_heap
  ├── HeapFree(ptr) → no-op (bump allocator)
  ├── HeapRealloc(ptr, size) → malloc+copy
  ├── VirtualAlloc(addr, size, type, prot) → mmap
  ├── VirtualFree(addr, size, type) → munmap
  ├── VirtualProtect(addr, size, prot, old) → mprotect
  └── VirtualQuery(addr, info, size) → /proc/self/maps parsing
```

### Phase 3: Import Resolution

```
resolve_imports()
  ├── Walk Import Directory Table (IDT)
  ├── For each DLL:
  │   ├── Parse Import Lookup Table (ILT)
  │   ├── For each import:
  │   │   ├── If ordinal: skip (not supported)
  │   │   ├── If by name: look up in MiniWin's stub table
  │   │   ├── Write stub address into IAT slot
  │   │   └── Log to API trace
  │   └── Report resolved/unresolved count
  └── Verify: 162 resolved, 0 unresolved

Import DLLs:
  KERNEL32.DLL: 68 imports (VirtualAlloc, HeapAlloc, CreateEventA, ...)
  msvcrt.dll:   94 imports (malloc, printf, fprintf, _initterm, ...)
```

### Phase 4: Win32 Compatibility Layer

```
~162 API stubs organized by category:

Process/Thread:
  GetCurrentProcess, GetCurrentProcessId, GetCurrentThread,
  GetCurrentThreadId, GetStartupInfoA, Sleep, TlsAlloc/GetValue/SetValue

Memory:
  VirtualAlloc, VirtualFree, VirtualProtect, VirtualQuery,
  HeapAlloc, HeapFree, HeapRealloc

Synchronization:
  CreateEventA, SetEvent, ResetEvent, CreateSemaphoreA,
  ReleaseSemaphore, InitializeCriticalSection, EnterCriticalSection,
  LeaveCriticalSection, DeleteCriticalSection, TryEnterCriticalSection

Console I/O:
  GetStdHandle, WriteConsoleOutputA, GetConsoleMode,
  GetConsoleScreenBufferInfo, SetConsoleCursorPosition,
  SetConsoleTextAttribute, SetConsoleCursorInfo, ScrollConsoleScreenBufferA

File I/O:
  GetFileTime, SetFileTime, OpenProcess, CloseHandle,
  DuplicateHandle, GetHandleInformation

Exception:
  RaiseException, SetUnhandledExceptionFilter,
  AddVectoredExceptionHandler, RemoveVectoredExceptionHandler,
  RtlLookupFunctionEntry, RtlVirtualUnwind, RtlUnwindEx,
  RtlCaptureContext, GetThreadContext, SetThreadContext

CRT (msvcrt.dll):
  _initterm, __getmainargs, __set_app_type, __iob_func,
  malloc, free, calloc, realloc, printf, fprintf, fprintf,
  fwrite, fputs, fputc, strlen, strcmp, strcpy, memcpy, memset,
  atoi, strtol, strtoul, getenv, signal, exit, abort,
  _beginthreadex, _endthreadex, _onexit, _cexit, and more
```

### Phase 5: TEB/PEB Environment

```
setup_teb_peb()
  ├── Allocate TEB storage (TEB_SIZE + PEB_SIZE + PARAMS_SIZE)
  ├── Zero-fill all structures
  ├── Get actual stack info via pthread_getattr_np
  ├── TEB layout (x64 Windows):
  │   ├── +0x00: ExceptionList (head of SEH chain)
  │   ├── +0x08: StackBase
  │   ├── +0x10: StackLimit
  │   ├── +0x30: ProcessEnvironmentBlock → PEB
  │   ├── +0x60: CommandLine (UNICODE_STRING)
  │   └── +0x70: ImagePathName (UNICODE_STRING)
  ├── PEB layout:
  │   ├── +0x00: InheritedAddressSpace
  │   ├── +0x02: BeingDebugged
  │   ├── +0x0C: ImageBaseAddress → PE image
  │   ├── +0x10: Ldr
  │   ├── +0x18: ProcessParameters → RTL_USER_PROCESS_PARAMETERS
  │   └── +0x30...: various fields
  ├── ProcessParameters:
  │   ├── Standard handles (stdin, stdout, stderr)
  │   ├── CommandLine (ANSI + Wide)
  │   └── ImagePathName
  └── Set FS segment base to TEB address (Linux-specific)
```

### Phase 6: CRT Startup

```
CRT Initialization (triggered by PE entry point):
  ├── __set_app_type(_CONSOLE_APP)  → sets g_app_type
  ├── __lconv_init()                 → locale initialization
  ├── _initterm(table1_start, table1_end)  → C initializers
  ├── _initterm(table2_start, table2_end)  → C++ initializers
  ├── __getmainargs(argc, argv, envp, expand) → command line parsing
  ├── SetUnhandledExceptionFilter(handler) → register UEF
  ├── malloc/calloc calls → heap working
  ├── CreateSemaphore/InitializeCriticalSection → sync working
  ├── TlsAlloc/TlsSetValue → TLS working
  └── Application main() would be called next
```

### Phase 7: Exception Runtime

```
Exception Subsystem:

  Signal Handler (SIGSEGV, SIGABRT)
    └── crash_handler(int sig, siginfo_t* info, void* ctx)
        ├── Log RIP, fault address, signal number
        ├── Log image base for diagnostics
        └── _exit(139)  [currently — no crash recovery]

  RaiseException (naked ms_abi stub)
    ├── mw_RaiseException (naked entry, captures RIP/RSP)
    │   ├── Captures return address from [RSP]
    │   ├── Captures caller's RSP (entry + 8)
    │   ├── Saves all nonvolatile registers
    │   └── Calls mw_RaiseException_impl (SysV ABI)
    └── mw_RaiseException_impl
        ├── Build EXCEPTION_RECORD
        ├── Build CONTEXT (0x4D0 bytes)
        └── Call seh_dispatch_exception

  Exception Dispatcher
    └── seh_dispatch_exception(EXCEPTION_RECORD*, CONTEXT*)
        ├── Check VEH chain (currently empty)
        ├── Frame walking loop:
        │   ├── Frame[N]: RtlLookupFunctionEntry(RIP) → RUNTIME_FUNCTION*
        │   ├── RtlVirtualUnwind(rip, rf, ctx, &est, &handler_rva)
        │   │   ├── Parse UNWIND_INFO (version, flags, prolog, codes)
        │   │   ├── Simulate unwind opcodes (PUSH_NONVOL, ALLOC_*, SET_FPREG, ...)
        │   │   ├── Calculate establisher frame
        │   │   └── Return handler address + LSDA pointer
        │   ├── If EHANDLER flag:
        │   │   ├── Build DISPATCHER_CONTEXT
        │   │   ├── Call handler(EXCEPTION_RECORD*, est, CONTEXT*, DISPATCHER_CONTEXT*)
        │   │   ├── Check disposition (0=Exec, 1=Search, 2=Nested, 3=Collided)
        │   │   └── If ContinueSearch → walk to parent frame
        │   └── If no handler → read parent RIP from [establisher_frame]
        └── If no handler found → fall through to UEF

  RtlUnwindEx (partial)
    └── Called by GCC personality routine when it wants to unwind
        ├── Parse target frame from parameters
        ├── Set CTX.Rax = exception object
        └── longjmp back to dispatcher with modified CONTEXT

  __C_specific_handler (stub)
    └── Currently returns DISP_ExceptionContinueSearch (1)
        └── TODO: Parse GCC LSDA for try/catch scope matching
```

### Phase 8: Application Execution

```
main()
  ├── load_pe(exe_path)
  ├── resolve_imports()
  ├── setup_teb_peb()
  ├── setup_signal_handlers()
  ├── Set up API trace log
  ├── Jump to PE entry point (ms_abi trampoline)
  │   └── Application runs...
  │       ├── CRT init
  │       ├── Application code
  │       ├── Exception handling (if needed)
  │       └── Exit
  └── Cleanup and exit
```

---

## ABI Bridge: ms_abi vs SysV

A critical architectural challenge: Windows PE code uses Microsoft x64 ABI,
while the loader runs under Linux System V ABI.

### Key Differences

| Aspect | Windows ms_abi | Linux SysV | MiniWin Solution |
|--------|---------------|------------|-----------------|
| Integer args | RCX, RDX, R8, R9 | RDI, RSI, RDX, RCX, R8, R9 | Inline asm shim |
| Shadow space | 32 bytes (4×8) on stack | None | Allocated in shim |
| XMM | XMM0-5 passed in regs | XMM0-7 | Not used yet |
| Stack align | 16-byte before CALL | 16-byte before CALL | Same |
| Caller cleanup | Caller pops shadow | N/A | Shim adds/subs 0x28 |
| Return | RAX | RAX | Same |

### ABI Bridging Points

1. **PE Entry Point**: Called via ms_abi inline asm with shadow space
2. **RaiseException Stub**: `__attribute__((naked, ms_abi))` — captures state before
   any compiler-generated prolog executes
3. **Handler Calls**: Inline asm in dispatcher allocates 0x28 shadow, moves
   args to RCX/RDX/R8/R9, calls handler
4. **Internal Functions**: SEH logic (lookup, unwind, dispatch) runs as SysV
   functions to avoid XMM alignment crashes

### Critical Lesson: Naked Stubs

The compiler prolog of `mw_RaiseException` changes size between compilations.
Hardcoding frame offsets is FUNDAMENTALLY UNSAFE. The solution is
`__attribute__((naked, ms_abi))` which captures registers at exact entry point.

Evidence:
- Checkpoint binary (older gcc): frame size = 0x208 bytes
- Current gcc 14.2.0: frame size varies (0x208 to 0x218)
- Naked stub: captures at entry → MATCH every time

---

## Data Structures

### RUNTIME_FUNCTION (.pdata entry) — 12 bytes

```c
typedef struct {
    uint32_t BeginAddress;    // Function start RVA
    uint32_t EndAddress;      // One past function end RVA
    uint32_t UnwindInfo;      // RVA to UNWIND_INFO
} RUNTIME_FUNCTION;
```

### UNWIND_INFO Format

```
Byte 0: [Flags(5 bits)][Version(3 bits)]
Byte 1: SizeOfProlog
Byte 2: CountOfCodes
Byte 3: [FrameOffset(4 bits)][FrameRegister(4 bits)]
Bytes 4+: UNWIND_CODE[] (2 bytes each, CountOfCodes entries)
Then (aligned to 4): Language Handler RVA (if EHANDLER/UHANDLER flag)
Then: Language-Specific Data (LSDA)
```

### UNWIND_CODE Encoding

```
Bits [15:12] = Opcode
Bits [11:8]  = Operation info
Bits [7:0]   = Code offset (within prolog)
```

### CONTEXT Structure (0x4D0 bytes)

Key register offsets (used by unwind engine):

| Register | Offset | Notes |
|----------|--------|-------|
| Rax | 0x78 | |
| Rcx | 0x80 | |
| Rdx | 0x88 | |
| Rbx | 0x90 | Nonvolatile |
| Rsp | 0x98 | |
| Rbp | 0xA0 | Nonvolatile |
| Rsi | 0xA8 | Nonvolatile |
| Rdi | 0xB0 | Nonvolatile |
| R8  | 0xB8 | |
| R9  | 0xC0 | |
| R10 | 0xC8 | |
| R11 | 0xD0 | |
| R12 | 0xD8 | Nonvolatile |
| R13 | 0xE0 | Nonvolatile |
| R14 | 0xE8 | Nonvolatile |
| R15 | 0xF0 | Nonvolatile |
| Rip | 0xF8 | |
| EFlags | 0x100 | |

---

## Module Dependencies (Internal)

```
loader.c
├── include/pe.h          (PE format definitions, CONTEXT offsets)
├── <stdio.h>             (trace logging)
├── <stdlib.h>            (malloc, exit)
├── <string.h>            (memcpy, memset, strcmp)
├── <sys/mman.h>          (mmap, mprotect, munmap)
├── <signal.h>            (SIGSEGV handler)
├── <pthread.h>           (TEB stack info)
├── <setjmp.h>            (RtlUnwindEx longjmp)
├── <dlfcn.h>             (future DLL loading)
└── <stdarg.h>            (variadic API stubs)
```

---

## Known Architecture Issues

### 1. Monolithic Design

Everything is in one file (~2400 lines). This was intentional for the research
phase (easy to modify, single compilation unit, no linker issues). Future
refactoring should split into the directory structure already in place:

- `src/loader/` — PE loading, section mapping, relocations
- `src/pe/` — PE format definitions
- `src/imports/` — Import resolution
- `src/memory/` — Heap, VirtualAlloc, VirtualProtect
- `src/win32/` — Win32 API stubs
- `src/exception/` — SEH dispatcher, RaiseException
- `src/unwind/` — RtlLookupFunctionEntry, RtlVirtualUnwind
- `src/runtime/` — TEB/PEB, CRT startup, signal handlers

### 2. Bump Allocator

HeapAlloc uses a simple bump allocator (never frees). This works for
short-lived CLI applications but will exhaust memory for long-running apps.

### 3. Static Stack

No guard page, no stack growth. PE applications with deep recursion may crash.

### 4. Single TEB

No thread-local storage beyond what the PE's own TLS provides.
Multi-threaded PE applications will not work.
