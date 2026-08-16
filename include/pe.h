#ifndef MINWIN_PE_H
#define MINWIN_PE_H

#include <stdint.h>

#pragma pack(push, 1)

/* DOS Header */
typedef struct {
    uint8_t  e_magic[2];       /* MZ */
    uint16_t e_cblp;
    uint16_t e_cp;
    uint16_t e_crlc;
    uint16_t e_cparhdr;
    uint16_t e_minalloc;
    uint16_t e_maxalloc;
    uint16_t e_ss;
    uint16_t e_sp;
    uint16_t e_csum;
    uint16_t e_ip;
    uint16_t e_cs;
    uint16_t e_lfarlc;
    uint16_t e_ovno;
    uint16_t e_res[4];
    uint16_t e_oemid;
    uint16_t e_oeminfo;
    uint16_t e_res2[10];
    uint32_t e_lfanew;
} DosHeader;

/* PE Signature */
typedef struct {
    uint8_t  signature[4]; /* PE\\0\\0 */
} PeSignature;

/* COFF Header */
typedef struct {
    uint16_t Machine;
    uint16_t NumberOfSections;
    uint32_t TimeDateStamp;
    uint32_t PointerToSymbolTable;
    uint32_t NumberOfSymbols;
    uint16_t SizeOfOptionalHeader;
    uint16_t Characteristics;
} CoffHeader;

/* PE32+ Optional Header */
typedef struct {
    uint16_t Magic;                    /* 0x020b for PE32+ */
    uint8_t  MajorLinkerVersion;
    uint8_t  MinorLinkerVersion;
    uint32_t SizeOfCode;
    uint32_t SizeOfInitializedData;
    uint32_t SizeOfUninitializedData;
    uint32_t AddressOfEntryPoint;
    uint32_t BaseOfCode;
    uint64_t ImageBase;
    uint32_t SectionAlignment;
    uint32_t FileAlignment;
    uint16_t MajorOperatingSystemVersion;
    uint16_t MinorOperatingSystemVersion;
    uint16_t MajorImageVersion;
    uint16_t MinorImageVersion;
    uint16_t MajorSubsystemVersion;
    uint16_t MinorSubsystemVersion;
    uint32_t Win32VersionValue;
    uint32_t SizeOfImage;
    uint32_t SizeOfHeaders;
    uint32_t CheckSum;
    uint16_t Subsystem;
    uint16_t DllCharacteristics;
    uint64_t SizeOfStackReserve;
    uint64_t SizeOfStackCommit;
    uint64_t SizeOfHeapReserve;
    uint64_t SizeOfHeapCommit;
    uint32_t LoaderFlags;
    uint32_t NumberOfRvaAndSizes;
    /* DataDirectory follows */
} PeOptHeader64;

/* Data Directory Entry */
typedef struct {
    uint32_t VirtualAddress;
    uint32_t Size;
} DataDirectory;

/* Section Header */
typedef struct {
    uint8_t  Name[8];
    uint32_t VirtualSize;
    uint32_t VirtualAddress;
    uint32_t SizeOfRawData;
    uint32_t PointerToRawData;
    uint32_t PointerToRelocations;
    uint32_t PointerToLinenumbers;
    uint16_t NumberOfRelocations;
    uint16_t NumberOfLinenumbers;
    uint32_t Characteristics;
} SectionHeader;

/* Import Directory Entry */
typedef struct {
    uint32_t OriginalFirstThunk;  /* ILT RVA (0 = use IAT) */
    uint32_t TimeDateStamp;
    uint32_t ForwarderChain;
    uint32_t Name;
    uint32_t FirstThunk;         /* IAT RVA */
} ImportDirectoryEntry;

/* TLS Directory (PE32+) */
typedef struct {
    uint64_t StartAddressOfRawData;
    uint64_t EndAddressOfRawData;
    uint64_t AddressOfIndex;
    uint64_t AddressOfCallBacks;
    uint32_t SizeOfZeroFill;
    uint32_t Characteristics;
} TlsDirectory64;

/* RUNTIME_FUNCTION (.pdata entry) — 12 bytes */
typedef struct {
    uint32_t BeginAddress;
    uint32_t EndAddress;
    uint32_t UnwindInfo;
} RUNTIME_FUNCTION;

/* UNWIND_INFO flags — stored in bits [5:3] of byte 0 (3-bit field) */
#define UNW_FLAG_EHANDLER  0x01
#define UNW_FLAG_UHANDLER  0x02
#define UNW_FLAG_CHAININFO 0x04

/* UNWIND_CODE opcodes */
#define UWOP_PUSH_NONVOL       0
#define UWOP_ALLOC_LARGE       1
#define UWOP_ALLOC_SMALL       2
#define UWOP_SET_FPREG         3
#define UWOP_SAVE_NONVOL       4
#define UWOP_SAVE_NONVOL_FAR   5
#define UWOP_SAVE_XMM128       6
#define UWOP_SAVE_XMM128_FAR   7
#define UWOP_PUSH_MACHFRAME    8

/* x64 CONTEXT structure register offsets */
/* CONTEXT is 0x4D0 bytes. Key offsets: */
#define CTX_Rax       0x78
#define CTX_Rcx       0x80
#define CTX_Rdx       0x88
#define CTX_Rbx       0x90
#define CTX_Rsp       0x98
#define CTX_Rbp       0xA0
#define CTX_Rsi       0xA8
#define CTX_Rdi       0xB0
#define CTX_R8        0xB8
#define CTX_R9        0xC0
#define CTX_R10       0xC8
#define CTX_R11       0xD0
#define CTX_R12       0xD8
#define CTX_R13       0xE0
#define CTX_R14       0xE8
#define CTX_R15       0xF0
#define CTX_Rip       0xF8
#define CTX_EFlags    0x100
#define CTX_SegCs     0x38
#define CTX_SegSs     0x44

