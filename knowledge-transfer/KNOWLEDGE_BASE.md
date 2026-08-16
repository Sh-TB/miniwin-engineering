# MiniWin Knowledge Base

> Accumulated engineering knowledge from the MiniWin runtime research.
> Every entry is backed by evidence. No speculation.

---

## 1. PE Loading

### PE64 Memory Layout

UPX 4.2.4 (MinGW/GCC compiled, AMD64):

```
ImageBase:     0x00400000
SizeOfImage:   0x00213000
EntryPoint:    0x000014F0 (RVA)
Sections:       10

Section     VA         VSize      RawSize    Characteristics
--------   ----------  ----------  ----------  -------------------
.text      0x001000   0x000DFBBO  0x000DFC00  RX  (0x60500060)
.data      0x00E1000  0x00000A50  0x00000C00  RW  (0xC0600040)
.rdata     0x00E2000  0x00115800  0x00115800  R   (0x40600040)
.pdata     0x001F8000  0x00008E08  0x00009000  R   (0x40300040)
.xdata     0x00201000  0x0000937C  0x00009400  R   (0x40300040)
.bss       0x0020B000  0x00002EC0  0x00000000  RW  (0xC0600080)
.idata     0x0020E000  0x000016A8  0x00001800  R   (0xC0300040)
.CRT       0x00210000  0x00000070  0x00000200  R   (0xC0400040)
.tls       0x00211000  0x00000010  0x00000200  R   (0xC0400040)
.rsrc      0x00212000  0x000005D0  0x00000600  R   (0xC0300040)
```

### Section Mapping Rules

1. Headers (first SizeOfHeaders bytes) are always mapped first
2. Each section is mapped at VirtualAddress with VirtualSize extent
3. Raw data is copied from PointerToRawData
4. Remaining bytes (VirtualSize - RawSize) are zero-filled
5. Permissions are derived from section Characteristics flags

### Text-Segment Placement

The MiniWin loader is linked at 0x2000000 (`-Wl,-Ttext-segment=0x2000000`)
to avoid collision with PE images mapped at 0x400000. Without this, the
loader's own code would overlap with the PE's .text section.

---

## 2. Import Resolution

### Import Directory Structure

Each imported DLL has an Import Directory Entry:
- OriginalFirstThunk (ILT RVA) — name/ordinal table
- FirstThunk (IAT RVA) — address table, overwritten at runtime
- Name — DLL name string

### Two-Pass Resolution

1. Walk ILT to discover import names
2. Look up each name in MiniWin's stub table
3. Write stub address into IAT slot

### KERNEL32.DLL Imports (68 total)

Critical imports with implementation notes:

| Import | Implementation | Notes |
|--------|---------------|-------|
| VirtualAlloc | mmap(MAP_ANONYMOUS) | Pages at preferred address |
| VirtualFree | munmap | |
| VirtualProtect | mprotect | |
| VirtualQuery | /proc/self/maps | Slow but correct |
| HeapAlloc | bump allocator | 16MB static heap |
| HeapFree | no-op | Bump allocator |
| CreateEventA | returns fake handle | Manual reset, non-signaled |
| CreateSemaphoreA | returns fake handle | |
| InitializeCriticalSection | memset to 0 | Works for single-threaded |
| EnterCriticalSection | no-op | Single-threaded |
| TlsAlloc | global counter | Returns 0, 1, 2... |
| TlsGetValue/TlsSetValue | global array | |
| RaiseException | full SEH path | See Exception section |
| RtlLookupFunctionEntry | binary search .pdata | 3030 entries |
| RtlVirtualUnwind | full opcode simulation | 9 opcodes |
| SetUnhandledExceptionFilter | stores handler | Called by CRT |
| WriteConsoleOutputA | write(2) | |
| GetStdHandle | returns 0/1/2 | stdin/stdout/stderr |

### msvcrt.dll Imports (94 total)

Key CRT imports:

| Import | Implementation | Notes |
|--------|---------------|-------|
| _initterm | calls function pointers | C/C++ initializers |
| __getmainargs | parses g_cmdline | Sets argc/argv |
| __set_app_type | sets g_app_type | 1=console |
| __iob_func | returns fake _iob | 3 FILE structs at 0x2018000 |
| malloc/free/calloc/realloc | HeapAlloc wrapper | Working correctly |
| printf/fprintf/vfprintf | vsnprintf + write | |
| exit/_cexit | cleanup + _exit | |
| abort | raise(SIGABRT) | |
| signal | signal() wrapper | |
| __C_specific_handler | no-op (returns 1) | CRITICAL: blocks C++ EH |
| _beginthreadex/_endthreadex | stub | |
| _onexit | registers atexit func | Working |

### Import Resolution Evidence

```
Total imports: 162 resolved, 0 unresolved
KERNEL32.DLL: 68 imports
msvcrt.dll: 94 imports
All IAT slots filled successfully.
No ordinal imports encountered.
```

---

## 3. TEB/PEB Implementation

### TEB (Thread Environment Block)

