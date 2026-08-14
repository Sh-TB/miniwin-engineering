# MiniWin — Current Implementation Status

**Date**: 2026-08-14
**Binary**: upx_decompressed.exe (UPX 4.2.4 win64, MinGW/GCC)
**Target Command**: `./minwin_loader samples/upx_decompressed.exe --version`
**Expected Output**: "upx 4.2.4"
**Actual Output**: (none — exits with SIGSEGV, code 139)

---

## Verified Working

| Component | Status | Evidence |
|-----------|--------|----------|
| PE64 Loading | DONE | 10 sections mapped, EP=0x4014f0 |
| Import Resolution | DONE | 162/162 resolved (KERNEL32: 68, msvcrt: 94) |
| TLS Execution | DONE | Callbacks checked (0 for UPX) |
| CRT Initialization | DONE | _initterm, __getmainargs, __set_app_type all called |
| TEB/PEB Setup | DONE | Real stack bounds, process params, wide strings |
| Heap APIs | DONE | malloc(24)=0x245ed10, calloc, realloc all working |
| Memory APIs | DONE | VirtualAlloc, VirtualProtect, VirtualQuery |
| Exception Metadata | DONE | .pdata: 3030 entries parsed |
| x64 Unwind Engine | DONE | RtlLookupFunctionEntry (binary search) + RtlVirtualUnwind (9 opcodes) |
| Exception Dispatcher | DONE | seh_dispatch_exception walks frames, finds handlers |
| EH Handler Invocation | DONE | 2 handlers called on real UPX binary |
| LSDA Parsing | DONE | GCC DWARF format parsed (LPStart, TType, CS) |
| RtlUnwindEx | PARTIAL | Longjmp works, context restoration incomplete |
| Regression Tests | DONE | 12/12 tests passing |
| EXP-NEXT-2 Proof | DONE | Handler discovery PASS on synthetic PE |
| API Tracing | DONE | JSON format, full execution trace captured |

---

## Current Challenge: UPX AMD64 Execution

### Target
```
miniwin upx.exe --version
```

### Current State

```
RaiseException reached (code=0x20474343, GCC C++ exception)
    ↓
SEH dispatcher working (seh_dispatch_exception)
    ↓
Frame walking: 5 frames walked correctly
    ↓
EH handlers discovered (Frame[3] and Frame[4])
    ↓
Handlers invoked via ms_abi inline asm
    ↓
Frame[3]: ContinueSearch (LSDA has no matching catch)
    ↓
Frame[4]: Triggers RtlUnwindEx to target_ip=0x40331c
    ↓
RtlUnwindEx: longjmp back to dispatcher
    ↓
[BLOCKED] Context restoration / execution resume at landing pad
    ↓
SIGSEGV at 0x49c9c6 (PE's UnhandledExceptionFilter path)
```

### Root Cause
RtlUnwindEx uses longjmp to return to the dispatcher after the GCC
personality routine requests an unwind to a catch landing pad. The
dispatcher receives the longjmp but does not properly restore the
CONTEXT to resume execution at the landing pad (target_ip=0x40331c).

The specific missing piece is: after longjmp, the dispatcher must
walk frames from the exception site to the target frame, restore all
nonvolatile registers from the unwind, set RSP = target_frame, and
actually jump to target_ip.

---

## Verification Evidence

### API Trace (key excerpt)
```
[API] Jumping to EP=0x4014f0
[API] _initterm(start=0x610018, end=0x610030)
[API] __getmainargs(argc=0x60b028, argv=0x60b020, envp=0x60b018)
[API] SetUnhandledExceptionFilter(handler=0x49c9c0)
[API] malloc(24) = 0x245ed10
[API] InitializeCriticalSection(0x245eda8)
[API] TlsAlloc() = 0
[API] [DISPATCH] === RtlDispatchException ===
[API] [DISPATCH] ExceptionCode=0x20474343 RIP=0x49d5b1 RSP=0x7fffe21cc8d0
[API] [DISPATCH] Frame[3]: EHANDLER at RVA 0xe0220
[API] [HANDLER_CALL] returned disposition=1 (ContinueSearch)
[API] [DISPATCH] Frame[4]: EHANDLER at RVA 0xe0220
[API] [HANDLER_CALL] LSDA: ff 9b 15 01...
[API] RtlUnwindEx(target_frame=0x..., target_ip=0x40331c)
[API] RtlUnwindEx: longjmping to dispatcher
```

### Exception Object
```
Bytes: 00 2b 2b 43 43 55 4e 47
Class: 0x474E5543432B2B00 ("GNUCC++\0")
This is a GCC C++ exception being thrown during option parsing.
```

### .pdata Statistics (UPX)
```
Total entries: 3030
With EHANDLER: ~20
Handler RVA 0xe0220: GCC __C_specific_handler personality
Malformed entries: 236 slot overflows + 87 missing ALLOC
```

---

## Recommended Next Steps

### Immediate (to unblock UPX --version)
1. **Implement context restoration in RtlUnwindEx path**
   - After longjmp, walk from current frame to target frame
   - For each intermediate frame, run RtlVirtualUnwind to restore registers
   - Set CONTEXT.Rsp = target_frame, CONTEXT.Rip = target_ip
   - Jump to CONTEXT.Rip via inline asm

### Short Term
2. **Implement GCC LSDA interpretation in __C_specific_handler**
   - Parse call site table to find matching catch clause
   - Parse action table to match exception type
   - Parse type table to compare exception classes
   - Return correct disposition based on LSDA analysis

3. **Add RtlUnwindEx full frame restoration**
   - Walk from exception site to target, unwinding each frame
   - This is needed when the personality routine requests unwind

### Medium Term
4. **Build Stage 2 target**: Simple C console app (printf only)
5. **Implement real file I/O**: CreateFileA, ReadFile, WriteFile
6. **Split loader.c into modules**

---

## Frozen Components (DO NOT MODIFY)

These are verified working and must not regress:

- PE loader (load_pe)
- Import resolver
- CRT stubs
- TLS stubs
- Heap allocation
- TEB/PEB setup
- Signal handler
- RtlLookupFunctionEntry
- RtlVirtualUnwind
- .pdata parser
- Naked RaiseException stub
