# Windows x64 ABI — Complete Reference

> Based on Microsoft documentation and MiniWin experimental evidence.

---

## 1. Calling Convention (Microsoft x64)

### Register Usage

| Category | Registers | Notes |
|----------|-----------|-------|
| Integer args | RCX, RDX, R8, R9 | First 4 arguments |
| Float args | XMM0-XMM3 | First 4 float/double args |
| Return value | RAX | Integer/pointer |
| Return value | XMM0 | Float/double |
| Volatile | RAX, RCX, RDX, R8-R11, XMM0-XMM5 | Caller-saved |
| Nonvolatile | RBX, RBP, RDI, RSI, R12-R15, XMM6-XMM15 | Callee-saved |
| Stack pointer | RSP | 16-byte aligned before CALL |
| Frame pointer | RBP | Optional |

### Shadow Space

The caller MUST allocate 32 bytes (4 × 8) of "shadow space" on the stack
before calling any function. This space is reserved for the callee to optionally
store the register arguments. Even if the callee doesn't use it, the space
must be allocated.

```asm
; Before calling a Windows function:
sub  rsp, 0x28      ; 32 bytes shadow + 8 for 16-byte alignment
mov  rcx, arg1
mov  rdx, arg2
call [SomeFunction]
add  rsp, 0x28      ; clean up
```

### Stack Alignment

The stack MUST be 16-byte aligned at the point of a CALL instruction.
Since CALL pushes 8 bytes (return address), the RSP at function entry
is 8 mod 16. The function prolog typically does `sub rsp, N` where
N is chosen to restore 16-byte alignment (e.g., `sub rsp, 0x28`
gives RSP = entry - 0x28, which is 16-aligned if entry was 8-mod-16).

### Prolog Rules

A valid x64 prolog MUST:
1. Push any nonvolatile registers it will use (PUSH_NONVOL)
2. Allocate stack space for locals (ALLOC_SMALL or ALLOC_LARGE)
3. Optionally set frame pointer (SET_FPREG)
4. Save nonvolatile registers to stack (SAVE_NONVOL)

The prolog size is recorded in UNWIND_INFO.SizeOfProlog.

### Epilog Rules

An epilog must be one of two forms:

**Form 1 (canonical)**:
```asm
add  rsp, <frame_size>  ; or: lea rsp, [rbp+<offset>]
pop  <nonvolatile_reg>   ; 0 or more pops
ret
```

**Form 2 (compact)**:
```asm
lea  rsp, [rbp+<offset>]
pop  rbp
ret
```

---

## 2. Exception Structures

### EXCEPTION_RECORD (0x98 bytes)

```c
#pragma pack(push, 8)
typedef struct {
    uint32_t ExceptionCode;                        // +0x00
    uint32_t ExceptionFlags;                       // +0x04
    uint64_t ExceptionRecord;                      // +0x08 (nested, NULL for first)
    uint64_t ExceptionAddress;                     // +0x10 (where exception occurred)
    uint32_t NumberParameters;                     // +0x18
    uint32_t __unusedAlignment;                    // +0x1C
    uint64_t ExceptionInformation[15];             // +0x20
} EXCEPTION_RECORD;  // Total: 0x98
#pragma pack(pop)
```

**Common Exception Codes**:

| Code | Name | Description |
|------|------|-------------|
| 0xE0434352 | C++ Exception | MSVC C++ exception ("RCC\xE0") |
| 0x20474343 | GCC Exception | GCC C++ exception ("CCG ") |
| 0xC0000005 | Access Violation | Read/write to invalid address |
| 0xC0000094 | Integer Divide | Division by zero |
| 0xC0000096 | Stack Overflow | Stack guard page hit |
| 0x80000003 | Breakpoint | INT3 instruction |
| 0x80000004 | Single Step | Trap flag set |

### CONTEXT (0x4D0 bytes)

```c
// CONTEXT_FULL = 0x100000
typedef struct {
    // ... many header fields ...
    uint64_t Rax;      // +0x78
    uint64_t Rcx;      // +0x80
    uint64_t Rdx;      // +0x88
    uint64_t Rbx;      // +0x90
    uint64_t Rsp;      // +0x98
    uint64_t Rbp;      // +0xA0
    uint64_t Rsi;      // +0xA8
    uint64_t Rdi;      // +0xB0
    uint64_t R8;       // +0xB8
    uint64_t R9;       // +0xC0
    uint64_t R10;      // +0xC8
    uint64_t R11;      // +0xD0
    uint64_t R12;      // +0xD8
    uint64_t R13;      // +0xE0
    uint64_t R14;      // +0xE8
    uint64_t R15;      // +0xF0
    uint64_t Rip;      // +0xF8
    uint32_t EFlags;   // +0x100
    uint16_t SegCs;    // +0x108
    uint16_t SegDs;    // +0x10A
    // ... more segments, floats, vectors ...
} CONTEXT;  // Total: 0x4D0
```