The TEB is allocated in static storage (not on the stack) to ensure
stable address across function calls. Linux's FS segment cannot be set
for a single thread, so the TEB address is stored in a global variable.

Critical TEB offsets (x64 Windows):
- +0x00: ExceptionList (SEH chain head)
- +0x08: StackBase (top of stack)
- +0x10: StackLimit (bottom of stack)
- +0x30: Self (pointer to TEB)
- +0x38: EnvironmentPointer
- +0x60: CommandLine (UNICODE_STRING: Length, MaxLength, Buffer)
- +0x70: ImagePathName (UNICODE_STRING)

### PEB (Process Environment Block)

Critical PEB offsets:
- +0x00: InheritedAddressSpace (BOOLEAN)
- +0x02: BeingDebugged (BOOLEAN)
- +0x0C: ImageBaseAddress (PVOID → PE image base)
- +0x10: Ldr (PEB_LDR_DATA*)
- +0x18: ProcessParameters (RTL_USER_PROCESS_PARAMETERS*)

### ProcessParameters

Contains:
- Standard handles (stdin=0, stdout=1, stderr=2)
- CommandLine (wide string)
- ImagePathName (wide string)

---

## 4. Exception Handling

### Exception Flow (Complete Path)

```
PE Code calls RaiseException via IAT
    ↓
Naked stub (ms_abi) captures: RIP, RSP, all registers
    ↓
mw_RaiseException_impl (SysV ABI):
    Build EXCEPTION_RECORD (code, flags, address, params)
    Build CONTEXT (0x4D0 bytes, register state)
    Call seh_dispatch_exception
    ↓
seh_dispatch_exception:
    1. Check VEH chain (currently empty)
    2. Frame walking loop:
       For each frame:
         a. RtlLookupFunctionEntry(RIP) → RUNTIME_FUNCTION*
         b. RtlVirtualUnwind(rip, rf, ctx, &est, &handler_rva)
            - Parse UNWIND_INFO (version, flags, prolog, codes)
            - Simulate unwind opcodes
            - Calculate establisher frame
            - Return handler + LSDA
         c. If EHANDLER found:
            - Build DISPATCHER_CONTEXT
            - Call handler via ms_abi inline asm
            - Check disposition:
              0 = ExceptionContinueExecution → stop, resume
              1 = ExceptionContinueSearch → walk to parent
              2 = ExceptionNestedException → error
              3 = ExceptionCollidedUnwind → error
         d. If no handler → read parent RIP from [establisher_frame]
    3. If no handler found → call UnhandledExceptionFilter
```

### .pdata Analysis (UPX)

```
Exception Data Directory: VA=0x001F8000, Size=0x00008E08
Total RUNTIME_FUNCTION entries: 3030

Unwind Opcode Distribution:
  SET_FPREG:       1708
  SAVE_XMM128:     1013
  SAVE_XMM128_FAR:  717
  SAVE_NONVOL_FAR:   516
  SAVE_NONVOL:       481
  PUSH_NONVOL:       461
  PUSH_MACHFRAME:     93
  ALLOC_SMALL:          7
  GCC-specific (9+):  ~2000 (opcodes 12, 13, 14, 15)
```

### Malformed UNWIND_INFO (Systematic in GCC/MinGW)

Static analysis of 601 RF entries revealed:
- **236** with slot overflows (count_codes < total slots needed)
- **87** with missing ALLOC opcode (prolog has `sub rsp` but UNWIND_INFO lacks it)
- **20** with EHANDLER flag

These are NOT edge cases — the GCC/MinGW toolchain generates systematically
malformed unwind metadata. The dispatcher must handle these gracefully.

### Exception Class: 0x20474343

```
Exception object first 8 bytes: 00 2B 2B 43 43 55 4E 47
Little-endian u64:              0x474E5543432B2B00
ASCII (reversed):               "GNUCC++\0"

This is the GCC C++ exception class.
MSVC uses 0xE0434352 ("RCC\xE0").
```

### EXP-NEXT-2 Proof: Handler Discovery Works

Synthetic test with 3 nested functions:
```
func_A calls func_B
func_B (EHANDLER at RVA 0x1060) calls func_C
func_C calls RaiseException

Frame 0 (func_C): no EHANDLER, parent RIP recovered → 0x1046
Frame 1 (func_B): EHANDLER found at RVA 0x1060 → PASS
```

### UPX Real Execution: Full Dispatch Trace

```
Frame[0]: RVA=0x9d5b1 — RaiseException wrapper, no EHANDLER
  → parent RIP=0x4e0203
Frame[1]: RVA=0xe0203 — GCC internal, no EHANDLER
  → parent RIP=0x4e02d9
Frame[2]: RVA=0xe02d9 — GCC internal, no EHANDLER
  → parent RIP=0x401593
Frame[3]: RVA=0x1593 — CRT/UPX frame, EHANDLER at RVA 0xe0220
  → Handler called, returned ContinueSearch (disposition=1)
  → LSDA at 0x601088, simple (no try/catch matches)
Frame[4]: RVA=0x32f2 — UPX frame, EHANDLER at RVA 0xe0220
  → Handler called, triggered RtlUnwindEx
  → LSDA at 0x601200, has catch clause
  → Unwind to target frame RIP=0x40331c
```

