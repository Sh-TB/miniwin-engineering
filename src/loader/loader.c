/*
 * MiniWin PE Loader v0.1
 * Minimal Windows x64 PE runtime for Linux
 * Executes Windows PE applications without Wine
 *
 * Target: upx_decompressed.exe --version
 * DLLs: KERNEL32.DLL (68 imports) + msvcrt.dll (94 imports)
 *
 * Build: gcc -o minwin_loader loader.c -O2 -no-pie -g
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <time.h>
#include <errno.h>
#include <signal.h>
#include <setjmp.h>
#include <locale.h>
#include <ctype.h>
#include <stdarg.h>
#include <pthread.h>
#include <wchar.h>
#include <dlfcn.h>

#include "../include/pe.h"

/* ============================================================
 * Global State
 * ============================================================ */

static const char* g_exe_path = NULL;
static char g_cmdline_ansi[4096];
static wchar_t g_cmdline_wide[4096];
static char* g_argv[256];
static int g_argc = 0;
static int g_app_type = 1;          /* _CONSOLE_APP */
static int g_fmode_val = 0;           /* _O_TEXT */
static int g_commode_val = 0;         /* default commode */
static int g_last_error = 0;

/* Image state */
static uint8_t* g_image_base = NULL;
static uint64_t g_image_size = 0;
static uint64_t g_entry_point = 0;

/* Trampoline area — reserved for future use */