/* Register index to CONTEXT offset mapping (nonvolatile GP regs) */
/* Index: 0=RAX 1=RCX 2=RDX 3=RBX 4=RSP 5=RBP 6=RSI 7=RDI
         8=R8  9=R9  10=R10 11=R11 12=R12 13=R13 14=R14 15=R15 */
static inline uint32_t reg_to_ctx_offset(int reg) {
    static const uint32_t offsets[] = {
        CTX_Rax, CTX_Rcx, CTX_Rdx, CTX_Rbx, CTX_Rsp, CTX_Rbp,
        CTX_Rsi, CTX_Rdi, CTX_R8,  CTX_R9,  CTX_R10, CTX_R11,
        CTX_R12, CTX_R13, CTX_R14, CTX_R15
    };
    if (reg >= 0 && reg < 16) return offsets[reg];
    return 0;
}

#pragma pack(pop)

/* Data Directory indices */
#define DD_EXPORT          0
#define DD_IMPORT          1
#define DD_RESOURCE        2
#define DD_EXCEPTION       3
#define DD_SECURITY        4
#define DD_BASERELOC       5
#define DD_DEBUG           6
#define DD_ARCHITECTURE    7
#define DD_GLOBALPTR       8
#define DD_TLS             9
#define DD_LOAD_CONFIG    10
#define DD_BOUND_IMPORT   11
#define DD_IAT            12
#define DD_DELAY_IMPORT   13
#define DD_CLR            14
#define DD_RESERVED       15

/* Section characteristic flags */
#define SCN_CNT_CODE          0x00000020
#define SCN_CNT_INITIALIZED   0x00000040
#define SCN_CNT_UNINITIALIZED 0x00000080
#define SCN_MEM_DISCARDABLE   0x02000000
#define SCN_MEM_NOT_CACHED    0x04000000
#define SCN_MEM_NOT_PAGED     0x08000000
#define SCN_MEM_SHARED        0x10000000
#define SCN_MEM_EXECUTE       0x20000000
#define SCN_MEM_READ          0x40000000
#define SCN_MEM_WRITE         0x80000000

/* Constants */
#define PE32PLUS_MAGIC 0x020b
#define KERNEL32_STDCALL 0

/* VEH / UnhandledExceptionFilter return values */
#define EXCEPTION_CONTINUE_EXECUTION (-1L)
#define EXCEPTION_CONTINUE_SEARCH    0
#define EXCEPTION_NONCONTINUABLE     0x01

/* EXCEPTION_DISPOSITION — returned by frame-based language handlers (C-specific) */
/* NOTE: These values DIFFER from VEH/UEF return values above! */
#define DISP_ExceptionContinueExecution 0
#define DISP_ExceptionContinueSearch    1
#define DISP_ExceptionNestedException   2
#define DISP_ExceptionCollidedUnwind    3

/* x64 CONTEXT structure size (CONTEXT_FULL = 0x100000) */
#define CONTEXT_SIZE 0x4D0

/* DISPATCHER_CONTEXT — passed to language-specific exception handlers */
#pragma pack(push, 8)
typedef struct {
    uint64_t ControlPc;          /* +0x00: PC where exception occurred */
    uint64_t ImageBase;          /* +0x08: base of image containing function */
    void*    FunctionEntry;      /* +0x10: RUNTIME_FUNCTION* for this frame */
    uint64_t EstablisherFrame;   /* +0x18: establisher frame for this function */
    uint64_t TargetIp;           /* +0x20: target IP for unwind (0 if not specific) */
    void*    ContextRecord;      /* +0x28: pointer to CONTEXT */
    void*    LanguageHandler;    /* +0x30: language-specific handler address */
    void*    HandlerData;        /* +0x38: LSDA / handler-specific data */
    uint64_t HistoryTable;       /* +0x40: history table (for chained unwind) */
    uint32_t ScopeIndex;         /* +0x48: scope index (for nested exceptions) */
    uint32_t Fill;               /* +0x4C: padding */
} DISPATCHER_CONTEXT;
#pragma pack(pop)

/* EXCEPTION_RECORD — passed to exception handlers and dispatcher */
#define EXCEPTION_MAXIMUM_PARAMETERS 15

#pragma pack(push, 8)
typedef struct {
    uint32_t ExceptionCode;                        /* +0x00 */
    uint32_t ExceptionFlags;                       /* +0x04 */
    uint64_t ExceptionRecord;                      /* +0x08: nested exception (NULL for first) */
    uint64_t ExceptionAddress;                     /* +0x10: where exception occurred */
    uint32_t NumberParameters;                     /* +0x18 */
    uint32_t __unusedAlignment;                    /* +0x1C */
    uint64_t ExceptionInformation[EXCEPTION_MAXIMUM_PARAMETERS]; /* +0x20 */
} EXCEPTION_RECORD;
#pragma pack(pop)

/* GCC C++ exception code */
#define GCC_EXCEPTION_CODE 0x20474343

static inline void* rva_to_ptr(uint8_t* base, uint32_t rva) {
    return (void*)(base + rva);
}

#endif /* MINWIN_PE_H */