### DISPATCHER_CONTEXT (0x50 bytes)

```c
#pragma pack(push, 8)
typedef struct {
    uint64_t ControlPc;          // +0x00: PC where exception occurred
    uint64_t ImageBase;          // +0x08: base of image containing function
    void*    FunctionEntry;      // +0x10: RUNTIME_FUNCTION* for this frame
    uint64_t EstablisherFrame;   // +0x18: establisher frame for this function
    uint64_t TargetIp;           // +0x20: target IP for unwind (0 if not specific)
    void*    ContextRecord;      // +0x28: pointer to CONTEXT
    void*    LanguageHandler;    // +0x30: language-specific handler address
    void*    HandlerData;        // +0x38: LSDA / handler-specific data
    uint64_t HistoryTable;       // +0x40: history table (for chained unwind)
    uint32_t ScopeIndex;         // +0x48: scope index (for nested exceptions)
    uint32_t Fill;               // +0x4C: padding
} DISPATCHER_CONTEXT;
#pragma pack(pop)
```

### RUNTIME_FUNCTION (12 bytes, .pdata entry)

```c
typedef struct {
    uint32_t BeginAddress;    // Function start RVA
    uint32_t EndAddress;      // One past function end RVA
    uint32_t UnwindInfo;      // RVA to UNWIND_INFO
} RUNTIME_FUNCTION;
```

The table is sorted by BeginAddress. Binary search is used for lookup.

### UNWIND_INFO Format

``nByte 0: [Flags(5 bits high)][Version(3 bits low)]
         Version = ui[0] & 0x07     (always 1 for PE)
         Flags   = (ui[0] >> 3) & 0x03
Byte 1: SizeOfProlog
Byte 2: CountOfCodes
Byte 3: [FrameOffset(4 bits low)][FrameRegister(4 bits high)]
Bytes 4+: UNWIND_CODE[CountOfCodes] (2 bytes each)
  Then padding to 4-byte alignment
  Then: Handler RVA (uint32_t) if UNW_FLAG_EHANDLER or UNW_FLAG_UHANDLER
  Then: Language-Specific Data (LSDA) — variable length
```

#### UNWIND_CODE (2 bytes)

``nBits [15:12] = Opcode (0-15)
Bits [11:8]  = Operation info
Bits [7:0]   = Code offset (within prolog)
```

#### UNWIND_INFO Flags

``n#define UNW_FLAG_EHANDLER  0x01  // Has exception handler
#define UNW_FLAG_UHANDLER  0x02  // Has termination handler (finally)
#define UNW_FLAG_CHAININFO 0x04  // Chained UNWIND_INFO
```

#### Unwind Opcodes

| Op | Name | Slots | Effect |
|----|------|-------|--------|
| 0 | UWOP_PUSH_NONVOL | 1 | RSP += 8 (push register on stack) |
| 1 | UWOP_ALLOC_LARGE (info=0) | 2 | RSP += next uint16 |
| 1 | UWOP_ALLOC_LARGE (info=1) | 3 | RSP += next uint32 |
| 2 | UWOP_ALLOC_SMALL | 1 | RSP += (info+1)*8 |
| 3 | UWOP_SET_FPREG | 1 | FP = RSP + frame_offset*16 |
| 4 | UWOP_SAVE_NONVOL | 2 | Reg saved at [FP - offset*8] |
| 5 | UWOP_SAVE_NONVOL_FAR | 3 | Reg saved at [FP - offset*8] (32-bit offset) |
| 6 | UWOP_SAVE_XMM128 | 2 | XMM reg saved at [FP - offset*16] |
| 7 | UWOP_SAVE_XMM128_FAR | 3 | XMM reg saved at [FP - offset*16] (32-bit offset) |
| 8 | UWOP_PUSH_MACHFRAME | 1 | RSP += 8, push SS:RSP:RFLAGS:CS:RIP |
| 9+ | GCC-specific | varies | Used by MinGW/GCC, logged and skipped |

---

## 3. Exception Flow (Complete)

### Normal SEH Flow (Windows)

``nRaiseException(ExceptionCode, ExceptionFlags, NumberParameters, ExceptionInformation)
    ↓
Kernel captures CONTEXT at call site
    ↓
RtlDispatchException(EXCEPTION_RECORD*, CONTEXT*)
    ↓
1. Check VEH (Vectored Exception Handlers) chain
   - If handler returns EXCEPTION_CONTINUE_EXECUTION → resume at CONTEXT.Rip
   - If EXCEPTION_CONTINUE_SEARCH → continue to step 2
    ↓
2. Frame-based handler search
   Loop:
     a. RtlLookupFunctionEntry(CONTEXT.Rip, &ImageBase, &HistoryTable)
        → Returns RUNTIME_FUNCTION* or NULL
     b. RtlVirtualUnwind(ImageBase, CONTROL_PC, FunctionEntry, CONTEXT,
                         &HandlerData, &EstablisherFrame)
        → Returns language handler address (or NULL)
        → Updates CONTEXT (unwound register state)
     c. If handler found:
        - Build DISPATCHER_CONTEXT
        - Call handler(EXCEPTION_RECORD*, EstablisherFrame, CONTEXT*, DISPATCHER_CONTEXT*)
        - Handler returns EXCEPTION_DISPOSITION:
          0 = ExceptionContinueExecution → resume
          1 = ExceptionContinueSearch → continue walking
          2 = ExceptionNestedException
          3 = ExceptionCollidedUnwind
     d. If no handler:
        - Read parent RIP from [EstablisherFrame]
        - Update CONTEXT.Rip, CONTEXT.Rsp
        - Continue loop
    ↓
3. If no handler found:
   - Call UnhandledExceptionFilter(EXCEPTION_POINTERS*)
   - If returns EXCEPTION_EXECUTE_HANDLER → call debugger or show error
   - Otherwise → call NtTerminateProcess
```