/* API trace */
static FILE* g_trace_log = NULL;
#define MW_TRACE(fmt, ...) do { \
    if (g_trace_log) { \
        fprintf(g_trace_log, "[API] " fmt "\n", ##__VA_ARGS__); \
        fflush(g_trace_log); \
    } else { \
        fprintf(stderr, "[TRACE] " fmt "\n", ##__VA_ARGS__); \
    } \
} while(0)

/* Handle table */
#define MAX_HANDLES 64
static void* g_handle_table[MAX_HANDLES];
static int    g_handle_count = 0;

static void* alloc_handle(void* ptr) {
    if (g_handle_count < MAX_HANDLES) {
        int h = g_handle_count + 4; /* Start handles at 4 */
        g_handle_table[g_handle_count++] = ptr;
        return (void*)(uintptr_t)h;
    }
    return (void*)(uintptr_t)-1; /* INVALID_HANDLE_VALUE */
}

/* ============================================================
 * TEB / PEB Structures
 * ============================================================ */

/* Fake TEB (Thread Environment Block) - minimal fields */
static uint8_t g_teb_storage[4096] __attribute__((aligned(4096)));
static uint8_t g_peb_storage[4096] __attribute__((aligned(4096)));
static uint8_t g_params_storage[4096] __attribute__((aligned(4096)));

/* RTL_USER_PROCESS_PARAMETERS layout (x64) */
struct FakeProcessParams {
    uint64_t MaximumLength;           /* 0x00 */
    uint64_t Length;                  /* 0x08 */
    uint32_t Flags;                   /* 0x10 */
    uint32_t DebugFlags;              /* 0x14 */
    uint64_t ConsoleHandle;           /* 0x18 */
    uint32_t ConsoleFlags;            /* 0x20 */
    uint32_t StdInputHandle;          /* 0x24 */
    uint32_t StdOutputHandle;         /* 0x28 */
    uint32_t StdErrorHandle;          /* 0x2C */
    uint8_t  pad1[16];                /* 0x30 */
    /* UNICODE_STRING CommandLine at 0x60 */
    uint16_t CmdLen;                  /* 0x60 */
    uint16_t CmdMaxLen;               /* 0x62 */
    uint64_t CmdBuffer;               /* 0x68 */
    /* UNICODE_STRING ImagePathName at 0x70 */
    uint16_t ImgPathLen;              /* 0x70 */
    uint16_t ImgPathMaxLen;           /* 0x72 */
    uint64_t ImgPathBuffer;           /* 0x78 */
};

static void setup_teb_peb(void) {
    memset(g_teb_storage, 0, sizeof(g_teb_storage));
    memset(g_peb_storage, 0, sizeof(g_peb_storage));
    memset(g_params_storage, 0, sizeof(g_params_storage));

    uint8_t* teb = g_teb_storage;
    uint8_t* peb = g_peb_storage;
    struct FakeProcessParams* params = (struct FakeProcessParams*)g_params_storage;

    /* Get actual stack info */
    void* stack_base = NULL;
    size_t stack_size = 0;
    pthread_attr_t attr;
    pthread_getattr_np(pthread_self(), &attr);
    pthread_attr_getstack(&attr, &stack_base, &stack_size);
    pthread_attr_destroy(&attr);
    uint8_t* stack_top = (uint8_t*)stack_base + stack_size;

    /* TEB layout (x64 Windows)
     * 0x00 ExceptionList
     * 0x08 StackBase
     * 0x10 StackLimit
     * 0x18 SubSystemTib
     * 0x20 FiberData
     * 0x28 Version (4) + ArbitraryUserPointer (4)
     * 0x30 Self (pointer to TEB)
     * 0x38 EnvironmentPointer
     * 0x40 ClientId.UniqueProcess
     * 0x48 ClientId.UniqueThread
     * 0x50 RpcHandle
     * 0x58 TlsSlots[0] (first TLS slot)
     * 0x60 PEB pointer
     * 0x68 LastErrorValue
     */
    *(uint64_t*)(teb + 0x00) = 0;                    /* ExceptionList = NULL */
    *(uint64_t*)(teb + 0x08) = (uint64_t)stack_top;  /* StackBase */
    *(uint64_t*)(teb + 0x10) = (uint64_t)stack_base; /* StackLimit */
    *(uint64_t*)(teb + 0x30) = (uint64_t)teb;        /* Self */
    *(uint64_t*)(teb + 0x40) = 1234;                 /* ClientId.UniqueProcess */
    *(uint64_t*)(teb + 0x48) = 5678;                 /* ClientId.UniqueThread */
    *(uint64_t*)(teb + 0x60) = (uint64_t)peb;        /* PEB pointer */
    *(uint32_t*)(teb + 0x68) = 0;                    /* LastErrorValue */

    /* PEB layout (minimal)
     * 0x00 InheritedAddressSpace (1)
     * 0x01 ReadImageFileExecOptions (1)
     * 0x02 BeingDebugged (1)
     * 0x03 BitField (1)
     * 0x08 Mutant
     * 0x10 ImageBaseAddress
     * 0x18 Ldr (PEB_LDR_DATA*)
     * 0x20 ProcessParameters
     * 0x28 SubSystemMajorVersion (2) + MinorVersion (2) + padding (4)
     */
    *(uint64_t*)(peb + 0x00) = 0;                    /* InheritedAddressSpace=0, ReadImageFileExecOptions=0, BeingDebugged=0 */
    *(uint64_t*)(peb + 0x10) = (uint64_t)g_image_base; /* ImageBaseAddress */
    *(uint64_t*)(peb + 0x18) = 0;                    /* Ldr = NULL */
    *(uint64_t*)(peb + 0x20) = (uint64_t)params;     /* ProcessParameters */

    /* Process Parameters */
    size_t cmd_len = strlen(g_cmdline_ansi) * 2; /* wide char bytes */
    params->MaximumLength = sizeof(struct FakeProcessParams);
    params->Length = sizeof(struct FakeProcessParams);
    params->Flags = 0;
    params->DebugFlags = 0;
    params->ConsoleHandle = (uint64_t)(uintptr_t)(-1); /* INVALID_HANDLE_VALUE */
    params->ConsoleFlags = 0;
    params->StdInputHandle = 0;  /* STD_INPUT_HANDLE = -10 → fd 0 */
    params->StdOutputHandle = 1; /* STD_OUTPUT_HANDLE = -11 → fd 1 */
    params->StdErrorHandle = 2;  /* STD_ERROR_HANDLE = -12 → fd 2 */
    params->CmdLen = (uint16_t)cmd_len;
    params->CmdMaxLen = (uint16_t)(cmd_len + 4);
    params->CmdBuffer = (uint64_t)(uintptr_t)g_cmdline_wide;

    /* Set GS base to TEB */
    syscall(SYS_arch_prctl, 0x1001 /* ARCH_SET_GS */, (uint64_t)(uintptr_t)teb);
}

/* ============================================================
 * Trampoline Generation (Windows → System V ABI)
 * ============================================================ */

/*
 * Windows x64 ABI: RCX, RDX, R8, R9, then stack (with 32-byte shadow space)
 * System V ABI:   RDI, RSI, RDX, RCX, R8, R9, then stack (no shadow space)
 *
 * Trampoline converts and adjusts stack.
 * For non-variadic functions, we use __attribute__((ms_abi)) instead.
 * Trampolines are only used for variadic functions.
 */

static void write_trampoline(void* addr, void* target) {
    uint8_t* p = (uint8_t*)addr;
    int64_t rel = (int64_t)(uintptr_t)target - (int64_t)(uintptr_t)(p + 24);

    /* sub rsp, 0x20     */  p[0] = 0x48; p[1] = 0x83; p[2] = 0xEC; p[3] = 0x20;
    /* mov rdi, rcx      */  p[4] = 0x48; p[5] = 0x89; p[6] = 0xCF;
    /* mov rsi, rdx      */  p[7] = 0x48; p[8] = 0x89; p[9] = 0xD6;
    /* mov rdx, r8       */  p[10] = 0x4D; p[11] = 0x89; p[12] = 0xC2;
    /* mov rcx, r9       */  p[13] = 0x49; p[14] = 0x89; p[15] = 0xC9;
    /* call rel32         */  p[16] = 0xE8;
    memcpy(p + 17, &rel, 4);
    /* add rsp, 0x20     */  p[21] = 0x48; p[22] = 0x83; p[23] = 0xC4; p[24] = 0x20;
    /* ret                */  p[25] = 0xC3;
}

/* Special trampoline for printf-like variadic functions.
 * Since va_list ABI differs between Windows and System V,
 * we intercept the format string and use vsnprintf internally.
 */

/* Forward declarations for System V ABI wrappers */
static int mw_printf_sv(const char* fmt, ...);
static int mw_fprintf_sv(void* file, const char* fmt, ...);
static int mw_vfprintf_sv(void* file, const char* fmt, void* ap);

/*
 * For printf/fprintf, we can't easily trampoline variadic args.
 * Instead, write asm trampolines that call ms_abi wrappers.
 * These wrappers use the format string + fixed-position args.
 *
 * Strategy: printf is called with at most a few args in registers.
 * For upx --version, printf("upx %s\n", version) has 2 args.
 * We implement printf in ms_abi directly, using vsnprintf to buffer.
 */

/* ============================================================
 * KERNEL32.DLL Stubs (ms_abi calling convention)
 * ============================================================ */

static void* g_unhandled_exception_filter = NULL;
static void* g_veh_handlers[16];
static int g_veh_count = 0;

/* .pdata (Exception Data Directory) — set by load_pe */
static uint32_t g_pdata_rva = 0;
static uint32_t g_pdata_size = 0;
static uint32_t g_num_rt_functions = 0;

__attribute__((ms_abi)) void* mw_AddVectoredExceptionHandler(uint32_t first, void* handler) {
    MW_TRACE("AddVectoredExceptionHandler(first=%u, handler=%p)", first, handler);
    if (g_veh_count < 16) {
        if (first) {
            memmove(g_veh_handlers + 1, g_veh_handlers, g_veh_count * sizeof(void*));
            g_veh_handlers[0] = handler;
        } else {
            g_veh_handlers[g_veh_count] = handler;
        }
        g_veh_count++;
        return handler;
    }
    return NULL;
}

__attribute__((ms_abi)) uint8_t mw_RemoveVectoredExceptionHandler(void* h) {
    MW_TRACE("RemoveVectoredExceptionHandler(%p)", h);
    for (int i = 0; i < g_veh_count; i++) {
        if (g_veh_handlers[i] == h) {
            memmove(g_veh_handlers + i, g_veh_handlers + i + 1,
                    (g_veh_count - i - 1) * sizeof(void*));
            g_veh_count--;
            return 1;
        }
    }
    return 0;
}

__attribute__((ms_abi)) int mw_CloseHandle(void* h) {
    MW_TRACE("CloseHandle(handle=%p)", h);
    return 1; /* TRUE */
}

__attribute__((ms_abi)) void* mw_CreateEventA(void* attr, int manual, int init, const char* name) {
    MW_TRACE("CreateEventA(manual=%d, init=%d, name=%s)", manual, init, name ? name : "NULL");
    return alloc_handle((void*)(uintptr_t)1);
}

__attribute__((ms_abi)) void* mw_CreateSemaphoreA(void* attr, int32_t init, int32_t max, const char* name) {
    MW_TRACE("CreateSemaphoreA(init=%d, max=%d, name=%s)", init, max, name ? name : "NULL");
    return alloc_handle((void*)(uintptr_t)1);
}

__attribute__((ms_abi)) void mw_DebugBreak(void) {
    MW_TRACE("DebugBreak()");
}

/* Critical Section state */
struct MwCritSec {
    pthread_mutex_t mutex;
    int initialized;
};

__attribute__((ms_abi)) void mw_InitializeCriticalSection(void* cs) {
    if (!cs) return;
    struct MwCritSec* mcs = (struct MwCritSec*)cs;
    if (!mcs->initialized) {
        pthread_mutexattr_t attr;
        pthread_mutexattr_init(&attr);
        pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
        pthread_mutex_init(&mcs->mutex, &attr);
        pthread_mutexattr_destroy(&attr);
        mcs->initialized = 1;
        /* Zero out the rest of the CRITICAL_SECTION structure (Windows CRITICAL_SECTION is 40 bytes) */
        size_t cs_total = 40; /* sizeof(CRITICAL_SECTION) on Windows x64 */
        if (cs_total > sizeof(struct MwCritSec))
            memset((uint8_t*)cs + sizeof(struct MwCritSec), 0, cs_total - sizeof(struct MwCritSec));
    }
    MW_TRACE("InitializeCriticalSection(%p)", cs);
}

__attribute__((ms_abi)) void mw_DeleteCriticalSection(void* cs) {
    if (!cs) return;
    struct MwCritSec* mcs = (struct MwCritSec*)cs;
    if (mcs->initialized) {
        pthread_mutex_destroy(&mcs->mutex);
        mcs->initialized = 0;
    }
    MW_TRACE("DeleteCriticalSection(%p)", cs);
}

__attribute__((ms_abi)) void mw_EnterCriticalSection(void* cs) {
    if (!cs) return;
    struct MwCritSec* mcs = (struct MwCritSec*)cs;
    if (!mcs->initialized) mw_InitializeCriticalSection(cs);
    pthread_mutex_lock(&mcs->mutex);
    MW_TRACE("EnterCriticalSection(%p)", cs);
}

__attribute__((ms_abi)) void mw_LeaveCriticalSection(void* cs) {
    if (!cs) return;
    struct MwCritSec* mcs = (struct MwCritSec*)cs;
    if (mcs->initialized) pthread_mutex_unlock(&mcs->mutex);
    MW_TRACE("LeaveCriticalSection(%p)", cs);
}

__attribute__((ms_abi)) int mw_TryEnterCriticalSection(void* cs) {
    if (!cs) return 0;
    struct MwCritSec* mcs = (struct MwCritSec*)cs;
    if (!mcs->initialized) mw_InitializeCriticalSection(cs);
    int ret = (pthread_mutex_trylock(&mcs->mutex) == 0) ? 1 : 0;
    MW_TRACE("TryEnterCriticalSection(%p) = %d", cs, ret);
    return ret;
}

__attribute__((ms_abi)) int mw_DuplicateHandle(void* src, void* src_h, void* tgt, void** tgt_h,
    uint32_t access, int inh, uint32_t opts) {
    MW_TRACE("DuplicateHandle(src_h=%p, tgt_h=%p)", src_h, tgt_h);
    if (tgt_h) *tgt_h = src_h; /* Just copy the handle */
    return 1; /* TRUE */
}

/* Console */
__attribute__((ms_abi)) int mw_GetConsoleMode(void* h, uint32_t* mode) {
    MW_TRACE("GetConsoleMode(handle=%p)", h);
    if (mode) *mode = 0x0003; /* ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT */
    return 1;
}

__attribute__((ms_abi)) int mw_GetConsoleScreenBufferInfo(void* h, void* info) {
    MW_TRACE("GetConsoleScreenBufferInfo(handle=%p)", h);
    if (info) {
        memset(info, 0, 48); /* CONSOLE_SCREEN_BUFFER_INFO is ~48 bytes */
        *(uint16_t*)((uint8_t*)info + 0) = 80;   /* dwSize.X */
        *(uint16_t*)((uint8_t*)info + 2) = 25;   /* dwSize.Y */
        *(uint16_t*)((uint8_t*)info + 4) = 80;   /* dwCursorPosition.X */
        *(uint16_t*)((uint8_t*)info + 6) = 0;    /* dwCursorPosition.Y */
        *(uint16_t*)((uint8_t*)info + 8) = 0x0007; /* wAttributes */
    }
    return 1;
}

__attribute__((ms_abi)) int mw_GetConsoleCursorInfo(void* h, void* info) {
    MW_TRACE("GetConsoleCursorInfo(handle=%p)", h);
    if (info) {
        *(uint32_t*)info = 1;  /* dwSize = 25% */
        *((uint8_t*)info + 4) = 1; /* bVisible = TRUE */
    }
    return 1;
}

__attribute__((ms_abi)) int mw_SetConsoleCursorInfo(void* h, void* info) {
    MW_TRACE("SetConsoleCursorInfo(handle=%p)", h);
    return 1;
}

__attribute__((ms_abi)) int mw_SetConsoleCursorPosition(void* h, uint32_t x, uint32_t y) {
    MW_TRACE("SetConsoleCursorPosition(handle=%p, x=%u, y=%u)", h, x, y);
    return 1;
}

__attribute__((ms_abi)) int mw_SetConsoleTextAttribute(void* h, uint16_t attr) {
    MW_TRACE("SetConsoleTextAttribute(handle=%p, attr=0x%x)", h, attr);
    return 1;
}

__attribute__((ms_abi)) int mw_WriteConsoleOutputA(void* h, void* buf, uint32_t size,
    uint32_t* coord, void* region) {
    MW_TRACE("WriteConsoleOutputA(handle=%p, size=%u)", h, size);
    /* Could write the buffer to stdout if needed */
    if (region) memset(region, 0, 16);
    return 1;
}

/* Process/Thread */
__attribute__((ms_abi)) void* mw_GetCurrentProcess(void) {
    return (void*)(uintptr_t)(-1); /* PSEUDO_HANDLE_CURRENT_PROCESS */
}

__attribute__((ms_abi)) uint32_t mw_GetCurrentProcessId(void) {
    return 1234;
}

__attribute__((ms_abi)) void* mw_GetCurrentThread(void) {
    return (void*)(uintptr_t)(-2); /* PSEUDO_HANDLE_CURRENT_THREAD */
}

__attribute__((ms_abi)) uint32_t mw_GetCurrentThreadId(void) {
    return 5678;
}

__attribute__((ms_abi)) int mw_GetFileTime(void* h, void* ct, void* at, void* wt) {
    MW_TRACE("GetFileTime()");
    return 0;
}

__attribute__((ms_abi)) int mw_GetHandleInformation(void* h, uint32_t* flags) {
    MW_TRACE("GetHandleInformation()");
    return 0;
}

__attribute__((ms_abi)) uint32_t mw_GetLastError(void) {
    uint32_t err = g_last_error;
    MW_TRACE("GetLastError() = %u", err);
    return err;
}

__attribute__((ms_abi)) void* mw_GetModuleHandleA(const char* name) {
    MW_TRACE("GetModuleHandleA(name=%s)", name ? name : "NULL");
    if (name == NULL || name[0] == '\0') return g_image_base;
    return NULL;
}

__attribute__((ms_abi)) void* mw_GetProcAddress(void* module, const char* name) {
    MW_TRACE("GetProcAddress(module=%p, name=%s)", module, name ? name : "NULL");
    return NULL;
}

__attribute__((ms_abi)) int mw_GetProcessAffinityMask(void* h, uint64_t* mask, uint64_t* sys) {
    MW_TRACE("GetProcessAffinityMask()");
    if (mask) *mask = 1;
    return 1;
}

__attribute__((ms_abi)) void mw_GetStartupInfoA(void* info) {
    MW_TRACE("GetStartupInfoA(info=%p)", info);
    if (info) {
        memset(info, 0, 104); /* STARTUPINFOA is 104 bytes on x64 */
        *(uint32_t*)((uint8_t*)info + 0) = 104; /* cb = sizeof(STARTUPINFOA) */
        *(uint32_t*)((uint8_t*)info + 8) = 0;   /* dwFlags = 0 */
        *(uint16_t*)((uint8_t*)info + 60) = 80; /* dwXSize */
        *(uint16_t*)((uint8_t*)info + 64) = 25; /* dwYSize */
        *(uint16_t*)((uint8_t*)info + 68) = 80; /* dwXCountChars */
        *(uint16_t*)((uint8_t*)info + 72) = 300; /* dwYCountChars */
        /* Std handles at offsets 80, 88, 96 */
        *(uint64_t*)((uint8_t*)info + 80) = 0; /* hStdInput */
        *(uint64_t*)((uint8_t*)info + 88) = 1; /* hStdOutput */
        *(uint64_t*)((uint8_t*)info + 96) = 2; /* hStdError */
    }
}

__attribute__((ms_abi)) void* mw_GetStdHandle(int32_t nStdHandle) {
    /* STD_INPUT_HANDLE=-10, STD_OUTPUT_HANDLE=-11, STD_ERROR_HANDLE=-12 */
    void* h = (void*)(uintptr_t)(-nStdHandle);
    MW_TRACE("GetStdHandle(%d) = %p", nStdHandle, h);
    return h;
}

__attribute__((ms_abi)) void mw_GetSystemTimeAsFileTime(void* ft) {
    MW_TRACE("GetSystemTimeAsFileTime()");
    if (ft) {
        struct timeval tv;
        gettimeofday(&tv, NULL);
        /* FILETIME: 100-nanosecond intervals since 1601-01-01 */
        /* Unix epoch (1970) is 11644473600 seconds after 1601 */
        uint64_t t = (uint64_t)tv.tv_sec * 10000000ULL + (uint64_t)tv.tv_usec * 10ULL;
        t += 116444736000000000ULL;
        *(uint64_t*)ft = t;
    }
}

__attribute__((ms_abi)) int mw_GetThreadContext(void* h, void* ctx) {
    MW_TRACE("GetThreadContext()");
    return 0;
}

__attribute__((ms_abi)) int mw_GetThreadPriority(void* h) {
    MW_TRACE("GetThreadPriority()");
    return 0;
}

__attribute__((ms_abi)) uint32_t mw_GetTickCount(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    uint32_t ms = (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
    MW_TRACE("GetTickCount() = %u", ms);
    return ms;
}

__attribute__((ms_abi)) int mw_IsDBCSLeadByteEx(int cp, uint8_t b) {
    return 0;
}

__attribute__((ms_abi)) int mw_IsDebuggerPresent(void) {
    MW_TRACE("IsDebuggerPresent() = FALSE");
    return 0;
}

__attribute__((ms_abi)) int mw_MultiByteToWideChar(uint32_t cp, uint32_t flags,
    const char* src, int srclen, wchar_t* dst, int dstlen) {
    MW_TRACE("MultiByteToWideChar(cp=%u, srclen=%d, dstlen=%d)", cp, srclen, dstlen);
    if (!src) return 0;
    int slen = srclen;
    if (slen < 0) slen = (int)strlen(src);
    if (dstlen == 0) return slen;
    int wlen = mbstowcs(dst, src, dstlen);
    return (wlen < 0) ? 0 : wlen;
}

__attribute__((ms_abi)) void* mw_OpenProcess(uint32_t access, int inh, uint32_t pid) {
    MW_TRACE("OpenProcess()");
    return NULL;
}

__attribute__((ms_abi)) void mw_OutputDebugStringA(const char* str) {
    MW_TRACE("OutputDebugStringA(%s)", str ? str : "NULL");
    if (str) fprintf(stderr, "[DEBUG] %s", str);
}

__attribute__((ms_abi)) int mw_QueryPerformanceCounter(int64_t* lp) {
    MW_TRACE("QueryPerformanceCounter()");
    if (lp) {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        *lp = (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
    }
    return 1;
}

__attribute__((ms_abi)) int mw_QueryPerformanceFrequency(int64_t* lp) {
    MW_TRACE("QueryPerformanceFrequency()");
    if (lp) *lp = 1000000000LL; /* nanoseconds */
    return 1;
}

/* ============================================================
 * EXP-NEXT DIAGNOSTIC: Exception stack walk validation
 * 
 * Hypothesis: Using prolog byte scanning to detect 'sub rsp, N',
 * we can correct RSP for frames with malformed UNWIND_INFO and
 * walk the stack to find a frame with EHANDLER.
 *
 * Method: Naked stub captures RIP/RSP into globals, then calls
 * an internal SysV function that walks frames with full logging.
 * ============================================================ */

/* Global capture — set by naked stub before calling the walker.
 * Cannot be static because naked asm references them by symbol name. */
uint64_t g_cap_rip = 0;
uint64_t g_cap_rsp_entry = 0;
uint64_t g_cap_rsp = 0;       /* caller's pre-call RSP */
uint32_t g_cap_code = 0;
uint32_t g_cap_flags = 0;
uint32_t g_cap_nargs = 0;
uint64_t g_cap_args = 0;
EXCEPTION_RECORD g_cap_er;  /* saved EXCEPTION_RECORD for RtlUnwindEx */

/* RtlUnwindEx support — setjmp/longjmp for non-returning unwind.
 * Cannot be static because naked asm references them by symbol name. */
static jmp_buf g_unwind_jmpbuf;
uint8_t* g_unwind_ctx = NULL;     /* CONTEXT buffer being used for dispatch */
int g_is_unwinding = 0;           /* set when RtlUnwindEx longjmps back */
uint64_t g_unwind_target_ip = 0;  /* target IP for unwind */
uint64_t g_unwind_target_frame = 0; /* target frame (RSP) for unwind */
/* CONTEXT register values saved by setjmp handler for naked stub access.
 * We copy from the stack-local CONTEXT to these globals to avoid the stack
 * being overwritten by function calls (MW_TRACE, fflush) before the naked
 * stub reads them. */
uint64_t g_unwind_regs[16]; /* RAX=0, RCX=1, RDX=2, RBX=3, RSP=4, RBP=5, RSI=6, RDI=7, R8=8, R9=9, R10=10, R11=11, R12=12, R13=13, R14=14, R15=15 */
#define UR_RAX 0
#define UR_RCX 1
#define UR_RDX 2
#define UR_RBX 3
#define UR_RSP 4
#define UR_RBP 5
#define UR_RSI 6
#define UR_RDI 7
#define UR_R8  8
#define UR_R9  9
#define UR_R10 10
#define UR_R11 11
#define UR_R12 12
#define UR_R13 13
#define UR_R14 14
#define UR_R15 15

/* Internal SysV-only RF lookup (avoids ms_abi ABI issues) */
static RUNTIME_FUNCTION* seh_internal_lookup(uint64_t rva) {
    if (!g_pdata_rva || g_num_rt_functions == 0) return NULL;
    RUNTIME_FUNCTION* base = (RUNTIME_FUNCTION*)(g_image_base + g_pdata_rva);
    int lo = 0, hi = (int)g_num_rt_functions - 1;
    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        if (rva < base[mid].BeginAddress) hi = mid - 1;
        else if (rva >= base[mid].EndAddress) lo = mid + 1;
        else return &base[mid];
    }
    return NULL;
}

/* Internal SysV-only UNWIND_INFO parser — returns total alloc from codes */
static uint32_t seh_unwind_alloc(uint8_t* ui) {
    uint8_t count = ui[2];
    uint8_t* codes = ui + 4;
    uint32_t total_alloc = 0;
    int slot = 0;
    for (int i = 0; i < count && slot * 2 + 1 < (int)(count * 2 + 8); i++) {
        uint16_t cw = *(uint16_t*)(codes + slot * 2);
        uint8_t op = (cw >> 8) & 0x0f;   /* UnwindOp: low nibble of byte 1 */
        uint8_t info = (cw >> 12) & 0x0f;  /* OpInfo: high nibble of byte 1 */
        if (op == 1) { /* ALLOC_LARGE */
            slot++;
            if (info == 0) {
                total_alloc += *(uint16_t*)(codes + slot * 2);
                slot++;
            } else {
                total_alloc += *(uint32_t*)(codes + slot * 2) << 16;
                slot += 2;
            }
        } else if (op == 2) { /* ALLOC_SMALL */
            total_alloc += (uint32_t)(info + 1) * 8;
            slot++;
        } else if (op == 4) { slot += 2; }      /* SAVE_NONVOL */
        else if (op == 5) { slot += 3; }      /* SAVE_NONVOL_FAR */
        else if (op == 6) { slot += 2; }      /* SAVE_XMM128 */
        else if (op == 7) { slot += 3; }      /* SAVE_XMM128_FAR */
        else { slot++; }
    }
    return total_alloc;
}

/* Scan function prolog bytes to detect 'sub rsp, N'.
 * Handles functions with pushes before the sub. */
static uint32_t scan_prolog_alloc(uint32_t func_rva) {
    uint8_t* p = g_image_base + func_rva;
    if (!g_image_base) return 0;
    int i = 0;
    /* Skip REX-prefixed push instructions at function start.
     * A REX prefix (0x40-0x4F) is only a REX prefix for a push
     * if the NEXT byte is 0x50-0x57 (push r64). Otherwise it's
     * a REX prefix for a different instruction (like REX.W for sub rsp).
     */
    for (; i < 20; i++) {
        if (p[i] >= 0x50 && p[i] <= 0x57) continue; /* push r64 */
        if (p[i] >= 0x40 && p[i] <= 0x4F && p[i+1] >= 0x50 && p[i+1] <= 0x57) {
            i += 2; continue; /* REX+push */
        }
        break;
    }
    MW_TRACE("[ALLOC_SCAN] i=%d after push skip");
    /* Now check for 'sub rsp' at current position */
    if (i + 3 < 20 && p[i] == 0x48 && p[i+1] == 0x83 && p[i+2] == 0xec) {
        MW_TRACE("[ALLOC_SCAN] MATCH imm8: 0x%x", p[i+3]);
        return p[i+3];
    }
    if (i + 6 < 20 && p[i] == 0x48 && p[i+1] == 0x81 && p[i+2] == 0xec) {
        MW_TRACE("[ALLOC_SCAN] MATCH imm32: 0x%x", *(uint32_t*)(p + i + 3));
        return *(uint32_t*)(p + i + 3);
    }
    MW_TRACE("[ALLOC_SCAN] NO MATCH");
    return 0;
}

/* Count PUSH reg instructions at function start (before any other opcode)
 * Fixed: 0x40-0x4F is only a REX prefix when followed by 0x50-0x57 (push).
 * Otherwise it could be REX.W/R for a non-push instruction.
 */
static uint32_t scan_prolog_pushes(uint32_t func_rva) {
    uint8_t* p = g_image_base + func_rva;
    if (!g_image_base) return 0;
    uint32_t pushes = 0;
    for (int i = 0; i < 32; i++) {
        if (p[i] >= 0x50 && p[i] <= 0x57) { pushes++; continue; }
        if (p[i] >= 0x40 && p[i] <= 0x4F && p[i+1] >= 0x50 && p[i+1] <= 0x57) {
            pushes++; i++; continue; /* REX+push */
        }
        break;
    }
    return pushes * 8;
}

/* Internal SysV diagnostic stack walk function */
static void seh_diagnostic_walk(void) __attribute__((used));

static void seh_diagnostic_walk(void) {
    MW_TRACE("[EXP-NEXT] === Diagnostic Stack Walk ===");
    MW_TRACE("[EXP-NEXT] RIP=0x%lx RSP=0x%lx", g_cap_rip, g_cap_rsp);
    MW_TRACE("[EXP-NEXT] code=0x%x nargs=%u", g_cap_code, g_cap_nargs);

    MW_TRACE("[EXP-NEXT] [PROOF] *[rsp_entry]=0x%lx (expect 0x%lx) %s",
             *(uint64_t*)(uintptr_t)g_cap_rsp_entry, g_cap_rip,
             (*(uint64_t*)(uintptr_t)g_cap_rsp_entry == g_cap_rip) ? "MATCH" : "MISMATCH");

    uint64_t img_base = (uint64_t)(uintptr_t)g_image_base;
    uint64_t img_end = img_base + g_image_size;
    uint64_t rva = g_cap_rip - img_base;
    uint64_t rsp = g_cap_rsp;

    /* --- FRAME 0 --- */
    MW_TRACE("[EXP-NEXT] Frame[0]: RIP=0x%lx RVA=0x%lx", g_cap_rip, rva);

    RUNTIME_FUNCTION* rf = seh_internal_lookup(rva);
    if (!rf) {
        MW_TRACE("[EXP-NEXT] Frame[0]: No RF for RVA 0x%lx", rva);
        return;
    }

    long rf_idx = (long)(rf - (RUNTIME_FUNCTION*)(g_image_base + g_pdata_rva));
    uint8_t* ui = g_image_base + rf->UnwindInfo;
    uint8_t ui_flags = (ui[0] >> 3) & 0x03;
    uint8_t count_codes = ui[2];
    uint32_t unwind_alloc = seh_unwind_alloc(ui);
    uint32_t prolog_alloc = scan_prolog_alloc(rf->BeginAddress);
    uint32_t prolog_pushes = scan_prolog_pushes(rf->BeginAddress);

    MW_TRACE("[EXP-NEXT] Frame[0]: RF[%ld] begin=0x%x end=0x%x unwind=0x%x",
             rf_idx, rf->BeginAddress, rf->EndAddress, rf->UnwindInfo);

    /* Dump first 8 bytes at function start for validation */
    {
        uint8_t* fb = g_image_base + rf->BeginAddress;
        MW_TRACE("[EXP-NEXT] Frame[0]: bytes@0x%x: %02x %02x %02x %02x %02x %02x %02x %02x",
                 rf->BeginAddress,
                 fb[0], fb[1], fb[2], fb[3], fb[4], fb[5], fb[6], fb[7]);
    }

    MW_TRACE("[EXP-NEXT] Frame[0]: UI flags=0x%x codes=%u unwind_alloc=0x%x",
             ui_flags, count_codes, unwind_alloc);
    MW_TRACE("[EXP-NEXT] Frame[0]: prolog_alloc=0x%x prolog_pushes=0x%x",
             prolog_alloc, prolog_pushes);

    if (prolog_alloc > 0 && unwind_alloc == 0)
        MW_TRACE("[EXP-NEXT] Frame[0]: *** MALFORMED: sub rsp,0x%x but no ALLOC", prolog_alloc);
    if (!(ui_flags & UNW_FLAG_EHANDLER))
        MW_TRACE("[EXP-NEXT] Frame[0]: no EHANDLER, walking to parent");

    /* Corrected parent RSP = CONTEXT.Rsp + prolog_alloc + prolog_pushes */
    uint64_t corrected_rsp = rsp + prolog_alloc + prolog_pushes;
    MW_TRACE("[EXP-NEXT] Frame[0]: CONTEXT.Rsp=0x%lx corrected=0x%lx",
             rsp, corrected_rsp);

    uint64_t parent_rip = *(uint64_t*)(uintptr_t)corrected_rsp;
    uint64_t parent_rva = parent_rip - img_base;
    uint64_t parent_rsp = corrected_rsp + 8;

    MW_TRACE("[EXP-NEXT] Frame[0]: parent_rip=[0x%lx]=0x%lx RVA=0x%lx",
             corrected_rsp, parent_rip, parent_rva);

    if (parent_rip < img_base || parent_rip >= img_end) {
        MW_TRACE("[EXP-NEXT] Frame[0]: parent 0x%lx OUTSIDE PE — walk FAILED", parent_rip);
        return;
    }

    /* --- WALK SUBSEQUENT FRAMES --- */
    for (int frame = 1; frame <= 20; frame++) {
        MW_TRACE("[EXP-NEXT] Frame[%d]: RIP=0x%lx RVA=0x%lx", frame, parent_rip, parent_rva);

        rf = seh_internal_lookup(parent_rva);
        if (!rf) {
            MW_TRACE("[EXP-NEXT] Frame[%d]: No RF — left PE code", frame);
            break;
        }

        rf_idx = (long)(rf - (RUNTIME_FUNCTION*)(g_image_base + g_pdata_rva));
        ui = g_image_base + rf->UnwindInfo;
        ui_flags = (ui[0] >> 3) & 0x03;
        count_codes = ui[2];
        unwind_alloc = seh_unwind_alloc(ui);
        prolog_alloc = scan_prolog_alloc(rf->BeginAddress);
        prolog_pushes = scan_prolog_pushes(rf->BeginAddress);

        MW_TRACE("[EXP-NEXT] Frame[%d]: RF[%ld] 0x%x-0x%x unwind=0x%x flags=0x%x",
                 frame, rf_idx, rf->BeginAddress, rf->EndAddress, rf->UnwindInfo, ui_flags);

        /* Dump first 8 bytes */
        {
            uint8_t* fb = g_image_base + rf->BeginAddress;
            MW_TRACE("[EXP-NEXT] Frame[%d]: bytes@0x%x: %02x %02x %02x %02x %02x %02x %02x %02x",
                     frame, rf->BeginAddress,
                     fb[0], fb[1], fb[2], fb[3], fb[4], fb[5], fb[6], fb[7]);
        }

        MW_TRACE("[EXP-NEXT] Frame[%d]: unwind_alloc=0x%x prolog_alloc=0x%x pushes=0x%x",
                 frame, unwind_alloc, prolog_alloc, prolog_pushes);

        if (prolog_alloc > 0 && unwind_alloc == 0)
            MW_TRACE("[EXP-NEXT] Frame[%d]: *** MALFORMED UNWIND (missing ALLOC)", frame);

        if (ui_flags & UNW_FLAG_EHANDLER) {
            uint32_t h_off = 4 + count_codes * 2;
            if (h_off % 4) h_off += 2;
            uint32_t handler_rva = *(uint32_t*)(ui + h_off);
            void* lsda = (void*)(ui + h_off + 4);
            MW_TRACE("[EXP-NEXT] Frame[%d]: *** EHANDLER at RVA 0x%x LSDA_RVA=0x%lx ***",
                     frame, handler_rva,
                     (uint64_t)(uintptr_t)lsda - img_base);
            MW_TRACE("[EXP-NEXT] RESULT: EHANDLER reachable via prolog-corrected walk!");
            break;
        }

        /* Compute next parent */
        if (prolog_alloc > 0 && unwind_alloc == 0)
            corrected_rsp = parent_rsp + prolog_alloc + prolog_pushes;
        else
            corrected_rsp = parent_rsp + unwind_alloc + prolog_pushes;

        parent_rip = *(uint64_t*)(uintptr_t)corrected_rsp;
        parent_rva = parent_rip - img_base;
        parent_rsp = corrected_rsp + 8;

        if (parent_rip < img_base || parent_rip >= img_end) {
            MW_TRACE("[EXP-NEXT] Frame[%d]: parent 0x%lx outside PE — walk ends",
                     frame, parent_rip);
            break;
        }
    }

    MW_TRACE("[EXP-NEXT] === End Diagnostic Walk ===");
}

/* ============================================================
 * BUG-024: mw_RtlDispatchException
 * 
 * Real Windows x64 style exception dispatcher.
 * Walks the unwind chain, discovers EHANDLER frames,
 * invokes language-specific handlers, handles ContinueSearch/ContinueExecution.
 *
 * Called from the naked RaiseException stub (SysV ABI context).
 * Uses globals set by the naked stub: g_cap_rip, g_cap_rsp, g_cap_code, etc.
 *
 * Algorithm:
 *   1. Build EXCEPTION_RECORD and CONTEXT from captured state
 *   2. For each frame (MAX_FRAMES limit):
 *      a. RtlLookupFunctionEntry for context->Rip
 *      b. RtlVirtualUnwind to get handler + establisher frame
 *      c. If EHANDLER found: call handler, check disposition
 *      d. If ContinueExecution: restore CONTEXT and return
 *      e. If ContinueSearch: continue walking
 *   3. If no handler handles it: fall through to UnhandledExceptionFilter
 * ============================================================ */

#define DISPATCH_MAX_FRAMES 64

/* Dispatcher result codes */
#define DISP_RESULT_NOT_HANDLED   0
#define DISP_RESULT_CONTINUE_EXEC 1
#define DISP_RESULT_HANDLER_ERROR 2

/* Internal SysV RtlVirtualUnwind — operates on raw context buffer.
 * Returns: handler VA (or NULL if no handler for this frame).
 * Updates ctx in place (unwound registers).
 * Sets *establisher_frame, *out_handler_rva, *out_lsda. */
static void* seh_internal_virtual_unwind(
    uint64_t control_pc,
    RUNTIME_FUNCTION* rf,
    uint8_t* ctx,
    uint64_t* establisher_frame,
    uint32_t* out_handler_rva,
    void** out_lsda)
{
    uint8_t* ui_base = g_image_base + rf->UnwindInfo;

    uint8_t version     = ui_base[0] & 0x07;
    uint8_t flags       = (ui_base[0] >> 3) & 0x03;
    uint8_t prolog_size = ui_base[1];
    uint8_t count_codes = ui_base[2];
    uint8_t frame_reg   = ui_base[3] >> 4;
    uint8_t frame_off   = ui_base[3] & 0x0f;

    (void)version; (void)prolog_size;

    /* Handle CHAININFO */
    if (flags & UNW_FLAG_CHAININFO) {
        uint32_t chain_off = 4 + count_codes * 2;
        if (chain_off % 4) chain_off += 2;
        RUNTIME_FUNCTION* chained = (RUNTIME_FUNCTION*)(ui_base + chain_off);
        uint8_t* chained_ui = g_image_base + chained->UnwindInfo;
        uint8_t chained_flags = (chained_ui[0] >> 3) & 0x03;
        uint8_t chained_codes = chained_ui[2];
        uint64_t chained_est = 0;

        /* TODO: run unwind codes for chained info if needed */
        if (establisher_frame) *establisher_frame = chained_est;

        if (chained_flags & UNW_FLAG_EHANDLER) {
            uint32_t h_off = 4 + chained_codes * 2;
            if (h_off % 4) h_off += 2;
            uint32_t handler_rva = *(uint32_t*)(chained_ui + h_off);
            void* lsda = (void*)(chained_ui + h_off + 4);
            if (out_handler_rva) *out_handler_rva = handler_rva;
            if (out_lsda) *out_lsda = lsda;
            return (void*)(g_image_base + handler_rva);
        }
        if (out_handler_rva) *out_handler_rva = 0;
        if (out_lsda) *out_lsda = NULL;
        return NULL;
    }

    /* Read current register values from context */
    uint64_t rsp = *(uint64_t*)(ctx + CTX_Rsp);
    uint64_t rip = *(uint64_t*)(ctx + CTX_Rip);
    uint32_t rip_rva = (uint32_t)(rip - (uint64_t)(uintptr_t)g_image_base);
    uint32_t func_offset = rip_rva - rf->BeginAddress;

    /* Track stack during prolog simulation */
    uint64_t new_rsp = rsp;
    int fp_set = 0;
    uint64_t fp_reg_val = 0;

    /* Simulate unwind codes to "undo" the prolog */
    uint8_t* codes = ui_base + 4;
    int slot = 0;
    for (int i = 0; i < count_codes; i++) {
        if (slot * 2 + 1 >= (int)(count_codes * 2 + 16)) break;
        uint16_t code_word = *(uint16_t*)(codes + slot * 2);
        uint8_t op_code = (code_word >> 8) & 0x0f;   /* UnwindOp: low nibble of byte 1 */
        uint8_t op_info = (code_word >> 12) & 0x0f;  /* OpInfo: high nibble of byte 1 */
        uint8_t code_offset = code_word & 0xff;

        /* Skip codes that haven't fully executed yet */
        if (func_offset > 0 && code_offset >= func_offset) {
            slot++;
            continue;
        }

        switch (op_code) {
        case UWOP_PUSH_NONVOL: {
            new_rsp += 8;
            uint32_t off = reg_to_ctx_offset(op_info);
            if (off && new_rsp <= rsp + 4096) {
                *(uint64_t*)(ctx + off) = *(uint64_t*)(new_rsp);
            }
            break;
        }
        case UWOP_ALLOC_LARGE: {
            slot++;
            if (op_info == 0) {
                uint16_t alloc = *(uint16_t*)(codes + slot * 2);
                new_rsp += alloc;
            } else {
                slot++;
                uint32_t alloc = *(uint32_t*)(codes + slot * 2 - 2);
                new_rsp += (uint64_t)alloc << 16;
            }
            break;
        }
        case UWOP_ALLOC_SMALL: {
            new_rsp += (uint64_t)(op_info + 1) * 8;
            break;
        }
        case UWOP_SET_FPREG: {
            fp_reg_val = new_rsp + (uint64_t)frame_off * 16;
            fp_set = 1;
            break;
        }
        case UWOP_SAVE_NONVOL: {
            slot++;
            uint16_t stack_off = *(uint16_t*)(codes + slot * 2);
            uint32_t off = reg_to_ctx_offset(op_info);
            if (off) {
                *(uint64_t*)(ctx + off) = *(uint64_t*)(new_rsp + stack_off * 8);
            }
            break;
        }
        case UWOP_SAVE_NONVOL_FAR: {
            slot++; slot++;
            uint32_t stack_off = *(uint32_t*)(codes + slot * 2 - 2);
            uint32_t off = reg_to_ctx_offset(op_info);
            if (off) {
                *(uint64_t*)(ctx + off) = *(uint64_t*)(new_rsp + stack_off);
            }
            break;
        }
        case UWOP_SAVE_XMM128: { slot++; break; }
        case UWOP_SAVE_XMM128_FAR: { slot++; slot++; break; }
        case UWOP_PUSH_MACHFRAME: {
            new_rsp += 40;
            if (new_rsp <= rsp + 4096) {
                *(uint64_t*)(ctx + CTX_Rip) = *(uint64_t*)(new_rsp + 16);
                new_rsp = *(uint64_t*)(new_rsp + 8);
            }
            break;
        }
        default: {
            if (op_code >= 9) slot++;
            break;
        }
        }
        slot++;
    }

    /* Apply frame register */
    if (fp_set) {
        uint32_t fp_off = reg_to_ctx_offset(frame_reg);
        if (fp_off) *(uint64_t*)(ctx + fp_off) = fp_reg_val;
    }

    /* Update RSP in context */
    *(uint64_t*)(ctx + CTX_Rsp) = new_rsp;

    /* Set establisher frame */
    uint64_t establisher = fp_set ? fp_reg_val : new_rsp;
    if (establisher_frame) *establisher_frame = establisher;

    /* Find handler and LSDA */
    uint32_t handler_rva = 0;
    void* lsda = NULL;
    if (flags & UNW_FLAG_EHANDLER) {
        uint32_t h_off = 4 + count_codes * 2;
        if (h_off % 4) h_off += 2;
        handler_rva = *(uint32_t*)(ui_base + h_off);
        lsda = (void*)(ui_base + h_off + 4);
    }

    if (out_handler_rva) *out_handler_rva = handler_rva;
    if (out_lsda) *out_lsda = lsda;

    MW_TRACE("[DISPATCH] Unwind: pc=0x%lx begin=0x%x handler=0x%x est=0x%lx",
             (uint64_t)rip, rf->BeginAddress, handler_rva, establisher);

    if (handler_rva == 0) return NULL;
    return (void*)(g_image_base + handler_rva);
}

/* Build a CONTEXT buffer from captured register state.
 * The naked stub captures RIP and RSP precisely.
 * Other registers are read from current CPU state.
 * Exception registers (RCX, RDX, R8, R9) were saved from ms_abi params. */
static void seh_build_context(uint8_t* ctx, uint64_t rip, uint64_t rsp) {
    memset(ctx, 0, CONTEXT_SIZE);

    /* Set CONTEXT header */
    *(uint64_t*)(ctx + 0x00) = 0x10007F; /* ContextFlags = CONTEXT_FULL */

    /* Set RIP and RSP from captured state */
    *(uint64_t*)(ctx + CTX_Rip) = rip;
    *(uint64_t*)(ctx + CTX_Rsp) = rsp;

    /* Read current nonvolatile registers from CPU state */
    uint64_t rax, rbx, rcx, rdx, rbp, rsi, rdi;
    uint64_t r8, r9, r10, r11, r12, r13, r14, r15;
    __asm__ volatile ("" : "=a"(rax));
    __asm__ volatile ("" : "=b"(rbx));
    __asm__ volatile ("" : "=c"(rcx));
    __asm__ volatile ("" : "=d"(rdx));
    __asm__ volatile ("movq %%rbp, %0" : "=r"(rbp));
    __asm__ volatile ("" : "=S"(rsi));
    __asm__ volatile ("" : "=D"(rdi));
    __asm__ volatile ("" : "=r"(r8));
    __asm__ volatile ("" : "=r"(r9));
    __asm__ volatile ("" : "=r"(r10));
    __asm__ volatile ("" : "=r"(r11));
    __asm__ volatile ("" : "=r"(r12));
    __asm__ volatile ("" : "=r"(r13));
    __asm__ volatile ("" : "=r"(r14));
    __asm__ volatile ("" : "=r"(r15));

    *(uint64_t*)(ctx + CTX_Rax) = rax;
    *(uint64_t*)(ctx + CTX_Rcx) = rcx;
    *(uint64_t*)(ctx + CTX_Rdx) = rdx;
    *(uint64_t*)(ctx + CTX_Rbx) = rbx;
    *(uint64_t*)(ctx + CTX_Rbp) = rbp;
    *(uint64_t*)(ctx + CTX_Rsi) = rsi;
    *(uint64_t*)(ctx + CTX_Rdi) = rdi;
    *(uint64_t*)(ctx + CTX_R8)  = r8;
    *(uint64_t*)(ctx + CTX_R9)  = r9;
    *(uint64_t*)(ctx + CTX_R10) = r10;
    *(uint64_t*)(ctx + CTX_R11) = r11;
    *(uint64_t*)(ctx + CTX_R12) = r12;
    *(uint64_t*)(ctx + CTX_R13) = r13;
    *(uint64_t*)(ctx + CTX_R14) = r14;
    *(uint64_t*)(ctx + CTX_R15) = r15;
}

/* Handler function pointer type (ms_abi). */
typedef int32_t (*exception_handler_fn)(
    EXCEPTION_RECORD* er, uint64_t establisher_frame,
    uint8_t* ctx, DISPATCHER_CONTEXT* dc);

/* The dispatcher itself — called from naked stub (SysV ABI).
 * Returns: DISP_RESULT_CONTINUE_EXEC if a handler handled it,
 *          DISP_RESULT_NOT_HANDLED if no handler was found. */
static int seh_dispatch_exception(void) __attribute__((used));

/* VEH-aligned trampoline: calls a VEH handler with proper stack alignment.
 * VEH handler takes PEXCEPTION_POINTERS* as RCX (only arg).
 * Returns LONG: -1 = EXCEPTION_CONTINUE_EXECUTION, 0 = EXCEPTION_CONTINUE_SEARCH. */
static long call_veh_aligned(void* veh_handler, void* exception_pointers) {
    long result = 0;
    __asm__ volatile (
        "pushq %%rbp\n\t"
        "movq %%rsp, %%rbp\n\t"
        "andq $-16, %%rsp\n\t"
        "subq $0x20, %%rsp\n\t"
        "movq %2, %%rcx\n\t"
        "call *%1\n\t"
        "leave\n\t"
        : "=a"(result)
        : "r"(veh_handler), "r"(exception_pointers)
        : "rcx", "rdx", "r8", "r9", "r10", "r11", "memory"
    );
    return result;
}

/* ABI trampoline: calls an ms_abi PE handler with proper stack alignment.
 * The PE handler may call back into our ms_abi stubs (RtlUnwindEx, abort, etc.),
 * which require 16-byte stack alignment. We ensure this by creating a new
 * aligned stack frame. */
static int32_t call_handler_aligned(
    void* handler, EXCEPTION_RECORD* er, uint64_t est_frame,
    uint8_t* ctx, DISPATCHER_CONTEXT* dc)
{
    int32_t disposition = DISP_ExceptionContinueSearch;
    __asm__ volatile (
        /* Create a properly aligned stack frame */
        "pushq %%rbp\n\t"
        "movq %%rsp, %%rbp\n\t"
        "andq $-16, %%rsp\n\t"     /* Force 16-byte alignment */
        "subq $0x20, %%rsp\n\t"    /* 32-byte ms_abi shadow space */
        /* Set up ms_abi register arguments */
        "movq %2, %%rcx\n\t"
        "movq %3, %%rdx\n\t"
        "movq %4, %%r8\n\t"
        "movq %5, %%r9\n\t"
        "call *%1\n\t"
        /* Restore stack frame */
        "leave\n\t"
        : "=a"(disposition)
        : "r"(handler), "r"((uint64_t)(uintptr_t)er),
          "r"(est_frame), "r"((uint64_t)(uintptr_t)ctx),
          "r"((uint64_t)(uintptr_t)dc)
        : "rcx", "rdx", "r8", "r9", "r10", "r11", "memory"
    );
    return disposition;
}

static int seh_dispatch_exception(void) {
    MW_TRACE("[DISPATCH] === RtlDispatchException ===");
    MW_TRACE("[DISPATCH] ExceptionCode=0x%x RIP=0x%lx RSP=0x%lx",
             g_cap_code, g_cap_rip, g_cap_rsp);

    uint64_t img_base = (uint64_t)(uintptr_t)g_image_base;
    uint64_t img_end = img_base + g_image_size;

    /* 1. Build EXCEPTION_RECORD (also saved to g_cap_er for RtlUnwindEx) */
    EXCEPTION_RECORD er;
    memset(&er, 0, sizeof(er));
    memcpy(&g_cap_er, &er, sizeof(EXCEPTION_RECORD));
    er.ExceptionCode = g_cap_code;
    er.ExceptionFlags = g_cap_flags;
    er.ExceptionRecord = 0; /* no nested exception */
    er.ExceptionAddress = g_cap_rip;
    er.NumberParameters = g_cap_nargs;
    /* Copy exception parameters from captured args */
    if (g_cap_nargs > 0 && g_cap_args != 0 && g_cap_nargs <= EXCEPTION_MAXIMUM_PARAMETERS) {
        uint64_t* args = (uint64_t*)(uintptr_t)g_cap_args;
        for (uint32_t i = 0; i < g_cap_nargs; i++) {
            er.ExceptionInformation[i] = args[i];
        }
    }

    /* === EXP-NEXT-3 STEP 1: Dump full exception state === */
    MW_TRACE("[EXC_STATE] EXCEPTION_BEGIN");
    MW_TRACE("[EXC_STATE] Code: 0x%x", er.ExceptionCode);
    MW_TRACE("[EXC_STATE] Flags: 0x%x", er.ExceptionFlags);
    MW_TRACE("[EXC_STATE] Address: 0x%lx", (uint64_t)er.ExceptionAddress);
    MW_TRACE("[EXC_STATE] NumParams: %u", er.NumberParameters);
    MW_TRACE("[EXC_STATE] Param[0]: 0x%lx", er.ExceptionInformation[0]);
    MW_TRACE("[EXC_STATE] Param[1]: 0x%lx", er.ExceptionInformation[1]);
    MW_TRACE("[EXC_STATE] Param[2]: 0x%lx", er.ExceptionInformation[2]);
    MW_TRACE("[EXC_STATE] Param[3]: 0x%lx", er.ExceptionInformation[3]);
    MW_TRACE("[EXC_STATE] g_cap_args ptr: 0x%lx", (uint64_t)(uintptr_t)g_cap_args);
    /* Dump first 64 bytes of exception object if Param[0] looks like a pointer */
    if (er.NumberParameters >= 1 && er.ExceptionInformation[0] != 0) {
        uint8_t* exc_obj = (uint8_t*)(uintptr_t)er.ExceptionInformation[0];
        MW_TRACE("[EXC_STATE] Exception object @ %p (first 32 bytes):", (void*)exc_obj);
        /* Check if pointer is readable */
        uint64_t check;
        if (memcpy(&check, exc_obj, 8) == NULL || 1) {
            /* Just dump - if it faults we'll know from signal handler */
            MW_TRACE("[EXC_STATE]   %02x %02x %02x %02x %02x %02x %02x %02x",
                     exc_obj[0], exc_obj[1], exc_obj[2], exc_obj[3],
                     exc_obj[4], exc_obj[5], exc_obj[6], exc_obj[7]);
            MW_TRACE("[EXC_STATE]   %02x %02x %02x %02x %02x %02x %02x %02x",
                     exc_obj[8], exc_obj[9], exc_obj[10], exc_obj[11],
                     exc_obj[12], exc_obj[13], exc_obj[14], exc_obj[15]);
            MW_TRACE("[EXC_STATE]   %02x %02x %02x %02x %02x %02x %02x %02x",
                     exc_obj[16], exc_obj[17], exc_obj[18], exc_obj[19],
                     exc_obj[20], exc_obj[21], exc_obj[22], exc_obj[23]);
            MW_TRACE("[EXC_STATE]   %02x %02x %02x %02x %02x %02x %02x %02x",
                     exc_obj[24], exc_obj[25], exc_obj[26], exc_obj[27],
                     exc_obj[28], exc_obj[29], exc_obj[30], exc_obj[31]);
            /* Also dump as uint64 for the exception class */
            uint64_t exc_class = *(uint64_t*)exc_obj;
            MW_TRACE("[EXC_STATE] Exception class (u64): 0x%016lx", exc_class);
        }
    }
    MW_TRACE("[EXC_STATE] EXCEPTION_END");

    /* 2. Build CONTEXT */
    uint8_t ctx[CONTEXT_SIZE];
    seh_build_context(ctx, g_cap_rip, g_cap_rsp);
    
    /* Register CONTEXT for RtlUnwindEx access and set up longjmp target */
    g_unwind_ctx = ctx;
    g_is_unwinding = 0;
    g_unwind_target_ip = 0;
    g_unwind_target_frame = 0;
    
    /* setjmp return point for RtlUnwindEx longjmp.
     * When the personality function calls RtlUnwindEx, RtlUnwindEx modifies
     * the CONTEXT and longjmps here. We then return DISP_RESULT_CONTINUE_EXEC
     * with the modified CONTEXT, which the naked stub will use to resume.
     */
    if (setjmp(g_unwind_jmpbuf) != 0) {
        /* IMMEDIATELY copy all register values from CONTEXT to globals.
         * Must do this before ANY function calls (MW_TRACE, fflush) because
         * those calls use stack space that may overlap with the CONTEXT buffer. */
        g_unwind_regs[UR_RAX] = *(uint64_t*)(ctx + CTX_Rax);
        g_unwind_regs[UR_RCX] = *(uint64_t*)(ctx + CTX_Rcx);
        g_unwind_regs[UR_RDX] = *(uint64_t*)(ctx + CTX_Rdx);
        g_unwind_regs[UR_RBX] = *(uint64_t*)(ctx + CTX_Rbx);
        g_unwind_regs[UR_RSP] = *(uint64_t*)(ctx + CTX_Rsp);
        g_unwind_regs[UR_RBP] = *(uint64_t*)(ctx + CTX_Rbp);
        g_unwind_regs[UR_RSI] = *(uint64_t*)(ctx + CTX_Rsi);
        g_unwind_regs[UR_RDI] = *(uint64_t*)(ctx + CTX_Rdi);
        g_unwind_regs[UR_R8]  = *(uint64_t*)(ctx + CTX_R8);
        g_unwind_regs[UR_R9]  = *(uint64_t*)(ctx + CTX_R9);
        g_unwind_regs[UR_R10] = *(uint64_t*)(ctx + CTX_R10);
        g_unwind_regs[UR_R11] = *(uint64_t*)(ctx + CTX_R11);
        g_unwind_regs[UR_R12] = *(uint64_t*)(ctx + CTX_R12);
        g_unwind_regs[UR_R13] = *(uint64_t*)(ctx + CTX_R13);
        g_unwind_regs[UR_R14] = *(uint64_t*)(ctx + CTX_R14);
        g_unwind_regs[UR_R15] = *(uint64_t*)(ctx + CTX_R15);
        g_unwind_target_ip = *(uint64_t*)(ctx + CTX_Rip);
        g_unwind_target_frame = *(uint64_t*)(ctx + CTX_Rsp);
        
        MW_TRACE("[DISPATCH] RtlUnwindEx longjmped back — unwind to RIP=0x%lx RSP=0x%lx",
                 g_unwind_target_ip, g_unwind_target_frame);
        MW_TRACE("[DISPATCH] CONTEXT after longjmp: Rax=0x%lx (saved to g_unwind_regs)",
                 g_unwind_regs[UR_RAX]);
        if (g_trace_log) fflush(g_trace_log);
        fflush(stderr);
        return DISP_RESULT_CONTINUE_EXEC;
    }

    /* 2.5. Try Vectored Exception Handlers (VEH) first.
     * VEH handlers are called BEFORE frame-based handlers.
     * VEH return: EXCEPTION_CONTINUE_EXECUTION (-1) = handled,
     *              EXCEPTION_CONTINUE_SEARCH (0) = continue to frame walk.
     * VEH handler signature: LONG Handler(PEXCEPTION_POINTERS*)
     * PEXCEPTION_POINTERS = {EXCEPTION_RECORD*, CONTEXT*, PVOID ExceptionAddr}
     */
    MW_TRACE("[DISPATCH] VEH chain: %d handlers", g_veh_count);
    if (g_veh_count > 0) {
        /* Build EXCEPTION_POINTERS on stack.
         * EXCEPTION_POINTERS layout: {EXCEPTION_RECORD*, CONTEXT*, PVOID ExceptionAddr} */
        uint64_t ep[3];
        ep[0] = (uint64_t)(uintptr_t)&er;  /* ExceptionRecord* */
        ep[1] = (uint64_t)(uintptr_t)ctx;   /* CONTEXT* */
        ep[2] = g_cap_rip;              /* ExceptionAddress */

        /* Call VEH handlers in reverse order (last registered = first called).
         * VEH handler signature: LONG WINAPI Handler(PEXCEPTION_POINTERS*).
         * PEXCEPTION_POINTERS* is passed in RCX (ms_abi first arg).
         * Returns LONG: -1 = EXCEPTION_CONTINUE_EXECUTION, 0 = EXCEPTION_CONTINUE_SEARCH. */
        for (int i = g_veh_count - 1; i >= 0; i--) {
            void* veh_handler = g_veh_handlers[i];
            MW_TRACE("[DISPATCH] VEH[%d]: handler=%p", i, veh_handler);

            long veh_result = call_veh_aligned(veh_handler, (void*)ep);

            MW_TRACE("[DISPATCH] VEH[%d]: returned %ld", i, veh_result);
            if (veh_result == EXCEPTION_CONTINUE_EXECUTION) {
                MW_TRACE("[DISPATCH] VEH handled exception — ContinueExecution");
                return DISP_RESULT_CONTINUE_EXEC;
            }
        }
    }

    /* 3. Walk frames */
    for (int frame = 0; frame < DISPATCH_MAX_FRAMES; frame++) {
        uint64_t current_rip = *(uint64_t*)(ctx + CTX_Rip);
        uint64_t current_rva = current_rip - img_base;

        MW_TRACE("[DISPATCH] Frame[%d]: RIP=0x%lx RVA=0x%lx", frame, current_rip, current_rva);

        /* Check if still inside PE */
        if (current_rip < img_base || current_rip >= img_end) {
            MW_TRACE("[DISPATCH] Frame[%d]: RIP outside PE — walk ends", frame);
            break;
        }

        /* Lookup RUNTIME_FUNCTION */
        RUNTIME_FUNCTION* rf = seh_internal_lookup(current_rva);
        if (!rf) {
            MW_TRACE("[DISPATCH] Frame[%d]: No RUNTIME_FUNCTION — walk ends", frame);
            break;
        }

        MW_TRACE("[DISPATCH] Frame[%d]: RF begin=0x%x end=0x%x ui=0x%x",
                 frame, rf->BeginAddress, rf->EndAddress, rf->UnwindInfo);

        /* VirtualUnwind this frame */
        uint64_t est_frame = 0;
        uint32_t handler_rva = 0;
        void* lsda = NULL;
        void* handler = seh_internal_virtual_unwind(
            current_rip, rf, ctx, &est_frame, &handler_rva, &lsda);

        if (!handler) {
            /* No handler for this frame. Read parent return address
             * from [establisher_frame] and continue walking. */
            uint64_t parent_rip = *(uint64_t*)(uintptr_t)est_frame;
            uint64_t parent_rva = parent_rip - img_base;
            MW_TRACE("[DISPATCH] Frame[%d]: no handler, parent RIP=0x%lx RVA=0x%lx",
                     frame, parent_rip, parent_rva);

            /* Update context for next frame */
            *(uint64_t*)(ctx + CTX_Rip) = parent_rip;
            *(uint64_t*)(ctx + CTX_Rsp) = est_frame + 8;
            continue;
        }

        /* EHANDLER found — build DISPATCHER_CONTEXT and call handler */
        MW_TRACE("[DISPATCH] Frame[%d]: EHANDLER at RVA 0x%x (VA 0x%lx)",
                 frame, handler_rva, (uint64_t)(uintptr_t)handler);
        MW_TRACE("[DISPATCH] Frame[%d]: establisher=0x%lx lsda=%p",
                 frame, est_frame, lsda);

        DISPATCHER_CONTEXT dc;
        memset(&dc, 0, sizeof(dc));
        /* CRITICAL: On Windows x64, after RtlVirtualUnwind, the context RIP is
         * the RETURN ADDRESS (one past the call instruction). GCC personality
         * functions expect the PC to point WITHIN the faulting call instruction.
         * The personality's internal _Unwind_GetIPInfo returns ip_before_insn=0,
         * causing the real personality to subtract 1. However, if the SEH wrapper
         * doesn't set ip_before_insn correctly, the personality misses the call site.
         * As a safety measure, we pass ControlPc = RIP - 1 to ensure the PC falls
         * within the call instruction's range in the LSDA call site table. */
        dc.ControlPc = current_rip > 0 ? current_rip - 1 : 0;
        dc.ImageBase = img_base;
        dc.FunctionEntry = rf;
        dc.EstablisherFrame = est_frame;
        dc.TargetIp = 0;
        dc.ContextRecord = ctx;
        dc.LanguageHandler = handler;
        dc.HandlerData = lsda;
        dc.HistoryTable = 0;
        dc.ScopeIndex = 0;
        dc.Fill = 0;

        /* Also adjust CONTEXT.Rip for personality functions that read it directly */
        uint64_t saved_ctx_rip = *(uint64_t*)(ctx + CTX_Rip);
        if (saved_ctx_rip > 0) *(uint64_t*)(ctx + CTX_Rip) = saved_ctx_rip - 1;

        /* Call the language-specific handler (ms_abi calling convention).
         * Handler signature: LONG Handler(EXCEPTION_RECORD*, ULONG64, CONTEXT*, DISPATCHER_CONTEXT*)
         * Returns EXCEPTION_DISPOSITION.
         *
         * CRITICAL ABI NOTE: The handler is ms_abi code inside the PE.
         * We're calling from SysV code. We must use an ms_abi function pointer
         * to ensure correct register assignment (RCX, RDX, R8, R9).
         */
        /* === EXP-NEXT-3: Dump handler call details === */
        MW_TRACE("[HANDLER_CALL] Frame[%d] BEGIN", frame);
        MW_TRACE("[HANDLER_CALL] RCX (EXCEPTION_RECORD*) = %p", (void*)&er);
        MW_TRACE("[HANDLER_CALL] RDX (EstablisherFrame) = 0x%lx", est_frame);
        MW_TRACE("[HANDLER_CALL] R8  (CONTEXT*)       = %p", (void*)ctx);
        MW_TRACE("[HANDLER_CALL] R9  (DISPATCHER_CONTEXT*) = %p", (void*)&dc);
        MW_TRACE("[HANDLER_CALL] Handler VA = %p  RVA = 0x%x", handler, handler_rva);
        MW_TRACE("[HANDLER_CALL] HandlerData (LSDA) = %p", lsda);
        MW_TRACE("[HANDLER_CALL] ER.Code=0x%x ER.Flags=0x%x ER.NumParams=%u",
                 er.ExceptionCode, er.ExceptionFlags, er.NumberParameters);
        MW_TRACE("[HANDLER_CALL] ER.Param[0]=0x%lx ER.Param[1]=0x%lx ER.Param[2]=0x%lx",
                 er.ExceptionInformation[0], er.ExceptionInformation[1], er.ExceptionInformation[2]);
        /* Dump first 64 bytes of handler function */
        {
            uint8_t* hp = (uint8_t*)handler;
            MW_TRACE("[HANDLER_CALL] Handler bytes [0:16]: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x",
                     hp[0], hp[1], hp[2], hp[3], hp[4], hp[5], hp[6], hp[7],
                     hp[8], hp[9], hp[10], hp[11], hp[12], hp[13], hp[14], hp[15]);
            MW_TRACE("[HANDLER_CALL] Handler bytes[16:32]: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x",
                     hp[16], hp[17], hp[18], hp[19], hp[20], hp[21], hp[22], hp[23],
                     hp[24], hp[25], hp[26], hp[27], hp[28], hp[29], hp[30], hp[31]);
            MW_TRACE("[HANDLER_CALL] Handler bytes[32:48]: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x",
                     hp[32], hp[33], hp[34], hp[35], hp[36], hp[37], hp[38], hp[39],
                     hp[40], hp[41], hp[42], hp[43], hp[44], hp[45], hp[46], hp[47]);
        }
        /* Dump 64 bytes of LSDA/HandlerData */
        if (lsda) {
            uint8_t* lp = (uint8_t*)lsda;
            MW_TRACE("[LSDA] raw[0:16]:  %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x",
                     lp[0], lp[1], lp[2], lp[3], lp[4], lp[5], lp[6], lp[7],
                     lp[8], lp[9], lp[10], lp[11], lp[12], lp[13], lp[14], lp[15]);
            MW_TRACE("[LSDA] raw[16:32]: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x",
                     lp[16], lp[17], lp[18], lp[19], lp[20], lp[21], lp[22], lp[23],
                     lp[24], lp[25], lp[26], lp[27], lp[28], lp[29], lp[30], lp[31]);
            MW_TRACE("[LSDA] raw[32:48]: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x",
                     lp[32], lp[33], lp[34], lp[35], lp[36], lp[37], lp[38], lp[39],
                     lp[40], lp[41], lp[42], lp[43], lp[44], lp[45], lp[46], lp[47]);
            MW_TRACE("[LSDA] raw[48:64]: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x",
                     lp[48], lp[49], lp[50], lp[51], lp[52], lp[53], lp[54], lp[55],
                     lp[56], lp[57], lp[58], lp[59], lp[60], lp[61], lp[62], lp[63]);
            /* Parse LSDA header for diagnostics */
            MW_TRACE("[LSDA] LPStart_enc=0x%02x (%s)", lp[0],
                     lp[0] == 0xff ? "omit" : "present");
            MW_TRACE("[LSDA] TType_enc=0x%02x", lp[1]);
            MW_TRACE("[LSDA] CS_enc=0x%02x", lp[2]);
        }
        /* Dump CONTEXT state being passed to handler */
        {
            uint64_t ctx_rip = *(uint64_t*)(ctx + CTX_Rip);
            uint64_t ctx_rsp = *(uint64_t*)(ctx + CTX_Rsp);
            uint64_t ctx_rbp = *(uint64_t*)(ctx + CTX_Rbp);
            MW_TRACE("[HANDLER_CALL] CTX.Rip=0x%lx CTX.Rsp=0x%lx CTX.Rbp=0x%lx",
                     ctx_rip, ctx_rsp, ctx_rbp);
        }
        MW_TRACE("[HANDLER_CALL] calling...");

        /* Call handler via aligned trampoline */
        int32_t disposition = call_handler_aligned(handler, &er, est_frame, ctx, &dc);

        /* CRITICAL: flush immediately so we don't lose trace if handler crashes */
        if (g_trace_log) fflush(g_trace_log);
        fflush(stderr);

        MW_TRACE("[HANDLER_CALL] returned disposition=%d (0=Exec %1=Search %2=Nested %3=Collided)", disposition);
        MW_TRACE("[HANDLER_CALL] Frame[%d] END", frame);

        /* Restore CONTEXT.Rip (was adjusted for personality) */
        *(uint64_t*)(ctx + CTX_Rip) = saved_ctx_rip;

        if (disposition == DISP_ExceptionContinueExecution) {
            MW_TRACE("[DISPATCH] Handler returned ContinueExecution — resuming");
            return DISP_RESULT_CONTINUE_EXEC;
        }

        if (disposition == DISP_ExceptionContinueSearch) {
            MW_TRACE("[DISPATCH] Handler returned ContinueSearch — walking to parent");
            /* Read parent return address from [establisher_frame] */
            uint64_t parent_rip = *(uint64_t*)(uintptr_t)est_frame;
            uint64_t parent_rva = parent_rip - img_base;
            MW_TRACE("[DISPATCH] Frame[%d]: parent RIP=0x%lx RVA=0x%lx",
                     frame, parent_rip, parent_rva);
            *(uint64_t*)(ctx + CTX_Rip) = parent_rip;
            *(uint64_t*)(ctx + CTX_Rsp) = est_frame + 8;
            continue;
        }

        /* DISP_ExceptionNestedException (2) or DISP_ExceptionCollidedUnwind (3)
         * — for now, treat as stop-walking */
        MW_TRACE("[DISPATCH] Handler returned disposition=%d — stopping", disposition);
        break;
    }

    MW_TRACE("[DISPATCH] No handler handled the exception");
    if (g_trace_log) fflush(g_trace_log);
    fflush(stderr);
    return DISP_RESULT_NOT_HANDLED;
}

/* Naked stub: captures RIP/RSP at RaiseException entry, calls dispatcher. */
__attribute__((naked, ms_abi))
void mw_RaiseException(uint32_t code, uint32_t flags,
    uint32_t nargs, uint64_t* args) {
    __asm__ volatile (
        /* Entry: RSP -> return address. MS ABI: rcx=code, rdx=flags, r8=nargs, r9=args */
        "pushq %rbx\n\t"
        "pushq %rbp\n\t"
        "pushq %r12\n\t"
        "pushq %r13\n\t"
        "pushq %r14\n\t"
        "pushq %r15\n\t"
        "subq $0x28, %rsp\n\t"   /* 5 qwords for our data, 16-byte aligned */
        /* Save rsp_entry (RSP before our pushes + sub) */
        "leaq 0x58(%rsp), %rax\n\t"  /* 6*8 + 0x28 = 48+40 = 88 = 0x58 */
        "movq %rax, 0x00(%rsp)\n\t"  /* save rsp_entry */
        /* Save return address = [rsp_entry] */
        "movq (%rax), %rbx\n\t"
        "movq %rbx, 0x08(%rsp)\n\t"  /* save rip */
        /* Save parameters (still in rcx, edx, r8d, r9 from ms_abi) */
        "movq %rcx, 0x10(%rsp)\n\t"
        "movl %edx, 0x18(%rsp)\n\t"
        "movl %r8d, 0x1C(%rsp)\n\t"
        "movq %r9, 0x20(%rsp)\n\t"
        /* Copy to globals */
        "movq 0x08(%rsp), %rax\n\t"
        "movq %rax, g_cap_rip(%rip)\n\t"
        "movq 0x00(%rsp), %rax\n\t"
        "movq %rax, g_cap_rsp_entry(%rip)\n\t"
        "leaq 8(%rax), %rax\n\t"
        "movq %rax, g_cap_rsp(%rip)\n\t"
        "movl 0x10(%rsp), %eax\n\t"
        "movl %eax, g_cap_code(%rip)\n\t"
        "movl 0x18(%rsp), %eax\n\t"
        "movl %eax, g_cap_flags(%rip)\n\t"
        "movl 0x1C(%rsp), %eax\n\t"
        "movl %eax, g_cap_nargs(%rip)\n\t"
        "movq 0x20(%rsp), %rax\n\t"
        "movq %rax, g_cap_args(%rip)\n\t"
        /* Call the real exception dispatcher (SysV ABI) */
        "call seh_dispatch_exception\n\t"
        /* Check if RtlUnwindEx longjmped back (unwind path) */
        "cmpb $0, g_is_unwinding(%rip)\n\t"
        "je .L_mw_normal_return\n\t"
        /* === UNWIND RETURN PATH === */
        /* Restore all registers from g_unwind_regs[] globals.
         * These were copied from CONTEXT by seh_dispatch_exception BEFORE any
         * function calls that could overwrite the stack-local CONTEXT buffer. */
        /* Register indices: RAX=0 RCX=1 RDX=2 RBX=3 RSP=4 RBP=5 RSI=6 RDI=7
         *                   R8=8 R9=9 R10=10 R11=11 R12=12 R13=13 R14=14 R15=15 */
        "movq g_unwind_regs+0(%rip), %rax\n\t"     /* RAX [0] */
        "movq g_unwind_regs+8(%rip), %rcx\n\t"     /* RCX [1] */
        "movq g_unwind_regs+16(%rip), %rdx\n\t"    /* RDX [2] */
        "movq g_unwind_regs+24(%rip), %rbx\n\t"    /* RBX [3] */
        /* RSP loaded last — after loading everything else */
        "movq g_unwind_regs+40(%rip), %rbp\n\t"    /* RBP [5] */
        "movq g_unwind_regs+48(%rip), %rsi\n\t"    /* RSI [6] */
        "movq g_unwind_regs+56(%rip), %rdi\n\t"    /* RDI [7] */
        "movq g_unwind_regs+64(%rip), %r8\n\t"     /* R8 [8] */
        "movq g_unwind_regs+72(%rip), %r9\n\t"     /* R9 [9] */
        "movq g_unwind_regs+80(%rip), %r10\n\t"    /* R10 [10] */
        "movq g_unwind_regs+88(%rip), %r11\n\t"    /* R11 [11] */
        "movq g_unwind_regs+96(%rip), %r12\n\t"    /* R12 [12] */
        "movq g_unwind_regs+104(%rip), %r13\n\t"   /* R13 [13] */
        "movq g_unwind_regs+112(%rip), %r14\n\t"   /* R14 [14] */
        "movq g_unwind_regs+120(%rip), %r15\n\t"   /* R15 [15] */
        /* Load target RIP into rcx, then set RSP and jump */
        "movq g_unwind_target_ip(%rip), %rcx\n\t"
        "movq g_unwind_regs+32(%rip), %rsp\n\t"   /* RSP [4*8=32] */
        "jmp *%rcx\n\t"                     /* jump to catch landing pad */
        
        ".L_mw_normal_return:\n\t"
        /* Clean up and return (normal path) */
        "addq $0x28, %rsp\n\t"
        "popq %r15\n\t"
        "popq %r14\n\t"
        "popq %r13\n\t"
        "popq %r12\n\t"
        "popq %rbp\n\t"
        "popq %rbx\n\t"
        "ret\n\t"
    );
}

/* Keep original implementation as reference (renamed, not called) */
static void mw_RaiseException_baseline(uint32_t code, uint32_t flags,
    uint32_t nargs, uint64_t* args) {
    (void)code; (void)flags; (void)nargs; (void)args;
    /* Original body removed — kept for reference only */
}

__attribute__((ms_abi)) int mw_ReleaseSemaphore(void* h, int32_t count, int32_t* prev) {
    MW_TRACE("ReleaseSemaphore()");
    if (prev) *prev = 0;
    return 1;
}

__attribute__((ms_abi)) int mw_ResetEvent(void* h) {
    MW_TRACE("ResetEvent()");
    return 1;
}

__attribute__((ms_abi)) uint32_t mw_ResumeThread(void* h) {
    MW_TRACE("ResumeThread()");
    return 0;
}

/* SEH: RtlLookupFunctionEntry — binary search through .pdata */
__attribute__((ms_abi)) void* mw_RtlLookupFunctionEntry(uint64_t control_pc,
    uint64_t* out_image_base, void* history) {
    if (out_image_base) *out_image_base = (uint64_t)(uintptr_t)g_image_base;

    if (g_pdata_rva == 0 || g_num_rt_functions == 0) return NULL;

    uint64_t rva = control_pc - (uint64_t)(uintptr_t)g_image_base;
    RUNTIME_FUNCTION* base = (RUNTIME_FUNCTION*)(g_image_base + g_pdata_rva);

    /* Binary search: entries are sorted by BeginAddress */
    int lo = 0, hi = (int)g_num_rt_functions - 1;
    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        RUNTIME_FUNCTION* rf = &base[mid];
        if (rva < rf->BeginAddress) {
            hi = mid - 1;
        } else if (rva >= rf->EndAddress) {
            lo = mid + 1;
        } else {
            MW_TRACE("RtlLookupFunctionEntry(pc=0x%lx rva=0x%lx) -> [%d] begin=0x%x end=0x%x unwind=0x%x",
                     control_pc, (uint32_t)rva, mid,
                     rf->BeginAddress, rf->EndAddress, rf->UnwindInfo);
            return (void*)rf;
        }
    }
    MW_TRACE("RtlLookupFunctionEntry(pc=0x%lx rva=0x%lx) -> NULL", control_pc, (uint32_t)rva);
    return NULL;
}

__attribute__((ms_abi)) void mw_RtlUnwindEx(void* target_frame, void* target_ip, void* ctx_ptr, void* exc_record, void* history) {
    MW_TRACE("RtlUnwindEx(target_frame=%p, target_ip=%p, ctx=%p, exc=%p)",
             target_frame, target_ip, ctx_ptr, exc_record);
    
    uint64_t tgt_ip = (uint64_t)(uintptr_t)target_ip;
    uint64_t tgt_frame = (uint64_t)(uintptr_t)target_frame;
    
    MW_TRACE("RtlUnwindEx: TargetIp=0x%lx TargetFrame=0x%lx", tgt_ip, tgt_frame);
    
    /* Walk frames from current to target, calling cleanup handlers.
     * For each frame between the current frame and the target frame,
     * we call the personality function with EH_UNWINDING flag.
     * 
     * IMPORTANT: The personality function that called us already found
     * the catch handler. RtlUnwindEx is responsible for:
     * 1. Running cleanup (dtors) for intermediate frames
     * 2. Setting the context to resume at the catch handler
     * 3. NEVER returning to the caller (non-returning function)
     */
    
    /* For now, implement the minimal unwind:
     * - Skip cleanup of intermediate frames (UPX --version likely doesn't
     *   need complex dtor cleanup for the throw path)
     * - Modify the CONTEXT to target the catch landing pad
     * - longjmp back to the dispatcher's setjmp point
     * 
     * This is a simplified implementation. A full implementation would
     * walk each frame and call handlers with EH_UNWINDING flag.
     */
    
    uint8_t* ctx = g_unwind_ctx;
    if (ctx) {
        /* Modify CONTEXT to target the landing pad */
        *(uint64_t*)(ctx + CTX_Rip) = tgt_ip;
        *(uint64_t*)(ctx + CTX_Rsp) = tgt_frame;
        
        /* CRITICAL: The GCC personality function sets RAX to the exception object
         * pointer via _Unwind_SetGR, but it writes to the GCC unwind context,
         * not our Windows CONTEXT buffer. We need to transfer the exception
         * object pointer to our CONTEXT's RAX so the catch handler can access it.
         * The 4th argument (exc_record) is the exception object pointer.
         * For C++ exceptions (code 0x20474343), this is the _Unwind_Exception*
         * which also serves as the exception object pointer for the catch handler. */
        uint64_t exc_obj = (uint64_t)(uintptr_t)exc_record;
        if (exc_obj != 0) {
            *(uint64_t*)(ctx + CTX_Rax) = exc_obj;
            MW_TRACE("RtlUnwindEx: Set CTX.Rax = 0x%lx (exception object)", exc_obj);
        }
        
        /* Store targets for the dispatcher */
        g_unwind_target_ip = tgt_ip;
        g_unwind_target_frame = tgt_frame;
        g_is_unwinding = 1;
        
        MW_TRACE("RtlUnwindEx: longjmping to dispatcher (RIP=0x%lx RSP=0x%lx)", tgt_ip, tgt_frame);
        if (g_trace_log) fflush(g_trace_log);
        fflush(stderr);
        
        /* longjmp back to seh_dispatch_exception's setjmp point.
         * This NEVER returns. */
        longjmp(g_unwind_jmpbuf, 1);
    }
    
    MW_TRACE("RtlUnwindEx: ERROR — no CONTEXT available, cannot unwind");
}

__attribute__((ms_abi)) void* mw_RtlVirtualUnwind(uint32_t handler_type, uint64_t image_base, uint64_t control_pc,
    void* function_entry, void* context_ptr, void* handler_data_ptr,
    uint64_t* establisher_frame_ptr, void* context_pointers) {
    RUNTIME_FUNCTION* rf = (RUNTIME_FUNCTION*)function_entry;
    uint8_t* ui_base = g_image_base + rf->UnwindInfo;
    uint8_t* ctx = (uint8_t*)context_ptr;

    uint8_t version = ui_base[0] & 0x07;
    uint8_t flags   = (ui_base[0] >> 3) & 0x03;
    uint8_t prolog_size = ui_base[1];
    uint8_t count_codes = ui_base[2];
    uint8_t frame_reg   = ui_base[3] >> 4;
    uint8_t frame_off   = ui_base[3] & 0x0f;

    (void)image_base; (void)context_pointers; (void)version;

    /* Handle CHAININFO */
    if (flags & UNW_FLAG_CHAININFO) {
        uint32_t chain_off = 4 + count_codes * 2;
        if (chain_off % 4) chain_off += 2;
        RUNTIME_FUNCTION* chained = (RUNTIME_FUNCTION*)(ui_base + chain_off);
        MW_TRACE("RtlVirtualUnwind: CHAININFO -> begin=0x%x unwind=0x%x",
                 chained->BeginAddress, chained->UnwindInfo);
        uint8_t* chained_ui = g_image_base + chained->UnwindInfo;
        uint8_t chained_flags = (chained_ui[0] >> 3) & 0x03;
        uint8_t chained_codes = chained_ui[2];
        if (chained_flags & handler_type) {
            uint32_t h_off = 4 + chained_codes * 2;
            if (h_off % 4) h_off += 2;
            uint32_t handler_rva = *(uint32_t*)(chained_ui + h_off);
            void* lsda = (void*)(chained_ui + h_off + 4);
            if (handler_data_ptr) *(void**)handler_data_ptr = lsda;
            return (void*)(g_image_base + handler_rva);
        }
        return NULL;
    }

    /* Read current register values from context */
    uint64_t rsp = *(uint64_t*)(ctx + CTX_Rsp);
    uint64_t rip = *(uint64_t*)(ctx + CTX_Rip);
    uint32_t rip_rva = (uint32_t)(rip - (uint64_t)(uintptr_t)g_image_base);
    uint32_t func_offset = rip_rva - rf->BeginAddress;

    /* Track stack during prolog simulation */
    uint64_t new_rsp = rsp;
    int fp_set = 0;
    uint64_t fp_reg = 0;

    /* Simulate unwind codes to "undo" the prolog */
    uint8_t* codes = ui_base + 4;
    int slot = 0;
    for (int i = 0; i < count_codes; i++) {
        uint16_t code_word = *(uint16_t*)(codes + slot * 2);
        uint8_t op_code = (code_word >> 8) & 0x0f;   /* UnwindOp: low nibble of byte 1 */
        uint8_t op_info = (code_word >> 12) & 0x0f;  /* OpInfo: high nibble of byte 1 */
        uint8_t code_offset = code_word & 0xff;

        /* Skip codes that haven't fully executed yet */
        if (func_offset > 0 && code_offset >= func_offset) {
            slot++;
            continue;
        }

        switch (op_code) {
        case UWOP_PUSH_NONVOL: {
            new_rsp += 8;
            uint32_t off = reg_to_ctx_offset(op_info);
            if (off && new_rsp <= rsp + 4096) {
                *(uint64_t*)(ctx + off) = *(uint64_t*)(new_rsp);
            }
            break;
        }
        case UWOP_ALLOC_LARGE: {
            slot++;
            if (op_info == 0) {
                uint16_t alloc = *(uint16_t*)(codes + slot * 2);
                new_rsp += alloc;
            } else {
                slot++;
                uint32_t alloc = *(uint32_t*)(codes + slot * 2 - 2);
                new_rsp += (uint64_t)alloc << 16;
            }
            break;
        }
        case UWOP_ALLOC_SMALL: {
            new_rsp += (op_info + 1) * 8;
            break;
        }
        case UWOP_SET_FPREG: {
            fp_reg = new_rsp + (uint64_t)frame_off * 16;
            fp_set = 1;
            break;
        }
        case UWOP_SAVE_NONVOL: {
            slot++;
            uint16_t stack_off = *(uint16_t*)(codes + slot * 2);
            uint32_t off = reg_to_ctx_offset(op_info);
            if (off) {
                *(uint64_t*)(ctx + off) = *(uint64_t*)(new_rsp + stack_off * 8);
            }
            break;
        }
        case UWOP_SAVE_NONVOL_FAR: {
            slot++; slot++;
            uint32_t stack_off = *(uint32_t*)(codes + slot * 2 - 2);
            uint32_t off = reg_to_ctx_offset(op_info);
            if (off) {
                *(uint64_t*)(ctx + off) = *(uint64_t*)(new_rsp + stack_off);
            }
            break;
        }
        case UWOP_SAVE_XMM128: {
            slot++;
            break;
        }
        case UWOP_SAVE_XMM128_FAR: {
            slot++; slot++;
            break;
        }
        case UWOP_PUSH_MACHFRAME: {
            new_rsp += 40;
            if (new_rsp <= rsp + 4096) {
                *(uint64_t*)(ctx + CTX_Rip) = *(uint64_t*)(new_rsp + 16);
                new_rsp = *(uint64_t*)(new_rsp + 8);
            }
            break;
        }
        default: {
            static int warned_unknown[7] = {0};
            if (op_code >= 9 && op_code <= 15 && !warned_unknown[op_code - 9]) {
                MW_TRACE("RtlVirtualUnwind: unknown opcode %d (skipping)", op_code);
                warned_unknown[op_code - 9] = 1;
            }
            if (op_code >= 9) slot++;
            break;
        }
        }
        slot++;
    }

    /* Apply frame register */
    if (fp_set) {
        uint32_t fp_off = reg_to_ctx_offset(frame_reg);
        if (fp_off) *(uint64_t*)(ctx + fp_off) = fp_reg;
    }

    /* Update RSP */
    *(uint64_t*)(ctx + CTX_Rsp) = new_rsp;

    /* Set establisher frame */
    uint64_t establisher = fp_set ? fp_reg : new_rsp;
    if (establisher_frame_ptr) *establisher_frame_ptr = establisher;

    /* Find handler and LSDA */
    uint32_t handler_rva = 0;
    void* lsda = NULL;
    if (flags & handler_type) {
        uint32_t h_off = 4 + count_codes * 2;
        if (h_off % 4) h_off += 2;
        handler_rva = *(uint32_t*)(ui_base + h_off);
        lsda = (void*)(ui_base + h_off + 4);
    }

    if (handler_data_ptr && lsda) {
        *(void**)handler_data_ptr = lsda;
    }

    MW_TRACE("RtlVirtualUnwind: pc=0x%lx begin=0x%x handler=0x%x est=0x%lx",
             (uint64_t)rip, rf->BeginAddress, handler_rva, establisher);

    if (handler_rva == 0) return NULL;
    return (void*)(g_image_base + handler_rva);
}

/* SEH: RtlCaptureContext stub */
__attribute__((ms_abi)) void mw_RtlCaptureContext(void* ctx) {
    MW_TRACE("RtlCaptureContext()");
}

__attribute__((ms_abi)) int mw_ScrollConsoleScreenBufferA(void* h, void* sr, void* dr, void* coord, void* fill) {
    MW_TRACE("ScrollConsoleScreenBufferA()");
    return 0;
}

__attribute__((ms_abi)) int mw_SetEvent(void* h) {
    MW_TRACE("SetEvent()");
    return 1;
}

__attribute__((ms_abi)) int mw_SetFileTime(void* h, void* ct, void* at, void* wt) {
    MW_TRACE("SetFileTime()");
    return 0;
}

__attribute__((ms_abi)) void mw_SetLastError(uint32_t err) {
    g_last_error = err;
    MW_TRACE("SetLastError(%u)", err);
}

__attribute__((ms_abi)) int mw_SetProcessAffinityMask(void* h, uint64_t mask) {
    MW_TRACE("SetProcessAffinityMask()");
    return 1;
}

__attribute__((ms_abi)) int mw_SetThreadContext(void* h, void* ctx) {
    MW_TRACE("SetThreadContext()");
    return 0;
}

__attribute__((ms_abi)) int mw_SetThreadPriority(void* h, int prio) {
    MW_TRACE("SetThreadPriority()");
    return 1;
}

__attribute__((ms_abi)) void* mw_SetUnhandledExceptionFilter(void* handler) {
    MW_TRACE("SetUnhandledExceptionFilter(handler=%p)", handler);
    void* old = g_unhandled_exception_filter;
    g_unhandled_exception_filter = handler;
    return old;
}

__attribute__((ms_abi)) void mw_Sleep(uint32_t ms) {
    MW_TRACE("Sleep(%u)", ms);
    if (ms > 0) usleep((useconds_t)ms * 1000);
}

__attribute__((ms_abi)) uint32_t mw_SuspendThread(void* h) {
    MW_TRACE("SuspendThread()");
    return 0;
}

/* TLS */
static void* g_tls_values[64] = {0};

__attribute__((ms_abi)) uint32_t mw_TlsAlloc(void) {
    static uint32_t next = 0;
    uint32_t idx = next++;
    if (idx < 64) {
        MW_TRACE("TlsAlloc() = %u", idx);
        return idx;
    }
    return (uint32_t)(-1);
}

__attribute__((ms_abi)) void* mw_TlsGetValue(uint32_t idx) {
    void* val = (idx < 64) ? g_tls_values[idx] : NULL;
    MW_TRACE("TlsGetValue(%u) = %p", idx, val);
    return val;
}

__attribute__((ms_abi)) int mw_TlsSetValue(uint32_t idx, void* val) {
    if (idx < 64) g_tls_values[idx] = val;
    MW_TRACE("TlsSetValue(%u, %p)", idx, val);
    return 1;
}

__attribute__((ms_abi)) int mw_VirtualProtect(void* addr, size_t size, uint32_t prot, uint32_t* old) {
    MW_TRACE("VirtualProtect(addr=%p, size=0x%zx, prot=0x%x)", addr, size, prot);
    int linux_prot = PROT_READ | PROT_WRITE;
    if (prot & 0x01) linux_prot = PROT_READ;
    if (prot & 0x02) linux_prot |= PROT_READ;
    if (prot & 0x04) linux_prot |= PROT_WRITE;
    if (prot & 0x20) linux_prot |= PROT_EXEC;
    if (old) *old = 0x04; /* PAGE_READWRITE */
    /* For --version, VirtualProtect on our own mapped memory should work */
    if (mprotect(addr, size, linux_prot) == 0) return 1;
    return 0;
}

__attribute__((ms_abi)) size_t mw_VirtualQuery(void* addr, void* info, size_t len) {
    MW_TRACE("VirtualQuery(addr=%p)", addr);
    if (info && len >= 48) {
        memset(info, 0, 48);
        *(uint64_t*)((uint8_t*)info + 0) = (uint64_t)(uintptr_t)addr; /* BaseAddress */
        *(uint64_t*)((uint8_t*)info + 8) = 0x1000;                    /* AllocationBase */
        *(uint32_t*)((uint8_t*)info + 16) = 0x1000;                   /* AllocationProtect */
        *(uint64_t*)((uint8_t*)info + 24) = g_image_size;              /* RegionSize */
        *(uint32_t*)((uint8_t*)info + 32) = 0x04;                     /* State = MEM_COMMIT */
        *(uint32_t*)((uint8_t*)info + 36) = 0x40 | 0x20;             /* Protect = PAGE_READWRITE | RWX */
        *(uint32_t*)((uint8_t*)info + 40) = 0x1000;                   /* Type = MEM_PRIVATE */
        return 48;
    }
    return 0;
}

__attribute__((ms_abi)) uint32_t mw_WaitForSingleObject(void* h, uint32_t ms) {
    MW_TRACE("WaitForSingleObject(handle=%p, ms=%u)", h, ms);
    return 0; /* WAIT_OBJECT_0 */
}

__attribute__((ms_abi)) uint32_t mw_WaitForMultipleObjects(uint32_t count, void** handles,
    int all, uint32_t ms) {
    MW_TRACE("WaitForMultipleObjects(count=%u, all=%d, ms=%u)", count, all, ms);
    return 0; /* WAIT_OBJECT_0 */
}

__attribute__((ms_abi)) int mw_WideCharToMultiByte(uint32_t cp, uint32_t flags,
    const wchar_t* src, int srclen, char* dst, int dstlen, char* defc, int* used) {
    MW_TRACE("WideCharToMultiByte(cp=%u, srclen=%d, dstlen=%d)", cp, srclen, dstlen);
    if (!src) return 0;
    int wlen = srclen;
    if (wlen < 0) wlen = (int)wcslen(src);
    if (dstlen == 0) return wlen;
    int mlen = wcstombs(dst, src, dstlen);
    return (mlen < 0) ? 0 : mlen;
}

/* ============================================================
 * msvcrt.dll Stubs (ms_abi calling convention)
 * ============================================================ */

/* CRT Data pointers */
static char* g_acmdln_ptr = NULL;     /* pointer to ANSI command line */
static char** g_initenv_ptr = NULL;   /* pointer to env array */
static int  g_errno_storage = 0;   /* our errno mirror */
static int* g_errno_ptr = &g_errno_storage;

/* These are DATA imports - the IAT slot gets the ADDRESS of the data,
 * not a function pointer. Handled specially in import resolution. */

/* CRT init functions */
__attribute__((ms_abi)) void mw___set_app_type(int type) {
    g_app_type = type;
    MW_TRACE("__set_app_type(%d)", type);
}

__attribute__((ms_abi)) void mw___setusermatherr(void* handler) {
    MW_TRACE("__setusermatherr(handler=%p)", handler);
}

__attribute__((ms_abi)) int mw___getmainargs(int* pargc, char*** pargv, char*** penvp,
    int dowildcard, void* startup) {
    MW_TRACE("__getmainargs(argc=%p, argv=%p, envp=%p)", pargc, pargv, penvp);
    if (pargc) *pargc = g_argc;
    if (pargv) *pargv = g_argv;
    if (penvp) *penvp = g_initenv_ptr;
    return 0;
}

__attribute__((ms_abi)) int mw___lconv_init(void) {
    MW_TRACE("__lconv_init()");
    localeconv(); /* initialize locale data */
    return 0;
}

__attribute__((ms_abi)) int mw___C_specific_handler(void* exc, void* frame, void* ctx,
    void* disp, void* history) {
    MW_TRACE("__C_specific_handler()");
    return 0;
}

/* _initterm: walk a table of function pointers, call each non-NULL one.
 * The table is terminated by a NULL entry.
 * Returns the return value of the last non-NULL function called.
 */
__attribute__((ms_abi)) int mw__initterm(void** start, void** end) {
    MW_TRACE("_initterm(start=%p, end=%p)", start, end);
    int ret = 0;
    if (!start || !end) return 0;
    for (void** p = start; p < end; p++) {
        if (*p != NULL) {
            MW_TRACE("_initterm: calling %p", *p);
            /* Call the function pointer using ms_abi since these are
             * internal CRT functions compiled with MSVC */
            int (*fn)(void) = (int (*)(void))*p;
            ret = fn();
        }
    }
    return ret;
}

__attribute__((ms_abi)) void mw__amsg_exit(int msg) {
    MW_TRACE("_amsg_exit(msg=%d)", msg);
    fprintf(stderr, "Runtime Error: message id %d\n", msg);
    exit(255);
}

__attribute__((ms_abi)) void mw__cexit(void) {
    MW_TRACE("_cexit()");
    fflush(stdout);
    fflush(stderr);
}

__attribute__((ms_abi)) void* mw___iob_func(void) {
    /* Returns pointer to array of FILE* (stdin, stdout, stderr, ...) */
    static void* iob_array[3];
    iob_array[0] = stdin;
    iob_array[1] = stdout;
    iob_array[2] = stderr;
    MW_TRACE("__iob_func() = %p", iob_array);
    return iob_array;
}

/* Data accessor functions */
__attribute__((ms_abi)) unsigned int mw___lc_codepage_func(void) {
    MW_TRACE("__lc_codepage_func() = %u", 0);
    return 0; /* CP_ACP */
}

__attribute__((ms_abi)) int mw___mb_cur_max_func(void) {
    MW_TRACE("__mb_cur_max_func() = 1");
    return 1;
}

/* printf - variadic, needs special handling */
__attribute__((ms_abi)) int mw_printf(const char* fmt, ...) {
    MW_TRACE("printf(fmt=%s)", fmt ? fmt : "NULL");
    va_list ap;
    va_start(ap, fmt);
    int ret = vprintf(fmt, ap);
    va_end(ap);
    return ret;
}

/* fprintf - variadic */
__attribute__((ms_abi)) int mw_fprintf(void* file, const char* fmt, ...) {
    MW_TRACE("fprintf(file=%p, fmt=%s)", file, fmt ? fmt : "NULL");
    va_list ap;
    va_start(ap, fmt);
    /* Use actual FILE* if it's a real glibc FILE */
    FILE* f = (file == stdout || file == stderr || file == stdin) ? (FILE*)file : stdout;
    int ret = vfprintf(f, fmt, ap);
    va_end(ap);
    return ret;
}

/* vfprintf - the va_list is in MS ABI format */
__attribute__((ms_abi)) int mw_vfprintf(void* file, const char* fmt, void* ap) {
    MW_TRACE("vfprintf(file=%p, fmt=%s)", file, fmt ? fmt : "NULL");
    /* The ap argument is a pointer to ms_abi va_list.
     * Since ms_abi va_list is just a char* pointing to stack args,
     * and the first 4 args are in registers (already consumed by fmt),
     * the remaining args are on the stack.
     * For simple format strings (few args), this may work.
     * For complex cases, we'd need to rebuild the va_list.
     */
    FILE* f = (file == stdout || file == stderr || file == stdin) ? (FILE*)file : stdout;
    /* Use a workaround: format to buffer first */
    char buf[8192];
    int ret = vsnprintf(buf, sizeof(buf), fmt, *(va_list*)ap);
    if (ret > 0) {
        fwrite(buf, 1, ret, f);
    }
    return ret;
}

/* stdio stubs */
__attribute__((ms_abi)) int mw_fflush(void* file) {
    MW_TRACE("fflush(file=%p)", file);
    FILE* f = (file == NULL) ? NULL :
              (file == stdout || file == stderr || file == stdin) ? (FILE*)file : stdout;
    return fflush(f);
}

__attribute__((ms_abi)) int mw_fputs(const char* str, void* file) {
    MW_TRACE("fputs(str=%s)", str ? str : "NULL");
    FILE* f = (file == stdout || file == stderr || file == stdin) ? (FILE*)file : stdout;
    return fputs(str, f);
}

__attribute__((ms_abi)) int mw_fputc(int ch, void* file) {
    MW_TRACE("fputc(ch=0x%x)", ch);
    FILE* f = (file == stdout || file == stderr || file == stdin) ? (FILE*)file : stdout;
    return fputc(ch, f);
}

__attribute__((ms_abi)) size_t mw_fwrite(const void* buf, size_t sz, size_t cnt, void* file) {
    MW_TRACE("fwrite(sz=%zu, cnt=%zu)", sz, cnt);
    FILE* f = (file == stdout || file == stderr || file == stdin) ? (FILE*)file : stdout;
    return fwrite(buf, sz, cnt, f);
}

__attribute__((ms_abi)) int mw_getc(void* file) {
    MW_TRACE("getc()");
    return EOF;
}

__attribute__((ms_abi)) int mw_ungetc(int ch, void* file) {
    MW_TRACE("ungetc(ch=0x%x)", ch);
    return EOF;
}

__attribute__((ms_abi)) int mw_puts(const char* str) {
    MW_TRACE("puts(str=%s)", str ? str : "NULL");
    return puts(str);
}

/* Memory */
__attribute__((ms_abi)) void* mw_malloc(size_t size) {
    void* p = malloc(size);
    MW_TRACE("malloc(%zu) = %p", size, p);
    return p;
}

__attribute__((ms_abi)) void mw_free(void* ptr) {
    MW_TRACE("free(%p)", ptr);
    free(ptr);
}

__attribute__((ms_abi)) void* mw_realloc(void* ptr, size_t size) {
    void* p = realloc(ptr, size);
    MW_TRACE("realloc(%p, %zu) = %p", ptr, size, p);
    return p;
}

__attribute__((ms_abi)) void* mw_calloc(size_t n, size_t sz) {
    void* p = calloc(n, sz);
    MW_TRACE("calloc(%zu, %zu) = %p", n, sz, p);
    return p;
}

/* String functions */
__attribute__((ms_abi)) size_t mw_strlen(const char* s) {
    return strlen(s);
}
__attribute__((ms_abi)) int mw_strcmp(const char* a, const char* b) {
    return strcmp(a, b);
}
__attribute__((ms_abi)) char* mw_strcpy(char* d, const char* s) {
    return strcpy(d, s);
}
__attribute__((ms_abi)) char* mw_strchr(const char* s, int c) {
    return (char*)strchr(s, c);
}
__attribute__((ms_abi)) char* mw_strrchr(const char* s, int c) {
    return (char*)strrchr(s, c);
}
__attribute__((ms_abi)) char* mw_strstr(const char* h, const char* n) {
    return (char*)strstr(h, n);
}
__attribute__((ms_abi)) size_t mw_strcspn(const char* s, const char* r) {
    return strcspn(s, r);
}
__attribute__((ms_abi)) int mw_strncmp(const char* a, const char* b, size_t n) {
    return strncmp(a, b, n);
}
__attribute__((ms_abi)) int mw__stricmp(const char* a, const char* b) {
    return strcasecmp(a, b);
}
__attribute__((ms_abi)) char* mw__strdup(const char* s) {
    return strdup(s);
}
__attribute__((ms_abi)) void* mw_memchr(const void* s, int c, size_t n) {
    return (void*)memchr(s, c, n);
}
__attribute__((ms_abi)) int mw_memcmp(const void* a, const void* b, size_t n) {
    return memcmp(a, b, n);
}
__attribute__((ms_abi)) void* mw_memcpy(void* d, const void* s, size_t n) {
    return memcpy(d, s, n);
}
__attribute__((ms_abi)) void* mw_memmove(void* d, const void* s, size_t n) {
    return memmove(d, s, n);
}
__attribute__((ms_abi)) void* mw_memset(void* s, int c, size_t n) {
    return memset(s, c, n);
}

/* Conversion */
__attribute__((ms_abi)) int mw_atoi(const char* s) { return atoi(s); }

__attribute__((ms_abi)) long mw_strtol(const char* s, char** end, int base) {
    return strtol(s, end, base);
}

__attribute__((ms_abi)) unsigned long mw_strtoul(const char* s, char** end, int base) {
    return strtoul(s, end, base);
}

__attribute__((ms_abi)) char* mw__ultoa(unsigned long val, char* buf, int base) {
    unsigned long v = val;
    char tmp[33];
    int i = 0;
    if (v == 0) { buf[0] = '0'; buf[1] = 0; return buf; }
    while (v > 0) { tmp[i++] = "0123456789abcdef"[v % base]; v /= base; }
    int j = 0;
    while (i > 0) buf[j++] = tmp[--i];
    buf[j] = 0;
    return buf;
}

__attribute__((ms_abi)) int mw_tolower(int c) { return tolower(c); }
__attribute__((ms_abi)) size_t mw_wcstombs(char* d, const wchar_t* s, size_t n) {
    return wcstombs(d, s, n);
}
__attribute__((ms_abi)) size_t mw_wcslen(const wchar_t* s) { return wcslen(s); }

/* Character classification */
__attribute__((ms_abi)) int mw_islower(int c) { return islower(c); }
__attribute__((ms_abi)) int mw_isspace(int c) { return isspace(c); }
__attribute__((ms_abi)) int mw_isupper(int c) { return isupper(c); }
__attribute__((ms_abi)) int mw_isxdigit(int c) { return isxdigit(c); }

/* Time */
__attribute__((ms_abi)) clock_t mw_clock(void) { return clock(); }

/* File I/O - low level MSVCRT functions */
__attribute__((ms_abi)) int mw__open(const char* path, int flags, ...) {
    MW_TRACE("_open(path=%s, flags=0x%x)", path ? path : "NULL", flags);
    int mode = 0;
    va_list ap;
    va_start(ap, flags);
    if (flags & 0x0100) mode = va_arg(ap, int); /* _O_CREAT */
    va_end(ap);
    /* Map MSVC flags to POSIX */
    int oflags = ((flags & 0x0003) == 0) ? O_RDONLY : 0;
    if (flags & 0x0001) oflags = O_WRONLY;
    if (flags & 0x0002) oflags = O_RDWR;
    if (flags & 0x0080) oflags |= O_APPEND;
    if (flags & 0x0100) oflags |= O_CREAT;
    if (flags & 0x0200) oflags |= O_TRUNC;
    if (flags & 0x0400) oflags |= O_EXCL;
    /* _O_TEXT (0x4000) and _O_BINARY (0x8000) ignored on Linux */
    int fd = open(path, oflags, mode);
    return fd;
}

__attribute__((ms_abi)) int mw__read(int fd, void* buf, unsigned int cnt) {
    MW_TRACE("_read(fd=%d, cnt=%u)", fd, cnt);
    return (int)read(fd, buf, cnt);
}

__attribute__((ms_abi)) int mw__write(int fd, const void* buf, unsigned int cnt) {
    MW_TRACE("_write(fd=%d, cnt=%u)", fd, cnt);
    int ret = (int)write(fd, buf, cnt);
    return ret;
}

__attribute__((ms_abi)) int mw__close(int fd) {
    MW_TRACE("_close(fd=%d)", fd);
    return close(fd);
}

__attribute__((ms_abi)) int mw__dup(int fd) {
    MW_TRACE("_dup(fd=%d)", fd);
    return dup(fd);
}

__attribute__((ms_abi)) int mw__isatty(int fd) {
    MW_TRACE("_isatty(fd=%d)", fd);
    return isatty(fd);
}

__attribute__((ms_abi)) int mw__fileno(void* file) {
    MW_TRACE("_fileno(file=%p)", file);
    if (file == stdin) return 0;
    if (file == stdout) return 1;
    if (file == stderr) return 2;
    return -1;
}

__attribute__((ms_abi)) int64_t mw__get_osfhandle(int fd) {
    MW_TRACE("_get_osfhandle(fd=%d)", fd);
    return (int64_t)(uintptr_t)fd;
}

__attribute__((ms_abi)) int mw__setmode(int fd, int mode) {
    MW_TRACE("_setmode(fd=%d, mode=%d)", fd, mode);
    return 0;
}

__attribute__((ms_abi)) int mw__sopen(const char* path, int flags, int shflag, int pmode) {
    MW_TRACE("_sopen(path=%s, flags=0x%x)", path ? path : "NULL", flags);
    return mw__open(path, flags, pmode);
}

__attribute__((ms_abi)) int mw__wopen(const wchar_t* path, int flags, ...) {
    MW_TRACE("_wopen()");
    char buf[1024];
    wcstombs(buf, path, sizeof(buf));
    return mw__open(buf, flags, 0);
}

/* stat */
#include <sys/types.h>
#include <sys/stat.h>

__attribute__((ms_abi)) int mw__fstat64(int fd, void* st) {
    MW_TRACE("_fstat64(fd=%d)", fd);
    struct stat64 st64;
    int ret = fstat64(fd, &st64);
    if (ret == 0 && st) {
        /* Map to MSVC _stat64 structure */
        memset(st, 0, 128);
        *(uint32_t*)((uint8_t*)st + 0) = st64.st_mode;
        *(int64_t*)((uint8_t*)st + 8) = st64.st_size;
        *(int64_t*)((uint8_t*)st + 24) = st64.st_atime;
        *(int64_t*)((uint8_t*)st + 32) = st64.st_mtime;
        *(int64_t*)((uint8_t*)st + 40) = st64.st_ctime;
    }
    return ret;
}

__attribute__((ms_abi)) int mw__stat64(const char* path, void* st) {
    MW_TRACE("_stat64(path=%s)", path ? path : "NULL");
    struct stat64 st64;
    int ret = stat64(path, &st64);
    if (ret == 0 && st) {
        memset(st, 0, 128);
        *(uint32_t*)((uint8_t*)st + 0) = st64.st_mode;
        *(int64_t*)((uint8_t*)st + 8) = st64.st_size;
        *(int64_t*)((uint8_t*)st + 24) = st64.st_atime;
        *(int64_t*)((uint8_t*)st + 32) = st64.st_mtime;
        *(int64_t*)((uint8_t*)st + 40) = st64.st_ctime;
    }
    return ret;
}

__attribute__((ms_abi)) int64_t mw__lseeki64(int fd, int64_t off, int whence) {
    MW_TRACE("_lseeki64(fd=%d, off=%ld, whence=%d)", fd, (long)off, whence);
    return lseek64(fd, off, whence);
}

__attribute__((ms_abi)) int mw_rename(const char* old, const char* new) {
    MW_TRACE("rename(old=%s, new=%s)", old, new);
    return rename(old, new);
}

__attribute__((ms_abi)) int mw__unlink(const char* path) {
    MW_TRACE("_unlink(path=%s)", path ? path : "NULL");
    return unlink(path);
}

__attribute__((ms_abi)) int mw__rmdir(const char* path) {
    MW_TRACE("_rmdir(path=%s)", path ? path : "NULL");
    return rmdir(path);
}

__attribute__((ms_abi)) int mw__chmod(const char* path, int mode) {
    MW_TRACE("_chmod(path=%s, mode=%d)", path ? path : "NULL", mode);
    return chmod(path, mode);
}

/* Time functions */
__attribute__((ms_abi)) time_t mw__time64(time_t* t) {
    return time(t);
}

__attribute__((ms_abi)) void* mw__gmtime64(const time_t* t) {
    return (void*)gmtime(t);
}

__attribute__((ms_abi)) void* mw__localtime64(const time_t* t) {
    return (void*)localtime(t);
}

/* Misc */
__attribute__((ms_abi)) void mw_exit(int code) {
    MW_TRACE("exit(%d)", code);
    fflush(stdout);
    fflush(stderr);
    exit(code);
}

__attribute__((ms_abi)) void mw_abort(void) {
    MW_TRACE("abort()");
    abort();
}

__attribute__((ms_abi)) void (*mw_signal(int sig, void (*handler)(int)))(int) {
    MW_TRACE("signal(sig=%d)", sig);
    return signal(sig, handler);
}

__attribute__((ms_abi)) void mw_longjmp(void* buf, int val) {
    MW_TRACE("longjmp(val=%d)", val);
    /* jmp_buf on glibc is struct __jmp_buf_tag[1] */
    siglongjmp(*(sigjmp_buf*)buf, val);
}

__attribute__((ms_abi)) int mw__setjmp(void* buf) {
    MW_TRACE("_setjmp()");
    return sigsetjmp(*(sigjmp_buf*)buf, 0);
}

__attribute__((ms_abi)) int mw__kbhit(void) { return 0; }

__attribute__((ms_abi)) char* mw_getenv(const char* name) {
    return getenv(name);
}

__attribute__((ms_abi)) void* mw_localeconv(void) {
    return (void*)localeconv();
}

__attribute__((ms_abi)) void mw_qsort(void* base, size_t n, size_t sz, int (*cmp)(const void*, const void*)) {
    qsort(base, n, sz, cmp);
}

__attribute__((ms_abi)) int mw_rand(void) { return rand(); }
__attribute__((ms_abi)) void mw_srand(unsigned int seed) { srand(seed); }

__attribute__((ms_abi)) int mw__onexit(void (*func)(void)) {
    MW_TRACE("_onexit(func=%p)", func);
    return atexit(func);
}

__attribute__((ms_abi)) unsigned long mw__beginthreadex(void* sec, unsigned int stk,
    void* (*start)(void*), void* arg, int init, unsigned int* tid) {
    MW_TRACE("_beginthreadex()");
    if (tid) *tid = 5678;
    return 0;
}

__attribute__((ms_abi)) void mw__endthreadex(unsigned int code) {
    MW_TRACE("_endthreadex(%u)", code);
    pthread_exit((void*)(uintptr_t)code);
}

__attribute__((ms_abi)) void mw__lock(int locknum) {
    MW_TRACE("_lock(%d)", locknum);
}

__attribute__((ms_abi)) void mw__unlock(int locknum) {
    MW_TRACE("_unlock(%d)", locknum);
}

/* ============================================================
 * Import Dispatch Table
 * Maps import name → function pointer (for code imports)
 * Data imports are handled separately.
 * ============================================================ */

typedef struct {
    const char* dll;
    const char* name;
    void*       func;  /* For code imports: ms_abi function pointer */
    int         is_data;
} ImportEntry;

#define ENTRY(dll, name, func) { dll, name, (void*)func, 0 }
#define DATA_ENTRY(dll, name, addr) { dll, name, (void*)addr, 1 }

static ImportEntry g_import_table[] = {
    /* KERNEL32.DLL */
    ENTRY("KERNEL32.DLL", "AddVectoredExceptionHandler",        mw_AddVectoredExceptionHandler),
    ENTRY("KERNEL32.DLL", "CloseHandle",                        mw_CloseHandle),
    ENTRY("KERNEL32.DLL", "CreateEventA",                       mw_CreateEventA),
    ENTRY("KERNEL32.DLL", "CreateSemaphoreA",                   mw_CreateSemaphoreA),
    ENTRY("KERNEL32.DLL", "DebugBreak",                         mw_DebugBreak),
    ENTRY("KERNEL32.DLL", "DeleteCriticalSection",              mw_DeleteCriticalSection),
    ENTRY("KERNEL32.DLL", "DuplicateHandle",                    mw_DuplicateHandle),
    ENTRY("KERNEL32.DLL", "EnterCriticalSection",               mw_EnterCriticalSection),
    ENTRY("KERNEL32.DLL", "GetConsoleCursorInfo",               mw_GetConsoleCursorInfo),
    ENTRY("KERNEL32.DLL", "GetConsoleMode",                      mw_GetConsoleMode),
    ENTRY("KERNEL32.DLL", "GetConsoleScreenBufferInfo",          mw_GetConsoleScreenBufferInfo),
    ENTRY("KERNEL32.DLL", "GetCurrentProcess",                   mw_GetCurrentProcess),
    ENTRY("KERNEL32.DLL", "GetCurrentProcessId",                 mw_GetCurrentProcessId),
    ENTRY("KERNEL32.DLL", "GetCurrentThread",                   mw_GetCurrentThread),
    ENTRY("KERNEL32.DLL", "GetCurrentThreadId",                 mw_GetCurrentThreadId),
    ENTRY("KERNEL32.DLL", "GetFileTime",                         mw_GetFileTime),
    ENTRY("KERNEL32.DLL", "GetHandleInformation",               mw_GetHandleInformation),
    ENTRY("KERNEL32.DLL", "GetLastError",                        mw_GetLastError),
    ENTRY("KERNEL32.DLL", "GetModuleHandleA",                    mw_GetModuleHandleA),
    ENTRY("KERNEL32.DLL", "GetProcAddress",                      mw_GetProcAddress),
    ENTRY("KERNEL32.DLL", "GetProcessAffinityMask",             mw_GetProcessAffinityMask),
    ENTRY("KERNEL32.DLL", "GetStartupInfoA",                     mw_GetStartupInfoA),
    ENTRY("KERNEL32.DLL", "GetStdHandle",                       mw_GetStdHandle),
    ENTRY("KERNEL32.DLL", "GetSystemTimeAsFileTime",             mw_GetSystemTimeAsFileTime),
    ENTRY("KERNEL32.DLL", "GetThreadContext",                    mw_GetThreadContext),
    ENTRY("KERNEL32.DLL", "GetThreadPriority",                   mw_GetThreadPriority),
    ENTRY("KERNEL32.DLL", "GetTickCount",                        mw_GetTickCount),
    ENTRY("KERNEL32.DLL", "InitializeCriticalSection",           mw_InitializeCriticalSection),
    ENTRY("KERNEL32.DLL", "IsDBCSLeadByteEx",                    mw_IsDBCSLeadByteEx),
    ENTRY("KERNEL32.DLL", "IsDebuggerPresent",                   mw_IsDebuggerPresent),
    ENTRY("KERNEL32.DLL", "LeaveCriticalSection",               mw_LeaveCriticalSection),
    ENTRY("KERNEL32.DLL", "MultiByteToWideChar",                 mw_MultiByteToWideChar),
    ENTRY("KERNEL32.DLL", "OpenProcess",                         mw_OpenProcess),
    ENTRY("KERNEL32.DLL", "OutputDebugStringA",                  mw_OutputDebugStringA),
    ENTRY("KERNEL32.DLL", "QueryPerformanceCounter",             mw_QueryPerformanceCounter),
    ENTRY("KERNEL32.DLL", "QueryPerformanceFrequency",           mw_QueryPerformanceFrequency),
    ENTRY("KERNEL32.DLL", "RaiseException",                      mw_RaiseException),
    ENTRY("KERNEL32.DLL", "ReleaseSemaphore",                    mw_ReleaseSemaphore),
    ENTRY("KERNEL32.DLL", "RemoveVectoredExceptionHandler",       mw_RemoveVectoredExceptionHandler),
    ENTRY("KERNEL32.DLL", "ResetEvent",                          mw_ResetEvent),
    ENTRY("KERNEL32.DLL", "ResumeThread",                        mw_ResumeThread),
    ENTRY("KERNEL32.DLL", "RtlCaptureContext",                   mw_RtlCaptureContext),
    ENTRY("KERNEL32.DLL", "RtlLookupFunctionEntry",              mw_RtlLookupFunctionEntry),
    ENTRY("KERNEL32.DLL", "RtlUnwindEx",                         mw_RtlUnwindEx),
    ENTRY("KERNEL32.DLL", "RtlVirtualUnwind",                    mw_RtlVirtualUnwind),
    ENTRY("KERNEL32.DLL", "ScrollConsoleScreenBufferA",          mw_ScrollConsoleScreenBufferA),
    ENTRY("KERNEL32.DLL", "SetConsoleCursorInfo",                mw_SetConsoleCursorInfo),
    ENTRY("KERNEL32.DLL", "SetConsoleCursorPosition",            mw_SetConsoleCursorPosition),
    ENTRY("KERNEL32.DLL", "SetConsoleTextAttribute",             mw_SetConsoleTextAttribute),
    ENTRY("KERNEL32.DLL", "SetEvent",                            mw_SetEvent),
    ENTRY("KERNEL32.DLL", "SetFileTime",                         mw_SetFileTime),
    ENTRY("KERNEL32.DLL", "SetLastError",                        mw_SetLastError),
    ENTRY("KERNEL32.DLL", "SetProcessAffinityMask",              mw_SetProcessAffinityMask),
    ENTRY("KERNEL32.DLL", "SetThreadContext",                    mw_SetThreadContext),
    ENTRY("KERNEL32.DLL", "SetThreadPriority",                   mw_SetThreadPriority),
    ENTRY("KERNEL32.DLL", "SetUnhandledExceptionFilter",         mw_SetUnhandledExceptionFilter),
    ENTRY("KERNEL32.DLL", "Sleep",                               mw_Sleep),
    ENTRY("KERNEL32.DLL", "SuspendThread",                       mw_SuspendThread),
    ENTRY("KERNEL32.DLL", "TlsAlloc",                            mw_TlsAlloc),
    ENTRY("KERNEL32.DLL", "TlsGetValue",                         mw_TlsGetValue),
    ENTRY("KERNEL32.DLL", "TlsSetValue",                         mw_TlsSetValue),
    ENTRY("KERNEL32.DLL", "TryEnterCriticalSection",             mw_TryEnterCriticalSection),
    ENTRY("KERNEL32.DLL", "VirtualProtect",                      mw_VirtualProtect),
    ENTRY("KERNEL32.DLL", "VirtualQuery",                        mw_VirtualQuery),
    ENTRY("KERNEL32.DLL", "WaitForMultipleObjects",              mw_WaitForMultipleObjects),
    ENTRY("KERNEL32.DLL", "WaitForSingleObject",                 mw_WaitForSingleObject),
    ENTRY("KERNEL32.DLL", "WideCharToMultiByte",                 mw_WideCharToMultiByte),
    ENTRY("KERNEL32.DLL", "WriteConsoleOutputA",                 mw_WriteConsoleOutputA),

    /* msvcrt.dll — Code imports */
    ENTRY("msvcrt.dll", "__C_specific_handler",   mw___C_specific_handler),
    ENTRY("msvcrt.dll", "__getmainargs",         mw___getmainargs),
    ENTRY("msvcrt.dll", "__iob_func",            mw___iob_func),
    ENTRY("msvcrt.dll", "__lconv_init",          mw___lconv_init),
    ENTRY("msvcrt.dll", "___lc_codepage_func",    mw___lc_codepage_func),
    ENTRY("msvcrt.dll", "___mb_cur_max_func",     mw___mb_cur_max_func),
    ENTRY("msvcrt.dll", "__set_app_type",        mw___set_app_type),
    ENTRY("msvcrt.dll", "__setusermatherr",      mw___setusermatherr),
    ENTRY("msvcrt.dll", "_amsg_exit",            mw__amsg_exit),
    ENTRY("msvcrt.dll", "_beginthreadex",        mw__beginthreadex),
    ENTRY("msvcrt.dll", "_cexit",                mw__cexit),
    ENTRY("msvcrt.dll", "_chmod",                mw__chmod),
    ENTRY("msvcrt.dll", "_close",                mw__close),
    ENTRY("msvcrt.dll", "_dup",                  mw__dup),
    ENTRY("msvcrt.dll", "_endthreadex",          mw__endthreadex),
    ENTRY("msvcrt.dll", "_fstat64",              mw__fstat64),
    ENTRY("msvcrt.dll", "_fileno",               mw__fileno),
    ENTRY("msvcrt.dll", "_get_osfhandle",        mw__get_osfhandle),
    ENTRY("msvcrt.dll", "_gmtime64",             mw__gmtime64),
    ENTRY("msvcrt.dll", "_initterm",             mw__initterm),
    ENTRY("msvcrt.dll", "_isatty",               mw__isatty),
    ENTRY("msvcrt.dll", "_kbhit",                mw__kbhit),
    ENTRY("msvcrt.dll", "_localtime64",          mw__localtime64),
    ENTRY("msvcrt.dll", "_lock",                 mw__lock),
    ENTRY("msvcrt.dll", "_lseeki64",             mw__lseeki64),
    ENTRY("msvcrt.dll", "_onexit",               mw__onexit),
    ENTRY("msvcrt.dll", "_open",                 mw__open),
    ENTRY("msvcrt.dll", "_read",                 mw__read),
    ENTRY("msvcrt.dll", "_rename",               mw_rename),
    ENTRY("msvcrt.dll", "_rmdir",                mw__rmdir),
    ENTRY("msvcrt.dll", "_sopen",                mw__sopen),
    ENTRY("msvcrt.dll", "_setmode",              mw__setmode),
    ENTRY("msvcrt.dll", "_setjmp",               mw__setjmp),
    ENTRY("msvcrt.dll", "_stat64",               mw__stat64),
    ENTRY("msvcrt.dll", "_strdup",               mw__strdup),
    ENTRY("msvcrt.dll", "_stricmp",              mw__stricmp),
    ENTRY("msvcrt.dll", "_time64",               mw__time64),
    ENTRY("msvcrt.dll", "_ultoa",                mw__ultoa),
    ENTRY("msvcrt.dll", "_unlink",               mw__unlink),
    ENTRY("msvcrt.dll", "_unlock",               mw__unlock),
    ENTRY("msvcrt.dll", "_wopen",                mw__wopen),
    ENTRY("msvcrt.dll", "_write",                mw__write),
    ENTRY("msvcrt.dll", "abort",                 mw_abort),
    ENTRY("msvcrt.dll", "atoi",                  mw_atoi),
    ENTRY("msvcrt.dll", "calloc",                mw_calloc),
    ENTRY("msvcrt.dll", "clock",                 mw_clock),
    ENTRY("msvcrt.dll", "exit",                  mw_exit),
    ENTRY("msvcrt.dll", "fflush",                mw_fflush),
    ENTRY("msvcrt.dll", "fprintf",               mw_fprintf),
    ENTRY("msvcrt.dll", "fputc",                 mw_fputc),
    ENTRY("msvcrt.dll", "fputs",                 mw_fputs),
    ENTRY("msvcrt.dll", "free",                  mw_free),
    ENTRY("msvcrt.dll", "fwrite",                mw_fwrite),
    ENTRY("msvcrt.dll", "getc",                  mw_getc),
    ENTRY("msvcrt.dll", "getenv",                mw_getenv),
    ENTRY("msvcrt.dll", "islower",               mw_islower),
    ENTRY("msvcrt.dll", "isspace",               mw_isspace),
    ENTRY("msvcrt.dll", "isupper",               mw_isupper),
    ENTRY("msvcrt.dll", "isxdigit",              mw_isxdigit),
    ENTRY("msvcrt.dll", "localeconv",            mw_localeconv),
    ENTRY("msvcrt.dll", "longjmp",               mw_longjmp),
    ENTRY("msvcrt.dll", "malloc",                mw_malloc),
    ENTRY("msvcrt.dll", "memchr",                mw_memchr),
    ENTRY("msvcrt.dll", "memcmp",                mw_memcmp),
    ENTRY("msvcrt.dll", "memcpy",                mw_memcpy),
    ENTRY("msvcrt.dll", "memmove",               mw_memmove),
    ENTRY("msvcrt.dll", "memset",                mw_memset),
    ENTRY("msvcrt.dll", "printf",                mw_printf),
    ENTRY("msvcrt.dll", "puts",                  mw_puts),
    ENTRY("msvcrt.dll", "qsort",                 mw_qsort),
    ENTRY("msvcrt.dll", "rand",                  mw_rand),
    ENTRY("msvcrt.dll", "realloc",               mw_realloc),
    ENTRY("msvcrt.dll", "rename",                mw_rename),
    ENTRY("msvcrt.dll", "signal",                mw_signal),
    ENTRY("msvcrt.dll", "srand",                 mw_srand),
    ENTRY("msvcrt.dll", "strchr",                mw_strchr),
    ENTRY("msvcrt.dll", "strcmp",                mw_strcmp),
    ENTRY("msvcrt.dll", "strcpy",                mw_strcpy),
    ENTRY("msvcrt.dll", "strcspn",               mw_strcspn),
    ENTRY("msvcrt.dll", "strerror",              (void*)strerror),
    ENTRY("msvcrt.dll", "strlen",                mw_strlen),
    ENTRY("msvcrt.dll", "strncmp",               mw_strncmp),
    ENTRY("msvcrt.dll", "strrchr",               mw_strrchr),
    ENTRY("msvcrt.dll", "strstr",                mw_strstr),
    ENTRY("msvcrt.dll", "strtol",                mw_strtol),
    ENTRY("msvcrt.dll", "strtoul",               mw_strtoul),
    ENTRY("msvcrt.dll", "tolower",               mw_tolower),
    ENTRY("msvcrt.dll", "ungetc",                mw_ungetc),
    ENTRY("msvcrt.dll", "vfprintf",              mw_vfprintf),
    ENTRY("msvcrt.dll", "wcstombs",               mw_wcstombs),
    ENTRY("msvcrt.dll", "wcslen",                mw_wcslen),
};

#define NUM_IMPORTS (sizeof(g_import_table) / sizeof(g_import_table[0]))

/* Data imports - resolved separately (IAT slot = address of data) */
typedef struct {
    const char* dll;
    const char* name;
    void*       data_addr; /* pointer to the actual data */
} DataImportEntry;

static DataImportEntry g_data_imports[] = {
    { "msvcrt.dll", "_acmdln",    &g_acmdln_ptr },
    { "msvcrt.dll", "__initenv",   &g_initenv_ptr },
    { "msvcrt.dll", "_commode",   &g_commode_val },
    { "msvcrt.dll", "_fmode",     &g_fmode_val },
    { "msvcrt.dll", "_errno",     &g_errno_ptr },
};
#define NUM_DATA_IMPORTS (sizeof(g_data_imports) / sizeof(g_data_imports[0]))

/* ============================================================
 * PE Loading
 * ============================================================ */

static int load_pe(const char* filepath) {
    FILE* f = fopen(filepath, "rb");
    if (!f) { fprintf(stderr, "[ERROR] Cannot open %s\n", filepath); return -1; }

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t* filedata = (uint8_t*)malloc(fsize);
    fread(filedata, 1, fsize, f);
    fclose(f);

    MW_TRACE("PE Loader: file=%s size=%ld", filepath, fsize);

    /* Parse DOS header */
    DosHeader* dos = (DosHeader*)filedata;
    if (dos->e_magic[0] != 'M' || dos->e_magic[1] != 'Z') {
        fprintf(stderr, "[ERROR] Not a valid PE file (bad DOS magic)\n");
        free(filedata);
        return -1;
    }

    uint32_t pe_off = dos->e_lfanew;
    uint8_t* pe_sig = filedata + pe_off;
    if (pe_sig[0] != 'P' || pe_sig[1] != 'E' || pe_sig[2] != 0 || pe_sig[3] != 0) {
        fprintf(stderr, "[ERROR] Not a valid PE file (bad PE signature)\n");
        free(filedata);
        return -1;
    }

    CoffHeader* coff = (CoffHeader*)(filedata + pe_off + 4);
    PeOptHeader64* opt = (PeOptHeader64*)(filedata + pe_off + 4 + sizeof(CoffHeader));

    if (opt->Magic != PE32PLUS_MAGIC) {
        fprintf(stderr, "[ERROR] Not a PE32+ file (magic=0x%x)\n", opt->Magic);
        free(filedata);
        return -1;
    }

    g_entry_point = (uint64_t)opt->AddressOfEntryPoint;
    uint64_t image_base = opt->ImageBase;
    uint32_t size_of_image = opt->SizeOfImage;
    uint32_t section_align = opt->SectionAlignment;
    uint32_t num_sections = coff->NumberOfSections;
    uint32_t num_dd = opt->NumberOfRvaAndSizes;

    MW_TRACE("PE Loader: ImageBase=0x%lx EP=0x%lx SizeOfImage=0x%x Sections=%u",
             (unsigned long)image_base, (unsigned long)g_entry_point, size_of_image, num_sections);

    /* Allocate image at ImageBase */
    g_image_base = (uint8_t*)mmap((void*)(uintptr_t)image_base, size_of_image,
        PROT_READ | PROT_WRITE | PROT_EXEC,
        MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);

    if (g_image_base == MAP_FAILED) {
        /* Try without MAP_FIXED_NOREPLACE — map anywhere */
        MW_TRACE("PE Loader: Cannot map at 0x%lx, trying any address", image_base);
        g_image_base = (uint8_t*)mmap(NULL, size_of_image,
            PROT_READ | PROT_WRITE | PROT_EXEC,
            MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (g_image_base == MAP_FAILED) {
            fprintf(stderr, "[ERROR] Cannot mmap %u bytes\n", size_of_image);
            free(filedata);
            return -1;
        }
        MW_TRACE("PE Loader: Mapped at %p instead of 0x%lx", g_image_base, image_base);
    } else {
        MW_TRACE("PE Loader: Mapped at 0x%lx (requested)", image_base);
    }
    g_image_size = size_of_image;

    /* Fix up entry point to absolute address */
    g_entry_point = (uint64_t)(uintptr_t)(g_image_base + opt->AddressOfEntryPoint);

    /* Copy headers */
    memcpy(g_image_base, filedata, opt->SizeOfHeaders);

    /* Map sections */
    uint32_t opt_hdr_size = coff->SizeOfOptionalHeader;
    SectionHeader* sections = (SectionHeader*)(filedata + pe_off + 4 + sizeof(CoffHeader) + opt_hdr_size);

    for (uint32_t i = 0; i < num_sections; i++) {
        SectionHeader* sec = &sections[i];
        if (sec->SizeOfRawData > 0) {
            memcpy(g_image_base + sec->VirtualAddress,
                   filedata + sec->PointerToRawData,
                   sec->SizeOfRawData);
        }
        /* Zero BSS */
        if (sec->VirtualSize > sec->SizeOfRawData) {
            memset(g_image_base + sec->VirtualAddress + sec->SizeOfRawData, 0,
                   sec->VirtualSize - sec->SizeOfRawData);
        }

        char sec_name[9] = {0};
        memcpy(sec_name, sec->Name, 8);
        MW_TRACE("Section [%u] %-8s VA=0x%08x VS=0x%08x Raw=0x%08x Ch=0x%08x",
                 i, sec_name, sec->VirtualAddress, sec->VirtualSize,
                 sec->SizeOfRawData, sec->Characteristics);

        /* Set section permissions */
        int prot = PROT_READ;
        if (sec->Characteristics & SCN_MEM_EXECUTE) prot |= PROT_EXEC;
        if (sec->Characteristics & SCN_MEM_WRITE)   prot |= PROT_WRITE;
        if (sec->Characteristics & SCN_MEM_READ)    prot |= PROT_READ;
        mprotect(g_image_base + sec->VirtualAddress,
                 (sec->VirtualSize + section_align - 1) & ~(section_align - 1), prot);
    }

    /* Parse and resolve imports */
    /* DataDirectory starts at offset 112 within the optional header */
    uint32_t dd_offset = pe_off + 4 + sizeof(CoffHeader) + 112;
    DataDirectory* dd = (DataDirectory*)(filedata + dd_offset);

    uint32_t import_rva = dd[DD_IMPORT].VirtualAddress;
    uint32_t import_size = dd[DD_IMPORT].Size;

    if (import_rva == 0) {
        fprintf(stderr, "[ERROR] No import directory\n");
        free(filedata);
        return -1;
    }

    MW_TRACE("Import Directory: RVA=0x%x Size=0x%x", import_rva, import_size);

    /* Walk import directory */
    int import_idx = 0;
    int total_resolved = 0;
    int total_unresolved = 0;

    for (uint32_t imp_idx = 0; ; imp_idx++) {
        ImportDirectoryEntry* imp = (ImportDirectoryEntry*)(g_image_base + import_rva + imp_idx * 20);
        if (imp->Name == 0 && imp->FirstThunk == 0) break;

        const char* dll_name = (const char*)(g_image_base + imp->Name);
        uint32_t ilt_rva = imp->OriginalFirstThunk;
        uint32_t iat_off = imp->FirstThunk;

        MW_TRACE("Import DLL: %s (IAT RVA=0x%x, ILT RVA=0x%x)", dll_name, iat_off, ilt_rva);

        /* Walk IAT/ILT entries */
        uint32_t lookup_rva = ilt_rva ? ilt_rva : iat_off;
        int slot = 0;

        for (;;) {
            uint64_t entry = *(uint64_t*)(g_image_base + lookup_rva + slot * 8);
            if (entry == 0) break;

            const char* func_name = NULL;
            uint16_t hint = 0;

            if (!(entry & 0x8000000000000000ULL)) {
                /* Import by name */
                uint32_t name_rva = (uint32_t)(entry & 0x7FFFFFFF);
                hint = *(uint16_t*)(g_image_base + name_rva);
                func_name = (const char*)(g_image_base + name_rva + 2);
            } else {
                /* Import by ordinal */
                uint16_t ordinal = (uint16_t)(entry & 0xFFFF);
                MW_TRACE("  [%d] Ordinal %u (not supported)", slot, ordinal);
                total_unresolved++;
                slot++;
                continue;
            }

            /* Search our dispatch table */
            int found = 0;
            for (size_t t = 0; t < NUM_IMPORTS; t++) {
                if (strcmp(g_import_table[t].name, func_name) == 0) {
                    /* Found! Patch IAT */
                    void* patch_addr = g_image_base + iat_off + slot * 8;
                    *(uint64_t*)patch_addr = (uint64_t)(uintptr_t)g_import_table[t].func;
                    found = 1;
                    total_resolved++;
                    break;
                }
            }

            /* Check data imports */
            if (!found) {
                for (size_t t = 0; t < NUM_DATA_IMPORTS; t++) {
                    if (strcmp(g_data_imports[t].name, func_name) == 0) {
                        void* patch_addr = g_image_base + iat_off + slot * 8;
                        *(uint64_t*)patch_addr = (uint64_t)(uintptr_t)g_data_imports[t].data_addr;
                        found = 1;
                        total_resolved++;
                        MW_TRACE("  [%d] %s -> DATA %p", slot, func_name, g_data_imports[t].data_addr);
                        break;
                    }
                }
            }

            if (!found) {
                MW_TRACE("  [%d] UNRESOLVED: %s", slot, func_name);
                total_unresolved++;
            } else {
                /* Only log code imports here (data imports logged above) */
                int is_data = 0;
                for (size_t t = 0; t < NUM_DATA_IMPORTS; t++) {
                    if (strcmp(g_data_imports[t].name, func_name) == 0) { is_data = 1; break; }
                }
                if (!is_data) {
                    void* resolved = (void*)*(uint64_t*)(g_image_base + iat_off + slot * 8);
                    MW_TRACE("  [%d] %s -> %p", slot, func_name, resolved);
                }
            }

            slot++;
            import_idx++;
        }
    }

    MW_TRACE("Import resolution: %d resolved, %d unresolved (total %d)",
             total_resolved, total_unresolved, total_resolved + total_unresolved);

    if (total_unresolved > 0) {
        fprintf(stderr, "[WARN] %d imports unresolved\n", total_unresolved);
    }

    /* Check TLS */
    if (num_dd > DD_TLS && dd[DD_TLS].VirtualAddress != 0) {
        TlsDirectory64* tls = (TlsDirectory64*)(g_image_base + dd[DD_TLS].VirtualAddress);
        MW_TRACE("TLS: StartRaw=0x%lx EndRaw=0x%lx Callbacks=0x%lx",
                 tls->StartAddressOfRawData, tls->EndAddressOfRawData,
                 tls->AddressOfCallBacks);
        /* No TLS callbacks for UPX 4.2.4 (AddressOfCallBacks = 0) */
    }

    /* Store Exception directory (.pdata) for SEH unwinding */
    if (num_dd > DD_EXCEPTION && dd[DD_EXCEPTION].VirtualAddress != 0) {
        g_pdata_rva = dd[DD_EXCEPTION].VirtualAddress;
        g_pdata_size = dd[DD_EXCEPTION].Size;
        g_num_rt_functions = g_pdata_size / sizeof(RUNTIME_FUNCTION);
        MW_TRACE("Exception: .pdata RVA=0x%x Size=0x%x Entries=%u",
                 g_pdata_rva, g_pdata_size, g_num_rt_functions);
    } else {
        MW_TRACE("Exception: no .pdata directory");
    }

    free(filedata);
    return 0;
}

/* ============================================================
 * Signal Handler (for debugging crashes)
 * ============================================================ */

static void crash_handler(int sig, siginfo_t* info, void* ctx) {
    ucontext_t* uc = (ucontext_t*)ctx;
    uint64_t rip = uc->uc_mcontext.gregs[REG_RIP];
    uint64_t rax = uc->uc_mcontext.gregs[REG_RAX];
    uint64_t rcx = uc->uc_mcontext.gregs[REG_RCX];
    uint64_t rdx = uc->uc_mcontext.gregs[REG_RDX];
    uint64_t r8  = uc->uc_mcontext.gregs[REG_R8];
    uint64_t r9  = uc->uc_mcontext.gregs[REG_R9];
    uint64_t rsp = uc->uc_mcontext.gregs[REG_RSP];
    uint64_t rbp = uc->uc_mcontext.gregs[REG_RBP];

    fprintf(stderr, "\n[CRASH] Signal %d at RIP=0x%lx\n", sig, rip);
    fprintf(stderr, "  RAX=0x%lx RCX=0x%lx RDX=0x%lx R8=0x%lx R9=0x%lx\n",
            rax, rcx, rdx, r8, r9);
    fprintf(stderr, "  RSP=0x%lx RBP=0x%lx\n", rsp, rbp);
    fprintf(stderr, "  g_image_base=%p g_image_size=0x%lx\n", g_image_base, g_image_size);

    if (g_image_base && rip >= (uint64_t)(uintptr_t)g_image_base &&
        rip < (uint64_t)(uintptr_t)g_image_base + g_image_size) {
        uint64_t rva = rip - (uint64_t)(uintptr_t)g_image_base;
        fprintf(stderr, "  RVA=0x%lx (inside loaded PE)\n", rva);

        /* Dump bytes at crash point */
        uint8_t* crash_ptr = (uint8_t*)(uintptr_t)rip;
        fprintf(stderr, "  Bytes: ");
        for (int i = 0; i < 16; i++) {
            fprintf(stderr, "%02x ", crash_ptr[i]);
        }
        fprintf(stderr, "\n");

        /* Dump return address from stack */
        fprintf(stderr, "  Return addr: 0x%lx\n", *(uint64_t*)rsp);
    } else {
        fprintf(stderr, "  (NOT inside loaded PE)\n");
    }

    /* Check if the crash is from calling a NULL function pointer */
    if (rip < 0x1000 || (info->si_code == SEGV_MAPERR && info->si_addr == NULL)) {
        fprintf(stderr, "  Likely: NULL function pointer call\n");
    }

    _exit(139);
}

static void setup_signal_handlers(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = crash_handler;
    sa.sa_flags = SA_SIGINFO;
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGILL, &sa, NULL);
    sigaction(SIGFPE, &sa, NULL);
    sigaction(SIGBUS, &sa, NULL);
    sigaction(SIGABRT, &sa, NULL);
}

/* ============================================================
 * Main
 * ============================================================ */

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printf("MiniWin PE Loader v0.1\n");
        printf("Usage: %s <pe_executable> [args...]\n", argv[0]);
        return 1;
    }

    const char* exe_path = argv[1];
    g_exe_path = exe_path;

    /* Build command line */
    snprintf(g_cmdline_ansi, sizeof(g_cmdline_ansi), "%s", exe_path);
    for (int i = 2; i < argc; i++) {
        strcat(g_cmdline_ansi, " ");
        strcat(g_cmdline_ansi, argv[i]);
    }
    MW_TRACE("Command line: %s", g_cmdline_ansi);

    /* Build wide command line */
    mbstowcs(g_cmdline_wide, g_cmdline_ansi, sizeof(g_cmdline_wide)/sizeof(wchar_t));

    /* Set up CRT data */
    g_acmdln_ptr = g_cmdline_ansi;
    g_initenv_ptr = NULL; /* no env for now */
    g_argv[0] = (char*)exe_path;
    for (int i = 2; i < argc; i++) {
        g_argv[i - 1] = argv[i];
    }
    g_argv[argc - 1] = NULL;
    g_argc = argc - 1;

    /* Set up signal handlers */
    setup_signal_handlers();

    /* Set up trace log */
    {
        char trace_path[512];
        const char* base = strrchr(exe_path, '/');
        base = base ? base + 1 : exe_path;
        snprintf(trace_path, sizeof(trace_path), "miniwin-results/%s/api_trace.json", base);
        /* Create directory if needed */
        char dir_path[512];
        snprintf(dir_path, sizeof(dir_path), "miniwin-results/%s", base);
        mkdir(dir_path, 0755);
        /* Also create subdirs */
        char subdir[600];
        snprintf(subdir, sizeof(subdir), "%s/execution", dir_path);
        mkdir(subdir, 0755);
        snprintf(subdir, sizeof(subdir), "%s/binary", dir_path);
        mkdir(subdir, 0755);
        snprintf(subdir, sizeof(subdir), "%s/analysis", dir_path);
        mkdir(subdir, 0755);
        snprintf(subdir, sizeof(subdir), "%s/screenshots", dir_path);
        mkdir(subdir, 0755);

        g_trace_log = fopen(trace_path, "w");
        if (g_trace_log) {
            fprintf(g_trace_log, "{\n  \"trace\": [\n");
        }
    }

    printf("[MiniWin] Loading %s...\n", exe_path);

    /* Load PE */
    if (load_pe(exe_path) != 0) {
        fprintf(stderr, "[ERROR] Failed to load PE\n");
        return 1;
    }

    /* Set up fake TEB/PEB for Windows TLS (gs:0x30) access */
    setup_teb_peb();

    /* Jump to entry point */
    MW_TRACE("Jumping to EP=0x%lx", g_entry_point);
    fflush(stdout);
    fflush(stderr);

    typedef int (*pe_entry_fn)(void);
    pe_entry_fn entry = (pe_entry_fn)(void*)(uintptr_t)g_entry_point;
    int ret = entry();

    MW_TRACE("Entry point returned %d", ret);

    /* Close trace log */
    if (g_trace_log) {
        fprintf(g_trace_log, "  ]\n}\n");
        fclose(g_trace_log);
        g_trace_log = NULL;
    }

    return ret;
}
const uint32_t EH_UNWINDING = 0x02;
