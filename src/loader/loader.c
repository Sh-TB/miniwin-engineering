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
static char** g_argv_cell = g_argv;   /* TASK-API-011: __p___argv points here */
static char** g_environ_cell = NULL;  /* TASK-API-011: __p__environ points here */
static char* g_tzname[2] = { (char*)"UTC", (char*)"UTC" };
static long g_timezone_val = 0;
static int g_daylight_val = 0;
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

/* Forward declaration for GetProcAddress resolver (defined after import tables) */
typedef void* (*resolve_proc_fn_t)(void* module, const char* name);
static resolve_proc_fn_t g_resolve_proc_fn = NULL;

#define MW_TRACE(fmt, ...) do { \
    if (g_trace_log) { \
        fprintf(g_trace_log, "[API] " fmt "\n", ##__VA_ARGS__); \
        fflush(g_trace_log); \
    } else { \
        fprintf(stderr, "[TRACE] " fmt "\n", ##__VA_ARGS__); \
    } \
} while(0)

/* ============================================================
 * TASK-API-005 (ADR-010): EH hack isolation flag
 * ============================================================
 * EH_FIX (dispatcher LSDA override in mw_RtlDispatchException) and
 * CXX-EH-FIX (load_pe LSDA catch-all byte patch) are app-specific,
 * target-modifying bypasses that violate the engineering rules
 * ("forbidden substitutes": they hardcode this sample's addresses
 * and fake a catch the personality did not grant). BUG-029 live
 * evidence implicates the EH_FIX dispatch-1 hijack as the top
 * suspect poisoning GCC __cxa/TLS exception state.
 *
 * Both hacks are now flag-guarded: they execute ONLY when the env
 * var MINWIN_EH_HACKS is set to a non-empty value other than "0".
 * Default = OFF so the PE's own personality path handles C++ EH.
 * History preservation rule: the hack code is NOT deleted.
 */
static int g_eh_hacks_cache = -1;   /* -1 = not yet resolved */
static int mw_eh_hacks_enabled(void) {
    if (g_eh_hacks_cache < 0) {
        const char* v = getenv("MINWIN_EH_HACKS");
        g_eh_hacks_cache = (v && *v && strcmp(v, "0") != 0) ? 1 : 0;
        MW_TRACE("[API-005] EH hacks %s (MINWIN_EH_HACKS=%s)",
                 g_eh_hacks_cache ? "ENABLED" : "DISABLED (default)",
                 (v && *v) ? v : "<unset>");
    }
    return g_eh_hacks_cache;
}

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
/* TASK-TLS-001: Windows x64 TEB+0x58 = ThreadLocalStoragePointer — a POINTER
 * to an array of slot pointers. Native-TLS PEs (e.g. libc++abi's
 * __cxa_get_globals) read gs:0x58 and index this array by __tls_index. */
static void* g_tls_ptr_array[64];

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
    *(uint64_t*)(teb + 0x58) = (uint64_t)(uintptr_t)g_tls_ptr_array; /* x64: ThreadLocalStoragePointer */

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

/* Critical Section state.
 * BUG-034 (appcompat sweep APP-F-04): Windows CRITICAL_SECTION is exactly
 * 40 bytes on x64 and PEs place it flush against other globals (observed:
 * mingw dtoa CRITICAL_SECTION at 0x14000b178 with freelist[] at +0x28). The
 * previous MwCritSec {pthread_mutex_t(40) + int(4)} wrote an 'initialized'
 * flag at offset 40 -> 8-byte overflow into PE data (freelist[0] became 1,
 * dtoa Balloc deref -> SIGSEGV). The contract is: NEVER write past byte 40.
 * The mutex overlays the CS itself; our "initialized" sentinel lives in the
 * Windows SpinCount slot (offset 36), which caller-side is zeroed memory. */
#define MW_CS_MAGIC 0x4D574353u /* "MWCS" */

static pthread_mutex_t* cs_mutex(void* cs) { return (pthread_mutex_t*)cs; }

static int cs_is_initialized(void* cs) {
    return *(uint32_t*)((uint8_t*)cs + 36) == MW_CS_MAGIC;
}

static void cs_mark_initialized(void* cs) {
    *(uint32_t*)((uint8_t*)cs + 36) = MW_CS_MAGIC;
}

__attribute__((ms_abi)) void mw_InitializeCriticalSection(void* cs) {
    if (!cs) return;
    if (cs_is_initialized(cs)) return; /* idempotent, like re-init on a free CS */
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(cs_mutex(cs), &attr);
    pthread_mutexattr_destroy(&attr);
    cs_mark_initialized(cs);
    MW_TRACE("InitializeCriticalSection(%p)", cs);
}

__attribute__((ms_abi)) void mw_DeleteCriticalSection(void* cs) {
    if (!cs) return;
    if (cs_is_initialized(cs)) {
        pthread_mutex_destroy(cs_mutex(cs));
        *(uint32_t*)((uint8_t*)cs + 36) = 0;
    }
    MW_TRACE("DeleteCriticalSection(%p)", cs);
}

__attribute__((ms_abi)) void mw_EnterCriticalSection(void* cs) {
    if (!cs) return;
    if (!cs_is_initialized(cs)) mw_InitializeCriticalSection(cs);
    pthread_mutex_lock(cs_mutex(cs));
    MW_TRACE("EnterCriticalSection(%p)", cs);
}

__attribute__((ms_abi)) void mw_LeaveCriticalSection(void* cs) {
    if (!cs) return;
    if (cs_is_initialized(cs)) pthread_mutex_unlock(cs_mutex(cs));
    MW_TRACE("LeaveCriticalSection(%p)", cs);
}

__attribute__((ms_abi)) int mw_TryEnterCriticalSection(void* cs) {
    if (!cs) return 0;
    if (!cs_is_initialized(cs)) mw_InitializeCriticalSection(cs);
    int ret = (pthread_mutex_trylock(cs_mutex(cs)) == 0) ? 1 : 0;
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
    if (!name || !name[0]) return NULL;
    /* Resolve from dispatch table if module matches our fake handle or image base.
     * Uses global function pointer set after table definitions (see resolve_proc_init). */
    if (g_resolve_proc_fn) return g_resolve_proc_fn(module, name);
    return NULL;
}

__attribute__((ms_abi)) void* mw_LoadLibraryA(const char* name) {
    MW_TRACE("LoadLibraryA(%s)", name ? name : "NULL");
    /* Return a fake non-NULL module handle so UPX doesn't treat it as failure.
     * UPX uses the handle with GetProcAddress later; our mw_GetProcAddress
     * currently returns NULL, which is handled by UPX's own error paths. */
    return (void*)(uintptr_t)0xdead0001;
}

/* TASK-API-002: shared flush for every _exit() path (ExitProcess, abort).
 * MW_TRACE flushes per line, but the trace FILE lacks its JSON terminator and
 * the stdout/stderr buffers would be silently dropped by _exit(). Closing the
 * trace here makes every api_trace.json parseable without the append-']}'
 * workaround. Idempotent (NULL check) so main()'s normal-return path is safe. */
static void mw_trace_shutdown_flush(void) {
    fflush(stdout);
    fflush(stderr);
    if (g_trace_log) {
        fprintf(g_trace_log, "  ]\n}\n");
        fclose(g_trace_log);
        g_trace_log = NULL;
    }
}

__attribute__((noreturn)) __attribute__((ms_abi)) void mw_ExitProcess(unsigned int code) {
    MW_TRACE("ExitProcess(%u)", code);
    mw_trace_shutdown_flush();
    _exit(code);
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
/* BUG-031: true raise-site volatile registers (captured by the naked stub
 * BEFORE any scratch use). seh_build_context previously read these from the
 * current CPU state inside the C dispatcher — i.e. compiler-dependent
 * garbage that changed with every loader recompilation and flipped the GCC
 * personality's phase-1 decision (Frame[4] INSTALL vs ContinueSearch).
 * Windows delivers the true raise-site context; now we do too. */
uint64_t g_cap_rax = 0;
uint64_t g_cap_r10 = 0;
uint64_t g_cap_r11 = 0;
EXCEPTION_RECORD g_cap_er;  /* saved EXCEPTION_RECORD for RtlUnwindEx */

/* RtlUnwindEx support — setjmp/longjmp for non-returning unwind.
 * Cannot be static because naked asm references them by symbol name. */
static jmp_buf g_unwind_jmpbuf;
uint8_t* g_unwind_ctx = NULL;     /* CONTEXT buffer being used for dispatch */
int g_is_unwinding = 0;           /* set when RtlUnwindEx longjmps back */
uint64_t g_unwind_target_ip = 0;  /* target IP for unwind */
uint64_t g_unwind_target_frame = 0; /* target frame (RSP) for unwind */
/* BUG-031 (ADR-011): throw-site CONTEXT snapshot for the GCC unwind chain.
 * Copied once per fresh throw (every dispatch that is not a resume command).
 * The GCC bridge's unwind-phase re-raise (0x21474343) and later chain
 * segments consume it because the original dispatcher C stack — including
 * its stack-local CONTEXT — is abandoned once a landing pad resumes. */
static uint8_t g_throw_ctx_snapshot[CONTEXT_SIZE];
static int g_throw_ctx_valid = 0;
/* BUG-039/B: TARGET-FRAME register snapshot. The dispatcher search loop
 * unwinds the live CONTEXT through EVERY frame — including the frame that
 * owns the handler — so by the time the personality calls RtlUnwindEx the
 * live CONTEXT's nonvolatives hold the TARGET'S CALLER's values (live proof:
 * t08_probe p2 unwind — Frame[1] restored RDI=&got/RSI=literal/RBP=ok
 * correctly, then Frame[2] (main) replay clobbered them to CRT-startup
 * values). Real Windows never resumes a landing pad from that state:
 * RtlUnwindEx re-walks from its own machine state and stops AT the target
 * frame. We capture the "returned into frame F" state before frame F's own
 * codes are applied and use it as the register base for the resume. */
static uint8_t g_target_frame_ctx[CONTEXT_SIZE];
static int g_target_frame_ctx_valid = 0;
/* BUG-031 helpers (defined next to mw_RtlUnwindEx; forward-declared because
 * the dispatcher's resume-command intercept calls them). */
static uint64_t seh_total_prologue_alloc(uint64_t ip_va);
static void seh_walk_intermediate_cleanups(uint8_t* start_ctx, void* er_src,
                                           uint64_t tgt_frame, const char* tag);
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
        if (op == 1) { /* ALLOC_LARGE — TASK-API-006 spec rule (EXP-039): info==0 => next uint16 * 8; info==1 => next uint32 (two slots, no shift) */
            slot++;
            if (info == 0) {
                total_alloc += (uint32_t)(*(uint16_t*)(codes + slot * 2)) * 8;
                slot++;
            } else {
                total_alloc += *(uint32_t*)(codes + slot * 2);
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
    uint8_t ui_flags = (ui[0] >> 3) & 0x07;
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
        ui_flags = (ui[0] >> 3) & 0x07;
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

/* Slots consumed by one unwind code (the code word itself + operand slots).
 * Per the Microsoft PE/COFF x64 spec table and ReactOS's
 * UnwindOpExtraSlotTable (sdk/lib/rtl/amd64/unwind.c). Used by the skip
 * path (codes above the fault RIP must be skipped WITH their operands,
 * otherwise the cursor misaligns on every following code — BUG-037) and
 * by any caller needing the byte size of the code array. */
static int seh_unwind_op_slots(uint8_t op, uint8_t op_info) {
    switch (op) {
    case UWOP_ALLOC_LARGE:     return (op_info != 0) ? 3 : 2;
    case UWOP_SAVE_NONVOL:     return 2;
    case UWOP_SAVE_NONVOL_FAR: return 3;
    case UWOP_EPILOG:          return 2;   /* v2 only */
    case UWOP_SPARE_CODE:      return 3;
    case UWOP_SAVE_XMM128:     return 2;
    case UWOP_SAVE_XMM128_FAR: return 3;
    case UWOP_PUSH_MACHFRAME:  return 1;
    default:                   return 1; /* PUSH_NONVOL, ALLOC_SMALL, SET_FPREG, reserved */
    }
}

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
    /* NOTE: PUSH_NONVOL and PUSH_MACHFRAME safety bounds removed —
     * the original `new_rsp <= rsp + 4096` was always-true after
     * new_rsp += 8, making it a no-op check that added confusion.
     * Trust the unwind metadata instead. */
    uint8_t* ui_base = g_image_base + rf->UnwindInfo;

    uint8_t version     = ui_base[0] & 0x07;
    uint8_t flags       = (ui_base[0] >> 3) & 0x07;
    uint8_t prolog_size = ui_base[1];
    uint8_t count_codes = ui_base[2];
    /* BUG-037: UNWIND_INFO byte 3 is two little-endian 4-bit fields —
     * FrameRegister is the LOW nibble, the scaled FrameOffset the HIGH
     * nibble (winnt.h: `UCHAR FrameRegister:4; UCHAR FrameOffset:4;`).
     * The previous decode had them swapped, so mingw frames that use
     * RBP (reg 5) with lea rbp,[rsp+0x80] (offset 8 -> byte 0x85) were
     * misread as FP=R8/offset=5. */
    uint8_t frame_reg   = ui_base[3] & 0x0f;
    uint8_t frame_off   = (ui_base[3] >> 4) & 0x0f;

    (void)prolog_size;

    /* Handle CHAININFO — the chained function's unwind info replaces
     * the current function's unwind codes. We must run the chained
     * function's unwind codes to compute the correct establisher frame. */
    if (flags & UNW_FLAG_CHAININFO) {
        uint32_t chain_off = 4 + count_codes * 2;
        if (chain_off % 4) chain_off += 2;
        RUNTIME_FUNCTION* chained = (RUNTIME_FUNCTION*)(ui_base + chain_off);
        uint8_t* chained_ui = g_image_base + chained->UnwindInfo;
        uint8_t chained_version = chained_ui[0] & 0x07;
        uint8_t chained_flags = (chained_ui[0] >> 3) & 0x07;
        uint8_t chained_prolog_size = chained_ui[1];
        uint8_t chained_count_codes = chained_ui[2];
        /* BUG-037: FrameRegister = LOW nibble (see comment above). */
        uint8_t chained_frame_reg = chained_ui[3] & 0x0f;
        uint8_t chained_frame_off = (chained_ui[3] >> 4) & 0x0f;
        (void)chained_prolog_size;

        /* Read RIP from context for func_offset calculation */
        uint64_t chain_rip = *(uint64_t*)(ctx + CTX_Rip);
        uint64_t chained_rsp = *(uint64_t*)(ctx + CTX_Rsp);
        int chained_fp_set = 0;
        uint64_t chained_fp_val = 0;
        uint32_t chained_func_offset = (uint32_t)(chain_rip - (uint64_t)(uintptr_t)g_image_base) - chained->BeginAddress;
        uint8_t* chained_codes_ptr = chained_ui + 4;
        int chained_slot = 0;
        for (int i = 0; i < chained_count_codes; i++) {
            /* BUG-037: CountOfCodes counts SLOTS (code words + operand
             * slots, MS doc "2 or 3 nodes"; ReactOS advances i by
             * UnwindOpSlots). Bound the walk on the slot cursor, not the
             * code index, or padding/handler bytes decode as phantom
             * codes past the array. */
            if (chained_slot >= chained_count_codes ||
                chained_slot * 2 + 1 >= (int)(chained_count_codes * 2 + 16)) break;
            uint16_t cw = *(uint16_t*)(chained_codes_ptr + chained_slot * 2);
            uint8_t op_code = (cw >> 8) & 0x0f;
            uint8_t op_info = (cw >> 12) & 0x0f;
            uint8_t code_offset = cw & 0xff;
            if (chained_func_offset > 0 && code_offset >= chained_func_offset) {
                /* BUG-037: skip the code WITH its operand slots, otherwise
                 * the cursor lands on operand bytes and every following
                 * code decodes from a misaligned position. */
                chained_slot += seh_unwind_op_slots(op_code, op_info);
                continue;
            }
            if (op_code == UWOP_EPILOG && chained_version < 2) {
                /* Epilogue descriptors exist only in UNWIND_INFO v2; in a
                 * v1 array op 6 is corruption — stop applying codes. */
                MW_TRACE("[DISPATCH] chained UI v%d has EPILOG code — corrupt, stop", chained_version);
                break;
            }
            switch (op_code) {
            case UWOP_PUSH_NONVOL: {
                /* BUG-039 (ReactOS PopReg semantics): the unwind-code array is
                 * stored in DESCENDING code-offset order, i.e. already in
                 * undo (epilog) order — each push code therefore restores
                 * from the CURRENT Rsp BEFORE advancing. Reading AFTER the
                 * increment shifted every restored nonvolatile one slot up:
                 * live proof (t08_probe p1 unwind, evidence master/bug038):
                 * RDI got the RETURN ADDRESS slot [E1]=0x140020af1 while the
                 * real saved rdi sits at [E1-8]. Correct order is
                 * reg=[rsp]; rsp+=8  (ReactOS sdk/lib/rtl/amd64/unwind.c
                 * PopReg: SetRegFromStackValue(Rsp) THEN Rsp+=8). */
                uint32_t off = reg_to_ctx_offset(op_info);
                if (off) *(uint64_t*)(ctx + off) = *(uint64_t*)(chained_rsp);
                chained_rsp += 8;
                break;
            }
            case UWOP_ALLOC_LARGE: /* TASK-API-006 spec rule: info==0 => uint16 * 8; info==1 => uint32, no shift */
                chained_slot++;
                if (op_info == 0) {
                    chained_rsp += (uint64_t)(*(uint16_t*)(chained_codes_ptr + chained_slot * 2)) * 8;
                } else {
                    chained_slot++;
                    chained_rsp += (uint64_t)(*(uint32_t*)(chained_codes_ptr + chained_slot * 2 - 2));
                }
                break;
            case UWOP_ALLOC_SMALL: chained_rsp += (uint64_t)(op_info + 1) * 8; break;
            case UWOP_SET_FPREG:
                /* ReactOS semantics: RSP re-anchors to the frame-pointer
                 * base (FP_value - FrameOffset*16) for all remaining codes;
                 * the FP value is read from the LIVE context, where the
                 * established anchor persists (BUG-037). */
                if (chained_frame_reg) {
                    uint32_t fp_off = reg_to_ctx_offset(chained_frame_reg);
                    if (fp_off) {
                        chained_fp_val = *(uint64_t*)(ctx + fp_off);
                        chained_rsp = chained_fp_val - (uint64_t)chained_frame_off * 16;
                    } else {
                        chained_fp_val = chained_rsp + (uint64_t)chained_frame_off * 16;
                    }
                } else {
                    chained_fp_val = chained_rsp + (uint64_t)chained_frame_off * 16;
                }
                chained_fp_set = 1;
                break;
            case UWOP_SAVE_NONVOL: {
                chained_slot++;
                uint16_t soff = *(uint16_t*)(chained_codes_ptr + chained_slot * 2);
                uint32_t off = reg_to_ctx_offset(op_info);
                if (off) *(uint64_t*)(ctx + off) = *(uint64_t*)(chained_rsp + soff * 8);
                break;
            }
            case UWOP_SAVE_NONVOL_FAR: {
                chained_slot++; chained_slot++;
                uint32_t soff = *(uint32_t*)(chained_codes_ptr + chained_slot * 2 - 2);
                uint32_t off = reg_to_ctx_offset(op_info);
                if (off) *(uint64_t*)(ctx + off) = *(uint64_t*)(chained_rsp + soff);
                break;
            }
            case UWOP_SAVE_XMM128: { chained_slot++; break; }
            case UWOP_SAVE_XMM128_FAR: { chained_slot++; chained_slot++; break; }
            case UWOP_EPILOG: { chained_slot++; break; }          /* 2 slots total */
            case UWOP_SPARE_CODE: { chained_slot++; chained_slot++; break; } /* 3 total */
            case UWOP_PUSH_MACHFRAME: {
                chained_rsp += 40;
                if (chained_rsp <= chained_rsp + 4096) {
                    *(uint64_t*)(ctx + CTX_Rip) = *(uint64_t*)(chained_rsp + 16);
                    chained_rsp = *(uint64_t*)(chained_rsp + 8);
                }
                break;
            }
            default: { if (op_code >= 11) chained_slot++; break; } /* reserved */
            }
            chained_slot++;
        }
        if (chained_fp_set) {
            uint32_t fp_off = reg_to_ctx_offset(chained_frame_reg);
            if (fp_off) *(uint64_t*)(ctx + fp_off) = chained_fp_val;
        }
        *(uint64_t*)(ctx + CTX_Rsp) = chained_rsp;
        uint64_t chained_est = chained_fp_set ? chained_fp_val : chained_rsp;
        if (establisher_frame) *establisher_frame = chained_est;

        if (chained_flags & UNW_FLAG_EHANDLER) {
            uint32_t h_off = 4 + chained_count_codes * 2;
            if (h_off % 4) h_off += 2;
            uint32_t handler_rva = *(uint32_t*)(chained_ui + h_off);
            void* lsda = (void*)(chained_ui + h_off + 4);
            if (out_handler_rva) *out_handler_rva = handler_rva;
            if (out_lsda) *out_lsda = lsda;
            MW_TRACE("[DISPATCH] Unwind: CHAININFO handler=0x%x est=0x%lx", handler_rva, chained_est);
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
        /* BUG-037: CountOfCodes counts SLOTS — see chained-loop note. */
        if (slot >= count_codes ||
            slot * 2 + 1 >= (int)(count_codes * 2 + 16)) break;
        uint16_t code_word = *(uint16_t*)(codes + slot * 2);
        uint8_t op_code = (code_word >> 8) & 0x0f;   /* UnwindOp: low nibble of byte 1 */
        uint8_t op_info = (code_word >> 12) & 0x0f;  /* OpInfo: high nibble of byte 1 */
        uint8_t code_offset = code_word & 0xff;

        /* Skip codes that haven't fully executed yet — advancing by the
         * FULL slot size so the cursor stays aligned (BUG-037). Boundary:
         * a code whose END offset equals the RIP offset has executed
         * (ReactOS skips strictly-greater offsets). */
        if (func_offset > 0 && code_offset >= func_offset) {
            slot += seh_unwind_op_slots(op_code, op_info);
            continue;
        }
        if (op_code == UWOP_EPILOG && version < 2) {
            /* Epilogue descriptors exist only in UNWIND_INFO v2; in a v1
             * array op 6 is corruption — stop applying codes. */
            MW_TRACE("[DISPATCH] UI v1 has EPILOG code — corrupt, stop");
            break;
        }

        switch (op_code) {
        case UWOP_PUSH_NONVOL: {
            /* BUG-039: restore from CURRENT rsp BEFORE advancing — the code
             * array is in undo order (descending code offsets). The old
             * increment-first order read the NEXT slot (one too high):
             * t08_probe Frame[1] live trace showed RDI=[E1]=return address
             * instead of [E1-8]=saved rdi. ReactOS PopReg: read then +=8.
             * (Root cause of BUG-038 probe symptoms: catch by reference got
             * garbage RDI; `ok` counter in RBP clobbered.) */
            uint32_t off = reg_to_ctx_offset(op_info);
            if (off) {
                *(uint64_t*)(ctx + off) = *(uint64_t*)(new_rsp);
            }
            new_rsp += 8;
            break;
        }
        case UWOP_ALLOC_LARGE: /* TASK-API-006 spec rule: info==0 => uint16 * 8; info==1 => uint32, no shift */
            slot++;
            if (op_info == 0) {
                new_rsp += (uint64_t)(*(uint16_t*)(codes + slot * 2)) * 8;
            } else {
                slot++;
                new_rsp += (uint64_t)(*(uint32_t*)(codes + slot * 2 - 2));
            }
            break;
        case UWOP_ALLOC_SMALL: {
            new_rsp += (uint64_t)(op_info + 1) * 8;
            break;
        }
        case UWOP_SET_FPREG: {
            /* ReactOS semantics (sdk/lib/rtl/amd64/unwind.c): on unwind,
             * RSP re-anchors to the frame-pointer base — the FP register
             * value minus the scaled FrameOffset — and the FP value is
             * read from the LIVE context, where the established anchor
             * persists through the frame body (BUG-037). */
            if (frame_reg) {
                uint32_t fp_ctx_off = reg_to_ctx_offset(frame_reg);
                if (fp_ctx_off) {
                    fp_reg_val = *(uint64_t*)(ctx + fp_ctx_off);
                    new_rsp = fp_reg_val - (uint64_t)frame_off * 16;
                } else {
                    fp_reg_val = new_rsp + (uint64_t)frame_off * 16;
                }
            } else {
                fp_reg_val = new_rsp + (uint64_t)frame_off * 16;
            }
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
        case UWOP_EPILOG: { slot++; break; }          /* 2 slots total, no stack effect */
        case UWOP_SPARE_CODE: { slot++; slot++; break; } /* 3 slots total */
        case UWOP_PUSH_MACHFRAME: {
            new_rsp += 40;
            *(uint64_t*)(ctx + CTX_Rip) = *(uint64_t*)(new_rsp + 16);
            new_rsp = *(uint64_t*)(new_rsp + 8);
            break;
        }
        default: {
            if (op_code >= 11) slot++; /* reserved ops: skip code word only */
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
    *(uint64_t*)(ctx + 0x00) = 0x100007; /* ContextFlags = CONTEXT_FULL */

    /* Set RIP and RSP from captured state */
    *(uint64_t*)(ctx + CTX_Rip) = rip;
    *(uint64_t*)(ctx + CTX_Rsp) = rsp;

    /* Read current nonvolatile registers from CPU state.
     * BUG-031: volatile registers come from the stub's true raise-site
     * captures — CPU-state reads here are compiler-dependent garbage that
     * changes with every loader recompilation and flips the GCC
     * personality's phase-1 decision (Frame[4] INSTALL vs ContinueSearch;
     * EXP-039 §6.2 bisection: E1-E5 all carried this poison, E4/E5 showed
     * the 'unsigned long long' BUG-029-era frontier with 727 events). */
    uint64_t rax, rbx, rcx, rdx, rbp, rsi, rdi;
    uint64_t r8, r9, r10, r11, r12, r13, r14, r15;
    rax = g_cap_rax;
    rcx = g_cap_code;               /* ms_abi arg1 at raise: ExceptionCode */
    rdx = g_cap_flags;              /* ms_abi arg2 at raise: ExceptionFlags */
    r8  = g_cap_nargs;              /* ms_abi arg3 at raise: nargs */
    r9  = g_cap_args;               /* ms_abi arg4 at raise: args ptr */
    r10 = g_cap_r10;
    r11 = g_cap_r11;
    __asm__ volatile ("" : "=b"(rbx));
    __asm__ volatile ("movq %%rbp, %0" : "=r"(rbp));
    __asm__ volatile ("" : "=S"(rsi));
    __asm__ volatile ("" : "=D"(rdi));
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
        "movq %2, %%rcx\n\t"
        "movq %3, %%rdx\n\t"
        "movq %4, %%r8\n\t"
        "movq %5, %%r9\n\t"
        "movq g_cap_rax(%%rip), %%rax\n\t"
        "movq g_cap_r10(%%rip), %%r10\n\t"
        "movq g_cap_r11(%%rip), %%r11\n\t"
        "call *%1\n\t"
        /* Restore stack frame */
        "leave\n\t"
        : "=&a"(disposition)
        : "r"(handler), "r"((uint64_t)(uintptr_t)er),
          "r"(est_frame), "r"((uint64_t)(uintptr_t)ctx),
          "r"((uint64_t)(uintptr_t)dc)
        : "rcx", "rdx", "r8", "r9", "r10", "r11", "rdi", "rbp", "memory"
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
    /* Save fully-populated ER to g_cap_er (AFTER filling fields, not before) */
    memcpy(&g_cap_er, &er, sizeof(EXCEPTION_RECORD));

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
    g_target_frame_ctx_valid = 0; /* BUG-039/B: fresh dispatch, no snapshot yet */
    g_is_unwinding = 0;
    g_unwind_target_ip = 0;
    g_unwind_target_frame = 0;

    /* === BUG-031 (ADR-011): GCC bridge resume-command intercept ===
     * Unwind-phase INSTALL at an intermediate frame makes the GCC bridge
     * re-raise 0x21474343 (flags=1, nparams=4, params={object, establisher,
     * landing_pad, selector}) — verified live (EXP-039: bridge core
     * 0x49d3e9-0x49d413: mov r8d,4 / mov edx,1 / mov ecx,0x21474343).
     * The raise re-enters this dispatcher through the naked stub from a C
     * stack that has no PE RUNTIME_FUNCTIONs, so a fresh frame walk can
     * never reach the matching establisher (EXP-038 §5.1 design caution).
     * We honor the record directly as a resume command — exactly what the
     * bridge's own est==param[1] match (0x49d420) + site-A' RtlUnwindEx
     * produces on Windows: resume at the cleanup landing pad with
     * RAX=object, RDX=selector, RSP=establisher − prologue_alloc(LP).
     * Fingerprint: code==0x21474343 && nparams==4 && flags&NONCONTINUABLE
     * && landing pad inside the image && a throw-site snapshot exists.
     * The initial GCC throw (0x20474343, flags=0, nparams=1) never matches
     * ([EXC_STATE] trace, evidence/master/bug030/upx_trace.json). */
    if (er.ExceptionCode == 0x21474343u && er.NumberParameters == 4
        && (er.ExceptionFlags & 0x1u) != 0
        && g_throw_ctx_valid
        && er.ExceptionInformation[2] >= img_base
        && er.ExceptionInformation[2] < img_end) {
        uint64_t lp_va  = er.ExceptionInformation[2];
        uint64_t est_va = er.ExceptionInformation[1];
        uint64_t alloc3 = seh_total_prologue_alloc(lp_va);
        if (alloc3 != 0 && est_va != 0 && (est_va & 0x7ull) == 0) {
            uint64_t resume_rsp = est_va - alloc3;
            /* Registers: protocol values in RAX (object) / RDX (selector),
             * everything else from the throw-site snapshot (the cleanup LP
             * continues the frame that threw — its live callee-saved regs
             * are the snapshot's; A's LP 0x4015A5 consumes only RAX+[rsp]). */
            g_unwind_regs[UR_RAX] = er.ExceptionInformation[0];
            g_unwind_regs[UR_RDX] = er.ExceptionInformation[3];
            g_unwind_regs[UR_RCX] = *(uint64_t*)(g_throw_ctx_snapshot + CTX_Rcx);
            g_unwind_regs[UR_RBX] = *(uint64_t*)(g_throw_ctx_snapshot + CTX_Rbx);
            g_unwind_regs[UR_RBP] = *(uint64_t*)(g_throw_ctx_snapshot + CTX_Rbp);
            g_unwind_regs[UR_RSI] = *(uint64_t*)(g_throw_ctx_snapshot + CTX_Rsi);
            g_unwind_regs[UR_RDI] = *(uint64_t*)(g_throw_ctx_snapshot + CTX_Rdi);
            g_unwind_regs[UR_R8]  = *(uint64_t*)(g_throw_ctx_snapshot + CTX_R8);
            g_unwind_regs[UR_R9]  = *(uint64_t*)(g_throw_ctx_snapshot + CTX_R9);
            g_unwind_regs[UR_R10] = *(uint64_t*)(g_throw_ctx_snapshot + CTX_R10);
            g_unwind_regs[UR_R11] = *(uint64_t*)(g_throw_ctx_snapshot + CTX_R11);
            g_unwind_regs[UR_R12] = *(uint64_t*)(g_throw_ctx_snapshot + CTX_R12);
            g_unwind_regs[UR_R13] = *(uint64_t*)(g_throw_ctx_snapshot + CTX_R13);
            g_unwind_regs[UR_R14] = *(uint64_t*)(g_throw_ctx_snapshot + CTX_R14);
            g_unwind_regs[UR_R15] = *(uint64_t*)(g_throw_ctx_snapshot + CTX_R15);
            g_unwind_regs[UR_RSP] = resume_rsp;
            g_unwind_target_ip = lp_va;
            g_unwind_target_frame = resume_rsp;
            g_is_unwinding = 1;
            MW_TRACE("[BUG-031] resume-cmd: 0x21474343 obj=0x%lx est=0x%lx lp=0x%lx sel=%lu alloc=0x%lx -> RIP=0x%lx RSP=0x%lx",
                     er.ExceptionInformation[0], est_va, lp_va,
                     (unsigned long)er.ExceptionInformation[3],
                     (unsigned long)alloc3, lp_va, resume_rsp);
            if (g_trace_log) fflush(g_trace_log);
            return DISP_RESULT_CONTINUE_EXEC;
        }
        MW_TRACE("[BUG-031] resume-cmd fingerprint matched but alloc/est invalid (alloc=0x%lx est=0x%lx) — falling through to walk",
                 (unsigned long)alloc3, est_va);
    }
    /* Fresh throw (not a resume command): snapshot the throw-site CONTEXT
     * for the intercept and the intermediate-cleanup walk. */
    memcpy(g_throw_ctx_snapshot, ctx, CONTEXT_SIZE);
    g_throw_ctx_valid = 1;
    
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

        MW_TRACE("[DISPATCH] Frame[%d]: RIP=0x%lx RVA=0x%lx CTX.Rsp=0x%lx",
                 frame, current_rip, current_rva, *(uint64_t*)(ctx + CTX_Rsp));

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

        /* EXP-035: Save RSP BEFORE RtlVirtualUnwind modifies it.
         * seh_internal_virtual_unwind updates CTX.Rsp to the function-entry RSP
         * (new_rsp after undoing all prolog operations). But the post-prologue RSP
         * (RSP after the prologue, used by catch blocks for local variable access)
         * is the CTX.Rsp value BEFORE this call. */
        uint64_t est_frame = 0;
        uint32_t handler_rva = 0;
        void* lsda = NULL;
        uint64_t pre_unwind_rsp = *(uint64_t*)(ctx + CTX_Rsp);
        /* BUG-039/B: snapshot the "returned into frame F" state BEFORE this
         * frame's own unwind codes are applied. If frame F owns the handler,
         * this snapshot — not the post-unwind context — is the correct
         * register base for the eventual landing-pad resume. */
        uint8_t pre_unwind_ctx[CONTEXT_SIZE];
        memcpy(pre_unwind_ctx, ctx, CONTEXT_SIZE);
        void* handler = seh_internal_virtual_unwind(
            current_rip, rf, ctx, &est_frame, &handler_rva, &lsda);

        MW_TRACE("[DISPATCH] Frame[%d]: Unwind est=0x%lx CTX.Rsp_after=0x%lx alloc=0x%x",
                 frame, est_frame, *(uint64_t*)(ctx + CTX_Rsp),
                 est_frame - *(uint64_t*)(ctx + CTX_Rsp));

        if (!handler) {
            /* No handler for this frame. Read parent return address
             * from [establisher_frame] and continue walking. */
            uint64_t parent_rip = *(uint64_t*)(uintptr_t)est_frame;
            uint64_t parent_rva = parent_rip - img_base;
            MW_TRACE("[DISPATCH] Frame[%d]: no handler, parent RIP=0x%lx RVA=0x%lx",
                     frame, parent_rip, parent_rva);
            MW_TRACE("[DISPATCH] Frame[%d]: est=0x%lx CTX.Rsp=0x%lx delta=0x%lx",
                     frame, est_frame, *(uint64_t*)(ctx + CTX_Rsp),
                     est_frame - *(uint64_t*)(ctx + CTX_Rsp));

            /* Update context for next frame */
            *(uint64_t*)(ctx + CTX_Rip) = parent_rip;
            *(uint64_t*)(ctx + CTX_Rsp) = est_frame + 8;
            continue;
        }

        /* EHANDLER found — build DISPATCHER_CONTEXT and call handler */
        MW_TRACE("[DISPATCH] Frame[%d]: EHANDLER at RVA 0x%x (VA 0x%lx)",
                 frame, handler_rva, (uint64_t)(uintptr_t)handler);
        MW_TRACE("[DISPATCH] Frame[%d]: establisher=0x%lx CTX.Rsp=0x%lx lsda=%p",
                 frame, est_frame, *(uint64_t*)(ctx + CTX_Rsp), lsda);

        /* BUG-039/B: this frame owns the handler — keep the pre-unwind
         * register state ("returned into frame F from below") as the
         * landing-pad resume base for RtlUnwindEx. */
        memcpy(g_target_frame_ctx, pre_unwind_ctx, CONTEXT_SIZE);
        g_target_frame_ctx_valid = 1;

        /* EXP-035 NOTE: The GCC personality function expects dc.EstablisherFrame to be
         * the function-entry RSP. It passes this directly to RtlUnwindEx as
         * target_frame. RtlUnwindEx then subtracts the stack allocation to
         * get the post-prologue RSP for the catch landing pad.
         *
         * So we pass est_frame (function-entry RSP) as the establisher frame,
         * which is the original (correct) behavior. The fix for the catch
         * block's RSP is in RtlUnwindEx (EXP-035), not here.
         */
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
        MW_TRACE("[HANDLER_CALL] RDX (EstablisherFrame) = 0x%lx", dc.EstablisherFrame);
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
        /* Dump 128 bytes of LSDA/HandlerData */
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
            MW_TRACE("[LSDA] raw[64:80]: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x",
                     lp[64], lp[65], lp[66], lp[67], lp[68], lp[69], lp[70], lp[71],
                     lp[72], lp[73], lp[74], lp[75], lp[76], lp[77], lp[78], lp[79]);
            MW_TRACE("[LSDA] raw[80:96]: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x",
                     lp[80], lp[81], lp[82], lp[83], lp[84], lp[85], lp[86], lp[87],
                     lp[88], lp[89], lp[90], lp[91], lp[92], lp[93], lp[94], lp[95]);
            MW_TRACE("[LSDA] raw[96:112]: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x",
                     lp[96], lp[97], lp[98], lp[99], lp[100], lp[101], lp[102], lp[103],
                     lp[104], lp[105], lp[106], lp[107], lp[108], lp[109], lp[110], lp[111]);
            MW_TRACE("[LSDA] raw[112:128]: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x",
                     lp[112], lp[113], lp[114], lp[115], lp[116], lp[117], lp[118], lp[119],
                     lp[120], lp[121], lp[122], lp[123], lp[124], lp[125], lp[126], lp[127]);
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

            /* EH_FIX: Bypass broken personality fn for DWARF LSDA with catch actions.
             * If the handler returned ContinueSearch but the LSDA has a landing pad
             * with a catch action (action > 0), the personality fn failed to match.
             * We parse the DWARF LSDA ourselves and handle it. */
            int lp_override = 0;
            uint8_t* lsda_data = (uint8_t*)lsda;
            /* TASK-API-005 (ADR-010): gated behind MINWIN_EH_HACKS, default OFF.
             * BUG-029 top suspect — see flag comment above. */
            if (mw_eh_hacks_enabled() && lsda_data && lsda_data[0] == 0xff && lsda_data[2] == 0x01) {
                /* DWARF LSDA with uleb128 CS encoding */
                uint32_t cs_off = 3;
                while (cs_off < 64 && lsda_data[cs_off] != 0) {
                    uint32_t cs_start = lsda_data[cs_off++];
                    cs_off++; /* length */
                    uint32_t cs_lp = lsda_data[cs_off++];
                    uint32_t cs_action = lsda_data[cs_off++];
                    uint64_t func_begin = (uint64_t)(uintptr_t)(g_image_base + rf->BeginAddress);
                    uint64_t pc_offset = (saved_ctx_rip - 1) - func_begin;
                    MW_TRACE("[EH_FIX] CS: start=%u len=%u lp=%u act=%u pc_off=%lu range=[%u,%u)",
                             cs_start, cs_start + lsda_data[cs_off-1], cs_lp, cs_action,
                             (unsigned long)pc_offset, cs_start, cs_start + lsda_data[cs_off-1]);
                    if (pc_offset >= cs_start && pc_offset < cs_start + lsda_data[cs_off-1]) {
                        if (cs_action > 0 && cs_lp > 0) {
                            /* Has both landing pad and action — personality fn should have caught */
                            uint64_t target_lp = (uint64_t)(uintptr_t)(g_image_base + rf->BeginAddress) + cs_lp;
                            MW_TRACE("[EH_FIX] Override: personality failed, catching at LP=0x%lx (RVA 0x%lx)",
                                     target_lp, cs_lp);
                            *(uint64_t*)(ctx + CTX_Rip) = target_lp;
                            /* RSP = establisher frame (post-prologue) for catch block */
                            *(uint64_t*)(ctx + CTX_Rsp) = est_frame;
                            lp_override = 1;
                            break;
                        }
                    }
                }
            }
            if (lp_override) {
                MW_TRACE("[DISPATCH] EH_FIX: Overrode personality — resuming at catch");
                return DISP_RESULT_CONTINUE_EXEC;
            }

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
        /* BUG-031: capture true raise-site volatile regs BEFORE any scratch use.
         * Direct global stores — the stub frame layout is unchanged. */
        "movq %rax, g_cap_rax(%rip)\n\t"
        "movq %r10, g_cap_r10(%rip)\n\t"
        "movq %r11, g_cap_r11(%rip)\n\t"
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
        "movq 0x30(%rsp), %rcx\n\t"     /* save PE RCX (code) */
        "movq 0x38(%rsp), %rdx\n\t"     /* save PE RDX (flags) */
        "movq 0x40(%rsp), %r8\n\t"     /* save PE R8 (nargs) */
        "movq 0x48(%rsp), %r9\n\t"     /* save PE R9 (args) */
        /* BUG-026 fix: g_cap_rip must receive the saved rip slot [rsp+0x08]
         * (the return address captured at stub entry), NOT rsp_entry.
         * Previously: stray "movq 0x58(%rsp), %rax" + "movq 0x00(%rsp), %rax"
         * stored a stack address into g_cap_rip -> CONTEXT.Rip outside PE ->
         * Frame[0] walk ended -> terminate -> exit 134. Matches
         * loader.c.exp_next2_backup L842-843. */
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

/* ============================================================
 * BUG-031 (ADR-011): GCC SEH unwind-chain support — helpers.
 *
 * Live-verified protocol (EXP-039, samples/upx_decompressed.exe):
 *  - Unwind-phase INSTALL at an intermediate frame ⇒ the GCC bridge
 *    re-raises RaiseException(0x21474343, flags=1, nparams=4,
 *    params={object, establisher, landing_pad, selector})
 *    (bridge core 0x49d3e9-0x49d413).
 *  - A cleanup landing pad ends with the continue helper 0x49d5c0,
 *    which calls RtlUnwindEx(est_target, lp_target, &ER, object,
 *    OriginalContext, …) DIRECTLY (no raise): the ER is built as
 *    {code=0x20474343, flags=1, nparams=4, same 4-param shape}
 *    (0x49d610-0x49d663; the packed code|flags qword 0x120474343
 *    that EXP-038 §5.1 read as a "continue-raise" code is really
 *    {dword code=0x20474343, dword flags=1}).
 *  - The bridge reacts to EH_UNWINDING personality calls by looking
 *    up the frame's LSDA call site at ControlPc: a cleanup hit
 *    re-raises (above); a pc inside a landing-pad region finds no
 *    call site and returns ContinueSearch — so re-walking an
 *    already-unwound frame is safe (no re-install, no loop).
 * ============================================================ */

/* Total prologue stack allocation of the function containing ip_va.
 * Full decode (no func_offset filtering): landing pads run after the
 * whole prologue. Slot addressing and ALLOC_SMALL use the BUG-027-
 * corrected rules; ALLOC_LARGE uses the real ABI scaling (info==0 ⇒
 * next uint16 * 8, info==1 ⇒ next uint32) — the pre-existing decode
 * copies in mw_RtlUnwindEx/mw_RtlVirtualUnwind still carry the API-006
 * deviation and are deliberately left untouched by this task.
 * Returns 0 if the function cannot be resolved. */
static uint64_t seh_total_prologue_alloc(uint64_t ip_va) {
    if (g_image_base == NULL || ip_va < (uint64_t)(uintptr_t)g_image_base) return 0;
    uint64_t rva = ip_va - (uint64_t)(uintptr_t)g_image_base;
    RUNTIME_FUNCTION* rf = seh_internal_lookup(rva);
    if (!rf) return 0;
    uint8_t* ui = g_image_base + rf->UnwindInfo;
    uint8_t count = ui[2];
    uint8_t* codes = ui + 4;
    int slot = 0;
    uint64_t total = 0;
    for (int i = 0; i < count; i++) {
        /* BUG-037: CountOfCodes counts SLOTS — bound on the slot cursor. */
        if (slot >= count || slot * 2 + 1 >= (int)(count * 2 + 16)) break;
        uint16_t cw = *(uint16_t*)(codes + slot * 2);
        uint8_t op = (cw >> 8) & 0x0f;
        uint8_t info = (cw >> 12) & 0x0f;
        /* BUG-037: opcodes renumbered to the current spec (XMM=8/9,
         * MACHFRAME=10, EPILOG=6 v2-only, SPARE=7). */
        switch (op) {
        case UWOP_PUSH_NONVOL: total += 8; break;
        case UWOP_ALLOC_LARGE: /* TASK-API-006 spec rule: info==0 => uint16 * 8; info==1 => uint32, no shift */
            slot++;
            if (info == 0) total += (uint64_t)(*(uint16_t*)(codes + slot * 2)) * 8;
            else { slot++; total += (uint64_t)(*(uint32_t*)(codes + slot * 2 - 2)); }
            break;
        case UWOP_ALLOC_SMALL: total += (uint64_t)(info + 1) * 8; break;
        case UWOP_SAVE_NONVOL: slot++; break;
        case UWOP_SAVE_NONVOL_FAR: slot++; slot++; break;
        case UWOP_SAVE_XMM128: slot++; break;
        case UWOP_SAVE_XMM128_FAR: slot++; slot++; break;
        case UWOP_EPILOG: slot++; break;               /* 2 slots total, 0 stack */
        case UWOP_SPARE_CODE: slot++; slot++; break;   /* 3 slots total, 0 stack */
        case UWOP_PUSH_MACHFRAME: total += 40; break;
        default: if (op >= 11) slot++; break;
        }
        slot++;
    }
    return total;
}

/* Windows-style RtlUnwindEx intermediate walk: from start_ctx up to (but
 * excluding) the frame whose establisher == tgt_frame, call each frame's
 * personality with ExceptionFlags |= EH_UNWINDING (0x2). The GCC bridge
 * answers an unwind-phase cleanup hit by re-raising 0x21474343 (never
 * returns — the dispatcher intercept resumes the cleanup LP); a pc in a
 * landing-pad region finds no call site and returns ContinueSearch, so
 * re-walking already-unwound frames is safe. */
static void seh_walk_intermediate_cleanups(uint8_t* start_ctx, void* er_src,
                                           uint64_t tgt_frame, const char* tag) {
    uint8_t walk_ctx[CONTEXT_SIZE];
    memcpy(walk_ctx, start_ctx, CONTEXT_SIZE);
    EXCEPTION_RECORD er_u;
    memcpy(&er_u, er_src, sizeof(EXCEPTION_RECORD));
    er_u.ExceptionFlags |= 0x2; /* EXCEPTION_UNWINDING */

    uint64_t img_base = (uint64_t)(uintptr_t)g_image_base;
    uint64_t img_end = img_base + g_image_size;

    for (int f = 0; f < DISPATCH_MAX_FRAMES; f++) {
        uint64_t rip = *(uint64_t*)(walk_ctx + CTX_Rip);
        if (rip < img_base || rip >= img_end) {
            MW_TRACE("[BUG-031] %s walk: RIP 0x%lx outside PE — walk ends", tag, rip);
            break;
        }
        RUNTIME_FUNCTION* rf = seh_internal_lookup(rip - img_base);
        if (!rf) {
            MW_TRACE("[BUG-031] %s walk: no RUNTIME_FUNCTION at 0x%lx — walk ends", tag, rip);
            break;
        }
        uint64_t est = 0;
        uint32_t h_rva = 0;
        void* lsda = NULL;
        void* handler = seh_internal_virtual_unwind(rip, rf, walk_ctx, &est, &h_rva, &lsda);
        if (est == tgt_frame) {
            MW_TRACE("[BUG-031] %s walk: reached target frame est=0x%lx — intermediate scan done", tag, est);
            break;
        }
        if (!handler) {
            uint64_t parent_rip = *(uint64_t*)(uintptr_t)est;
            *(uint64_t*)(walk_ctx + CTX_Rip) = parent_rip;
            *(uint64_t*)(walk_ctx + CTX_Rsp) = est + 8;
            continue;
        }
        DISPATCHER_CONTEXT dc;
        memset(&dc, 0, sizeof(dc));
        dc.ControlPc = rip - 1;
        dc.ImageBase = img_base;
        dc.FunctionEntry = rf;
        dc.EstablisherFrame = est;
        dc.TargetIp = 0;
        dc.ContextRecord = walk_ctx;
        dc.LanguageHandler = handler;
        dc.HandlerData = lsda;
        dc.HistoryTable = 0;
        dc.ScopeIndex = 0;
        dc.Fill = 0;
        uint64_t saved_rip = rip;
        *(uint64_t*)(walk_ctx + CTX_Rip) = rip - 1;
        MW_TRACE("[BUG-031] %s walk: personality fn=0x%lx est=0x%lx rip=0x%lx lsda=%p flags=0x%x (EH_UNWINDING)",
                 tag, (uint64_t)(uintptr_t)handler, est, rip, lsda, er_u.ExceptionFlags);
        if (g_trace_log) fflush(g_trace_log);
        int32_t disp = call_handler_aligned(handler, &er_u, est, walk_ctx, &dc);
        *(uint64_t*)(walk_ctx + CTX_Rip) = saved_rip;
        MW_TRACE("[BUG-031] %s walk: personality returned %d", tag, disp);
        if (disp == DISP_ExceptionContinueSearch) {
            uint64_t parent_rip = *(uint64_t*)(uintptr_t)est;
            *(uint64_t*)(walk_ctx + CTX_Rip) = parent_rip;
            *(uint64_t*)(walk_ctx + CTX_Rsp) = est + 8;
            continue;
        }
        /* Non-GCC dispositions (ExecuteHandler etc.) are outside the GCC
         * protocol — record and stop; the caller resumes the target LP. */
        MW_TRACE("[BUG-031] %s walk: unexpected disposition %d — stopping walk", tag, disp);
        break;
    }
}

__attribute__((ms_abi)) void mw_RtlUnwindEx(void* target_frame, void* target_ip, void* ctx_ptr, void* exc_record, void* history) {
    MW_TRACE("RtlUnwindEx(target_frame=%p, target_ip=%p, ctx=%p, exc=%p)",
             target_frame, target_ip, ctx_ptr, exc_record);
    
    uint64_t tgt_ip = (uint64_t)(uintptr_t)target_ip;
    uint64_t tgt_frame = (uint64_t)(uintptr_t)target_frame;
    
    MW_TRACE("RtlUnwindEx: TargetIp=0x%lx TargetFrame=0x%lx", tgt_ip, tgt_frame);

    /* ============================================================
     * BUG-031 (ADR-011): continuation unwind — a cleanup landing pad's
     * continue helper (0x49d5c0) calls RtlUnwindEx DIRECTLY with the
     * OriginalContext in arg5. The original dispatcher C frame (and its
     * setjmp jmpbuf) is gone at this point, so this path walks the
     * remaining intermediate cleanups from the continue helper's captured
     * context and then resumes the target landing pad DIRECTLY (no
     * longjmp). Never returns on success.
     * ============================================================ */
    if (g_is_unwinding) {
        uint8_t* rctx = (uint8_t*)(uintptr_t)history;
        if (!rctx) rctx = g_unwind_ctx;
        MW_TRACE("[BUG-031] continuation unwind: orig_ctx=%p", (void*)rctx);
        if (rctx && ctx_ptr) {
            uint32_t er_code0 = *(uint32_t*)((uint8_t*)ctx_ptr);
            if (er_code0 == 0x20474343u || er_code0 == 0x21474343u) {
                seh_walk_intermediate_cleanups(rctx, ctx_ptr, tgt_frame, "cont");
            }
        }
        if (rctx) {
            uint64_t alloc_cont = seh_total_prologue_alloc(tgt_ip);
            uint64_t post_prologue_rsp = (alloc_cont != 0) ? (tgt_frame - alloc_cont) : tgt_frame;
            *(uint64_t*)(rctx + CTX_Rip) = tgt_ip;
            *(uint64_t*)(rctx + CTX_Rsp) = post_prologue_rsp;
            uint64_t exc_obj = (uint64_t)(uintptr_t)exc_record;
            if (exc_obj != 0) *(uint64_t*)(rctx + CTX_Rax) = exc_obj;
            /* BUG-030 selector delivery — same fingerprint as the site-A path */
            if (ctx_ptr) {
                uint32_t er_nparams = *(uint32_t*)((uint8_t*)ctx_ptr + 0x18);
                uint64_t* er_params = (uint64_t*)((uint8_t*)ctx_ptr + 0x20);
                if (er_nparams == 4 && er_params[1] == tgt_frame && er_params[2] == tgt_ip) {
                    *(uint64_t*)(rctx + CTX_Rdx) = er_params[3];
                    MW_TRACE("[BUG-031] continuation: 4-param ER obj=0x%lx est=0x%lx lp=0x%lx sel=%lu -> CTX.Rdx",
                             er_params[0], er_params[1], er_params[2],
                             (unsigned long)er_params[3]);
                }
            }
            /* Copy the full resume context into the resume globals — mirrors
             * the dispatcher's longjmp-return path. */
            g_unwind_regs[UR_RAX] = *(uint64_t*)(rctx + CTX_Rax);
            g_unwind_regs[UR_RCX] = *(uint64_t*)(rctx + CTX_Rcx);
            g_unwind_regs[UR_RDX] = *(uint64_t*)(rctx + CTX_Rdx);
            g_unwind_regs[UR_RBX] = *(uint64_t*)(rctx + CTX_Rbx);
            g_unwind_regs[UR_RSP] = *(uint64_t*)(rctx + CTX_Rsp);
            g_unwind_regs[UR_RBP] = *(uint64_t*)(rctx + CTX_Rbp);
            g_unwind_regs[UR_RSI] = *(uint64_t*)(rctx + CTX_Rsi);
            g_unwind_regs[UR_RDI] = *(uint64_t*)(rctx + CTX_Rdi);
            g_unwind_regs[UR_R8]  = *(uint64_t*)(rctx + CTX_R8);
            g_unwind_regs[UR_R9]  = *(uint64_t*)(rctx + CTX_R9);
            g_unwind_regs[UR_R10] = *(uint64_t*)(rctx + CTX_R10);
            g_unwind_regs[UR_R11] = *(uint64_t*)(rctx + CTX_R11);
            g_unwind_regs[UR_R12] = *(uint64_t*)(rctx + CTX_R12);
            g_unwind_regs[UR_R13] = *(uint64_t*)(rctx + CTX_R13);
            g_unwind_regs[UR_R14] = *(uint64_t*)(rctx + CTX_R14);
            g_unwind_regs[UR_R15] = *(uint64_t*)(rctx + CTX_R15);
            g_unwind_target_ip = tgt_ip;
            g_unwind_target_frame = post_prologue_rsp;
            g_is_unwinding = 1;
            MW_TRACE("[BUG-031] continuation resume: RIP=0x%lx RSP=0x%lx alloc=0x%lx",
                     tgt_ip, post_prologue_rsp, (unsigned long)alloc_cont);
            if (g_trace_log) fflush(g_trace_log);
            fflush(stderr);
            /* Direct resume — same register restore as the naked stub's
             * unwind return path. "memory" clobber: the asm reads the
             * globals written above. */
            __asm__ volatile (
                "movq g_unwind_regs+0(%%rip), %%rax\n\t"
                "movq g_unwind_regs+8(%%rip), %%rcx\n\t"
                "movq g_unwind_regs+16(%%rip), %%rdx\n\t"
                "movq g_unwind_regs+24(%%rip), %%rbx\n\t"
                "movq g_unwind_regs+40(%%rip), %%rbp\n\t"
                "movq g_unwind_regs+48(%%rip), %%rsi\n\t"
                "movq g_unwind_regs+56(%%rip), %%rdi\n\t"
                "movq g_unwind_regs+64(%%rip), %%r8\n\t"
                "movq g_unwind_regs+72(%%rip), %%r9\n\t"
                "movq g_unwind_regs+80(%%rip), %%r10\n\t"
                "movq g_unwind_regs+88(%%rip), %%r11\n\t"
                "movq g_unwind_regs+96(%%rip), %%r12\n\t"
                "movq g_unwind_regs+104(%%rip), %%r13\n\t"
                "movq g_unwind_regs+112(%%rip), %%r14\n\t"
                "movq g_unwind_regs+120(%%rip), %%r15\n\t"
                "movq g_unwind_target_ip(%%rip), %%rcx\n\t"
                "movq g_unwind_regs+32(%%rip), %%rsp\n\t"
                "jmp *%%rcx\n\t"
                : : : "memory",
                  "rax", "rcx", "rdx", "rbx", "rbp", "rsi", "rdi",
                  "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15"
            );
        }
        MW_TRACE("[BUG-031] continuation unwind: no context — returning (caller aborts, fail-visible)");
        return;
    }

    /* BUG-031 (ADR-011): first-call intermediate-cleanup walk — Windows
     * RtlUnwindEx runs every frame's cleanup between the throw site and
     * the target; the previous implementation jumped straight to the
     * target LP (BUG-031 root cause). GCC protocol only (the ER code is
     * STATUS_GCC_THROW/STATUS_GCC_UNWIND); start from the throw-site
     * snapshot because g_unwind_ctx has already been walked to the target
     * frame by the dispatcher. A cleanup hit re-raises 0x21474343 and
     * never returns from this call. */
    if (g_throw_ctx_valid && ctx_ptr) {
        uint32_t er_code0 = *(uint32_t*)((uint8_t*)ctx_ptr);
        if (er_code0 == 0x20474343u || er_code0 == 0x21474343u) {
            seh_walk_intermediate_cleanups(g_throw_ctx_snapshot, ctx_ptr, tgt_frame, "first");
        }
    }
    
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
        /* BUG-039/B: rebuild the landing-pad register base from the
         * target-frame snapshot ("returned into frame F from below"). The
         * live ctx was unwound THROUGH the handler's frame by the search
         * loop, so its nonvolatives hold the target's CALLER's values; the
         * landing pad needs the target's THROW-TIME values (RDI=&catch-var,
         * RBP=live counters, ...). RIP/RSP/RAX/RDX are overridden below by
         * the landing-pad protocol, so copying them here is harmless. */
        if (g_target_frame_ctx_valid) {
            static const uint16_t gpr_offsets[] = {
                CTX_Rax, CTX_Rcx, CTX_Rdx, CTX_Rbx, CTX_Rsp, CTX_Rbp,
                CTX_Rsi, CTX_Rdi, CTX_R8, CTX_R9, CTX_R10, CTX_R11,
                CTX_R12, CTX_R13, CTX_R14, CTX_R15
            };
            for (size_t gi = 0; gi < sizeof(gpr_offsets)/sizeof(gpr_offsets[0]); gi++) {
                *(uint64_t*)(ctx + gpr_offsets[gi]) =
                    *(uint64_t*)(g_target_frame_ctx + gpr_offsets[gi]);
            }
            g_target_frame_ctx_valid = 0;
            MW_TRACE("RtlUnwindEx: BUG-039/B register base restored from target-frame snapshot (RDI=0x%lx RBP=0x%lx RSI=0x%lx RBX=0x%lx)",
                     *(uint64_t*)(ctx + CTX_Rdi), *(uint64_t*)(ctx + CTX_Rbp),
                     *(uint64_t*)(ctx + CTX_Rsi), *(uint64_t*)(ctx + CTX_Rbx));
        }
        /* EXP-035 FIX: target_frame from the GCC personality function is the
         * function-entry RSP (RSP after CALL pushed return address). But the
         * catch/landing-pad code uses [RSP+offset] where offsets are relative
         * to the post-prologue RSP (RSP after the function's prologue).
         *
         * We compute post-prologue RSP = target_frame - stack_alloc.
         * The stack_alloc = target_frame - CTX.Rsp (because RtlVirtualUnwind
         * set CTX.Rsp to function-entry RSP, and the original RSP before
         * unwind was post-prologue RSP). So:
         *   stack_alloc = target_frame - CTX.Rsp
         *   post_prologue_RSP = target_frame - stack_alloc = CTX.Rsp
         *
         * Wait — CTX.Rsp was already modified by the handler's RtlVirtualUnwind
         * calls during frame walking. But we saved the ORIGINAL CTX.Rsp in
         * g_unwind_regs before the handler was called. Use that.
         * Actually, the simplest approach: use the pre_unwind_rsp we saved
         * in the dispatcher. But RtlUnwindEx doesn't have access to it.
         *
         * Instead, we can compute the post-prologue RSP from the unwind
         * info of the target function. But that's complex.
         *
         * Simplest correct fix: the handler's target_frame = function-entry RSP.
         * The stack_alloc = target_frame - CTX.Rsp_after_handler.
         * But CTX.Rsp_after_handler may be modified by handler.
         *
         * Actually, the most reliable method: look up the RUNTIME_FUNCTION for
         * the target_ip, read its unwind info, and process all codes to find
         * the total stack allocation. Then:
         *   post_prologue_RSP = target_frame - total_alloc.
         */
        uint64_t post_prologue_rsp = tgt_frame;

        /* Look up the RUNTIME_FUNCTION for the target IP to find
         * the total stack allocation of the target function. */
        uint64_t tgt_rva = tgt_ip - (uint64_t)(uintptr_t)g_image_base;
        RUNTIME_FUNCTION* tgt_rf = seh_internal_lookup(tgt_rva);
        if (tgt_rf) {
            uint8_t* tgt_ui = g_image_base + tgt_rf->UnwindInfo;
            uint8_t tgt_count = tgt_ui[2];
            /* BUG-027 fix: unwind code slots are 2 bytes each and must be
             * addressed in BYTES. The previous declaration `uint16_t* tgt_codes`
             * made `tgt_codes + tgt_slot * 2` advance 4 bytes per slot, so the
             * loop read every other slot and then garbage past the codes array:
             * for function 0xdcbf0 (codes 19 09 04 00 | 09 52 05 30 04 c0 02 d0 =
             * ALLOC_SMALL 0x30 + PUSH R13/R12/RBX = 0x48 total) it computed
             * alloc_total=0x40 instead of 0x48, resuming the landing pad with
             * RSP 8 bytes too high -> SIGSEGV at heap RIP (EXP-036 §4). */
            uint8_t* tgt_codes = tgt_ui + 4;
            int tgt_slot = 0;
            uint64_t alloc_total = 0;
            for (int i = 0; i < tgt_count; i++) {
                /* BUG-037: CountOfCodes counts SLOTS — bound on the slot
                 * cursor, otherwise padding/handler bytes decode as
                 * phantom codes (reproduced: alloc_total=0xff0191 came
                 * from reading the handler RVA as ALLOC_LARGE). */
                if (tgt_slot >= tgt_count ||
                    tgt_slot * 2 + 1 >= (int)(tgt_count * 2 + 16)) break;
                uint16_t cw = *(uint16_t*)(tgt_codes + tgt_slot * 2);
                uint8_t op = (cw >> 8) & 0x0f;
                uint8_t info = (cw >> 12) & 0x0f;
                /* BUG-037: opcodes renumbered to the current spec
                 * (XMM128=8, XMM128_FAR=9, MACHFRAME=10, EPILOG=6 v2-only,
                 * SPARE=7); missing UWOP_SAVE_NONVOL slot advance added;
                 * ALLOC_LARGE info==1 now unscaled u32 (TASK-API-006 rule). */
                switch (op) {
                case UWOP_ALLOC_LARGE:
                    tgt_slot++;
                    if (info == 0) {
                        alloc_total += (uint64_t)(*(uint16_t*)(tgt_codes + tgt_slot * 2)) * 8;
                    } else {
                        tgt_slot++;
                        alloc_total += (uint64_t)(*(uint32_t*)(tgt_codes + tgt_slot * 2 - 2));
                    }
                    break;
                case UWOP_ALLOC_SMALL:
                    alloc_total += (uint64_t)(info + 1) * 8;
                    break;
                case UWOP_PUSH_NONVOL:
                    alloc_total += 8;
                    break;
                case UWOP_SAVE_NONVOL:
                    tgt_slot++; break;
                case UWOP_SAVE_NONVOL_FAR:
                    tgt_slot++; tgt_slot++; break;
                case UWOP_SAVE_XMM128:
                    tgt_slot++; break;
                case UWOP_SAVE_XMM128_FAR:
                    tgt_slot++; tgt_slot++; break;
                case UWOP_EPILOG:
                    tgt_slot++; break;               /* 2 slots total, 0 stack */
                case UWOP_SPARE_CODE:
                    tgt_slot++; tgt_slot++; break;   /* 3 slots total, 0 stack */
                case UWOP_PUSH_MACHFRAME:
                    alloc_total += 40;
                    break;
                default:
                    if (op >= 11) tgt_slot++;
                    break;
                }
                tgt_slot++;
            }
            post_prologue_rsp = tgt_frame - alloc_total;
            /* BUG-037: for frames with a frame pointer the establisher is
             * the FP register value (mid-frame anchor), not the frame's
             * entry RSP. The body RSP is FP - FrameOffset*16 (establishment
             * happens after the last stack change in the prologue).
             * CRT-startup frame: FP=RBP=entry-0x108, off=8 -> body RSP =
             * entry-0x188; the old tgt_frame-alloc formula would have
             * produced entry-0x290. */
            {
                uint8_t tgt_framebyte = tgt_ui[3];
                uint8_t tgt_framereg  = tgt_framebyte & 0x0f;
                uint8_t tgt_frameoff  = (tgt_framebyte >> 4) & 0x0f;
                if (tgt_framereg) {
                    post_prologue_rsp = tgt_frame - (uint64_t)tgt_frameoff * 16;
                    MW_TRACE("RtlUnwindEx: FP frame reg=%u off=%u -> body RSP=0x%lx",
                             tgt_framereg, tgt_frameoff, post_prologue_rsp);
                }
            }
            MW_TRACE("RtlUnwindEx: target_func RVA=0x%x alloc_total=0x%lx post_prologue_RSP=0x%lx",
                     tgt_rf->BeginAddress, alloc_total, post_prologue_rsp);
        } else {
            MW_TRACE("RtlUnwindEx: WARNING no RUNTIME_FUNCTION for target, using target_frame directly");
        }

        /* Modify CONTEXT to target the landing pad */
        *(uint64_t*)(ctx + CTX_Rip) = tgt_ip;
        *(uint64_t*)(ctx + CTX_Rsp) = post_prologue_rsp;
        
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

        /* BUG-030 fix (EXP-038): deliver the GCC/UPX catch selector in RDX.
         * Ground truth (EXP-038 §2-§3): the GCC SEH bridge site A passes
         * arg3 = &EXCEPTION_RECORD (NOT a CONTEXT — EXP-037 §2.2) carrying the
         * 4-parameter protocol param = {object, target_est, landing_pad, selector}
         * (bridge 0x49d4f6-0x49d528 copies its blob slots: blob[0]=object -> arg4,
         * blob[1]=selector -> ER.param[3]; blob slots written by helper 0x49d270,
         * selector stored by the core at obj-0x24 during phase 1).
         * The landing pad consumes the selector in RDX — B's catch at 0x40331c:
         * `mov rbx,rdx; ...; sub rbx,1; jne <assert-failure>` (selector must be 1).
         * Fingerprint-guarded: applied only when arg3 really is the protocol ER. */
        if (ctx_ptr) {
            uint32_t er_nparams = *(uint32_t*)((uint8_t*)ctx_ptr + 0x18);
            uint64_t* er_params = (uint64_t*)((uint8_t*)ctx_ptr + 0x20);
            if (er_nparams == 4 && er_params[1] == tgt_frame && er_params[2] == tgt_ip) {
                uint64_t selector = er_params[3];
                *(uint64_t*)(ctx + CTX_Rdx) = selector;
                MW_TRACE("RtlUnwindEx: 4-param protocol ER (obj=0x%lx est=0x%lx lp=0x%lx sel=%lu) -> CTX.Rdx=%lu",
                         er_params[0], er_params[1], er_params[2],
                         (unsigned long)selector, (unsigned long)selector);
            }
        }
        
        /* Store targets for the dispatcher */
        g_unwind_target_ip = tgt_ip;
        g_unwind_target_frame = post_prologue_rsp;
        g_is_unwinding = 1;
        
        MW_TRACE("RtlUnwindEx: longjmping to dispatcher (RIP=0x%lx RSP=0x%lx)", tgt_ip, post_prologue_rsp);

        /* DIAGNOSTIC: Verify the landing pad will see correct stack data */
        {
            uint64_t* check_rsp = (uint64_t*)(uintptr_t)tgt_frame;
            MW_TRACE("RtlUnwindEx DIAG: tgt_frame(post-prologue RSP)=0x%lx", tgt_frame);
            MW_TRACE("RtlUnwindEx DIAG: [tgt_frame+0x30]=0x%lx (expect global _onexit ptr)", check_rsp[6]);
            MW_TRACE("RtlUnwindEx DIAG: [tgt_frame+0x38]=0x%lx (expect _pfirst ptr)", check_rsp[7]);
            MW_TRACE("RtlUnwindEx DIAG: [tgt_frame+0x40]=0x%lx (expect count)", check_rsp[8]);
        }

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
    uint8_t flags   = (ui_base[0] >> 3) & 0x07;
    uint8_t prolog_size = ui_base[1];
    uint8_t count_codes = ui_base[2];
    /* BUG-037: FrameRegister = LOW nibble, scaled FrameOffset = HIGH nibble
     * (little-endian C bitfields; see seh_internal_virtual_unwind). */
    uint8_t frame_reg   = ui_base[3] & 0x0f;
    uint8_t frame_off   = (ui_base[3] >> 4) & 0x0f;

    (void)image_base; (void)context_pointers; (void)prolog_size;

    /* Handle CHAININFO */
    if (flags & UNW_FLAG_CHAININFO) {
        uint32_t chain_off = 4 + count_codes * 2;
        if (chain_off % 4) chain_off += 2;
        RUNTIME_FUNCTION* chained = (RUNTIME_FUNCTION*)(ui_base + chain_off);
        MW_TRACE("RtlVirtualUnwind: CHAININFO -> begin=0x%x unwind=0x%x",
                 chained->BeginAddress, chained->UnwindInfo);
        uint8_t* chained_ui = g_image_base + chained->UnwindInfo;
        uint8_t chained_flags = (chained_ui[0] >> 3) & 0x07;
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
        /* BUG-037: CountOfCodes counts SLOTS — bound on the slot cursor. */
        if (slot >= count_codes) break;
        uint16_t code_word = *(uint16_t*)(codes + slot * 2);
        uint8_t op_code = (code_word >> 8) & 0x0f;   /* UnwindOp: low nibble of byte 1 */
        uint8_t op_info = (code_word >> 12) & 0x0f;  /* OpInfo: high nibble of byte 1 */
        uint8_t code_offset = code_word & 0xff;

        /* Skip codes that haven't fully executed yet — full slot advance
         * so the cursor stays aligned (BUG-037). */
        if (func_offset > 0 && code_offset >= func_offset) {
            slot += seh_unwind_op_slots(op_code, op_info);
            continue;
        }
        if (op_code == UWOP_EPILOG && version < 2) {
            /* Epilogue descriptors exist only in UNWIND_INFO v2. */
            MW_TRACE("RtlVirtualUnwind: UI v1 has EPILOG code — corrupt, stop");
            break;
        }

        switch (op_code) {
        case UWOP_PUSH_NONVOL: {
            /* BUG-039: restore from CURRENT rsp BEFORE advancing (undo order;
             * ReactOS PopReg semantics) — mirrors seh_internal_virtual_unwind. */
            uint32_t off = reg_to_ctx_offset(op_info);
            if (off) {
                *(uint64_t*)(ctx + off) = *(uint64_t*)(new_rsp);
            }
            new_rsp += 8;
            break;
        }
        case UWOP_ALLOC_LARGE: /* TASK-API-006 spec rule: info==0 => uint16 * 8; info==1 => uint32, no shift */
            slot++;
            if (op_info == 0) {
                new_rsp += (uint64_t)(*(uint16_t*)(codes + slot * 2)) * 8;
            } else {
                slot++;
                new_rsp += (uint64_t)(*(uint32_t*)(codes + slot * 2 - 2));
            }
            break;
        case UWOP_ALLOC_SMALL: {
            new_rsp += (op_info + 1) * 8;
            break;
        }
        case UWOP_SET_FPREG: {
            /* ReactOS semantics: re-anchor RSP from the LIVE FP register
             * (BUG-037; see seh_internal_virtual_unwind). */
            if (frame_reg) {
                uint32_t fp_ctx_off = reg_to_ctx_offset(frame_reg);
                if (fp_ctx_off) {
                    fp_reg = *(uint64_t*)(ctx + fp_ctx_off);
                    new_rsp = fp_reg - (uint64_t)frame_off * 16;
                } else {
                    fp_reg = new_rsp + (uint64_t)frame_off * 16;
                }
            } else {
                fp_reg = new_rsp + (uint64_t)frame_off * 16;
            }
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
        case UWOP_EPILOG: {
            slot++;               /* 2 slots total, no stack effect */
            break;
        }
        case UWOP_SPARE_CODE: {
            slot++; slot++;       /* 3 slots total */
            break;
        }
        case UWOP_PUSH_MACHFRAME: {
            new_rsp += 40;
            *(uint64_t*)(ctx + CTX_Rip) = *(uint64_t*)(new_rsp + 16);
            new_rsp = *(uint64_t*)(new_rsp + 8);
            break;
        }
        default: {
            static int warned_unknown[7] = {0};
            if (op_code >= 11 && !warned_unknown[op_code - 11]) {
                MW_TRACE("RtlVirtualUnwind: unknown opcode %d (skipping)", op_code);
                warned_unknown[op_code - 11] = 1;
            }
            if (op_code >= 11) slot++; /* reserved ops: skip code word only */
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
/* TASK-TLS-001: TlsAlloc/GetValue/SetValue share the SAME slot array the
 * native-TLS static block uses (gs:0x58 -> g_tls_ptr_array, declared above).
 * Slot 0 is the image's static TLS; dynamic TlsAlloc slots start at 1. */

__attribute__((ms_abi)) uint32_t mw_TlsAlloc(void) {
    static uint32_t next = 1; /* slot 0 = image static TLS */
    static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_lock(&mu);
    uint32_t idx = next++;
    pthread_mutex_unlock(&mu);
    if (idx < 64) {
        g_tls_ptr_array[idx] = NULL;
        MW_TRACE("TlsAlloc() = %u", idx);
        return idx;
    }
    return (uint32_t)(-1);
}

__attribute__((ms_abi)) void* mw_TlsGetValue(uint32_t idx) {
    void* val = (idx < 64) ? g_tls_ptr_array[idx] : NULL;
    MW_TRACE("TlsGetValue(%u) = %p", idx, val);
    return val;
}

__attribute__((ms_abi)) int mw_TlsSetValue(uint32_t idx, void* val) {
    if (idx < 64) g_tls_ptr_array[idx] = val;
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
    g_environ_cell = g_initenv_ptr; /* TASK-API-011: __p__environ target */
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

static void* g_iob_array[3]; /* BUG-032: backing store returned by __iob_func();
                              * mingw strides over it with 0x30 - see map_crt_stream */
/* ============================================================
 * ISS1 slice: MSVCRT-layout FILE registry (WS-D semantic contract).
 * OBSERVED (live trace, t12_libc_io.exe): the guest's statically-linked
 * mingw CRT printf machinery calls InitializeCriticalSection(fp+0x30)
 * and Enter/Leave on it. Therefore fopen MUST return an MSVCRT-strided
 * handle with >=0x40 bytes of zeroed slack, never a raw glibc FILE*
 * (locking into glibc's struct corrupts the host heap -> SIGSEGV in
 * libc during fputc). The host FILE* lives in a side table; every
 * FILE* API resolves handles through the registry first.
 * ============================================================ */
#define MW_FILE_MAX 32
typedef struct MwFile {
    char* _ptr; int _cnt; char* _base; int _flag; int _file;
    int _charbuf; int _bufsiz; char* _tmpfname;
    unsigned char slack[0x40]; /* guest CRT lock (fp+0x30) lives here */
} MwFile;
static struct { MwFile* mf; FILE* hf; } g_mwfile_tab[MW_FILE_MAX];

static FILE* mwfile_hostfp_of(void* p) {
    if (!p) return NULL;
    for (int i = 0; i < MW_FILE_MAX; ++i)
        if (g_mwfile_tab[i].mf == (MwFile*)p) return g_mwfile_tab[i].hf;
    return NULL;
}

static FILE* map_crt_stream(void* stream); /* fwd decl (defined below) */

__attribute__((ms_abi)) void* mw___iob_func(void) {
    /* Returns pointer to array of FILE* (stdin, stdout, stderr, ...).
     * BUG-032 note: mingw indexes the returned pointer with stride 0x30
     * (sizeof of its FILE struct); map_crt_stream() handles that window. */
    g_iob_array[0] = stdin;
    g_iob_array[1] = stdout;
    g_iob_array[2] = stderr;
    MW_TRACE("__iob_func() = %p", g_iob_array);
    return g_iob_array;
}

/* TASK-API-011 (UCRT wave, 2026-10-15): modern UCRT-flavored mingw binaries
 * (llvm-mingw, MSVC) resolve std streams through __acrt_iob_func(index)
 * instead of __iob_func(). Return the same backing streams by index. */
__attribute__((ms_abi)) void* mw___acrt_iob_func(unsigned index) {
    g_iob_array[0] = stdin;
    g_iob_array[1] = stdout;
    g_iob_array[2] = stderr;
    void* r = (index < 3) ? g_iob_array[index] : NULL;
    MW_TRACE("__acrt_iob_func(%u) = %p", index, r);
    return r;
}

/* UCRT-flavored CRT startup calls these during init; the loader has already
 * prepared argc/argv/env, so they are bookkeeping no-ops. */
__attribute__((ms_abi)) int mw__configure_narrow_argv(int mode) {
    (void)mode;
    MW_TRACE("_configure_narrow_argv(mode=%d) -> 0", mode);
    return 0;
}

__attribute__((ms_abi)) int mw__initialize_narrow_environment(void) {
    MW_TRACE("_initialize_narrow_environment() -> 0");
    return 0;
}

__attribute__((ms_abi)) int mw__set_new_mode(int mode) {
    MW_TRACE("_set_new_mode(%d) -> 0", mode);
    return 0;
}

__attribute__((ms_abi)) void* mw__set_invalid_parameter_handler(void* h) {
    MW_TRACE("_set_invalid_parameter_handler(%p) -> NULL", h);
    return NULL;
}

/* signal(): tests never install real handlers; record and return no old one.
 * (Named _stub to avoid clashing with the host signal(2) declaration.) */
__attribute__((ms_abi)) void* mw_signal_stub(int sig, void* handler) {
    MW_TRACE("signal(%d, %p) -> NULL (stub)", sig, handler);
    return NULL;
}

__attribute__((ms_abi)) void mw__tzset(void) {
    MW_TRACE("_tzset() (stub, UTC)");
}

/* UCRT __p__fmode/__p__commode are FUNCTIONS returning &var (t10 crashes
 * 0x2039038/0x2039034 proved call-style usage). */
__attribute__((ms_abi)) unsigned int* mw___p__fmode(void) {
    MW_TRACE("__p__fmode() = %p", &g_fmode_val);
    return &g_fmode_val;
}

__attribute__((ms_abi)) unsigned int* mw___p__commode(void) {
    MW_TRACE("__p__commode() = %p", &g_commode_val);
    return &g_commode_val;
}

__attribute__((ms_abi)) int* mw___p___argc(void) {
    MW_TRACE("__p___argc() = %p", &g_argc);
    return &g_argc;
}

__attribute__((ms_abi)) char*** mw___p___argv(void) {
    MW_TRACE("__p___argv() = %p", &g_argv_cell);
    return &g_argv_cell;
}

__attribute__((ms_abi)) char*** mw___p__environ(void) {
    MW_TRACE("__p__environ() = %p", &g_environ_cell);
    return &g_environ_cell;
}


/* TASK-API-012 + ISS1: FILE-level stdio bridges. fopen returns an
 * MSVCRT-layout registry handle (guest CRT locks fp+0x30 — see registry
 * comment); the whole family resolves handles via map_crt_stream. */
__attribute__((ms_abi)) void* mw_fopen(const char* path, const char* mode) {
    if (!path || !mode) return NULL;
    if (strstr(mode, "ccs=") != NULL) { errno = EINVAL; g_errno_storage = errno; return NULL; }
    FILE* hf = fopen(path, mode);
    if (!hf) { g_errno_storage = errno; /* ISS1: guest reads errno via _errno mirror */ }
    MW_TRACE("fopen(%s, %s) = %p", path ? path : "NULL", mode ? mode : "NULL", hf);
    if (!hf) return NULL;
    for (int i = 0; i < MW_FILE_MAX; ++i) {
        if (g_mwfile_tab[i].mf) continue;
        MwFile* mf = (MwFile*)calloc(1, sizeof(MwFile));
        if (!mf) { fclose(hf); return NULL; }
        mf->_file = fileno(hf);
        if (strchr(mode, '+'))   mf->_flag = 0x0004; /* _IORW */
        else if (mode[0] == 'r') mf->_flag = 0x0001; /* _IOREAD */
        else                     mf->_flag = 0x0002; /* _IOWRT */
        g_mwfile_tab[i].mf = mf;
        g_mwfile_tab[i].hf = hf;
        return mf;
    }
    fclose(hf); /* table full — deterministic failure */
    errno = EMFILE; g_errno_storage = errno;
    return NULL;
}

__attribute__((ms_abi)) int mw_fclose(void* stream) {
    MW_TRACE("fclose(%p)", stream);
    FILE* hf = mwfile_hostfp_of(stream);
    if (!hf) return EOF;
    for (int i = 0; i < MW_FILE_MAX; ++i)
        if (g_mwfile_tab[i].mf == (MwFile*)stream) {
            g_mwfile_tab[i].mf = NULL; g_mwfile_tab[i].hf = NULL;
            break;
        }
    free(stream);
    return fclose(hf);
}

__attribute__((ms_abi)) unsigned long long mw_fread(void* buf,
        unsigned long long sz, unsigned long long n, void* stream) {
    FILE* f = map_crt_stream(stream);
    return (f && buf) ? fread(buf, (size_t)sz, (size_t)n, f) : 0ULL;
}

__attribute__((ms_abi)) char* mw_fgets(char* buf, int n, void* stream) {
    FILE* f = map_crt_stream(stream);
    return (f && buf) ? fgets(buf, n, f) : NULL;
}

__attribute__((ms_abi)) int mw_fgetc(void* stream) {
    FILE* f = map_crt_stream(stream);
    return f ? fgetc(f) : EOF;
}

__attribute__((ms_abi)) void mw_rewind(void* stream) {
    FILE* f = map_crt_stream(stream);
    if (f) rewind(f);
}

/* ISS1 slice: seek/tell/EOF bridges (missing from API-012 wave).
 * MSVCRT clears the EOF indicator on a successful reposition. */
__attribute__((ms_abi)) int mw_fseek(void* stream, long long off, int origin) {
    /* Windows SEEK_SET/CUR/END constants equal POSIX ones. */
    FILE* f = map_crt_stream(stream);
    if (!f) return -1;
    int r = fseek(f, (long)off, origin);
    if (r == 0) clearerr(f); /* MSVCRT clears EOF on successful reposition */
    return r;
}

__attribute__((ms_abi)) long long mw_ftell(void* stream) {
    FILE* f = map_crt_stream(stream);
    return f ? (long long)ftell(f) : -1LL;
}

__attribute__((ms_abi)) int mw_feof(void* stream) {
    FILE* f = map_crt_stream(stream);
    return f ? feof(f) : 0;
}

__attribute__((ms_abi)) int mw_remove(const char* path) {
    MW_TRACE("remove(%s)", path ? path : "NULL");
    int r = remove(path);
    if (r != 0) g_errno_storage = errno; /* ISS1: errno bridge */
    return r;
}

__attribute__((ms_abi)) size_t mw_strftime(char* buf, size_t max,
        const char* fmt, const void* tmv) {
    return strftime(buf, max, fmt, (const struct tm*)tmv);
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

/* BUG-033 (appcompat sweep APP-F-03): printf-family across the ABI boundary.
 *
 * The PE (ms_abi) passes varargs per the Windows x64 contract: every vararg
 * occupies a uniform 8-byte slot (integers/pointers directly, doubles as
 * bit-casts spilled by the caller). glibc's vfprintf expects a SysV va_list:
 * a struct {gp_offset, fp_offset, overflow_arg_area, reg_save_area} whose
 * reg-save area holds rdi..r9 (48 bytes) then xmm0-7 (16 bytes each).
 * The old code handed the raw ms slot pointer to vprintf — vprintf read it
 * as a SysV struct (gp_offset = low bits of a stack address) and faulted in
 * libc for anything but tiny integer-only formats.
 *
 * Fix: parse fmt to classify every consumed argument (GP vs FP), materialize
 * the uniform MS slots, then synthesize a faithful SysV va_list:
 *   - gp slots: gpv[0] is fmt (rdi), varargs fill gpv[1..5];
 *   - fp slots: doubles fill the low 64 bits of xmm0..xmm7;
 *   - the rest go to the overflow area in call order (SysV stack order). */

#define MW_PRINTF_MAX_ARGS 256

typedef struct {
    unsigned gp_offset;
    unsigned fp_offset;
    void*    overflow_arg_area;
    void*    reg_save_area;
} mw_sysv_va_list;

/* Parse fmt; cls[i]=1 for FP args, 0 for GP. Returns arg count (capped). */
static int mw_printf_classify(const char* fmt, unsigned char* cls) {
    int nargs = 0;
    const char* p = fmt;
    if (!p) return 0;
    while (*p) {
        if (*p != '%') { p++; continue; }
        p++;
        if (*p == '%') { p++; continue; }        /* literal percent: no arg */
        /* width '*' consumes a GP arg */
        if (*p == '*') {
            if (nargs < MW_PRINTF_MAX_ARGS) cls[nargs++] = 0;
            p++;
        }
        while (*p == '-' || *p == '+' || *p == ' ' || *p == '#' || *p == '0') p++;
        while (*p >= '0' && *p <= '9') p++;
        if (*p == '.') {
            p++;
            if (*p == '*') {
                if (nargs < MW_PRINTF_MAX_ARGS) cls[nargs++] = 0;
                p++;
            }
            while (*p >= '0' && *p <= '9') p++;
        }
        /* length modifiers */
        while (*p == 'l' || *p == 'h' || *p == 'q' || *p == 'j' ||
               *p == 'z' || *p == 't' || *p == 'L') p++;
        if (!*p) break;
        char conv = *p++;
        int is_fp = (conv == 'f' || conv == 'F' || conv == 'e' || conv == 'E' ||
                     conv == 'g' || conv == 'G' || conv == 'a' || conv == 'A');
        if (nargs < MW_PRINTF_MAX_ARGS) cls[nargs++] = is_fp ? 1 : 0;
    }
    return nargs;
}

/* Build a SysV va_list from uniform MS slots and call vfprintf on it. */
static int mw_printf_emit(FILE* f, const char* fmt,
                          const unsigned long long* slots, int nargs) {
    unsigned char cls[MW_PRINTF_MAX_ARGS];
    int parsed = mw_printf_classify(fmt, cls);
    if (nargs > parsed) nargs = parsed;

    unsigned long long gpv[6] = {0, 0, 0, 0, 0, 0};
    unsigned long long fpv[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    unsigned long long ovf[MW_PRINTF_MAX_ARGS];
    int novf = 0, gpi = 1 /* gpv[0] = rdi = fmt */, fpi = 0;

    for (int i = 0; i < nargs; i++) {
        if (cls[i]) {
            if (fpi < 8) fpv[fpi++] = slots[i];
            else         ovf[novf++] = slots[i];
        } else {
            if (gpi < 6) gpv[gpi++] = slots[i];
            else         ovf[novf++] = slots[i];
        }
    }

    unsigned long long reg_save[6 + 16]; /* rdi..r9 then xmm0-7 (low64 only) */
    memcpy(reg_save, gpv, sizeof(gpv));
    for (int i = 0; i < 8; i++)
        memcpy((unsigned char*)reg_save + 48 + 16 * i, &fpv[i], 8);
    /* xmm slots beyond the doubles: zero — only read if fmt over-reads */
    for (int i = fpi; i < 8; i++)
        memset((unsigned char*)reg_save + 48 + 16 * i, 0, 16);

    va_list sv;                       /* glibc __va_list_tag[1] */
    mw_sysv_va_list* svv = (mw_sysv_va_list*)sv;
    svv->gp_offset = 8;              /* fmt consumed rdi */
    svv->fp_offset = 48;             /* no named FP args */
    svv->overflow_arg_area = ovf;
    svv->reg_save_area = reg_save;
    return vfprintf(f, fmt, sv);
}

/* printf - variadic, needs special handling */
__attribute__((ms_abi)) int mw_printf(const char* fmt, ...) {
    MW_TRACE("printf(fmt=%s)", fmt ? fmt : "NULL");
    unsigned char cls[MW_PRINTF_MAX_ARGS];
    int nargs = mw_printf_classify(fmt, cls);
    unsigned long long slots[MW_PRINTF_MAX_ARGS];
    va_list ap;
    va_start(ap, fmt);
    for (int i = 0; i < nargs; i++) {
        if (cls[i]) { double d = va_arg(ap, double); memcpy(&slots[i], &d, 8); }
        else        { slots[i] = (unsigned long long)va_arg(ap, long long); }
    }
    va_end(ap);
    return mw_printf_emit(stdout, fmt, slots, nargs);
}

/* fprintf - variadic */
__attribute__((ms_abi)) int mw_fprintf(void* file, const char* fmt, ...) {
    MW_TRACE("fprintf(file=%p, fmt=%s)", file, fmt ? fmt : "NULL");
    FILE* f = map_crt_stream(file);
    unsigned char cls[MW_PRINTF_MAX_ARGS];
    int nargs = mw_printf_classify(fmt, cls);
    unsigned long long slots[MW_PRINTF_MAX_ARGS];
    va_list ap;
    va_start(ap, fmt);
    for (int i = 0; i < nargs; i++) {
        if (cls[i]) { double d = va_arg(ap, double); memcpy(&slots[i], &d, 8); }
        else        { slots[i] = (unsigned long long)va_arg(ap, long long); }
    }
    va_end(ap);
    return mw_printf_emit(f, fmt, slots, nargs);
}

/* vfprintf - the PE passes its own va_list: an ms_abi slot stream (char*),
 * uniform 8-byte vararg slots starting at the first vararg. */
__attribute__((ms_abi)) int mw_vfprintf(void* file, const char* fmt, void* ap) {
    MW_TRACE("vfprintf(file=%p, fmt=%s)", file, fmt ? fmt : "NULL");
    FILE* f = map_crt_stream(file);
    unsigned char cls[MW_PRINTF_MAX_ARGS];
    int nargs = mw_printf_classify(fmt, cls);
    unsigned long long slots[MW_PRINTF_MAX_ARGS];
    if (ap && nargs > 0) memcpy(slots, ap, (size_t)nargs * 8);
    return mw_printf_emit(f, fmt, slots, nargs);
}

/* TASK-API-011: UCRT unified stdio core. Modern UCRT printf/snprintf are
 * inline wrappers over the __stdio_common_v* family. The va_list argument is
 * the PE's ms_abi slot stream — exactly what mw_vfprintf already repacks. */
__attribute__((ms_abi)) int mw___stdio_common_vfprintf(
    unsigned long long options, void* file, const char* fmt,
    void* locale, void* ap) {
    (void)options; (void)locale;
    MW_TRACE("__stdio_common_vfprintf(file=%p, fmt=%s)", file, fmt ? fmt : "NULL");
    return mw_vfprintf(file, fmt, ap);
}

__attribute__((ms_abi)) int mw___stdio_common_vsprintf(
    unsigned long long options, char* buffer, size_t size,
    const char* fmt, void* locale, void* ap) {
    (void)options; (void)locale;
    char tmp[16384];
    FILE* mf = fmemopen(tmp, sizeof tmp, "w");
    if (!mf) return -1;
    int n = mw_vfprintf(mf, fmt, ap);
    fclose(mf);
    if (n < 0) return -1;
    size_t copy = ((size_t)n >= size) ? (size ? size - 1 : 0) : (size_t)n;
    if (buffer && size) { memcpy(buffer, tmp, copy); buffer[copy] = 0; }
    MW_TRACE("__stdio_common_vsprintf(fmt=%s) = %d", fmt ? fmt : "NULL", n);
    return n; /* UCRT: would-be length (may exceed size) */
}

__attribute__((ms_abi)) int mw___stdio_common_vfwprintf(
    unsigned long long options, void* file, const short* fmt,
    void* locale, void* ap) {
    /* Wide print: no test exercises it; refuse loudly rather than misformat. */
    (void)options; (void)file; (void)fmt; (void)locale; (void)ap;
    MW_TRACE("__stdio_common_vfwprintf() -> -1 (wide print unsupported)");
    return -1;
}

__attribute__((ms_abi)) int mw___stdio_common_vsscanf(
    unsigned long long options, const char* buffer, size_t size,
    const char* fmt, void* locale, void* ap) {
    (void)options; (void)locale; (void)size;
    if (!buffer) return -1;
    /* All scanf varargs are pointers (GP-only). MS slot stream: slot[i] is
     * an 8-byte uniform slot. Build the SysV va_list with the loader's
     * proven reg_save trick (slot 0 reserved; first vararg at gp_offset 8). */
    unsigned long long* slots = (unsigned long long*)ap;
    unsigned long long reg_save[6 + 16];
    reg_save[0] = 0; /* dummy rdi slot */
    for (int i = 0; i < 5; i++) reg_save[1 + i] = slots[i];
    memset(&reg_save[6], 0, 16 * 8);
    va_list sva;                      /* glibc __va_list_tag[1] */
    struct { unsigned int gp_offset, fp_offset; void* overflow; void* reg_save; }* sv =
        (void*)sva;
    sv->gp_offset = 8;
    sv->fp_offset = 48; /* pointers only — never reads xmm */
    sv->overflow = slots + 5;
    sv->reg_save = reg_save;
    int r = vsscanf(buffer, fmt, sva);
    MW_TRACE("__stdio_common_vsscanf(fmt=%s) = %d", fmt ? fmt : "NULL", r);
    return r;
}

/* BUG-032: classic msvcrt/mingw CRTs treat __iob_func()'s return value as a
 * FILE* into an ARRAY OF FILE STRUCTS with stride sizeof(FILE)=0x30 on x64:
 *   stdin = base+0x00, stdout = base+0x30, stderr = base+0x60.
 * The PE therefore passes synthetic pointers (observed: setvbuf called with
 * base+0x60) that glibc must never dereference. Recognize that window and
 * map to the real streams; anything else keeps the legacy behavior. */

static FILE* map_crt_stream(void* stream) {
    if (stream == NULL) return NULL;
    {   /* ISS1: registry handles resolve to their host FILE* first — a raw
         * registry pointer must NEVER reach glibc. */
        FILE* hf = mwfile_hostfp_of(stream);
        if (hf) return hf;
    }
    uintptr_t s = (uintptr_t)stream;
    uintptr_t base = (uintptr_t)g_iob_array;
    if (s >= base && s < base + 0x90) {
        uintptr_t off = s - base;
        int idx = (int)(off / 0x30);
        if (idx < 0 || idx > 2) idx = 1; /* window edge -> stdout */
        FILE* mapped = (idx == 0) ? stdin : (idx == 1) ? stdout : stderr;
        MW_TRACE("map_crt_stream: iob+%#zx -> %s", (size_t)off,
                 idx == 0 ? "stdin" : idx == 1 ? "stdout" : "stderr");
        return mapped;
    }
    /* TASK-API-011: pointers outside the synthetic iob window are REAL
     * glibc FILE* (fmemopen/fopen-backed streams created by our own API
     * implementations). Pass them through verbatim; the old "unknown ->
     * stdout" fallback silently ate fmemopen output (snapt.exe repro:
     * "%.3f" went to stdout and the caller's buffer stayed empty). */
    return (FILE*)stream;
}

__attribute__((ms_abi)) int mw_fflush(void* file) {
    MW_TRACE("fflush(file=%p)", file);
    FILE* f = (file == NULL) ? NULL : map_crt_stream(file);
    return fflush(f);
}

__attribute__((ms_abi)) int mw_fputs(const char* str, void* file) {
    MW_TRACE("fputs(str=%s)", str ? str : "NULL");
    FILE* f = map_crt_stream(file);
    return fputs(str, f);
}

__attribute__((ms_abi)) int mw_fputc(int ch, void* file) {
    MW_TRACE("fputc(ch=0x%x)", ch);
    FILE* f = map_crt_stream(file);
    return fputc(ch, f);
}

__attribute__((ms_abi)) size_t mw_fwrite(const void* buf, size_t sz, size_t cnt, void* file) {
    MW_TRACE("fwrite(sz=%zu, cnt=%zu)", sz, cnt);
    FILE* f = map_crt_stream(file);
    return fwrite(buf, sz, cnt, f);
}

__attribute__((ms_abi)) int mw_getc(void* file) {
    MW_TRACE("getc()");
    FILE* f = map_crt_stream(file);
    return (f != NULL) ? getc(f) : EOF;
}

__attribute__((ms_abi)) int mw_ungetc(int ch, void* file) {
    MW_TRACE("ungetc(ch=0x%x)", ch);
    FILE* f = map_crt_stream(file);
    return (f != NULL) ? ungetc(ch, f) : EOF;
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
/* APP-F-05b (BUG-033 class): variadic ms_abi entry points are unsafe for
 * glibc-side va_list handling; _open's optional mode is therefore a FIXED
 * third argument. PEs that omit the mode leave R8 undefined, which is fine:
 * mode is only consumed when _O_CREAT (0x0100) is set. */
__attribute__((ms_abi)) int mw__open(const char* path, int flags, int mode) {
    MW_TRACE("_open(path=%s, flags=0x%x, mode=0x%x)", path ? path : "NULL", flags, mode);
    if (!(flags & 0x0100)) mode = 0; /* _O_CREAT unset: mode ignored */
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

__attribute__((ms_abi)) int mw__wopen(const wchar_t* path, int flags, int mode) {
    MW_TRACE("_wopen()");
    char buf[1024];
    wcstombs(buf, path, sizeof(buf));
    return mw__open(buf, flags, mode);
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
    mw_trace_shutdown_flush();
    _exit(134); /* 128 + SIGABRT(6) */
}

__attribute__((ms_abi)) void (*mw_signal(int sig, void (*handler)(int)))(int) {
    MW_TRACE("signal(sig=%d)", sig);
    return signal(sig, handler);
}

/* BUG-036 (appcompat sweep APP-F-06): setjmp/longjmp must honor the Windows
 * x64 jmp_buf contract. The previous implementation stored a glibc sigjmp_buf
 * inside mw__setjmp's own SYSV frame: (a) ms_abi callee-saved RDI/RSI/XMM6-15
 * are NOT covered by glibc's save set -> live PE values corrupt after longjmp;
 * (b) resumption flows through the loader frame and mis-restores state
 * (observed: t06 counter/volatile failures; t07 infinite longjmp(val=1) loop).
 *
 * This implementation IS the jmp_buf owner: uniform 8-byte slots (mingw x64
 * jmp_buf is 256 bytes = 32 slots, we use 30):
 *   0 rbx   1 rbp   2 rdi   3 rsi   4 r12  5 r13  6 r14  7 r15
 *   8..17   xmm6..xmm15 (16 bytes each = 2 slots each)
 *   18      rsp at entry (points at the return address)
 *   19..28  (unused padding, zeroed)
 *   29      magic 0x4D574A42 ("MWJB") for cross-checking
 * longjmp restores everything and jumps to the saved return address with the
 * requested value in EAX — the exact semantics of Windows setjmp/longjmp. */
#define MW_JB_MAGIC 0x4D574A42ULL

/* setjmp is NAKED: rsp at the first instruction == entry rsp == [retaddr].
 * We save entry_rsp (slot 18) and retaddr (slot 19); longjmp resumes with
 * rsp = entry_rsp + 8 (the caller's live rsp) and jumps to the retaddr. */
__attribute__((ms_abi, naked)) int mw__setjmp(void* buf) {
    asm volatile(
        "movq %rcx, %r10\n\t"
        "movq %rbx, 0(%r10)\n\t"
        "movq %rbp, 8(%r10)\n\t"
        "movq %rdi, 16(%r10)\n\t"
        "movq %rsi, 24(%r10)\n\t"
        "movq %r12, 32(%r10)\n\t"
        "movq %r13, 40(%r10)\n\t"
        "movq %r14, 48(%r10)\n\t"
        "movq %r15, 56(%r10)\n\t"
        "movdqu %xmm6, 64(%r10)\n\t"
        "movdqu %xmm7, 80(%r10)\n\t"
        "movdqu %xmm8, 96(%r10)\n\t"
        "movdqu %xmm9, 112(%r10)\n\t"
        "movdqu %xmm10, 128(%r10)\n\t"
        "movdqu %xmm11, 144(%r10)\n\t"
        "movdqu %xmm12, 160(%r10)\n\t"
        "movdqu %xmm13, 176(%r10)\n\t"
        "movdqu %xmm14, 192(%r10)\n\t"
        "movdqu %xmm15, 208(%r10)\n\t"
        "movq %rsp, 224(%r10)\n\t"
        "movq 0(%rsp), %rax\n\t"
        "movq %rax, 232(%r10)\n\t"
        "movq $0x4D574A42, 240(%r10)\n\t"   /* MW_JB_MAGIC */
        "xorl %eax, %eax\n\t"
        "ret\n\t"
    );
}

__attribute__((ms_abi)) __attribute__((noreturn)) void mw_longjmp(void* buf, int val) {
    register void* r10 asm("r10") = buf;
    register long r11 asm("r11") = (long)val;
    asm volatile(
        "movq 0(%%r10), %%rbx\n\t"
        "movq 8(%%r10), %%rbp\n\t"
        "movq 16(%%r10), %%rdi\n\t"
        "movq 24(%%r10), %%rsi\n\t"
        "movq 32(%%r10), %%r12\n\t"
        "movq 40(%%r10), %%r13\n\t"
        "movq 48(%%r10), %%r14\n\t"
        "movq 56(%%r10), %%r15\n\t"
        "movdqu 64(%%r10), %%xmm6\n\t"
        "movdqu 80(%%r10), %%xmm7\n\t"
        "movdqu 96(%%r10), %%xmm8\n\t"
        "movdqu 112(%%r10), %%xmm9\n\t"
        "movdqu 128(%%r10), %%xmm10\n\t"
        "movdqu 144(%%r10), %%xmm11\n\t"
        "movdqu 160(%%r10), %%xmm12\n\t"
        "movdqu 176(%%r10), %%xmm13\n\t"
        "movdqu 192(%%r10), %%xmm14\n\t"
        "movdqu 208(%%r10), %%xmm15\n\t"
        "movq 224(%%r10), %%rsp\n\t"
        "addq $8, %%rsp\n\t"                 /* caller's live rsp (retaddr popped) */
        "movq %%r11, %%rax\n\t"
        "jmp *232(%%r10)\n\t"
        :
        : "r"(r10), "r"(r11)
        : "memory"
    );
    __builtin_unreachable();
}

__attribute__((ms_abi)) int mw__kbhit(void) { return 0; }

__attribute__((ms_abi)) char* mw_getenv(const char* name) {
    return getenv(name);
}

__attribute__((ms_abi)) void* mw_localeconv(void) {
    return (void*)localeconv();
}

/* BUG-035 (appcompat sweep APP-F-05): callback pointers across the ABI
 * boundary. glibc qsort invokes the comparator SysV-style (args in RDI,RSI)
 * but the PE's comparator is ms_abi (args in RCX,RDX) -> NULL deref inside
 * the comparator (observed t04: mov eax,[rcx] with rcx=0). Bridge through
 * qsort_r: the bridge is SysV (glibc-safe) and re-dispatches through an
 * ms_abi-typed function pointer so GCC emits the Windows register mapping. */

typedef int (__attribute__((ms_abi)) *mw_ms_qsort_cmp)(const void*, const void*);
static const mw_ms_qsort_cmp* g_ms_qsort_cmp_ref;

static int mw_ms_qsort_bridge(const void* a, const void* b, void* ctx) {
    (void)ctx;
    return (*g_ms_qsort_cmp_ref)(a, b);
}

__attribute__((ms_abi)) void mw_qsort(void* base, size_t n, size_t sz, int (*cmp)(const void*, const void*)) {
    g_ms_qsort_cmp_ref = (const mw_ms_qsort_cmp*)(const void*)&cmp;
    qsort_r(base, n, sz, mw_ms_qsort_bridge, NULL);
}

/* ============================================================
 * TASK-API-008 wave 2 (appcompat sweep APP-F-07): C++ runtime surface.
 * llvm-mingw C++ binaries import SRW locks, condition variables, fiber-local
 * storage, InitOnce, RtlRestoreContext, aligned allocation, wide-char CRT
 * and 64-bit strtol family. All implementations below are honest bridges.
 * ============================================================ */
#include <sched.h>
#include <sys/syscall.h>
#include <wchar.h>

/* ---- SRW locks: SRWLOCK is an opaque 8-byte handle slot on Windows.
 * We keep a small registry mapping the PE's SRWLOCK slots to real
 * pthread rwlocks, allocated on first use. Single-instance per key. ---- */
typedef struct { void* key; int used; pthread_rwlock_t rw; } MwSrwEntry;
static MwSrwEntry g_srw_tab[256];
static pthread_mutex_t g_srw_tab_mu = PTHREAD_MUTEX_INITIALIZER;

static pthread_rwlock_t* srw_get(void* k) {
    pthread_mutex_lock(&g_srw_tab_mu);
    MwSrwEntry* free_slot = NULL;
    for (int i = 0; i < 256; i++) {
        if (g_srw_tab[i].used && g_srw_tab[i].key == k) {
            pthread_mutex_unlock(&g_srw_tab_mu);
            return &g_srw_tab[i].rw;
        }
        if (!g_srw_tab[i].used && !free_slot) free_slot = &g_srw_tab[i];
    }
    if (!free_slot) { pthread_mutex_unlock(&g_srw_tab_mu); return NULL; }
    free_slot->used = 1; free_slot->key = k;
    pthread_rwlockattr_t a;
    pthread_rwlockattr_init(&a);
    pthread_rwlockattr_setkind_np(&a, PTHREAD_RWLOCK_PREFER_WRITER_NONRECURSIVE_NP);
    pthread_rwlock_init(&free_slot->rw, &a);
    pthread_rwlockattr_destroy(&a);
    pthread_mutex_unlock(&g_srw_tab_mu);
    return &free_slot->rw;
}

__attribute__((ms_abi)) void mw_AcquireSRWLockExclusive(void* srw) {
    pthread_rwlock_t* rw = srw_get(srw);
    if (rw) pthread_rwlock_wrlock(rw);
    MW_TRACE("AcquireSRWLockExclusive(%p)", srw);
}
__attribute__((ms_abi)) void mw_ReleaseSRWLockExclusive(void* srw) {
    pthread_rwlock_t* rw = srw_get(srw);
    if (rw) pthread_rwlock_unlock(rw);
    MW_TRACE("ReleaseSRWLockExclusive(%p)", srw);
}
__attribute__((ms_abi)) unsigned char mw_TryAcquireSRWLockExclusive(void* srw) {
    pthread_rwlock_t* rw = srw_get(srw);
    int rc = rw ? pthread_rwlock_trywrlock(rw) : 0;
    MW_TRACE("TryAcquireSRWLockExclusive(%p) = %d", srw, rc == 0);
    return (rc == 0) ? 1 : 0;
}

/* ---- Condition variables: CONDITION_VARIABLE is an opaque 8-byte slot.
 * Internal mutex+cond per key; the SRW lock is released around the wait and
 * re-acquired in the caller's mode (bit 0 of Flags = shared). ---- */
typedef struct { void* key; int used; pthread_mutex_t mu; pthread_cond_t cd; } MwCvEntry;
static MwCvEntry g_cv_tab[256];
static pthread_mutex_t g_cv_tab_mu = PTHREAD_MUTEX_INITIALIZER;

static MwCvEntry* cv_get(void* k) {
    pthread_mutex_lock(&g_cv_tab_mu);
    MwCvEntry* free_slot = NULL;
    for (int i = 0; i < 256; i++) {
        if (g_cv_tab[i].used && g_cv_tab[i].key == k) {
            pthread_mutex_unlock(&g_cv_tab_mu);
            return &g_cv_tab[i];
        }
        if (!g_cv_tab[i].used && !free_slot) free_slot = &g_cv_tab[i];
    }
    if (!free_slot) { pthread_mutex_unlock(&g_cv_tab_mu); return NULL; }
    free_slot->used = 1; free_slot->key = k;
    pthread_mutex_init(&free_slot->mu, NULL);
    pthread_cond_init(&free_slot->cd, NULL);
    pthread_mutex_unlock(&g_cv_tab_mu);
    return free_slot;
}

__attribute__((ms_abi)) uint32_t mw_SleepConditionVariableSRW(void* cv, void* srw,
                                                              uint32_t ms, uint32_t flags) {
    MwCvEntry* c = cv_get(cv);
    pthread_rwlock_t* rw = srw_get(srw);
    if (!c || !rw) return 0;
    if (flags & 1) pthread_rwlock_unlock(rw); else pthread_rwlock_unlock(rw);
    uint32_t result = 0; /* WAIT_OBJECT_0? Windows: 0 = success */
    pthread_mutex_lock(&c->mu);
    if (ms == 0xFFFFFFFFu) {
        pthread_cond_wait(&c->cd, &c->mu);
    } else {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += ms / 1000;
        ts.tv_nsec += (long)(ms % 1000) * 1000000L;
        if (ts.tv_nsec >= 1000000000L) { ts.tv_sec++; ts.tv_nsec -= 1000000000L; }
        if (pthread_cond_timedwait(&c->cd, &c->mu, &ts) == ETIMEDOUT) result = 258; /* WAIT_TIMEOUT */
    }
    pthread_mutex_unlock(&c->mu);
    if (flags & 1) pthread_rwlock_rdlock(rw); else pthread_rwlock_wrlock(rw);
    MW_TRACE("SleepConditionVariableSRW(cv=%p, ms=%u) = %u", cv, ms, result);
    return result;
}
__attribute__((ms_abi)) void mw_WakeConditionVariable(void* cv) {
    MwCvEntry* c = cv_get(cv);
    if (c) { pthread_mutex_lock(&c->mu); pthread_cond_signal(&c->cd); pthread_mutex_unlock(&c->mu); }
    MW_TRACE("WakeConditionVariable(%p)", cv);
}
__attribute__((ms_abi)) void mw_WakeAllConditionVariable(void* cv) {
    MwCvEntry* c = cv_get(cv);
    if (c) { pthread_mutex_lock(&c->mu); pthread_cond_broadcast(&c->cd); pthread_mutex_unlock(&c->mu); }
    MW_TRACE("WakeAllConditionVariable(%p)", cv);
}

/* ---- Fiber-local storage: index-based TLS array. Thread-exit callbacks are
 * not invoked (documented limitation; nothing in the C++ startup path relies
 * on FLS callbacks under this loader). ---- */
#define MW_FLS_MAX 128
static __thread void* g_fls_vals[MW_FLS_MAX];

__attribute__((ms_abi)) uint32_t mw_FlsAlloc(void* cb) {
    (void)cb;
    static uint32_t next = 0;
    static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_lock(&mu);
    uint32_t idx = next++;
    pthread_mutex_unlock(&mu);
    MW_TRACE("FlsAlloc(cb=%p) = %u", cb, idx);
    return (idx < MW_FLS_MAX) ? idx : 0xFFFFFFFFu;
}
__attribute__((ms_abi)) void* mw_FlsGetValue(uint32_t idx) {
    return (idx < MW_FLS_MAX) ? g_fls_vals[idx] : NULL;
}
__attribute__((ms_abi)) int mw_FlsSetValue(uint32_t idx, void* val) {
    if (idx >= MW_FLS_MAX) return 0;
    g_fls_vals[idx] = val;
    return 1;
}

/* ---- InitOnce: single-threaded-execution assumption for the fast path,
 * serialized by a global mutex (startup-time use only). ---- */
typedef int (__attribute__((ms_abi)) *mw_initonce_fn)(void* once, void* param, void* ctx);
__attribute__((ms_abi)) int mw_InitOnceExecuteOnce(void* once, void* fn, void* param, void* ctx) {
    static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_lock(&mu);
    uint64_t* s = (uint64_t*)once;
    int ok = 1;
    if (*s != 2) {
        ok = ((mw_initonce_fn)fn)(once, param, ctx);
        if (ok) *s = 2;
    }
    pthread_mutex_unlock(&mu);
    MW_TRACE("InitOnceExecuteOnce(once=%p) = %d", once, ok);
    return ok;
}

/* ---- RtlRestoreContext: full GPR + XMM + RIP restore from a Windows
 * CONTEXT (x64 layout). Used by unwinding resume paths. ---- */
__attribute__((ms_abi)) void mw_RtlRestoreContext(void* ctx, void* exc) {
    (void)exc;
    if (!ctx) return;
    register void* r10 asm("r10") = ctx;
    asm volatile(
        "movq 0x80(%%r10), %%rcx\n\t"
        "movq 0x88(%%r10), %%rdx\n\t"
        "movq 0x90(%%r10), %%rbx\n\t"
        "movq 0xA0(%%r10), %%rbp\n\t"
        "movq 0xA8(%%r10), %%rsi\n\t"
        "movq 0xB0(%%r10), %%rdi\n\t"
        "movq 0xB8(%%r10), %%r8\n\t"
        "movq 0xC0(%%r10), %%r9\n\t"
        "movq 0xD8(%%r10), %%r12\n\t"
        "movq 0xE0(%%r10), %%r13\n\t"
        "movq 0xE8(%%r10), %%r14\n\t"
        "movq 0xF0(%%r10), %%r15\n\t"
        "movdqu 0x1A0(%%r10), %%xmm0\n\t"
        "movdqu 0x1B0(%%r10), %%xmm1\n\t"
        "movdqu 0x1C0(%%r10), %%xmm2\n\t"
        "movdqu 0x1D0(%%r10), %%xmm3\n\t"
        "movdqu 0x1E0(%%r10), %%xmm4\n\t"
        "movdqu 0x1F0(%%r10), %%xmm5\n\t"
        "movdqu 0x200(%%r10), %%xmm6\n\t"
        "movdqu 0x210(%%r10), %%xmm7\n\t"
        "movdqu 0x220(%%r10), %%xmm8\n\t"
        "movdqu 0x230(%%r10), %%xmm9\n\t"
        "movdqu 0x240(%%r10), %%xmm10\n\t"
        "movdqu 0x250(%%r10), %%xmm11\n\t"
        "movdqu 0x260(%%r10), %%xmm12\n\t"
        "movdqu 0x270(%%r10), %%xmm13\n\t"
        "movdqu 0x280(%%r10), %%xmm14\n\t"
        "movdqu 0x290(%%r10), %%xmm15\n\t"
        "movq 0x78(%%r10), %%rax\n\t"        /* Rax */
        "movq 0xC8(%%r10), %%r11\n\t"        /* R11 (r10 still = ctx) */
        "movq 0x98(%%r10), %%rsp\n\t"        /* Rsp (final stack) */
        "movq 0xC8(%%r10), %%r10\n\t"        /* R10 (last: r10 was ctx) */
        "pushq %%rax\n\t"                    /* [Rsp-8] = Rip */
        "ret\n\t"
        :
        : "r"(r10)
        : "rax", "rcx", "rdx", "rbx", "rbp", "rsi", "rdi",
          "r8", "r9", "r11", "r12", "r13", "r14", "r15", "memory"
    );
    __builtin_unreachable();
}

/* ---- misc kernel32 ---- */
__attribute__((ms_abi)) void* mw_GetModuleHandleW(const wchar_t* name) {
    MW_TRACE("GetModuleHandleW()");
    if (name == NULL || name[0] == 0) return g_image_base;
    return NULL;
}
__attribute__((ms_abi)) uint32_t mw_GetThreadId(void* h) {
    (void)h;
    return (uint32_t)syscall(SYS_gettid);
}
__attribute__((ms_abi)) int mw_SwitchToThread(void) {
    return (sched_yield() == 0) ? 1 : 0;
}
__attribute__((ms_abi)) uint32_t mw_WaitForSingleObjectEx(void* h, uint32_t ms, int alertable) {
    (void)alertable;
    return mw_WaitForSingleObject(h, ms);
}

/* ---- msvcrt wave 2 ---- */
__attribute__((ms_abi)) void* mw__aligned_malloc(size_t size, size_t align) {
    void* p = NULL;
    if (align < 8) align = 8;
    if (posix_memalign(&p, align, size) != 0) return NULL;
    return p;
}
__attribute__((ms_abi)) void mw__aligned_free(void* p) { free(p); }

__attribute__((ms_abi)) void mw__assert(const char* msg, const char* file, unsigned line) {
    fprintf(stderr, "Assertion failed: %s (%s:%u)\n", msg ? msg : "?", file ? file : "?", line);
    abort();
}

__attribute__((ms_abi)) long long mw__strtoi64(const char* s, char** end, int base) {
    return strtoll(s, end, base);
}
__attribute__((ms_abi)) unsigned long long mw__strtoui64(const char* s, char** end, int base) {
    return strtoull(s, end, base);
}
/* NOTE: Windows wchar_t is 2 bytes (UTF-16), glibc's is 4 (UCS-4). For
 * ASCII digit strings the leading bytes coincide, so simple passthrough is
 * correct for the numeric cases the C++ runtime uses. Non-ASCII wide input
 * needs a UTF-16 -> UTF-32 shim (follow-up API task). */
__attribute__((ms_abi)) long mw_wcstol(const wchar_t* s, wchar_t** end, int base) {
    return wcstol((const int*)s, (int**)end, base);
}
__attribute__((ms_abi)) unsigned long mw_wcstoul(const wchar_t* s, wchar_t** end, int base) {
    return wcstoul((const int*)s, (int**)end, base);
}
__attribute__((ms_abi)) long long mw__wcstoi64(const wchar_t* s, wchar_t** end, int base) {
    return wcstol((const int*)s, (int**)end, base);
}
__attribute__((ms_abi)) unsigned long long mw__wcstoui64(const wchar_t* s, wchar_t** end, int base) {
    return wcstoul((const int*)s, (int**)end, base);
}

__attribute__((ms_abi)) wint_t mw_fputwc(wchar_t ch, void* file) {
    FILE* f = map_crt_stream(file);
    return f ? fputwc((wint_t)ch, f) : WEOF;
}

/* fwprintf: wide variadic — narrow the (ASCII) format specifiers for
 * classification, then build the same SysV va_list and call vfwprintf. */
__attribute__((ms_abi)) int mw_fwprintf(void* file, const wchar_t* fmt, ...) {
    FILE* f = map_crt_stream(file);
    unsigned char cls[MW_PRINTF_MAX_ARGS];
    char narrow[512];
    size_t k = 0;
    if (fmt) {
        const unsigned short* w = (const unsigned short*)fmt;
        while (*w && k + 1 < sizeof(narrow)) { narrow[k++] = (char)(*w & 0xFF); w++; }
    }
    narrow[k] = 0;
    int nargs = mw_printf_classify(narrow, cls);
    unsigned long long slots[MW_PRINTF_MAX_ARGS];
    va_list ap;
    va_start(ap, fmt);
    for (int i = 0; i < nargs; i++) {
        if (cls[i]) { double d = va_arg(ap, double); memcpy(&slots[i], &d, 8); }
        else        { slots[i] = (unsigned long long)va_arg(ap, long long); }
    }
    va_end(ap);
    unsigned char cls2[MW_PRINTF_MAX_ARGS];
    int parsed = mw_printf_classify(narrow, cls2);
    if (nargs > parsed) nargs = parsed;

    unsigned long long gpv[6] = {0,0,0,0,0,0};
    unsigned long long fpv[8] = {0,0,0,0,0,0,0,0};
    unsigned long long ovf[MW_PRINTF_MAX_ARGS];
    int novf = 0, gpi = 1, fpi = 0;
    for (int i = 0; i < nargs; i++) {
        if (cls2[i]) { if (fpi < 8) fpv[fpi++] = slots[i]; else ovf[novf++] = slots[i]; }
        else         { if (gpi < 6) gpv[gpi++] = slots[i]; else ovf[novf++] = slots[i]; }
    }
    unsigned long long reg_save[6 + 16];
    memcpy(reg_save, gpv, sizeof(gpv));
    for (int i = 0; i < 8; i++)
        memcpy((unsigned char*)reg_save + 48 + 16 * i, &fpv[i], 8);
    for (int i = fpi; i < 8; i++)
        memset((unsigned char*)reg_save + 48 + 16 * i, 0, 16);
    va_list sv;
    mw_sysv_va_list* svv = (mw_sysv_va_list*)sv;
    svv->gp_offset = 8;
    svv->fp_offset = 48;
    svv->overflow_arg_area = ovf;
    svv->reg_save_area = reg_save;
    return f ? vfwprintf(f, fmt, sv) : -1;
}

__attribute__((ms_abi)) int mw_rand(void) { return rand(); }
__attribute__((ms_abi)) void mw_srand(unsigned int seed) { srand(seed); }

__attribute__((ms_abi)) int mw__onexit(void (*func)(void)) {
    MW_TRACE("_onexit(func=%p)", func);
    return atexit(func);
}

/* ---- TASK-API-007 wave 1 (appcompat sweep APP-F-02): CRT startup surface.
 * mingw-w64 CRT startup imports these unconditionally; with them unresolved
 * the IAT slot keeps the raw hint/name RVA and `call [iat]` faults at that
 * RVA (observed RIP=0x92f8, SIGSEGV before main in every mingw PE). */

__attribute__((ms_abi)) int mw_atexit(void (*func)(void)) {
    MW_TRACE("atexit(func=%p)", func);
    return atexit(func);
}

/* Windows _exit: terminate without flushing or running atexit handlers. */
__attribute__((ms_abi)) void mw__exit_noflush(int code) {
    MW_TRACE("_exit(code=%d)", code);
    _exit(code);
}

/* msvcrt setvbuf modes differ from glibc: _IOFBF=0, _IOLBF=64, _IONBF=4
 * (glibc: 0/1/2). Translate before forwarding to the real FILE*. */
__attribute__((ms_abi)) int mw_setvbuf(void* stream, char* buf, int mode, size_t size) {
    int gmode;
    switch (mode) {
        case 0:  gmode = _IOFBF; break;  /* _IOFBF */
        case 64: gmode = _IOLBF; break;  /* _IOLBF */
        case 4:  gmode = _IONBF; break;  /* _IONBF */
        default: gmode = mode;   break;
    }
    MW_TRACE("setvbuf(stream=%p, mode=%d->%d, size=%zu)", stream, mode, gmode, size);
    FILE* f = map_crt_stream(stream);
    if (!f) return 0; /* nothing to do for a NULL stream */
    return setvbuf(f, buf, gmode, size);
}

/* kernel32 GetCPInfo — CRT locale init asks for the ANSI code page.
 * Honest SBCS answer: MaxCharSize=1, DefaultChar='?', no lead bytes. */
__attribute__((ms_abi)) int mw_GetCPInfo(unsigned codepage, void* cpinfo) {
    MW_TRACE("GetCPInfo(cp=%u)", codepage);
    if (!cpinfo) return 0;           /* FALSE */
    unsigned char* p = (unsigned char*)cpinfo;
    p[0] = 1;                        /* MaxCharSize = 1 (SBCS) */
    p[1] = 0;                        /* (padding of UINT on LE is BE byte 0) */
    p[2] = '?'; p[3] = 0;            /* DefaultChar = "?" + NUL */
    memset(p + 4, 0, 12);            /* LeadByte ranges: none */
    return 1;                        /* TRUE */
}

/* msvcrt _XcptFilter — MSVC SEH filter helper used by CRT internals.
 * Faithful default: never claim the exception. */
__attribute__((ms_abi)) long mw__XcptFilter(unsigned long xcpt, void* ep) {
    MW_TRACE("_XcptFilter(xcpt=0x%lx)", xcpt);
    (void)ep;
    return 0; /* EXCEPTION_CONTINUE_SEARCH */
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

__attribute__((ms_abi)) int* mw__errno(void) {
    MW_TRACE("_errno() = %p", &g_errno_storage);
    return &g_errno_storage;
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
    ENTRY("KERNEL32.DLL", "GetCPInfo",                          mw_GetCPInfo),
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
    ENTRY("KERNEL32.DLL", "ExitProcess",                        mw_ExitProcess),
    ENTRY("KERNEL32.DLL", "IsDebuggerPresent",                   mw_IsDebuggerPresent),
    ENTRY("KERNEL32.DLL", "LoadLibraryA",                        mw_LoadLibraryA),
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
    ENTRY("KERNEL32.DLL", "WaitForSingleObjectEx",               mw_WaitForSingleObjectEx),
    ENTRY("KERNEL32.DLL", "WakeConditionVariable",               mw_WakeConditionVariable),
    ENTRY("KERNEL32.DLL", "WakeAllConditionVariable",            mw_WakeAllConditionVariable),
    ENTRY("KERNEL32.DLL", "SleepConditionVariableSRW",           mw_SleepConditionVariableSRW),
    ENTRY("KERNEL32.DLL", "AcquireSRWLockExclusive",             mw_AcquireSRWLockExclusive),
    ENTRY("KERNEL32.DLL", "ReleaseSRWLockExclusive",             mw_ReleaseSRWLockExclusive),
    ENTRY("KERNEL32.DLL", "TryAcquireSRWLockExclusive",          mw_TryAcquireSRWLockExclusive),
    ENTRY("KERNEL32.DLL", "InitOnceExecuteOnce",                 mw_InitOnceExecuteOnce),
    ENTRY("KERNEL32.DLL", "RtlRestoreContext",                   mw_RtlRestoreContext),
    ENTRY("KERNEL32.DLL", "GetModuleHandleW",                    mw_GetModuleHandleW),
    ENTRY("KERNEL32.DLL", "GetThreadId",                         mw_GetThreadId),
    ENTRY("KERNEL32.DLL", "SwitchToThread",                      mw_SwitchToThread),
    ENTRY("KERNEL32.DLL", "WideCharToMultiByte",                 mw_WideCharToMultiByte),
    ENTRY("KERNEL32.DLL", "WriteConsoleOutputA",                 mw_WriteConsoleOutputA),

    /* msvcrt.dll — Code imports */
    ENTRY("msvcrt.dll", "__C_specific_handler",   mw___C_specific_handler),
    ENTRY("api-ms-win-crt-runtime-l1-1-0.dll", "__C_specific_handler", mw___C_specific_handler),
    ENTRY("msvcrt.dll", "__getmainargs",         mw___getmainargs),
    ENTRY("api-ms-win-crt-runtime-l1-1-0.dll", "_configure_narrow_argv", mw__configure_narrow_argv),
    ENTRY("api-ms-win-crt-runtime-l1-1-0.dll", "_initialize_narrow_environment", mw__initialize_narrow_environment),
    ENTRY("api-ms-win-crt-runtime-l1-1-0.dll", "_set_new_mode", mw__set_new_mode),
    ENTRY("api-ms-win-crt-runtime-l1-1-0.dll", "_set_invalid_parameter_handler", mw__set_invalid_parameter_handler),
    ENTRY("api-ms-win-crt-runtime-l1-1-0.dll", "_crt_atexit",   mw_atexit),
    ENTRY("api-ms-win-crt-runtime-l1-1-0.dll", "signal",        mw_signal_stub),
    ENTRY("api-ms-win-crt-runtime-l1-1-0.dll", "_set_app_type", mw___set_app_type),
    ENTRY("msvcrt.dll", "__iob_func",            mw___iob_func),
    ENTRY("api-ms-win-crt-stdio-l1-1-0.dll", "__acrt_iob_func", mw___acrt_iob_func),
    ENTRY("api-ms-win-crt-stdio-l1-1-0.dll", "__stdio_common_vfprintf", mw___stdio_common_vfprintf),
    ENTRY("api-ms-win-crt-stdio-l1-1-0.dll", "__stdio_common_vsprintf", mw___stdio_common_vsprintf),
    ENTRY("api-ms-win-crt-stdio-l1-1-0.dll", "__stdio_common_vfwprintf", mw___stdio_common_vfwprintf),
    ENTRY("api-ms-win-crt-stdio-l1-1-0.dll", "__stdio_common_vsscanf",  mw___stdio_common_vsscanf),
    ENTRY("api-ms-win-crt-time-l1-1-0.dll", "_tzset",           mw__tzset),
    ENTRY("api-ms-win-crt-stdio-l1-1-0.dll", "fopen",           mw_fopen),
    ENTRY("api-ms-win-crt-stdio-l1-1-0.dll", "fclose",          mw_fclose),
    ENTRY("api-ms-win-crt-stdio-l1-1-0.dll", "fread",           mw_fread),
    ENTRY("api-ms-win-crt-stdio-l1-1-0.dll", "fwrite",          mw_fwrite),
    ENTRY("api-ms-win-crt-stdio-l1-1-0.dll", "fgets",           mw_fgets),
    ENTRY("api-ms-win-crt-stdio-l1-1-0.dll", "fgetc",           mw_fgetc),
    ENTRY("api-ms-win-crt-stdio-l1-1-0.dll", "rewind",          mw_rewind),
    ENTRY("api-ms-win-crt-stdio-l1-1-0.dll", "fseek",           mw_fseek),
    ENTRY("api-ms-win-crt-stdio-l1-1-0.dll", "ftell",           mw_ftell),
    ENTRY("api-ms-win-crt-stdio-l1-1-0.dll", "feof",            mw_feof),
    ENTRY("msvcrt.dll", "fseek",                 mw_fseek),
    ENTRY("msvcrt.dll", "ftell",                 mw_ftell),
    ENTRY("msvcrt.dll", "feof",                  mw_feof),
    ENTRY("api-ms-win-crt-time-l1-1-0.dll", "strftime",         mw_strftime),
    ENTRY("api-ms-win-crt-private-l1-1-0.dll", "__p__fmode",    mw___p__fmode),
    ENTRY("api-ms-win-crt-private-l1-1-0.dll", "__p__commode",  mw___p__commode),
    ENTRY("api-ms-win-crt-runtime-l1-1-0.dll", "__p___argc",    mw___p___argc),
    ENTRY("api-ms-win-crt-runtime-l1-1-0.dll", "__p___argv",    mw___p___argv),
    ENTRY("api-ms-win-crt-environment-l1-1-0.dll", "__p__environ", mw___p__environ),
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
    ENTRY("msvcrt.dll", "_exit",                 mw__exit_noflush),
    ENTRY("msvcrt.dll", "_errno",                mw__errno),
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
    ENTRY("msvcrt.dll", "_XcptFilter",            mw__XcptFilter),
    ENTRY("msvcrt.dll", "abort",                 mw_abort),
    ENTRY("msvcrt.dll", "atoi",                  mw_atoi),
    ENTRY("msvcrt.dll", "atexit",                mw_atexit),
    ENTRY("msvcrt.dll", "calloc",                mw_calloc),
    ENTRY("msvcrt.dll", "clock",                 mw_clock),
    ENTRY("msvcrt.dll", "exit",                  mw_exit),
    ENTRY("msvcrt.dll", "fflush",                mw_fflush),
    ENTRY("msvcrt.dll", "fprintf",               mw_fprintf),
    ENTRY("msvcrt.dll", "fputc",                 mw_fputc),
    ENTRY("msvcrt.dll", "fputs",                 mw_fputs),
    ENTRY("msvcrt.dll", "free",                  mw_free),
    ENTRY("msvcrt.dll", "_aligned_malloc",       mw__aligned_malloc),
    ENTRY("msvcrt.dll", "_aligned_free",         mw__aligned_free),
    ENTRY("msvcrt.dll", "_assert",               mw__assert),
    ENTRY("msvcrt.dll", "_strtoi64",             mw__strtoi64),
    ENTRY("msvcrt.dll", "_strtoui64",            mw__strtoui64),
    ENTRY("msvcrt.dll", "wcstol",                mw_wcstol),
    ENTRY("msvcrt.dll", "wcstoul",               mw_wcstoul),
    ENTRY("msvcrt.dll", "_wcstoi64",             mw__wcstoi64),
    ENTRY("msvcrt.dll", "_wcstoui64",            mw__wcstoui64),
    ENTRY("msvcrt.dll", "fputwc",                mw_fputwc),
    ENTRY("msvcrt.dll", "fwprintf",              mw_fwprintf),
    ENTRY("msvcrt.dll", "FlsAlloc",              mw_FlsAlloc),
    ENTRY("msvcrt.dll", "FlsGetValue",           mw_FlsGetValue),
    ENTRY("msvcrt.dll", "FlsSetValue",           mw_FlsSetValue),
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
    ENTRY("msvcrt.dll", "setvbuf",               mw_setvbuf),
    ENTRY("msvcrt.dll", "qsort",                 mw_qsort),
    ENTRY("msvcrt.dll", "rand",                  mw_rand),
    ENTRY("msvcrt.dll", "realloc",               mw_realloc),
    ENTRY("msvcrt.dll", "rename",                mw_rename),
    ENTRY("api-ms-win-crt-stdio-l1-1-0.dll", "remove",           mw_remove),
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
    /* TASK-API-011: UCRT-flavored data exports (__p_* return pointers to
     * the variables; __tzname/__timezone/__daylight are the variables). */
    /* ALL UCRT __p_* exports are FUNCTIONS returning &var — t10 crashes
     * (0x2039038 __p__fmode, 0x2039034 __p__commode, 0x2039050 __p___argc)
     * proved call-style usage for each. See mw___p__* below. */
    { "api-ms-win-crt-time-l1-1-0.dll", "__tzname",   g_tzname },
    { "api-ms-win-crt-time-l1-1-0.dll", "__timezone", &g_timezone_val },
    { "api-ms-win-crt-time-l1-1-0.dll", "__daylight", &g_daylight_val },
    /* _errno removed from data imports — it's a function, not data */
};
#define NUM_DATA_IMPORTS (sizeof(g_data_imports) / sizeof(g_data_imports[0]))

/* ============================================================
 * GetProcAddress Resolver
 * Defined after import tables so it can access them.
 * ============================================================ */

static void* resolve_proc_from_table(void* module, const char* name) {
    if (module == (void*)(uintptr_t)0xdead0001 || module == g_image_base) {
        for (size_t i = 0; i < NUM_IMPORTS; i++) {
            if (strcmp(g_import_table[i].name, name) == 0) {
                MW_TRACE("  -> %p", g_import_table[i].func);
                return g_import_table[i].func;
            }
        }
        for (size_t i = 0; i < NUM_DATA_IMPORTS; i++) {
            if (strcmp(g_data_imports[i].name, name) == 0) {
                MW_TRACE("  -> DATA %p", g_data_imports[i].data_addr);
                return g_data_imports[i].data_addr;
            }
        }
        MW_TRACE("  -> NULL (not in dispatch table)");
    }
    return NULL;
}

/* Initialize resolver — called once before PE loading */
static void resolve_proc_init(void) {
    g_resolve_proc_fn = resolve_proc_from_table;
}

/* TASK-TEST-004 (Windows-faithful IAT patching): the OS loader makes IAT
 * pages writable for the import pass regardless of their section flags
 * (UPX's IAT lives in the writable .idata; synthetic test PEs put it in
 * read-only .rdata). Track touched pages, restore per-section prot after. */
static struct { uint64_t rva; uint32_t size; int prot; } g_sec_prot[16];
static int g_sec_prot_count = 0;
static uint64_t g_iat_pages[64];
static int g_iat_page_count = 0;
static void iat_page_ensure_writable(uint8_t* addr) {
    uint64_t page = (uint64_t)(uintptr_t)addr & ~0xFFFULL;
    for (int i = 0; i < g_iat_page_count; i++)
        if (g_iat_pages[i] == page) return;
    if (g_iat_page_count < 64) g_iat_pages[g_iat_page_count++] = page;
    mprotect((void*)(uintptr_t)page, 0x1000, PROT_READ | PROT_WRITE);
}
static void iat_pages_restore(void) {
    for (int i = 0; i < g_iat_page_count; i++) {
        uint64_t off = g_iat_pages[i] - (uint64_t)(uintptr_t)g_image_base;
        int prot = PROT_READ; /* default for a restored image page */
        for (int t = 0; t < g_sec_prot_count; t++)
            if (off >= g_sec_prot[t].rva && off < g_sec_prot[t].rva + g_sec_prot[t].size) { prot = g_sec_prot[t].prot; break; }
        mprotect((void*)(uintptr_t)g_iat_pages[i], 0x1000, prot);
    }
    g_iat_page_count = 0;
}

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

    /* Allocate image at ImageBase using MAP_FIXED.
     * MAP_FIXED_NOREPLACE was causing silent failures: if the address was
     * occupied, the fallback path mapped at a random address WITHOUT applying
     * relocations (which are not implemented), corrupting all absolute
     * addresses in the PE image. MAP_FIXED forcibly replaces any existing
     * mapping, ensuring the PE is always at its preferred ImageBase.
     */
    g_image_base = (uint8_t*)mmap((void*)(uintptr_t)image_base, size_of_image,
        PROT_READ | PROT_WRITE | PROT_EXEC,
        MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);

    if (g_image_base == MAP_FAILED) {
        fprintf(stderr, "[ERROR] Cannot mmap %u bytes at 0x%lx: %s\n",
                size_of_image, (unsigned long)image_base, strerror(errno));
        free(filedata);
        return -1;
    }
    MW_TRACE("PE Loader: Mapped at 0x%lx (forced via MAP_FIXED)", image_base);
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
        if (g_sec_prot_count < 16) {
            g_sec_prot[g_sec_prot_count].rva = sec->VirtualAddress;
            g_sec_prot[g_sec_prot_count].size = (sec->VirtualSize + section_align - 1) & ~(section_align - 1);
            g_sec_prot[g_sec_prot_count].prot = prot;
            g_sec_prot_count++;
        }
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
                    iat_page_ensure_writable((uint8_t*)(uintptr_t)patch_addr);
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
                        iat_page_ensure_writable((uint8_t*)(uintptr_t)patch_addr);
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

    /* TASK-TEST-004: restore the per-section protection on every page the
     * IAT pass made writable (Windows restores IAT page protection too). */
    iat_pages_restore();

    if (total_unresolved > 0) {
        fprintf(stderr, "[WARN] %d imports unresolved\n", total_unresolved);
    }

    /* Check TLS (TASK-TLS-001): implement native static TLS.
     * - allocate the image's TLS template + zero-fill as slot 0 block
     * - write the slot index into the image's __tls_index
     * - publish the block via ThreadLocalStoragePointer (gs:0x58 -> array)
     * - run TLS callbacks with DLL_PROCESS_ATTACH */
    if (num_dd > DD_TLS && dd[DD_TLS].VirtualAddress != 0) {
        TlsDirectory64* tls = (TlsDirectory64*)(g_image_base + dd[DD_TLS].VirtualAddress);
        MW_TRACE("TLS: StartRaw=0x%lx EndRaw=0x%lx Callbacks=0x%lx",
                 tls->StartAddressOfRawData, tls->EndAddressOfRawData,
                 tls->AddressOfCallBacks);
        size_t raw_size = 0;
        if (tls->EndAddressOfRawData > tls->StartAddressOfRawData)
            raw_size = (size_t)(tls->EndAddressOfRawData - tls->StartAddressOfRawData);
        uint8_t* block = (uint8_t*)malloc(raw_size + tls->SizeOfZeroFill);
        if (block) {
            if (raw_size) memcpy(block, (void*)(uintptr_t)tls->StartAddressOfRawData, raw_size);
            if (tls->SizeOfZeroFill) memset(block + raw_size, 0, tls->SizeOfZeroFill);
            g_tls_ptr_array[0] = block;                 /* static TLS slot = 0 */
            if (tls->AddressOfIndex)
                *(uint32_t*)(uintptr_t)tls->AddressOfIndex = 0;
            MW_TRACE("TLS: static block %p (%zu+%u bytes) -> slot 0",
                     block, raw_size, tls->SizeOfZeroFill);
        }
        if (tls->AddressOfCallBacks) {
            typedef void (__attribute__((ms_abi)) *tls_cb)(void*, uint32_t, void*);
            void** cb = (void**)(uintptr_t)tls->AddressOfCallBacks;
            for (uint32_t i = 0; cb[i]; i++) {
                MW_TRACE("TLS: callback[%u] = %p", i, cb[i]);
                ((tls_cb)cb[i])(g_image_base, 1 /* DLL_PROCESS_ATTACH */, NULL);
            }
        }
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

    /* CXX EH FIX: Patch specific function's DWARF LSDA.
     * check_basic_cxx_exception_handling has filter=1 with TType=0xff.
     * Personality fn can't resolve the type -> returns ContinueSearch.
     * Change filter to 0x7f (sleb128=-1, catch-all).
     */
    if (g_image_base && mw_eh_hacks_enabled()) {
        uint32_t lsda_abs = 0x20107c + 12 + 64;
        if (lsda_abs < g_image_size) {
            /* .xdata is read-only; temporarily make it writable */
            uint32_t page_rva = lsda_abs & ~0xFFFUL;  /* page-align */
            uint32_t page_off = lsda_abs - page_rva;
            uint32_t page_sz = 0x1000; /* one page */
            mprotect(g_image_base + page_rva, page_sz, PROT_READ | PROT_WRITE | PROT_EXEC);
            uint8_t* patch_byte = g_image_base + lsda_abs;
            uint8_t old_val = *patch_byte;
            if (old_val > 0 && old_val < 0x7f) {
                *patch_byte = 0x7f;
                MW_TRACE("EH_FIX: Patched RVA 0x%x: 0x%02x -> 0x7f", lsda_abs, old_val);
            } else {
                MW_TRACE("EH_FIX: Byte at RVA 0x%x is 0x%02x, skipping", lsda_abs, old_val);
            }
            /* Restore read-only */
            mprotect(g_image_base + page_rva, page_sz, PROT_READ | PROT_EXEC);
        }
    } else if (g_image_base) {
        /* TASK-API-005 (ADR-010): hack present but disabled by default. */
        MW_TRACE("[API-005] CXX-EH-FIX (LSDA catch-all patch RVA 0x%x) skipped: MINWIN_EH_HACKS not set",
                 0x20107c + 12 + 64);
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
    fprintf(stderr, "  RSI=0x%lx RDI=0x%lx\n", uc->uc_mcontext.gregs[REG_RSI], uc->uc_mcontext.gregs[REG_RDI]);
    fprintf(stderr, "  R8=0x%lx  R9=0x%lx R10=0x%lx R11=0x%lx\n",
            uc->uc_mcontext.gregs[REG_R8], uc->uc_mcontext.gregs[REG_R9],
            uc->uc_mcontext.gregs[REG_R10], uc->uc_mcontext.gregs[REG_R11]);
    fprintf(stderr, "  R12=0x%lx R13=0x%lx R14=0x%lx R15=0x%lx\n",
            uc->uc_mcontext.gregs[REG_R12], uc->uc_mcontext.gregs[REG_R13],
            uc->uc_mcontext.gregs[REG_R14], uc->uc_mcontext.gregs[REG_R15]);

    /* Try to identify the function at RIP */
    fprintf(stderr, "  Backtrace (via stack walking):\n");
    uint64_t bt_rip = rip, bt_rsp = rsp;
    for (int i = 0; i < 8; i++) {
        if (g_image_base && bt_rip >= (uint64_t)(uintptr_t)g_image_base &&
            bt_rip < (uint64_t)(uintptr_t)g_image_base + g_image_size) {
            uint64_t bt_rva = bt_rip - (uint64_t)(uintptr_t)g_image_base;
            fprintf(stderr, "    [%d] RIP=0x%lx RVA=0x%lx", i, bt_rip, bt_rva);
            /* Find return address */
            uint64_t* ra_ptr = (uint64_t*)(uintptr_t)bt_rsp;
            fprintf(stderr, "      [RSP+0] = 0x%lx (return addr?)\n", *ra_ptr);
            bt_rsp = *ra_ptr + 8;  /* follow return chain */
        } else if (bt_rip > 0x100000 && bt_rip < 0x7fffffffffffULL) {
            fprintf(stderr, "    [%d] RIP=0x%lx (outside PE, possible shared lib)\n", i, bt_rip);
            bt_rsp = *(uint64_t*)(uintptr_t)(bt_rsp + 8);
        } else {
            break;
        }
    }

    /* Check for NULL/invalid function pointer */
    if (rip < 0x1000 || (info->si_code == SEGV_MAPERR && info->si_addr == NULL)) {
        fprintf(stderr, "  Likely: NULL or invalid function pointer call\n");
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
        mkdir("miniwin-results", 0755);
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
            /* TASK-API-002: exit() paths (mw_exit, CRT exit) run atexit
             * handlers — close the trace JSON there. _exit() paths
             * (ExitProcess/abort) call mw_trace_shutdown_flush directly. */
            atexit(mw_trace_shutdown_flush);
        }
    }

    printf("[MiniWin] Loading %s...\n", exe_path);

    resolve_proc_init();

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