### GCC Exception Flow (MinGW/GCC)

GCC uses a different mechanism than MSVC SEH:

``nC++ throw expression
    ↓
__cxa_throw allocates exception object
    ↓
Unwind_RaiseException (from libgcc/libunwind)
    ↓
Walk frames using .eh_frame or .pdata/.xdata
    ↓
For each frame with personality routine:
    ↓
__gcc_personality_v0 (at RVA 0xE0220 in UPX)
    ↓
Parse LSDA (Language-Specific Data Area):
    - LPStart (landing pad start)
    - TType (type table for catch matching)
    - CS (call site table with actions and landing pads)
    ↓
Match exception type against catch clauses
    ↓
If match found:
    - Call _Unwind_SetIP to set target to landing pad
    - Call _Unwind_Resume or RtlUnwindEx to unwind stack
    ↓
If no match:
    - Return _URC_CONTINUE_UNWIND
```

### MiniWin's Current Path

``nPE Code → RaiseException(0x20474343) [via IAT]
    ↓
Naked stub captures: RIP=0x49d5b1, RSP, all registers
    ↓
Builds EXCEPTION_RECORD (code=0x20474343, params[0]=exception_obj)
Builds CONTEXT (0x4D0 bytes)
    ↓
seh_dispatch_exception:
    VEH chain: 0 handlers
    ↓
    Frame[0]: RVA=0x9d5b1, no EHANDLER → parent=0x4e0203
    Frame[1]: RVA=0xe0203, no EHANDLER → parent=0x4e02d9
    Frame[2]: RVA=0xe02d9, no EHANDLER → parent=0x401593
    Frame[3]: RVA=0x1593, EHANDLER at 0xe0220, LSDA=0x601088
      → Handler called → returned ContinueSearch (no try/catch match)
    Frame[4]: RVA=0x32f2, EHANDLER at 0xe0220, LSDA=0x601200
      → Handler called → triggered RtlUnwindEx to RIP=0x40331c
      → RtlUnwindEx: longjmp back to dispatcher
      → [BLOCKED: Context restoration incomplete]
```

---

## 4. GCC LSDA Format

### Structure

``nByte 0: LPStart encoding
Byte 1: TType encoding
Byte 2: CS encoding

LPStart:
  0xFF = omit (use function start as base)
  Other = DWARF pointer encoding

TType (type table):
  0xFF = omit (no type info)
  0x9B = DW_EH_PE_udata8 (8-byte unsigned)
  Other = DWARF pointer encoding

CS (call site table):
  0x01 = DW_EH_PE_uleb128 (ULEB128 encoded)
  Other = DWARF pointer encoding