---

## 5. ABI Bridge Knowledge

### ms_abi Calling Convention

- Args: RCX, RDX, R8, R9 (shadow space required on stack)
- Shadow space: 32 bytes (4 × 8) allocated by caller
- Caller cleans up shadow space
- Volatile: RAX, RCX, RDX, R8-R11, XMM0-5
- Nonvolatile: RBX, RBP, RDI, RSI, R12-R15, XMM6-15

### Critical ABI Bugs Fixed

1. **REX prefix classification**: 0x48 (REX.W for `sub rsp`) was misclassified
   as a REX prefix for push. Fixed by only treating 0x40-0x4F as REX when
   followed by 0x50-0x57 (push).

2. **Compiler prolog instability**: Hardcoded frame offsets break when
   compiler version or optimization changes. Fixed with naked stub.

3. **XMM alignment crash**: Direct SysV→ms_abi calls crash with movaps
   on unaligned RSP. Fixed by using internal SysV-only functions for
   SEH logic and inline asm for ms_abi calls.

---

## 6. API Behavior Knowledge

### __iob_func

Returns pointer to 3 FILE structs (stdin, stdout, stderr) at static
address 0x2018000. Each FILE struct is initialized with fd 0/1/2.

### _initterm

Walks an array of function pointers (terminated by NULL) and calls each.
UPX has two _initterm calls:
- Table 1: C initializers (0x610018 to 0x610030)
- Table 2: C++ initializers (0x610000 to 0x610010)

### GetLastError/SetLastError

Simple thread-local storage. Currently uses a global variable.
Real Windows uses TEB->LastErrorValue.

---

## 7. Toolchain & Build Knowledge

### Build Requirements

- GCC 14.2.0+ (tested)
- `-no-pie` required (both compiler and linker)
- `-Wl,-Ttext-segment=0x2000000` prevents address collision
- `-ldl` for future dlopen support
- `-O2` for performance (code size doesn't matter)
- `-g` for debugging

### Known Build Issues

1. **movaps alignment**: Without `-no-pie`, PIE code may have
   misaligned stack when calling ms_abi functions
2. **Inline asm constraints**: Must use `"memory"` clobber for
   any asm that touches memory

---

## 8. Rejected Hypotheses

### Hypothesis 1: HeapAlloc returns NULL (BUG-001 era)
- **Evidence**: Trace shows `malloc(24) = 0x318eb20` (non-NULL)
- **Why rejected**: Heap allocation works correctly
- **Status**: FIXED in earlier iteration

### Hypothesis 2: InitializeCriticalSection crash
- **Evidence**: Trace shows `InitializeCriticalSection(0x318ebb8)` without crash
- **Why rejected**: CS initialization works (single-threaded)
- **Status**: FIXED

### Hypothesis 3: Missing DLL imports
- **Evidence**: 162/162 resolved, 0 unresolved
- **Why rejected**: Import count matches exactly
- **Status**: NOT the issue

### Hypothesis 4: TLS callback failure
- **Evidence**: Callbacks pointer at 0x610040 has 0 entries
- **Why rejected**: No TLS callbacks to execute
- **Status**: NOT the issue

### Hypothesis 5: Source truncation caused regression
- **Evidence**: api_trace.json shows full path from EP to RaiseException
- **Why rejected**: The "truncation" was in a checkpoint zip, not current code
- **Status**: MISDIAGNOSIS

---

## 9. UPX-Specific Knowledge

### Binary Details

```
Binary: upx_decompressed.exe
Version: UPX 4.2.4
Compiler: MinGW/GCC
Hash: 254ac80deb8fc54bda028d574022e8231a3b684c177c2d35da75802ff78d4f4e
Size: 2,140,672 bytes
Type: PE32+ AMD64 (0x8664)
Subsys: WINDOWS_CUI (console)
```

### UPX Internal Structure

UPX is a self-decompressing executable. The decompressed version used
for testing has all sections expanded but retains the GCC-compiled
internal structure:

- GCC personality routine at RVA 0xE0220 (appears in ~20 .pdata entries)
- GCC exception class: 0x474E5543432B2B00 ("GNUCC++\0")
- LSDA format: GCC DWARF-style (not MSVC)
- Exception handling: try/catch via RaiseException wrapper

### UPX --version Code Path

```
main() → option parsing
  → throws C++ exception (unexpected condition in option parser)
  → RaiseException(0x20474343, ...) — GCC exception class
  → Exception dispatcher walks frames
  → Frame[3]: handler returns ContinueSearch (LSDA has no match)
  → Frame[4]: handler triggers RtlUnwindEx to catch clause
  → If RtlUnwindEx works: resumes at catch block, prints version
  → If RtlUnwindEx fails: SIGSEGV
```