```

### Call Site Table Entries

Each entry has:
- `start` (relative to LPStart or function start)
- `length`
- `landing_pad` (0 = no landing pad)
- `action` (index into action table, 0 = no action)

### Evidence from UPX

**Frame[3] LSDA (RVA 0x601088)** — Simple, no try/catch:
```
ff ff 01 08 20 03 35 00 45 06 00 00
LPStart=omit, TType=omit, CS=uleb128
Call sites: 6 entries, all with action=0 (no catch)
→ Handler returns ContinueSearch (no matching catch clause)
```

**Frame[4] LSDA (RVA 0x601200)** — Has catch clause:
```
ff 9b 15 01 04 2d 05 5c 05 02 00 01 7d 00 7d 00
LPStart=omit, TType=udata8, CS=uleb128
Call sites: 5 entries with actions
→ Handler triggers RtlUnwindEx to catch landing pad
```

---

## 5. ABI Differences: Windows vs Linux

### Function Call

```asm
; Windows (ms_abi): args in RCX, RDX, R8, R9
mov  rcx, arg1
mov  rdx, arg2
sub  rsp, 0x28    ; shadow space + alignment
call Func
add  rsp, 0x28

; Linux (SysV): args in RDI, RSI, RDX, RCX, R8, R9
mov  rdi, arg1
mov  rsi, arg2
call Func
```

### Struct Return

- Windows: Hidden first param in RCX
- Linux: Hidden first param in RDI

### Red Zone

- Windows: No red zone
- Linux: 128-byte red zone below RSP (don't use in ms_abi code)

### XMM Register Alignment

Both ABIs require 16-byte stack alignment for XMM operations.
However, when calling ms_abi functions from SysV code, the shadow space
allocation may misalign the stack if not careful. Always allocate
multiples of 16 bytes for the total stack adjustment.

---

## 6. MiniWin ABI Bridge Implementation

### PE Entry Point Call

```c
// In main(), calling PE entry point with ms_abi:
void (*entry)(void) = (void (*)(void))(g_image_base + g_entry_point);
__asm__ volatile (
    "subq $0x28, %%rsp\n\t"  // shadow space
    "call *%0\n\t"
    "addq $0x28, %%rsp\n\t"
    :
    : "r"(entry)
    : "rcx", "rdx", "r8", "r9", "r10", "r11", "memory"
);
```

### Naked RaiseException Stub

```c
__attribute__((naked, ms_abi))
void mw_RaiseException(uint32_t code, uint32_t flags,
                       uint32_t nargs, uint64_t* args) {
    __asm__ volatile (
        // Save all nonvolatile registers
        "pushq %%rbx\n\t"
        "pushq %%rbp\n\t"
        "pushq %%rdi\n\t"
        "pushq %%rsi\n\t"
        "pushq %%r12\n\t"
        "pushq %%r13\n\t"
        "pushq %%r14\n\t"
        "pushq %%r15\n\t"
        // Save RSP at entry (before our pushes) and RIP
        "movq %%rsp, g_cap_rsp_entry(%%rip)\n\t"
        "movq (%%rsp), %%rax\n\t"  // original RSP = entry_rsp + 64 (8 pushes)
        "leaq 64(%%rax), %%rax\n\t"  // add back our 8 push slots
        "movq %%rax, g_cap_rsp(%%rip)\n\t"
        "movq (%%rax), %%rax\n\t"  // return address
        "movq %%rax, g_cap_rip(%%rip)\n\t"
        // Save params
        "movl %%ecx, g_cap_code(%%rip)\n\t"
        // Call implementation
        "call mw_RaiseException_impl\n\t"
        // Restore and return
        "popq %%r15\n\t"
        "popq %%r14\n\t"
        "popq %%r13\n\t"
        "popq %%r12\n\t"
        "popq %%rsi\n\t"
        "popq %%rdi\n\t"
        "popq %%rbp\n\t"
        "popq %%rbx\n\t"
        "ret\n\t"
        ::: "rax", "rcx", "rdx", "r8", "r9", "r10", "r11", "memory"
    );
}
```

### Handler Call (ms_abi from SysV dispatcher)

```c
int32_t disposition = 1;
__asm__ volatile (
    "movq %2, %%rcx\n\t"      // EXCEPTION_RECORD*
    "movq %3, %%rdx\n\t"      // EstablisherFrame
    "movq %4, %%r8\n\t"       // CONTEXT*
    "movq %5, %%r9\n\t"       // DISPATCHER_CONTEXT*
    "subq $0x28, %%rsp\n\t"   // shadow space
    "call *%1\n\t"            // call handler
    "addq $0x28, %%rsp\n\t"   // clean up
    : "=a"(disposition)
    : "r"(handler), "r"(&er), "r"(est), "r"(ctx), "r"(&dc)
    : "rcx", "rdx", "r8", "r9", "r10", "r11", "memory"
);
```